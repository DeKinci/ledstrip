"""Connection lifecycle tests — reconnect cycles, slot reuse, state tracking."""

import asyncio
import pytest
import httpx
from bleak import BleakClient
from conftest import BleEchoClient


@pytest.mark.timeout(90)
async def test_5_reconnect_cycles(ble_device, base_url):
    """5 connect → echo small + large → disconnect cycles."""
    async with httpx.AsyncClient() as http:
        await http.post(f"{base_url}/debug/ble/reset")

    for cycle in range(5):
        client = BleakClient(ble_device)
        await client.connect()
        echo = BleEchoClient(client)
        await echo.start()

        msg = f"cycle-{cycle}".encode()
        assert await echo.echo(msg) == msg

        big = bytes([(cycle + j) & 0xFF for j in range(500)])
        assert await echo.echo(big) == big

        await echo.stop()
        await client.disconnect()
        await asyncio.sleep(0.3)

    async with httpx.AsyncClient() as http:
        r = await http.get(f"{base_url}/debug/ble/state")
        assert r.json()["connectedCount"] == 0

        r = await http.get(f"{base_url}/debug/ble/stats")
        stats = r.json()
        assert stats["messagesReceived"] >= 10
        assert stats["messagesSent"] == stats["messagesReceived"]
        assert stats["connects"] >= 5
        assert stats["disconnects"] >= 5


@pytest.mark.timeout(30)
async def test_rapid_reconnect_3_times(ble_device):
    """3 quick connect/disconnect without data exchange, then normal use."""
    for _ in range(3):
        client = BleakClient(ble_device)
        await client.connect()
        await client.disconnect()
        await asyncio.sleep(0.2)

    # Now actually use the connection
    client = BleakClient(ble_device)
    await client.connect()
    echo = BleEchoClient(client)
    await echo.start()
    assert await echo.echo(b"after-rapid") == b"after-rapid"
    await echo.stop()
    await client.disconnect()


async def test_slot_freed_on_disconnect(ble_device, base_url):
    """Verify HTTP debug shows 0 connections after disconnect."""
    client = BleakClient(ble_device)
    await client.connect()

    async with httpx.AsyncClient() as http:
        r = await http.get(f"{base_url}/debug/ble/state")
        assert r.json()["connectedCount"] >= 1

    await client.disconnect()
    await asyncio.sleep(0.5)

    async with httpx.AsyncClient() as http:
        r = await http.get(f"{base_url}/debug/ble/state")
        assert r.json()["connectedCount"] == 0


async def test_heap_after_lifecycle(base_url):
    async with httpx.AsyncClient() as http:
        r = await http.get(f"{base_url}/debug/ble/heap")
        assert r.json()["freeHeap"] > 80000
