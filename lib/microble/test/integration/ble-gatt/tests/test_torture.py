"""Torture tests — adversarial scenarios, overflows, floods, unusual timing."""

import asyncio
import random
import pytest
import httpx
from bleak import BleakClient
from conftest import (
    BleEchoClient, RX_CHAR_UUID,
    FRAG_START, FRAG_END, FRAG_COMPLETE,
)


# -- Oversized messages (uses ble) --

async def test_message_at_exact_max_buffer(ble):
    """2048 bytes = firmware MAX_MSG_SIZE. Must echo correctly."""
    msg = bytes(range(256)) * 8
    assert await ble.echo(msg, timeout=15.0) == msg


async def test_message_exceeds_max_buffer(ble):
    """2049 bytes > MAX_MSG_SIZE. Device should drop, not crash."""
    msg = bytes(range(256)) * 8 + b"\xFF"
    mtu = ble.client.mtu_size - 3
    chunk_size = mtu - 1
    offset = 0
    while offset < len(msg):
        remaining = len(msg) - offset
        this_chunk = min(remaining, chunk_size)
        is_first = offset == 0
        is_last = offset + this_chunk >= len(msg)
        header = 0
        if is_first: header |= FRAG_START
        if is_last: header |= FRAG_END
        frag = bytes([header]) + msg[offset:offset + this_chunk]
        await ble.client.write_gatt_char(RX_CHAR_UUID, frag, response=is_last)
        offset += this_chunk

    result = await ble.recv_or_none(timeout=2.0)
    assert result is None, f"Expected no echo for oversized message, got {len(result)} bytes"


async def test_echo_works_after_overflow(ble):
    """Device must recover after a dropped oversized message."""
    assert await ble.echo(b"post-overflow") == b"post-overflow"


# -- Garbage data (uses ble) --

async def test_garbage_single_byte(ble):
    """Single non-header byte — not a valid fragment."""
    await ble.write_raw(bytes([0x01]))
    assert await ble.recv_or_none(timeout=1.5) is None
    assert await ble.echo(b"ok") == b"ok"


async def test_garbage_random_bytes(ble):
    """Random bytes that don't form valid fragments."""
    for _ in range(5):
        garbage = bytes(random.randint(0, 255) for _ in range(random.randint(1, 50)))
        # Mask out START/END bits so it's never a valid complete message
        garbage = bytes([garbage[0] & 0x3F] + list(garbage[1:])) if garbage else b"\x00"
        await ble.write_raw(garbage)
    await asyncio.sleep(0.5)
    while await ble.recv_or_none(timeout=0.3) is not None:
        pass
    assert await ble.echo(b"after-garbage") == b"after-garbage"


# -- Flood (uses ble) --

async def test_write_flood_no_read(ble):
    """Send 20 messages rapidly without consuming echoes. Device must not crash."""
    for i in range(20):
        frag = bytes([FRAG_COMPLETE, i])
        await ble.client.write_gatt_char(RX_CHAR_UUID, frag, response=False)

    await asyncio.sleep(1.0)

    drained = 0
    while True:
        msg = await ble.recv_or_none(timeout=0.5)
        if msg is None:
            break
        drained += 1

    assert await ble.echo(b"after-flood") == b"after-flood"


# -- Long idle (uses ble) --

@pytest.mark.timeout(45)
async def test_idle_30s_then_echo(ble):
    """Connection survives 30s of silence."""
    await asyncio.sleep(30)
    assert await ble.echo(b"still-here") == b"still-here"


# -- Disconnect/reconnect tests (fresh connections — MUST come last) --

@pytest.mark.timeout(60)
async def test_disconnect_during_large_echo(ble_device, base_url):
    """Disconnect while device is sending a multi-fragment response."""
    client = BleakClient(ble_device)
    await client.connect()
    echo = BleEchoClient(client)
    await echo.start()

    msg = bytes(range(256)) * 6  # 1536 bytes
    mtu = client.mtu_size - 3
    chunk_size = mtu - 1
    offset = 0
    while offset < len(msg):
        remaining = len(msg) - offset
        this_chunk = min(remaining, chunk_size)
        is_first = offset == 0
        is_last = offset + this_chunk >= len(msg)
        header = 0
        if is_first: header |= FRAG_START
        if is_last: header |= FRAG_END
        frag = bytes([header]) + msg[offset:offset + this_chunk]
        await client.write_gatt_char(RX_CHAR_UUID, frag, response=is_last)
        offset += this_chunk

    await asyncio.sleep(0.05)
    await echo.stop()
    await client.disconnect()
    await asyncio.sleep(0.5)

    client2 = BleakClient(ble_device)
    await client2.connect()
    echo2 = BleEchoClient(client2)
    await echo2.start()
    assert await echo2.echo(b"recovered") == b"recovered"
    await echo2.stop()
    await client2.disconnect()


@pytest.mark.timeout(60)
async def test_reconnect_after_unclean_disconnect(ble_device):
    """Disconnect forcefully, then reconnect and verify device works."""
    client = BleakClient(ble_device)
    await client.connect()
    echo = BleEchoClient(client)
    await echo.start()
    assert await echo.echo(b"before-drop") == b"before-drop"

    await echo.stop()
    await client.disconnect()
    await asyncio.sleep(1.0)

    client2 = BleakClient(ble_device)
    await client2.connect()
    echo2 = BleEchoClient(client2)
    await echo2.start()
    assert await echo2.echo(b"after-drop") == b"after-drop"
    await echo2.stop()
    await client2.disconnect()


# -- Stability after all torture --

async def test_heap_after_torture(base_url):
    async with httpx.AsyncClient() as http:
        r = await http.get(f"{base_url}/debug/ble/heap")
        assert r.json()["freeHeap"] > 80000


async def test_http_after_torture(base_url):
    async with httpx.AsyncClient() as http:
        r = await http.get(f"{base_url}/ping")
        assert r.text == "pong"
