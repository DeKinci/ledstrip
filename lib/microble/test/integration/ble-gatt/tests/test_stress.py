"""Stress and throughput tests."""

import pytest
import httpx



async def test_50_sequential_small(ble):
    for i in range(50):
        msg = bytes([i & 0xFF, (i >> 8) & 0xFF])
        assert await ble.echo(msg) == msg


async def test_20_medium_256_bytes(ble):
    for i in range(20):
        msg = bytes((i + j) & 0xFF for j in range(256))
        assert await ble.echo(msg) == msg


@pytest.mark.timeout(60)
async def test_10_large_1kb(ble):
    for i in range(10):
        msg = bytes((i * 13 + j) & 0xFF for j in range(1024))
        assert await ble.echo(msg) == msg


@pytest.mark.timeout(60)
async def test_size_ramp(ble):
    for size in [10, 50, 100, 200, 500, 1000, 1500]:
        msg = bytes((size + i) & 0xFF for i in range(size))
        assert await ble.echo(msg) == msg


async def test_heap_stable(base_url):
    async with httpx.AsyncClient() as http:
        r = await http.get(f"{base_url}/debug/ble/heap")
        heap = r.json()
        assert heap["freeHeap"] > 80000


async def test_stats_consistent(base_url):
    async with httpx.AsyncClient() as http:
        r = await http.get(f"{base_url}/debug/ble/stats")
        stats = r.json()
        assert stats["messagesSent"] == stats["messagesReceived"]
        assert stats["bytesSent"] == stats["bytesReceived"]
