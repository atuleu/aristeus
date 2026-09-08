package arisble

import (
	"context"
	"encoding/binary"
	"fmt"
	"log/slog"
	"sync"
	"time"

	"github.com/go-ble/ble"
)

type BLEDeviceConn struct {
	client  ble.Client
	address ble.Addr
	service *ble.Service

	mx     sync.Mutex
	onData func([]byte)
	onRACP func([]byte)

	racp, stream, epoch, location, pressure *ble.Characteristic
}

type BLEDialer interface {
	Dial(context.Context, ble.Addr) (ble.Client, error)
}

func NewBLEDeviceConn(dev BLEDialer, ctx context.Context, addr ble.Addr) (conn *BLEDeviceConn, err error) {
	defer func() {
		if err != nil && conn != nil {
			conn.client.Conn().Close()
		}
	}()
	client, err := dev.Dial(ctx, addr)
	if err != nil {
		return nil, fmt.Errorf("could not dial `%s`: %w", addr, err)
	}
	conn = &BLEDeviceConn{
		client:  client,
		address: addr,
	}

	services, err := client.DiscoverServices([]ble.UUID{CustomServiceUUID})
	if err != nil {
		return nil, fmt.Errorf("could not discover custom service for `%s`: %w", addr, err)
	}
	if len(services) == 0 {
		return nil, fmt.Errorf("device `%s` is missing service %s", addr, CustomServiceUUID)
	}
	conn.service = services[0]
	return conn, nil
}

func (c *BLEDeviceConn) SetContext(ctx context.Context) {
	c.client.Conn().SetContext(ctx)
}

func (c *BLEDeviceConn) Close() error {
	c.mx.Lock()
	defer c.mx.Unlock()
	err := c.client.Conn().Close()
	c.service = nil
	c.racp = nil
	c.stream = nil
	c.epoch = nil
	return err
}

func (c *BLEDeviceConn) ensureCharacteristic(char *ble.Characteristic, UUID ble.UUID) (*ble.Characteristic, error) {
	if char != nil {
		return char, nil
	}
	chars, err := c.client.DiscoverCharacteristics([]ble.UUID{UUID}, c.service)
	if err != nil {
		return nil, fmt.Errorf("could not discover characteristics for device '%s': %w", c.address, err)
	}
	if len(chars) == 0 {
		return nil, fmt.Errorf("device '%s' is missing characteristics %s", c.address, UUID)
	}
	return chars[0], nil
}

func (c *BLEDeviceConn) ensureRACPCharacteristics() error {
	if c.racp != nil && c.stream != nil {
		return nil
	}

	chars, err := c.client.DiscoverCharacteristics([]ble.UUID{RACPUUID, StreamDataUUID}, c.service)
	if err != nil {
		return fmt.Errorf("could not discover characteristics for `%s`: %w", c.address, err)
	}
	if len(chars) < 2 {
		return fmt.Errorf("device is missing characteristics %s", EpochCharUUID)
	}
	if chars[0].UUID.Equal(RACPUUID) {
		c.racp = chars[0]
		c.stream = chars[1]
	} else {
		c.racp = chars[1]
		c.stream = chars[0]
	}

	_, err = c.client.DiscoverDescriptors(nil, c.racp)
	if err != nil {
		return fmt.Errorf("could not discover descriptor for RACP characteristic of '%s': %w", c.address, err)
	}

	_, err = c.client.DiscoverDescriptors(nil, c.stream)
	if err != nil {
		return fmt.Errorf("could not discover descriptor for streaming characteristic of '%s': %w", c.address, err)
	}

	err = c.client.Subscribe(c.racp, true, func(d []byte) {
		c.mx.Lock()
		defer c.mx.Unlock()
		if c.onRACP != nil {
			c.onRACP(d)
		}
	})
	if err != nil {
		c.racp = nil
		return fmt.Errorf("could not subscribe to device `%s` indications: %w", c.address, err)
	}
	err = c.client.Subscribe(c.stream, false, func(d []byte) {
		c.mx.Lock()
		defer c.mx.Unlock()

		if c.onData != nil {
			c.onData(d)
		}
	})
	if err != nil {
		c.stream = nil
		return fmt.Errorf("could not subscribe to device `%s` notifications: %w", c.address, err)
	}
	return nil
}

func (c *BLEDeviceConn) SynchronizeBLEDevice() error {
	var err error
	c.epoch, err = c.ensureCharacteristic(c.epoch, EpochCharUUID)
	if err != nil {
		return err
	}

	payload := make([]byte, 0, 4)
	payload = NewTimestamp(time.Now()).AppendBinary(payload)
	if err != nil {
		return fmt.Errorf("could not encode current time: %w", err)
	}

	return c.client.WriteCharacteristic(c.epoch, payload, false)
}

type RACPResult[T any] struct {
	Value T
	Error error
}

func parseRACPResponse(d []byte, expected RACPOpcode) (uint16, error) {
	if len(d) != 4 {
		return 0, fmt.Errorf("unexpected GATT Server response %x, length should be 4", d)
	}

	if d[1] != RACP_operator_null {
		return 0, fmt.Errorf("unexpected GATT server response %x, expected NULL operator, but got %d", d, d[1])
	}

	switch d[0] {
	case RACP_opcode_response:
		break
	case RACP_opcode_response_number:
		return binary.LittleEndian.Uint16(d[2:4]), nil
	default:
		return 0, fmt.Errorf("unexpected GATT server response %x, expected response type %x", d, d[0])
	}
	if RACPOpcode(d[2]) != expected {
		return 0, fmt.Errorf("unexpected GATT server response %x, expected opcode %x (%s), got: %x (%s)", d, int(expected), expected, int(d[2]), RACPOpcode(d[2]))
	}

	if RACPResponseCode(d[3]) != RACP_response_success {
		return uint16(d[2]), fmt.Errorf("unexpected GATT server response %x, response: %x (%s)",
			d, uint16(d[3]), RACPResponseCode(d[3]))
	}
	return uint16(d[2]), nil
}

func populateRACPRange(payload []byte, since, until Timestamp) []byte {
	slog.Info("populating range",
		slog.Int("since", int(since)),
		slog.Int("until", int(until)),
		slog.Time("since_date", since.ToTime()),
		slog.Time("until_date", until.ToTime()))

	if since != TimestampNaN {
		if until != TimestampNaN {
			payload = append(payload, RACP_operator_in_range)
			payload = since.AppendBinary(payload)
			payload = until.AppendBinary(payload)
		} else {
			payload = append(payload, RACP_operator_ge)
			payload = since.AppendBinary(payload)
		}
	} else if until != TimestampNaN {
		payload = append(payload, RACP_operator_le)
		payload = until.AppendBinary(payload)
	} else {
		payload = append(payload, RACP_operator_all)
	}
	return payload
}

func (c *BLEDeviceConn) ReportRecords(since, until Timestamp) (<-chan RACPResult[DataPoint], error) {
	if err := c.ensureRACPCharacteristics(); err != nil {
		return nil, err
	}
	c.mx.Lock()
	defer c.mx.Unlock()
	if c.onData != nil || c.onRACP != nil {
		return nil, fmt.Errorf("operation already in use")
	}
	result := make(chan RACPResult[DataPoint], 10)
	cleanup := func() {
		close(result)
		c.onData = nil
		c.onRACP = nil
	}

	c.onData = func(data []byte) {
		var point DataPoint
		err := point.UnmarshalBinary(data)
		if err != nil {
			result <- RACPResult[DataPoint]{Error: err}
		} else {
			result <- RACPResult[DataPoint]{Value: point}
		}
	}

	c.onRACP = func(data []byte) {
		defer cleanup()
		_, err := parseRACPResponse(data, RACP_opcode_report_records)
		if err != nil {
			result <- RACPResult[DataPoint]{Error: err}
		}
	}

	payload := make([]byte, 0, 10)
	payload = append(payload, RACP_opcode_report_records)
	payload = populateRACPRange(payload, since, until)
	err := c.client.WriteCharacteristic(c.racp, payload, false)
	if err != nil {
		cleanup()
		return nil, err
	}
	return result, nil
}

func (c *BLEDeviceConn) DeleteRecords(since, until Timestamp) (<-chan error, error) {
	if err := c.ensureRACPCharacteristics(); err != nil {
		return nil, err
	}
	c.mx.Lock()
	defer c.mx.Unlock()
	if c.onData != nil || c.onRACP != nil {
		return nil, fmt.Errorf("operation already in use")
	}
	result := make(chan error)
	cleanup := func() {
		close(result)
		c.onRACP = nil
		c.onData = nil
	}

	c.onRACP = func(data []byte) {
		defer cleanup()
		_, err := parseRACPResponse(data, RACP_opcode_delete_records)
		if err != nil {
			result <- err
		}
	}

	payload := make([]byte, 0, 10)
	payload = append(payload, RACP_opcode_delete_records)
	payload = populateRACPRange(payload, since, until)
	err := c.client.WriteCharacteristic(c.racp, payload, false)
	if err != nil {
		cleanup()
		return nil, err
	}
	return result, nil
}

func (c *BLEDeviceConn) CountRecords(since, until Timestamp) (<-chan RACPResult[int], error) {
	if err := c.ensureRACPCharacteristics(); err != nil {
		return nil, err
	}
	c.mx.Lock()
	defer c.mx.Unlock()
	if c.onData != nil || c.onRACP != nil {
		return nil, fmt.Errorf("operation already in use")
	}
	result := make(chan RACPResult[int])
	cleanup := func() {
		close(result)
		c.onRACP = nil
		c.onData = nil
	}
	c.onRACP = func(data []byte) {
		defer cleanup()
		count, err := parseRACPResponse(data, RACP_opcode_report_number)
		if err != nil {
			result <- RACPResult[int]{Error: err}
		} else {
			result <- RACPResult[int]{Value: int(count)}
		}
	}
	payload := make([]byte, 0, 10)
	payload = append(payload, RACP_opcode_report_number)
	payload = populateRACPRange(payload, since, until)
	err := c.client.WriteCharacteristic(c.racp, payload, false)
	if err != nil {
		cleanup()
		return nil, err
	}
	return result, nil
}

func (c *BLEDeviceConn) GetLocation() (Location, error) {
	var err error
	c.location, err = c.ensureCharacteristic(c.location, HiveLocationUUID)
	if err != nil {
		return Location{}, err
	}

	data, err := c.client.ReadCharacteristic(c.location)

	if err != nil {
		return Location{}, fmt.Errorf("could not read HiveLocation for '%s': %w", c.address, err)
	}

	res := Location{}
	return res, res.UnmarshalBinary(data)
}

func (c *BLEDeviceConn) SetLocation(l Location) error {
	var err error
	c.location, err = c.ensureCharacteristic(c.location, HiveLocationUUID)
	if err != nil {
		return err
	}

	payload := l.MarshalBinary(nil)

	err = c.client.WriteCharacteristic(c.location, payload, false)
	if err != nil {
		return fmt.Errorf("could not write HiveLocation for '%s': %w", c.address, err)
	}

	return nil
}

func (c *BLEDeviceConn) SetPressure(p Pressure, co2Target CO2Concentration) error {
	var err error
	c.pressure, err = c.ensureCharacteristic(c.pressure, PressureUUID)
	if err != nil {
		return err
	}

	payload := p.MarshalBinary(nil)
	if co2Target != CO2ConcentrationNaN {
		payload = binary.LittleEndian.AppendUint16(payload, uint16(co2Target))
	}

	err = c.client.WriteCharacteristic(c.pressure, payload, false)

	if err != nil {
		return fmt.Errorf("could not write pressure for '%s': %w", c.address, err)
	}

	return nil
}
