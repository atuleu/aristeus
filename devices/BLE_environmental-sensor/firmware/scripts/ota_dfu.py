#!/usr/bin/env python3
"""Push a .gbl to a Silicon Labs in-place OTA DFU (apploader) device over BLE.

Requires: pip install bleak   (uses BlueZ on Linux)
"""

import argparse
import asyncio
import subprocess
import sys

from bleak import BleakClient, BleakScanner

OTA_CONTROL = "f7bf3564-fb6d-4e53-88a4-5e37e0326063"
OTA_DATA = "984227f3-34fc-4045-a5d0-2c581f81a153"

OTA_BEGIN = b"\x00"
OTA_END = b"\x03"

SCAN_TIMEOUT = 10.0


async def find(address=None, name=None):
    if address:
        dev = await BleakScanner.find_device_by_address(address, timeout=SCAN_TIMEOUT)
    else:
        dev = await BleakScanner.find_device_by_name(name, timeout=SCAN_TIMEOUT)
    if dev is None:
        sys.exit(f"device {address or name!r} not found")
    return dev


def forget(address):
    # App and apploader share the address but expose different GATT tables:
    # drop BlueZ's cache so services are rediscovered.
    subprocess.run(["bluetoothctl", "remove", address],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


async def reboot_to_apploader(dev):
    disconnected = asyncio.Event()
    async with BleakClient(dev, disconnected_callback=lambda _: disconnected.set()) as client:
        print(f"connected to app {dev.address}, requesting DFU mode")
        await client.write_gatt_char(OTA_CONTROL, OTA_BEGIN, response=True)
        await asyncio.wait_for(disconnected.wait(), timeout=10.0)
    print("device rebooting into apploader")


async def chunk_size(client):
    acquire = getattr(client._backend, "_acquire_mtu", None)
    if acquire is not None:
        try:
            await acquire()
        except Exception:
            pass
    return max(20, min(client.mtu_size - 3, 244))


async def upload(dev, image):
    async with BleakClient(dev) as client:
        size = await chunk_size(client)
        print(f"connected to apploader {dev.address}, chunk size {size} bytes")
        await client.write_gatt_char(OTA_CONTROL, OTA_BEGIN, response=True)
        total = len(image)
        for off in range(0, total, size):
            await client.write_gatt_char(OTA_DATA, image[off:off + size], response=True)
            done = min(off + size, total)
            bar = "#" * (40 * done // total)
            print(f"\r[{bar:<40}] {done}/{total} bytes", end="", flush=True)
        print()
        await client.write_gatt_char(OTA_CONTROL, OTA_END, response=True)
    print("transfer complete, device rebooting into new application")


async def main():
    p = argparse.ArgumentParser(description=__doc__)
    target = p.add_mutually_exclusive_group(required=True)
    target.add_argument("--address", help="device BLE address (AA:BB:CC:DD:EE:FF)")
    target.add_argument("--name", help="advertised device name")
    p.add_argument("--skip-app-phase", action="store_true",
                   help="device is already in the apploader")
    p.add_argument("gbl", help="application .gbl file")
    args = p.parse_args()

    with open(args.gbl, "rb") as f:
        image = f.read()

    dev = await find(args.address, args.name)
    address = dev.address

    if not args.skip_app_phase:
        await reboot_to_apploader(dev)
        forget(address)
        await asyncio.sleep(1.0)
        dev = await find(address=address)

    await upload(dev, image)
    forget(address)


if __name__ == "__main__":
    asyncio.run(main())
