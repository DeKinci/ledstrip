"""MicroProto BLE ping/pong tests."""

import pytest
from conftest import OP_PING, OP_PROPERTY_UPDATE, PING_FLAG_RESPONSE


async def test_ping_pong(proto):
    """PING should receive a PONG response."""
    # First do handshake so client is ready
    await proto.handshake()
    await proto.drain()

    await proto.send_ping(payload=42)

    # Wait for PONG, skip any property broadcasts
    for _ in range(10):
        msg = await proto.recv(timeout=5.0)
        opcode, flags = proto.decode_opcode(msg)
        if opcode == OP_PING and (flags & PING_FLAG_RESPONSE):
            return  # Got PONG
        if opcode == OP_PROPERTY_UPDATE:
            continue  # Skip broadcast

    pytest.fail("Did not receive PONG response")


async def test_multiple_pings(proto):
    """Multiple PINGs should each get a PONG."""
    await proto.handshake()
    await proto.drain()

    pong_count = 0
    for i in range(5):
        await proto.send_ping(payload=i + 1)

        for _ in range(10):
            msg = await proto.recv(timeout=5.0)
            opcode, flags = proto.decode_opcode(msg)
            if opcode == OP_PING and (flags & PING_FLAG_RESPONSE):
                pong_count += 1
                break
            if opcode == OP_PROPERTY_UPDATE:
                continue

    assert pong_count == 5, f"Expected 5 PONGs, got {pong_count}"
