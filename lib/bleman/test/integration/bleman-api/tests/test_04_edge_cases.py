"""Edge case tests for BleMan — disconnect during hold, reconnect, etc."""

import time
import asyncio
import pytest

from tests.test_02_connect import find_test_button, wait_connected, wait_disconnected, ensure_removed


@pytest.fixture
def button_addr(api, hid_peripheral):
    """Get the TestButton address and ensure clean state. Retries scan if needed."""
    addr = find_test_button(api, hid_peripheral)
    if addr is None:
        time.sleep(3)
        from tests.test_01_scan import scan_and_find
        dev = scan_and_find(api, hid_peripheral, timeout=15)
        if dev:
            addr = dev["address"]
    assert addr is not None, "TestButton not found after retry"
    ensure_removed(api, addr)
    yield addr
    ensure_removed(api, addr)


def test_reconnect_after_remove_readd(api, debug, hid_peripheral, button_addr):
    """Remove known device, re-add, should reconnect."""
    addr = button_addr

    api.add_known(addr, name="TestButton", type="button")
    assert wait_connected(api, addr), "Initial connect failed"

    # Remove and wait for disconnect (may take a while for async BLE cleanup)
    api.remove_known(addr)
    assert wait_disconnected(api, addr, timeout=15), "Did not disconnect after remove"

    api.add_known(addr, name="TestButton", type="button")
    assert wait_connected(api, addr, timeout=20), "Reconnect after re-add failed"


def test_add_device_no_type(api, hid_peripheral, button_addr):
    """Adding device without a type — connects. Verify via known list (not connected)."""
    addr = button_addr

    api.add_known(addr, name="TestButton", type="")

    # Verify known device has empty type
    devices = api.known()["devices"]
    dev = next((d for d in devices if d["address"].upper() == addr.upper()), None)
    assert dev is not None
    assert dev["type"] == ""

    # Device should still connect (type just means no driver)
    assert wait_connected(api, addr), "Connect failed"


async def test_disconnect_during_hold(api, debug, hid_peripheral, button_addr):
    """Disconnect while button is held — should not crash."""
    addr = button_addr

    api.add_known(addr, name="TestButton", type="button")
    assert wait_connected(api, addr), "Connect failed"
    # Wait for driver to subscribe
    await asyncio.sleep(2)
    debug.reset()

    await hid_peripheral.press()
    await asyncio.sleep(0.6)  # Past hold threshold

    api.remove_known(addr)
    await asyncio.sleep(1.0)

    await hid_peripheral.release()
    await asyncio.sleep(0.5)

    # Device should be stable
    heap = debug.heap()
    assert heap["freeHeap"] > 10000, "Heap looks unhealthy after disconnect-during-hold"


def test_rapid_add_remove(api, hid_peripheral, button_addr):
    """Rapidly add/remove the same device — should not corrupt state."""
    addr = button_addr

    for _ in range(5):
        api.add_known(addr, name="TestButton", type="button")
        api.remove_known(addr)

    # Final state: no known devices with this address
    devices = api.known()["devices"]
    addrs = [d["address"].upper() for d in devices]
    assert addr.upper() not in addrs


async def test_click_during_disconnect(api, debug, hid_peripheral, button_addr):
    """Send a click notification while disconnect is in progress."""
    addr = button_addr

    api.add_known(addr, name="TestButton", type="button")
    assert wait_connected(api, addr), "Connect failed"
    await asyncio.sleep(2)  # Wait for driver subscribe
    debug.reset()

    # Click and immediately disconnect
    await hid_peripheral.click(hold_ms=30)
    api.remove_known(addr)

    await asyncio.sleep(2)

    # Device should be stable — no crash
    heap = debug.heap()
    assert heap["freeHeap"] > 10000


def test_add_same_device_twice(api, hid_peripheral, button_addr):
    """Adding the same device twice updates it, doesn't duplicate."""
    addr = button_addr

    api.add_known(addr, name="TestButton", type="button")
    api.add_known(addr, name="UpdatedName", type="button")

    devices = api.known()["devices"]
    matches = [d for d in devices if d["address"].upper() == addr.upper()]
    assert len(matches) == 1, f"Expected 1 device, got {len(matches)}"
    assert matches[0]["name"] == "UpdatedName"


def test_change_type_while_connected(api, debug, hid_peripheral, button_addr):
    """Changing device type in known list while connected doesn't crash.
    The connected device retains its original type until reconnect."""
    addr = button_addr

    api.add_known(addr, name="TestButton", type="button")
    assert wait_connected(api, addr), "Connect failed"

    # Update type in known list
    api.add_known(addr, name="TestButton", type="sensor")

    # Known list should reflect new type
    devices = api.known()["devices"]
    dev = next(d for d in devices if d["address"].upper() == addr.upper())
    assert dev["type"] == "sensor"

    # Connected device retains old type (from connection time)
    conn = api.connected()["devices"]
    cdev = next((d for d in conn if d["address"].upper() == addr.upper()), None)
    assert cdev is not None  # Still connected, not crashed

    # Device should be stable
    heap = debug.heap()
    assert heap["freeHeap"] > 10000


async def test_heap_stability_connect_cycle(api, debug, hid_peripheral, button_addr):
    """Full connect+button+disconnect cycle should not leak memory."""
    addr = button_addr

    # Baseline heap
    heap_before = debug.heap()["freeHeap"]

    # Connect, use buttons, disconnect — 3 cycles
    for i in range(3):
        api.add_known(addr, name="TestButton", type="button")
        assert wait_connected(api, addr, timeout=15), f"Connect failed cycle {i}"
        await asyncio.sleep(2)  # Driver subscribe

        # Do some button activity
        await hid_peripheral.click(hold_ms=50)
        await asyncio.sleep(0.5)

        api.remove_known(addr)
        assert wait_disconnected(api, addr), f"Disconnect failed cycle {i}"

    # Check heap after cycles
    time.sleep(2)
    heap_after = debug.heap()["freeHeap"]

    # Allow some variance (NimBLE internal allocations) but not a major leak
    leak = heap_before - heap_after
    assert leak < 4096, f"Heap leaked {leak} bytes over 3 connect cycles (before={heap_before}, after={heap_after})"


def test_rapid_add_remove_10x(api, debug, hid_peripheral, button_addr):
    """10 rapid add/remove cycles — more aggressive than the 5x test."""
    addr = button_addr

    for _ in range(10):
        api.add_known(addr, name="TestButton", type="button")
        api.remove_known(addr)

    devices = api.known()["devices"]
    addrs = [d["address"].upper() for d in devices]
    assert addr.upper() not in addrs

    # Device should be responsive
    state = debug.state()
    assert state["connectedCount"] == 0
