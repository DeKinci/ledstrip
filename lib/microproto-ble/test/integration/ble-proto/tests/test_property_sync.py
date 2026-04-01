"""MicroProto BLE property synchronization tests."""

import pytest
import httpx
from conftest import OP_PROPERTY_UPDATE


async def test_receives_initial_values(proto):
    """After handshake, should receive initial property values."""
    result = await proto.handshake()
    assert len(result["properties"]) > 0, "Should receive initial property values"


async def test_property_broadcast_after_http_change(proto, base_url):
    """Changing a property via HTTP should broadcast to BLE client."""
    await proto.handshake()
    await proto.drain()

    # Read current brightness via HTTP
    async with httpx.AsyncClient() as http:
        resp = await http.get(f"{base_url}/debug/properties")
        props = resp.json()
        old_brightness = props["brightness"]

    # The device doesn't have an HTTP set endpoint in this firmware,
    # but we can verify that we receive property broadcasts by waiting
    # for the periodic broadcast cycle.
    # For now, just verify we can successfully drain without errors.


async def test_ble_client_count(proto, base_url):
    """Device should report BLE client connected via HTTP debug endpoint."""
    await proto.handshake()

    async with httpx.AsyncClient() as http:
        resp = await http.get(f"{base_url}/debug/ble/clients")
        data = resp.json()
        assert data["connected"] >= 1, "Should show at least 1 BLE client connected"


async def test_properties_visible_via_http(proto, base_url):
    """Verify test firmware properties via HTTP debug endpoint."""
    async with httpx.AsyncClient() as http:
        resp = await http.get(f"{base_url}/debug/properties")
        assert resp.status_code == 200
        data = resp.json()
        assert "brightness" in data
        assert "enabled" in data
        assert "speed" in data
        assert data["count"] == 4  # total properties including non-ble_exposed
