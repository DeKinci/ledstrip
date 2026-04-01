"""Tests for full connect flow — scan, add, connect, verify driver init."""

import time
import pytest
from tests.test_01_scan import scan_and_find


_cached_addr = None


def find_test_button(api, hid_peripheral):
    """Return the peripheral's address as seen by ESP32.
    Uses the address discovered during hid_peripheral fixture setup.
    Falls back to a fresh scan if needed.
    """
    global _cached_addr
    if _cached_addr:
        return _cached_addr
    # The hid_peripheral fixture already verified discoverability and has the address
    if hid_peripheral.address:
        _cached_addr = hid_peripheral.address
        return _cached_addr
    # Fallback: try a fresh scan
    dev = scan_and_find(api, hid_peripheral)
    if dev:
        _cached_addr = dev["address"]
    return _cached_addr


def wait_connected(api, address, timeout=15):
    """Poll until a device appears in the connected list."""
    for _ in range(timeout * 2):
        time.sleep(0.5)
        devices = api.connected()["devices"]
        for d in devices:
            if d["address"].upper() == address.upper():
                return True
    return False


def wait_disconnected(api, address, timeout=10):
    """Poll until a device disappears from the connected list."""
    for _ in range(timeout * 2):
        time.sleep(0.5)
        devices = api.connected()["devices"]
        if not any(d["address"].upper() == address.upper() for d in devices):
            return True
    return False


def ensure_removed(api, address):
    """Remove device from known list and wait for disconnect."""
    api.remove_known(address)
    wait_disconnected(api, address)


def test_full_connect_flow(api, debug, hid_peripheral):
    """Scan -> add as known with type 'button' -> auto-connect -> driver init."""
    addr = find_test_button(api, hid_peripheral)
    assert addr is not None, "TestButton not found in scan"

    api.add_known(addr, name="TestButton", type="button", auto_connect=True)
    assert wait_connected(api, addr), (
        f"TestButton ({addr}) did not connect. "
        f"Connected: {api.connected()['devices']}"
    )

    devices = api.connected()["devices"]
    dev = next((d for d in devices if d["address"].upper() == addr.upper()), None)
    assert dev is not None
    assert dev["type"] == "button"

    state = debug.state()
    assert state["connectedCount"] >= 1

    ensure_removed(api, addr)


def test_manual_connect(api, hid_peripheral):
    """Add device without auto-connect, then trigger manual connect."""
    addr = find_test_button(api, hid_peripheral)
    assert addr is not None, "TestButton not found in scan"

    api.add_known(addr, name="TestButton", type="button", auto_connect=False)
    api.connect(addr)
    assert wait_connected(api, addr), "Manual connect failed"

    ensure_removed(api, addr)


def test_disconnect(api, hid_peripheral):
    """Connect then remove -> device disconnects."""
    addr = find_test_button(api, hid_peripheral)
    assert addr is not None

    api.add_known(addr, name="TestButton", type="button")
    assert wait_connected(api, addr), "Failed to connect"

    api.remove_known(addr)
    assert wait_disconnected(api, addr), "Device did not disconnect after remove"


def test_connect_unknown_type_no_driver(api, hid_peripheral):
    """Device with unregistered type connects but has no driver."""
    addr = find_test_button(api, hid_peripheral)
    assert addr is not None

    ensure_removed(api, addr)

    api.add_known(addr, name="TestButton", type="mystery")
    assert wait_connected(api, addr), "Failed to connect"

    devices = api.connected()["devices"]
    dev = next((d for d in devices if d["address"].upper() == addr.upper()), None)
    assert dev is not None
    assert dev["type"] == "mystery"

    ensure_removed(api, addr)


def test_connect_already_connected(api, hid_peripheral):
    """Connecting to an already-connected device returns success, doesn't duplicate."""
    addr = find_test_button(api, hid_peripheral)
    assert addr is not None

    ensure_removed(api, addr)

    api.add_known(addr, name="TestButton", type="button")
    assert wait_connected(api, addr), "Initial connect failed"

    # Try to connect again
    result = api.connect(addr)
    assert result.get("success") is True

    # Should still be exactly one connection
    devices = api.connected()["devices"]
    matches = [d for d in devices if d["address"].upper() == addr.upper()]
    assert len(matches) == 1, f"Expected 1 connection, got {len(matches)}"

    ensure_removed(api, addr)


def test_disconnect_already_disconnected(api, hid_peripheral):
    """Disconnecting a device that's not connected returns error."""
    addr = find_test_button(api, hid_peripheral)
    assert addr is not None

    ensure_removed(api, addr)

    result = api.disconnect(addr)
    assert result.get("error") is not None


def test_remove_during_scan(api, hid_peripheral):
    """Remove a known device while an active scan is running."""
    addr = find_test_button(api, hid_peripheral)
    assert addr is not None

    ensure_removed(api, addr)

    api.add_known(addr, name="TestButton", type="button")

    # Trigger scan and immediately remove
    api.scan()
    time.sleep(0.5)  # Let scan start
    result = api.remove_known(addr)
    assert result.get("success") is True

    # Device should not be in known list
    devices = api.known()["devices"]
    addrs = [d["address"].upper() for d in devices]
    assert addr.upper() not in addrs

    # Wait for scan to finish
    for _ in range(25):
        time.sleep(0.5)
        if not api.scan_results()["scanning"]:
            break


def test_scan_while_connected(api, hid_peripheral):
    """Trigger active scan while a device is connected — should not disrupt connection."""
    addr = find_test_button(api, hid_peripheral)
    assert addr is not None

    ensure_removed(api, addr)

    api.add_known(addr, name="TestButton", type="button")
    assert wait_connected(api, addr), "Connect failed"

    # Trigger scan
    api.scan()
    time.sleep(1)

    # Device should still be connected during scan
    devices = api.connected()["devices"]
    connected_addrs = [d["address"].upper() for d in devices]
    assert addr.upper() in connected_addrs

    # Wait for scan to complete
    for _ in range(25):
        time.sleep(0.5)
        if not api.scan_results()["scanning"]:
            break

    # Still connected after scan
    devices = api.connected()["devices"]
    connected_addrs = [d["address"].upper() for d in devices]
    assert addr.upper() in connected_addrs

    ensure_removed(api, addr)


def test_fill_known_device_slots(api, hid_peripheral):
    """Fill all known device slots, verify add fails when full."""
    addr = find_test_button(api, hid_peripheral)
    assert addr is not None
    ensure_removed(api, addr)

    # Fill slots with fake addresses
    fake_addrs = []
    for i in range(8):
        fake = f"FA:KE:00:00:00:{i:02X}"
        fake_addrs.append(fake)
        result = api.add_known(fake, name=f"Fake{i}", type="", auto_connect=False)
        assert result.get("success") is True, f"Failed to add device {i}"

    # Verify all slots full
    assert len(api.known()["devices"]) == 8

    # Adding one more should fail
    result = api.add_known("FA:KE:FF:FF:FF:FF", name="Overflow", auto_connect=False)
    assert result.get("error") is not None or result.get("success") is not True

    # Clean up
    for fake in fake_addrs:
        api.remove_known(fake)


def test_rapid_connect_disconnect_cycles(api, hid_peripheral):
    """Multiple connect/disconnect cycles without waiting for full completion."""
    addr = find_test_button(api, hid_peripheral)
    assert addr is not None

    ensure_removed(api, addr)

    for i in range(3):
        api.add_known(addr, name="TestButton", type="button")
        assert wait_connected(api, addr, timeout=15), f"Connect failed on cycle {i}"
        api.remove_known(addr)
        assert wait_disconnected(api, addr), f"Disconnect failed on cycle {i}"

    # Device should be stable
    state = api.connected()
    assert len(state["devices"]) == 0
