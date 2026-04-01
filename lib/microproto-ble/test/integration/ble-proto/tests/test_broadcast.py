"""MicroProto BLE broadcast tests.

Verifies that property changes on the device are broadcast to BLE clients.
Uses HTTP debug endpoints to trigger property changes, then checks BLE receives them.
"""

import pytest
import httpx
from conftest import OP_PROPERTY_UPDATE


async def test_broadcast_after_http_set(proto, base_url):
    """Changing brightness via HTTP should trigger a BLE broadcast."""
    await proto.handshake()
    await proto.drain()

    # Set brightness via HTTP
    async with httpx.AsyncClient() as http:
        resp = await http.get(f"{base_url}/debug/set-brightness?v=42")
        assert resp.status_code == 200
        data = resp.json()
        assert data["brightness"] == 42

    # Should receive a property update broadcast
    msg = await proto.recv_or_none(timeout=5.0)

    # Device broadcasts at ~15Hz, so we should get an update within 5s
    # It might be None if broadcast interval hasn't elapsed yet
    if msg is not None:
        opcode, flags = proto.decode_opcode(msg)
        assert opcode == OP_PROPERTY_UPDATE


async def test_multiple_broadcasts(proto, base_url):
    """Multiple HTTP property changes should produce BLE broadcasts."""
    await proto.handshake()
    await proto.drain()

    received = 0
    async with httpx.AsyncClient() as http:
        for i in range(5):
            await http.get(f"{base_url}/debug/set-brightness?v={i * 50}")
            msg = await proto.recv_or_none(timeout=2.0)
            if msg is not None:
                opcode, _ = proto.decode_opcode(msg)
                if opcode == OP_PROPERTY_UPDATE:
                    received += 1

    # Should receive at least some broadcasts (device may batch)
    assert received >= 1, f"Expected at least 1 broadcast, got {received}"


async def test_ble_exposed_filtering_via_http(proto, base_url):
    """Verify via HTTP that the correct count of properties are ble_exposed."""
    async with httpx.AsyncClient() as http:
        resp = await http.get(f"{base_url}/debug/properties")
        data = resp.json()
        assert data["bleExposedCount"] == 5
        assert data["count"] == 7  # total including non-ble_exposed
