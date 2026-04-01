"""Shared fixtures for MicroProto BLE integration tests."""

import os
import asyncio
import struct
import pytest
import pytest_asyncio
from bleak import BleakClient, BleakScanner

# MicroProto BLE GATT UUIDs (from MicroProtoBleServer.cpp)
SERVICE_UUID = "e3a10001-f5a3-4aa0-b726-5d1be14a1d00"
RX_CHAR_UUID = "e3a10002-f5a3-4aa0-b726-5d1be14a1d00"
TX_CHAR_UUID = "e3a10003-f5a3-4aa0-b726-5d1be14a1d00"

# Fragment header flags (same as BleFragmentation)
FRAG_START = 0x80
FRAG_END = 0x40
FRAG_COMPLETE = FRAG_START | FRAG_END

# MicroProto opcodes
OP_HELLO = 0x0
OP_PROPERTY_UPDATE = 0x1
OP_SCHEMA_UPSERT = 0x3
OP_PING = 0x6
OP_ERROR = 0x7

# HELLO flags
HELLO_FLAG_RESPONSE = 0x01

# PING flags
PING_FLAG_RESPONSE = 0x01


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
    """Scan and return BLEDevice for the MicroProto BLE service."""
    device = await BleakScanner.find_device_by_filter(
        lambda d, adv: SERVICE_UUID.lower() in [u.lower() for u in (adv.service_uuids or [])],
        timeout=10.0,
    )
    assert device is not None, "MicroProto BLE device not found"
    return device


class BleProtoClient:
    """MicroProto BLE client for integration testing.

    Handles BLE fragmentation and provides high-level methods for
    MicroProto protocol operations (HELLO, PING, property reads).
    """

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
        """Notification callback — reassembles fragments."""
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

    async def send(self, data: bytes):
        """Send a MicroProto message, auto-fragmenting for BLE MTU."""
        mtu = self.client.mtu_size - 3
        chunk_size = mtu - 1

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

    async def recv(self, timeout: float = 10.0) -> bytes:
        """Wait for next complete reassembled message."""
        return await asyncio.wait_for(self._rx_queue.get(), timeout=timeout)

    async def recv_or_none(self, timeout: float = 2.0) -> bytes | None:
        try:
            return await asyncio.wait_for(self._rx_queue.get(), timeout=timeout)
        except asyncio.TimeoutError:
            return None

    async def drain(self, timeout: float = 0.5):
        """Drain any pending messages."""
        while True:
            msg = await self.recv_or_none(timeout)
            if msg is None:
                break

    def decode_opcode(self, msg: bytes) -> tuple[int, int]:
        """Extract (opcode, flags) from a MicroProto message."""
        header = msg[0]
        return (header & 0x0F, (header >> 4) & 0x0F)

    async def send_hello(self):
        """Send HELLO request with protocol version 1."""
        # HELLO: opcode=0x0, flags=0x0
        # Body: varint(protocol_version=1), varint(schema_version=0), varint(device_id=0x1234)
        hello = bytes([0x00, 0x01, 0x00, 0x80, 0x24])
        await self.send(hello)

    async def send_ping(self, payload: int = 0):
        """Send PING with varint payload."""
        # PING: opcode=0x6, flags=0x0
        buf = bytearray([0x06])
        # Encode varint
        val = payload
        while val > 0x7F:
            buf.append((val & 0x7F) | 0x80)
            val >>= 7
        buf.append(val & 0x7F)
        await self.send(bytes(buf))

    async def handshake(self, timeout: float = 10.0) -> dict:
        """Perform full handshake. Returns dict with schema and property info."""
        await self.send_hello()

        messages = {"hello": None, "schema": [], "properties": []}
        deadline = asyncio.get_event_loop().time() + timeout

        while asyncio.get_event_loop().time() < deadline:
            remaining = deadline - asyncio.get_event_loop().time()
            msg = await self.recv_or_none(min(remaining, 2.0))
            if msg is None:
                break

            opcode, flags = self.decode_opcode(msg)
            if opcode == OP_HELLO:
                messages["hello"] = msg
            elif opcode == OP_SCHEMA_UPSERT:
                messages["schema"].append(msg)
            elif opcode == OP_PROPERTY_UPDATE:
                messages["properties"].append(msg)

        return messages


@pytest_asyncio.fixture(loop_scope="session")
async def proto(ble_device) -> BleProtoClient:
    """Fresh BLE MicroProto connection per test."""
    client = BleakClient(ble_device)
    await client.connect()
    proto = BleProtoClient(client)
    await proto.start()
    yield proto
    await proto.stop()
    await client.disconnect()
