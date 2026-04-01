"""Shared fixtures for BLE GATT integration tests."""

import os
import asyncio
import struct
import pytest
import pytest_asyncio
from bleak import BleakClient, BleakScanner

SERVICE_UUID = "aaa10001-f5a3-4aa0-b726-5d1be14a1d00"
RX_CHAR_UUID = "aaa10002-f5a3-4aa0-b726-5d1be14a1d00"
TX_CHAR_UUID = "aaa10003-f5a3-4aa0-b726-5d1be14a1d00"

FRAG_START = 0x80
FRAG_END = 0x40
FRAG_COMPLETE = FRAG_START | FRAG_END


def device_ip():
    ip = os.environ.get("DEVICE_IP")
    if not ip:
        raise RuntimeError("DEVICE_IP not set")
    return ip


@pytest.fixture(scope="session")
def ip():
    return device_ip()


@pytest.fixture(scope="session")
def base_url(ip):
    return f"http://{ip}"


@pytest_asyncio.fixture(scope="session", loop_scope="session")
async def ble_device():
    """Scan and return BLEDevice for the test service."""
    device = await BleakScanner.find_device_by_filter(
        lambda d, adv: SERVICE_UUID.lower() in [u.lower() for u in (adv.service_uuids or [])],
        timeout=10.0,
    )
    assert device is not None, "BLE test device not found"
    return device


class BleEchoClient:
    """High-level BLE echo client using bleak."""

    def __init__(self, client: BleakClient):
        self.client = client
        self._rx_queue: asyncio.Queue[bytes] = asyncio.Queue()
        self._chunks: list[bytes] = []

    async def start(self):
        await self.client.start_notify(TX_CHAR_UUID, self._on_notify)

    async def stop(self):
        try:
            await self.client.stop_notify(TX_CHAR_UUID)
        except Exception:
            pass

    def _on_notify(self, _sender, data: bytearray):
        """Notification callback — reassembles fragments into complete messages."""
        if len(data) < 1:
            return
        header = data[0]
        payload = bytes(data[1:])

        if header & FRAG_START:
            self._chunks.clear()
        self._chunks.append(payload)

        if header & FRAG_END:
            msg = b"".join(self._chunks)
            self._chunks.clear()
            self._rx_queue.put_nowait(msg)

    async def recv(self, timeout: float = 10.0) -> bytes:
        """Wait for next complete reassembled message."""
        return await asyncio.wait_for(self._rx_queue.get(), timeout=timeout)

    async def recv_or_none(self, timeout: float = 2.0) -> bytes | None:
        """Wait for a message, return None on timeout."""
        try:
            return await asyncio.wait_for(self._rx_queue.get(), timeout=timeout)
        except asyncio.TimeoutError:
            return None

    async def echo(self, data: bytes, timeout: float = 10.0) -> bytes:
        """Send a message (auto-fragment) and wait for the echo."""
        mtu = self.client.mtu_size - 3  # ATT overhead
        chunk_size = mtu - 1  # fragment header

        if len(data) <= chunk_size:
            frag = bytes([FRAG_COMPLETE]) + data
            await self.client.write_gatt_char(RX_CHAR_UUID, frag, response=True)
        else:
            offset = 0
            while offset < len(data):
                remaining = len(data) - offset
                this_chunk = min(remaining, chunk_size)
                is_first = offset == 0
                is_last = offset + this_chunk >= len(data)

                header = 0
                if is_first:
                    header |= FRAG_START
                if is_last:
                    header |= FRAG_END

                frag = bytes([header]) + data[offset : offset + this_chunk]
                await self.client.write_gatt_char(RX_CHAR_UUID, frag, response=is_last)
                offset += this_chunk

        return await self.recv(timeout=timeout)

    async def write_raw(self, data: bytes):
        """Write raw bytes without fragment header."""
        await self.client.write_gatt_char(RX_CHAR_UUID, data, response=False)


@pytest_asyncio.fixture(loop_scope="session")
async def ble(ble_device) -> BleEchoClient:
    """Fresh BLE connection per test function. Fully isolated, no order dependencies."""
    client = BleakClient(ble_device)
    await client.connect()
    echo = BleEchoClient(client)
    await echo.start()
    yield echo
    await echo.stop()
    await client.disconnect()
