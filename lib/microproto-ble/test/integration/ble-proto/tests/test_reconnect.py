"""MicroProto BLE reconnection tests."""

import pytest
import pytest_asyncio
from bleak import BleakClient
from conftest import BleProtoClient, OP_HELLO, HELLO_FLAG_RESPONSE


async def test_reconnect_handshake(ble_device):
    """Device handles multiple BLE connect/disconnect cycles."""
    successes = 0

    for i in range(3):
        client = BleakClient(ble_device)
        await client.connect()
        proto = BleProtoClient(client)
        await proto.start()

        try:
            await proto.send_hello()
            msg = await proto.recv(timeout=5.0)
            opcode, flags = proto.decode_opcode(msg)
            if opcode == OP_HELLO and (flags & HELLO_FLAG_RESPONSE):
                successes += 1
        finally:
            await proto.stop()
            await client.disconnect()

    assert successes == 3, f"Expected 3 successful handshakes, got {successes}"
