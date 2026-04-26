"""Test clock synchronization via BLE SetClock command."""

import time

from conftest import RetranslatorClient


async def test_set_clock_board_a(board_a: RetranslatorClient):
    now = int(time.time())
    await board_a.set_clock(now)
    info = await board_a.get_self_info()
    assert abs(info.clock - now) <= 2, f"Clock drift: expected ~{now}, got {info.clock}"


async def test_set_clock_board_b(board_b: RetranslatorClient):
    now = int(time.time())
    await board_b.set_clock(now)
    info = await board_b.get_self_info()
    assert abs(info.clock - now) <= 2, f"Clock drift: expected ~{now}, got {info.clock}"
