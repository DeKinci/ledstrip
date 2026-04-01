"""Error path and protocol violation tests."""

import pytest



async def test_orphan_middle_fragment_dropped(ble):
    """MIDDLE fragment without START — no echo expected."""
    await ble.write_raw(bytes([0x00, 0xDE, 0xAD]))
    assert await ble.recv_or_none(timeout=1.5) is None


async def test_orphan_end_fragment_dropped(ble):
    """END fragment without START — no echo expected."""
    await ble.write_raw(bytes([0x40, 0xBE, 0xEF]))
    assert await ble.recv_or_none(timeout=1.5) is None


async def test_echo_works_after_orphan_fragments(ble):
    assert await ble.echo(b"recover") == b"recover"


async def test_start_then_new_start_discards_first(ble):
    """Sending a new START abandons the in-progress message."""
    await ble.write_raw(bytes([0x80, 0x01, 0x02]))

    # New complete message — device should discard the partial and echo this one
    assert await ble.echo(b"fresh") == b"fresh"


async def test_empty_complete_echoes_empty(ble):
    """COMPLETE header with zero payload bytes."""
    await ble.write_raw(bytes([0xC0]))
    msg = await ble.recv_or_none(timeout=3.0)
    assert msg is not None
    assert len(msg) == 0
