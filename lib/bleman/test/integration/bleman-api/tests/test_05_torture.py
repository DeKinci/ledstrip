"""Torture tests — sustained stress, rapid state transitions, heap stability."""

import time
import asyncio
import pytest

from tests.test_02_connect import find_test_button, wait_connected, wait_disconnected, ensure_removed


@pytest.fixture
def button_addr(api, hid_peripheral):
    """Get the TestButton address and ensure clean state. Retries scan if needed."""
    addr = find_test_button(api, hid_peripheral)
    if addr is None:
        # Retry — bless may need a second scan to be discoverable
        time.sleep(3)
        from tests.test_01_scan import scan_and_find
        dev = scan_and_find(api, hid_peripheral, timeout=15)
        if dev:
            addr = dev["address"]
    assert addr is not None, "TestButton not found after retry"
    ensure_removed(api, addr)
    yield addr
    ensure_removed(api, addr)


async def test_sustained_button_activity(api, debug, hid_peripheral, button_addr):
    """20 clicks — sustained driver activity."""
    addr = button_addr

    api.add_known(addr, name="TestButton", type="button")
    assert wait_connected(api, addr), "Connect failed"
    await asyncio.sleep(2)
    debug.reset()

    for i in range(20):
        await hid_peripheral.click(hold_ms=30)
        await asyncio.sleep(0.4)

    time.sleep(1.5)
    data = debug.actions()
    assert data["total"] >= 5, f"Expected >= 5 actions from 20 clicks, got {data['total']}"

    heap = debug.heap()
    assert heap["freeHeap"] > 10000, f"Heap low after sustained activity: {heap['freeHeap']}"


async def test_mixed_gestures_sustained(api, debug, hid_peripheral, button_addr):
    """Mix of clicks, holds, double-clicks over 20 iterations."""
    addr = button_addr

    api.add_known(addr, name="TestButton", type="button")
    assert wait_connected(api, addr), "Connect failed"
    await asyncio.sleep(2)
    debug.reset()

    for i in range(20):
        gesture = i % 4
        if gesture == 0:
            # Single click
            await hid_peripheral.click(hold_ms=50)
            await asyncio.sleep(0.5)
        elif gesture == 1:
            # Double click
            await hid_peripheral.click(hold_ms=40)
            await asyncio.sleep(0.1)
            await hid_peripheral.click(hold_ms=40)
            await asyncio.sleep(0.5)
        elif gesture == 2:
            # Hold
            await hid_peripheral.press()
            await asyncio.sleep(0.6)
            await hid_peripheral.release()
            await asyncio.sleep(0.3)
        else:
            # Click + hold
            await hid_peripheral.click(hold_ms=40)
            await asyncio.sleep(0.1)
            await hid_peripheral.press()
            await asyncio.sleep(0.5)
            await hid_peripheral.release()
            await asyncio.sleep(0.3)

    time.sleep(2)
    data = debug.actions()
    assert data["total"] >= 10, f"Expected >= 10 actions from mixed gestures, got {data['total']}"

    # Verify we got a variety of action types
    types = set(data["actions"])
    assert len(types) >= 2, f"Expected multiple action types, got: {types}"


def test_connect_disconnect_10_cycles(api, debug, hid_peripheral, button_addr):
    """10 full connect/disconnect cycles — tests slot reuse and cleanup."""
    addr = button_addr

    heap_before = debug.heap()["freeHeap"]

    for i in range(10):
        api.add_known(addr, name="TestButton", type="button")
        assert wait_connected(api, addr, timeout=15), f"Connect failed on cycle {i}"
        api.remove_known(addr)
        assert wait_disconnected(api, addr), f"Disconnect failed on cycle {i}"

    time.sleep(2)
    heap_after = debug.heap()["freeHeap"]
    leak = heap_before - heap_after
    assert leak < 4096, f"Heap leaked {leak} bytes over 10 connect cycles"


async def test_buttons_across_reconnect(api, debug, hid_peripheral, button_addr):
    """Connect, click, disconnect, reconnect, click again — driver re-init works."""
    addr = button_addr

    # First session
    api.add_known(addr, name="TestButton", type="button")
    assert wait_connected(api, addr), "First connect failed"
    await asyncio.sleep(2)
    debug.reset()

    await hid_peripheral.click(hold_ms=50)
    time.sleep(1)
    data1 = debug.actions()
    assert data1["total"] >= 1, f"First session: no actions received"

    # Disconnect
    api.remove_known(addr)
    assert wait_disconnected(api, addr), "Disconnect failed"

    # Second session
    api.add_known(addr, name="TestButton", type="button")
    assert wait_connected(api, addr, timeout=20), "Reconnect failed"
    await asyncio.sleep(2)
    debug.reset()

    await hid_peripheral.click(hold_ms=50)
    time.sleep(1)
    data2 = debug.actions()
    assert data2["total"] >= 1, f"Second session: no actions after reconnect"


def test_rapid_add_remove_20x(api, debug, hid_peripheral, button_addr):
    """20 rapid add/remove cycles — stress NVS and state management."""
    addr = button_addr

    for _ in range(20):
        api.add_known(addr, name="TestButton", type="button")
        api.remove_known(addr)

    # Device should be stable
    state = debug.state()
    assert state["connectedCount"] == 0
    assert debug.heap()["freeHeap"] > 10000


def test_rapid_scan_triggers(api, debug, hid_peripheral):
    """Trigger scan 5 times in quick succession — should not crash."""
    for _ in range(5):
        api.scan()
        time.sleep(0.2)

    # Wait for all scans to settle
    for _ in range(30):
        time.sleep(0.5)
        if not api.scan_results()["scanning"]:
            break

    # Device stable
    assert debug.heap()["freeHeap"] > 10000


async def test_hold_across_disconnect(api, debug, hid_peripheral, button_addr):
    """Start hold, disconnect, reconnect, start new hold — clean state machine reset."""
    addr = button_addr

    # Connect and start holding
    api.add_known(addr, name="TestButton", type="button")
    assert wait_connected(api, addr), "Connect failed"
    await asyncio.sleep(2)
    debug.reset()

    await hid_peripheral.press()
    await asyncio.sleep(0.6)

    # Disconnect mid-hold
    api.remove_known(addr)
    assert wait_disconnected(api, addr), "Disconnect failed"

    await hid_peripheral.release()
    await asyncio.sleep(0.5)

    # Reconnect and hold again — gesture state should be fresh
    api.add_known(addr, name="TestButton", type="button")
    assert wait_connected(api, addr, timeout=20), "Reconnect failed"
    await asyncio.sleep(2)
    debug.reset()

    await hid_peripheral.press()
    await asyncio.sleep(0.6)
    await hid_peripheral.release()

    time.sleep(1)
    data = debug.actions()
    assert "hold_tick" in data["actions"], f"Expected hold_tick after reconnect, got: {data}"
    assert "hold_end" in data["actions"], f"Expected hold_end after reconnect, got: {data}"


def test_alternating_types(api, debug, hid_peripheral, button_addr):
    """Switch device type between connections — driver changes correctly."""
    addr = button_addr

    # Connect as button
    api.add_known(addr, name="TestButton", type="button")
    assert wait_connected(api, addr), "Connect as button failed"

    conn = api.connected()["devices"]
    dev = next(d for d in conn if d["address"].upper() == addr.upper())
    assert dev["type"] == "button"

    api.remove_known(addr)
    assert wait_disconnected(api, addr)

    # Connect with no type
    api.add_known(addr, name="TestButton", type="")
    assert wait_connected(api, addr, timeout=20), "Connect with no type failed"

    conn = api.connected()["devices"]
    dev = next(d for d in conn if d["address"].upper() == addr.upper())
    assert dev["type"] == ""

    api.remove_known(addr)
    assert wait_disconnected(api, addr)

    # Connect as button again
    api.add_known(addr, name="TestButton", type="button")
    assert wait_connected(api, addr, timeout=20), "Reconnect as button failed"

    conn = api.connected()["devices"]
    dev = next(d for d in conn if d["address"].upper() == addr.upper())
    assert dev["type"] == "button"
