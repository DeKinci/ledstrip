"""Edge case and boundary tests."""

import pytest



async def test_all_zeros(ble):
    msg = bytes(100)
    assert await ble.echo(msg) == msg


async def test_all_0xff(ble):
    msg = bytes([0xFF] * 100)
    assert await ble.echo(msg) == msg


async def test_bytes_matching_fragment_headers(ble):
    """Data bytes 0x80, 0x40, 0xC0 must not confuse the framing layer."""
    msg = bytes([0x80, 0x40, 0xC0, 0x80, 0x40, 0xC0, 0x00])
    assert await ble.echo(msg) == msg


async def test_embedded_nulls(ble):
    msg = bytes([0x01, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03])
    assert await ble.echo(msg) == msg


async def test_exact_chunk_boundary(ble):
    """Message that fills exactly one chunk (no split)."""
    chunk_size = ble.client.mtu_size - 3 - 1
    msg = bytes(i & 0xFF for i in range(chunk_size))
    assert await ble.echo(msg) == msg


async def test_one_byte_over_chunk_boundary(ble):
    """Message one byte over chunk size — forces a 2-fragment split."""
    chunk_size = ble.client.mtu_size - 3 - 1
    msg = bytes(i & 0xFF for i in range(chunk_size + 1))
    assert await ble.echo(msg) == msg


async def test_exact_multiple_of_chunk_size(ble):
    """Message at exact multiple of chunk size (3 full chunks)."""
    chunk_size = ble.client.mtu_size - 3 - 1
    msg = bytes(i & 0xFF for i in range(chunk_size * 3))
    assert await ble.echo(msg) == msg


async def test_sequential_byte_pattern(ble):
    msg = bytes(i & 0xFF for i in range(512))
    assert await ble.echo(msg) == msg


async def test_alternating_aa_55(ble):
    msg = bytes(0xAA if i % 2 == 0 else 0x55 for i in range(200))
    assert await ble.echo(msg) == msg
