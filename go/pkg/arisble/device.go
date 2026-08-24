package arisble

import (
	"context"
	"encoding/binary"
	"fmt"
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

	racp, stream, epoch *ble.Characteristic
}

func NewBLEDeviceConn(dev ble.Device, ctx context.Context, addr ble.Addr) (conn *BLEDeviceConn, err error) {
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

func (c *BLEDeviceConn) ensureEPOCH() error {
	if c.epoch != nil {
		return nil
	}

	chars, err := c.client.DiscoverCharacteristics([]ble.UUID{EpochCharUUID}, c.service)
	if err != nil {
		return fmt.Errorf("could not discover characteristics for `%s`: %w", c.address, err)
	}
	if len(chars) == 0 {
		return fmt.Errorf("device is missing characteristics %s", EpochCharUUID)
	}
	c.epoch = chars[0]
	return nil
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
	err := c.ensureEPOCH()
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
		return 0, fmt.Errorf("unexpected GATT server response %x, expected opcode %x, got: %x", d, expected, d[2])
	}

	switch d[3] {
	case RACP_response_reserved:
		return uint16(d[2]), fmt.Errorf("unexpected GATT server response %x, reserved response code", d)
	case RACP_response_success:
		return uint16(d[2]), nil
	case RACP_response_opcode_not_supported:
		return uint16(d[2]), fmt.Errorf("unexpected GATT server response %x, opcode not supported", d)
	case RACP_response_invalid_operator:
		return uint16(d[2]), fmt.Errorf("unexpected GATT server response %x, invalid operator", d)
	case RACP_response_operator_not_supported:
		return uint16(d[2]), fmt.Errorf("unexpected GATT server response %x, operator not supported", d)
	case RACP_response_invalid_operand:
		return uint16(d[2]), fmt.Errorf("unexpected GATT server response %x, invalid operand", d)
	case RACP_response_no_records_found:
		return uint16(d[2]), fmt.Errorf("unexpected GATT server response %x, no records found", d)
	case RACP_response_abort_unsuccessful:
		return uint16(d[2]), fmt.Errorf("unexpected GATT server response %x, abort unsuccessful", d)
	case RACP_response_procedure_not_completed:
		return uint16(d[2]), fmt.Errorf("unexpected GATT server response %x, procedure not completed", d)
	case RACP_response_operand_not_supported:
		return uint16(d[2]), fmt.Errorf("unexpected GATT server response %x, operand not supported", d)
	case RACP_response_server_busy:
		return uint16(d[2]), fmt.Errorf("unexpected GATT server response %x, server busy", d)
	default:
		return uint16(d[2]), fmt.Errorf("unexpected GATT server response %x, unknown response code %x", d, d[3])
	}

}

func populateRACPRange(payload []byte, since, until Timestamp) []byte {
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
	payload = append(payload, RACP_opcode_report_number)
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
		result <- err
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
