"""Tests for /bleman/* HTTP API — no BLE peripheral needed."""

import pytest


def test_ping(base_url):
    """Device is reachable."""
    import requests
    r = requests.get(f"{base_url}/ping")
    assert r.status_code == 200


def test_types_includes_button(api):
    """Button driver should be registered."""
    data = api.types()
    assert "button" in data["types"]


def test_known_initially_empty(api):
    """No known devices after fresh boot."""
    data = api.known()
    assert len(data["devices"]) == 0


def test_add_known_device(api):
    """Add a device, verify it appears in the list."""
    api.add_known("AA:BB:CC:DD:EE:01", name="TestDev", type="button")
    data = api.known()
    addrs = [d["address"] for d in data["devices"]]
    assert "AA:BB:CC:DD:EE:01" in addrs

    # Verify fields
    dev = next(d for d in data["devices"] if d["address"] == "AA:BB:CC:DD:EE:01")
    assert dev["name"] == "TestDev"
    assert dev["type"] == "button"
    assert dev["autoConnect"] is True


def test_remove_known_device(api):
    """Remove a device, verify it's gone."""
    api.add_known("AA:BB:CC:DD:EE:02", name="ToRemove")
    result = api.remove_known("AA:BB:CC:DD:EE:02")
    assert result.get("success") is True

    data = api.known()
    addrs = [d["address"] for d in data["devices"]]
    assert "AA:BB:CC:DD:EE:02" not in addrs


def test_remove_nonexistent_returns_404(base_url):
    """Removing an unknown address returns 404."""
    import requests
    r = requests.delete(f"{base_url}/bleman/known/FF:FF:FF:FF:FF:FF")
    assert r.status_code == 404


def test_add_known_missing_address(base_url):
    """Adding without address returns 400."""
    import requests
    r = requests.post(f"{base_url}/bleman/known", json={"name": "NoAddr"})
    assert r.status_code == 400


def test_connected_initially_empty(api):
    """No connected devices at start."""
    data = api.connected()
    assert len(data["devices"]) == 0


def test_scan_results_structure(api):
    """Scan results endpoint returns valid structure."""
    data = api.scan_results()
    assert "scanning" in data
    assert "devices" in data
    assert isinstance(data["devices"], list)


def test_debug_state(debug):
    """Debug endpoint returns valid state."""
    state = debug.state()
    assert "knownCount" in state
    assert "connectedCount" in state
    assert "scanning" in state
    assert "driverTypes" in state
    assert state["driverTypes"] >= 1


def test_debug_actions_initially_empty(debug):
    """No actions logged at start."""
    data = debug.actions()
    assert data["total"] == 0
    assert len(data["actions"]) == 0


def test_debug_heap(debug):
    """Heap endpoint returns reasonable value."""
    data = debug.heap()
    assert data["freeHeap"] > 10000


@pytest.fixture(autouse=True)
def cleanup_known(api):
    """Clean up known devices after each test."""
    yield
    # Remove all known devices
    data = api.known()
    for dev in data["devices"]:
        api.remove_known(dev["address"])
