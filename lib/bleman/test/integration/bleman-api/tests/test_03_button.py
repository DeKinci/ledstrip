"""Tests for BLE button driver — send HID notifications, verify gesture actions."""

import time
import asyncio
import pytest

from tests.test_02_connect import find_test_button, wait_connected


def wait_actions(debug, min_count, timeout=10):
    """Wait until at least min_count actions have been logged."""
    for _ in range(timeout * 4):
        time.sleep(0.25)
        data = debug.actions()
        if data["total"] >= min_count:
            return data
    return debug.actions()


@pytest.fixture
def connected_button(api, debug, hid_peripheral):
    """Ensure TestButton is connected with button driver before test."""
    addr = find_test_button(api, hid_peripheral)
    assert addr is not None, "TestButton not found in scan"

    api.add_known(addr, name="TestButton", type="button")
    assert wait_connected(api, addr), "TestButton did not connect"

    # Wait a moment for driver to subscribe to HID notifications
    time.sleep(2)
    debug.reset()

    yield hid_peripheral, addr

    api.remove_known(addr)
    time.sleep(1)


async def test_single_click(connected_button, debug):
    """Short press → single_click action."""
    peripheral, addr = connected_button

    await peripheral.click(hold_ms=50)

    # Wait for gesture timeout (doubleClickWindowMs = 300ms) + processing
    time.sleep(1.0)

    data = wait_actions(debug, 1)
    assert "single_click" in data["actions"], f"Expected single_click, got: {data}"


async def test_double_click(connected_button, debug):
    """Two quick clicks → double_click action."""
    peripheral, addr = connected_button

    await peripheral.click(hold_ms=50)
    await asyncio.sleep(0.1)  # Short gap between clicks
    await peripheral.click(hold_ms=50)

    time.sleep(1.0)

    data = wait_actions(debug, 1)
    assert "double_click" in data["actions"], f"Expected double_click, got: {data}"


async def test_hold(connected_button, debug):
    """Long press → hold_tick actions + hold_end."""
    peripheral, addr = connected_button

    await peripheral.press()
    await asyncio.sleep(0.8)  # Hold past threshold (400ms) + some ticks
    await peripheral.release()

    time.sleep(0.5)

    data = wait_actions(debug, 2)
    assert "hold_tick" in data["actions"], f"Expected hold_tick, got: {data}"
    assert "hold_end" in data["actions"], f"Expected hold_end, got: {data}"


async def test_multiple_clicks(connected_button, debug):
    """Several clicks generate multiple actions."""
    peripheral, addr = connected_button

    for _ in range(3):
        await peripheral.click(hold_ms=50)
        await asyncio.sleep(0.5)  # Wait past double-click window

    time.sleep(1.0)

    data = wait_actions(debug, 3)
    click_count = data["actions"].count("single_click")
    assert click_count >= 2, f"Expected at least 2 single_clicks, got {click_count}: {data}"


async def test_click_hold(connected_button, debug):
    """Click then hold → click_hold_tick actions (brightness down pattern)."""
    peripheral, addr = connected_button

    # First click
    await peripheral.click(hold_ms=50)
    await asyncio.sleep(0.1)

    # Then hold (within double-click window)
    await peripheral.press()
    await asyncio.sleep(0.8)  # Past hold threshold
    await peripheral.release()

    time.sleep(0.5)

    data = wait_actions(debug, 1)
    assert "click_hold_tick" in data["actions"], f"Expected click_hold_tick, got: {data}"


async def test_rapid_clicks(connected_button, debug):
    """Rapid clicks stress test — 10 clicks at ~100ms intervals."""
    peripheral, addr = connected_button

    for _ in range(10):
        await peripheral.click(hold_ms=30)
        await asyncio.sleep(0.07)

    # Wait for all gesture timeouts to resolve
    time.sleep(2.0)

    data = wait_actions(debug, 1)
    total = data["total"]
    assert total >= 3, f"Expected at least 3 actions from 10 rapid clicks, got {total}: {data}"


async def test_hold_generates_multiple_ticks(connected_button, debug):
    """Long hold produces multiple hold_tick actions (for ramping)."""
    peripheral, addr = connected_button

    await peripheral.press()
    await asyncio.sleep(1.5)  # Hold for 1.5s — should produce many ticks
    await peripheral.release()

    time.sleep(0.5)

    data = wait_actions(debug, 5)
    tick_count = data["actions"].count("hold_tick")
    assert tick_count >= 5, f"Expected >= 5 hold_ticks for 1.5s hold, got {tick_count}: {data}"
    assert "hold_end" in data["actions"], f"Expected hold_end, got: {data}"
