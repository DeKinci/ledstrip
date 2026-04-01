"""Echo protocol tests — small and chunked messages."""

import pytest
import httpx



async def test_short_string(ble):
    assert await ble.echo(b"hello") == b"hello"


async def test_binary_data(ble):
    msg = bytes([0x00, 0x01, 0xFF, 0xAB, 0xCD])
    assert await ble.echo(msg) == msg


async def test_single_byte(ble):
    assert await ble.echo(bytes([0x42])) == bytes([0x42])


async def test_empty_payload(ble):
    assert await ble.echo(b"") == b""


async def test_500_bytes(ble):
    msg = bytes(i & 0xFF for i in range(500))
    assert await ble.echo(msg) == msg


async def test_1kb(ble):
    msg = bytes((i * 7) & 0xFF for i in range(1024))
    assert await ble.echo(msg) == msg


async def test_2kb_near_max(ble):
    msg = bytes((i * 3) & 0xFF for i in range(2048))
    assert await ble.echo(msg, timeout=15.0) == msg


async def test_rapid_sequential_300_byte(ble):
    for round in range(3):
        msg = bytes([round + 1] * 300)
        assert await ble.echo(msg) == msg


async def test_mtu_reported(ble):
    """Bleak should negotiate MTU > 23."""
    mtu = ble.client.mtu_size
    assert mtu > 23, f"MTU should be negotiated above default, got {mtu}"
