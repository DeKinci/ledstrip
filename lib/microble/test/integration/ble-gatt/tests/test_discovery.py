"""BLE GATT service discovery and structure validation."""

import pytest
import httpx
from bleak import BleakClient
from conftest import SERVICE_UUID, RX_CHAR_UUID, TX_CHAR_UUID


async def test_http_reachable(base_url):
    async with httpx.AsyncClient() as http:
        r = await http.get(f"{base_url}/ping")
        assert r.text == "pong"


async def test_ble_device_found(ble_device):
    assert ble_device is not None
    assert ble_device.address is not None


async def test_gatt_service_structure(ble_device):
    """Connect, discover services, validate UUIDs and properties."""
    client = BleakClient(ble_device)
    await client.connect()
    try:
        services = client.services

        # Service exists
        svc = services.get_service(SERVICE_UUID)
        assert svc is not None, f"Service {SERVICE_UUID} not found"

        # RX characteristic: must support write
        rx = svc.get_characteristic(RX_CHAR_UUID)
        assert rx is not None, f"RX characteristic {RX_CHAR_UUID} not found"
        assert "write" in rx.properties
        assert "write-without-response" in rx.properties

        # TX characteristic: must support notify
        tx = svc.get_characteristic(TX_CHAR_UUID)
        assert tx is not None, f"TX characteristic {TX_CHAR_UUID} not found"
        assert "notify" in tx.properties
    finally:
        await client.disconnect()


async def test_mtu_negotiated(ble):
    """Bleak should negotiate MTU above the default 23."""
    mtu = ble.client.mtu_size
    assert mtu > 23, f"Expected negotiated MTU > 23, got {mtu}"


async def test_device_reports_connection(ble, base_url):
    async with httpx.AsyncClient() as http:
        r = await http.get(f"{base_url}/debug/ble/state")
        state = r.json()
        assert state["connectedCount"] >= 1
        connected = [c for c in state["clients"] if c["connected"]]
        assert len(connected) >= 1
        assert connected[0]["mtu"] >= 23


async def test_device_has_4_slots(base_url):
    async with httpx.AsyncClient() as http:
        r = await http.get(f"{base_url}/debug/ble/state")
        state = r.json()
        assert len(state["clients"]) == 4
