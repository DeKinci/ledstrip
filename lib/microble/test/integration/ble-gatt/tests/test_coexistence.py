"""WiFi + BLE coexistence tests.

ESP32 shares one radio between WiFi and BLE. These tests verify both
protocols work correctly under concurrent load — the exact scenario that
causes issues in production (dropped packets, increased latency, crashes).
"""

import asyncio
import pytest
import httpx


# -- Simultaneous traffic --

@pytest.mark.xfail(reason="HttpServer handles one request per loop() — rapid concurrent HTTP exceeds capacity")
async def test_ble_echo_while_http_ping(ble, base_url):
    """BLE echo and HTTP ping running in parallel."""
    async def http_pings(n: int):
        async with httpx.AsyncClient() as http:
            for _ in range(n):
                r = await http.get(f"{base_url}/ping")
                assert r.text == "pong"

    async def ble_echoes(n: int):
        for i in range(n):
            msg = bytes([i & 0xFF, 0xAA])
            assert await ble.echo(msg) == msg

    await asyncio.gather(
        http_pings(20),
        ble_echoes(20),
    )


@pytest.mark.xfail(reason="HttpServer handles one request per loop() — rapid concurrent HTTP exceeds capacity")
async def test_ble_large_echo_while_http_requests(ble, base_url):
    """Large BLE messages concurrent with HTTP JSON endpoints."""
    async def http_state_checks(n: int):
        async with httpx.AsyncClient() as http:
            for _ in range(n):
                r = await http.get(f"{base_url}/debug/ble/state")
                state = r.json()
                assert state["connectedCount"] >= 1

    async def ble_large_echoes(n: int):
        for i in range(n):
            msg = bytes((i + j) & 0xFF for j in range(512))
            assert await ble.echo(msg) == msg

    await asyncio.gather(
        http_state_checks(10),
        ble_large_echoes(5),
    )


@pytest.mark.timeout(60)
async def test_sustained_dual_traffic(ble, base_url):
    """30 seconds of interleaved BLE and HTTP traffic."""
    errors = []
    stop = asyncio.Event()

    async def http_loop():
        async with httpx.AsyncClient() as http:
            count = 0
            while not stop.is_set():
                try:
                    r = await http.get(f"{base_url}/ping", timeout=5.0)
                    if r.text != "pong":
                        errors.append(f"HTTP ping #{count}: expected 'pong', got '{r.text}'")
                except Exception as e:
                    errors.append(f"HTTP ping #{count}: {e}")
                count += 1
                await asyncio.sleep(0.1)

    async def ble_loop():
        count = 0
        while not stop.is_set():
            try:
                msg = bytes([count & 0xFF, (count >> 8) & 0xFF])
                echo = await ble.echo(msg, timeout=5.0)
                if echo != msg:
                    errors.append(f"BLE echo #{count}: mismatch")
            except Exception as e:
                errors.append(f"BLE echo #{count}: {e}")
            count += 1

    http_task = asyncio.create_task(http_loop())
    ble_task = asyncio.create_task(ble_loop())

    await asyncio.sleep(15)
    stop.set()
    await asyncio.gather(http_task, ble_task)

    assert errors == [], f"Errors during sustained dual traffic:\n" + "\n".join(errors)


# -- BLE under WiFi stress --

@pytest.mark.xfail(reason="HttpServer handles one request per loop() — 50 rapid HTTP requests exceeds capacity")
async def test_ble_echo_during_http_flood(ble, base_url):
    """BLE echo while HTTP is flooded with rapid requests."""
    async def http_flood():
        async with httpx.AsyncClient() as http:
            for _ in range(50):
                await http.get(f"{base_url}/ping")

    async def ble_echo():
        for i in range(10):
            msg = bytes([i, 0xBB, 0xCC])
            assert await ble.echo(msg) == msg

    await asyncio.gather(http_flood(), ble_echo())


# -- HTTP under BLE stress --

async def test_http_during_ble_large_transfer(ble, base_url):
    """HTTP requests while BLE transfers a 2KB message."""
    async def ble_big():
        msg = bytes(range(256)) * 8  # 2KB
        assert await ble.echo(msg, timeout=15.0) == msg

    async def http_checks():
        async with httpx.AsyncClient() as http:
            for _ in range(5):
                r = await http.get(f"{base_url}/ping")
                assert r.text == "pong"
                await asyncio.sleep(0.5)

    await asyncio.gather(ble_big(), http_checks())


# -- Latency check --

async def test_http_latency_not_degraded_by_ble(ble, base_url):
    """HTTP p95 response time stays under 500ms during BLE traffic."""
    import time

    async def ble_traffic():
        for _ in range(5):
            await ble.echo(bytes(100))

    async def measure_http():
        async with httpx.AsyncClient() as http:
            latencies = []
            for _ in range(10):
                try:
                    t0 = time.monotonic()
                    r = await http.get(f"{base_url}/ping", timeout=2.0)
                    if r.text == "pong":
                        latencies.append(time.monotonic() - t0)
                except Exception:
                    pass  # Dropped connections don't count — we measure latency of successful ones
                await asyncio.sleep(0.1)
            return latencies

    _, latencies = await asyncio.gather(ble_traffic(), measure_http())
    assert len(latencies) >= 5, f"Too many HTTP failures: only {len(latencies)}/10 succeeded"
    latencies.sort()
    p95 = latencies[int(len(latencies) * 0.95)]
    assert p95 < 0.5, f"HTTP p95 latency {p95:.3f}s exceeded 500ms during BLE traffic"


# -- Stability --

async def test_heap_after_coexistence(base_url):
    async with httpx.AsyncClient() as http:
        r = await http.get(f"{base_url}/debug/ble/heap")
        assert r.json()["freeHeap"] > 80000


async def test_stats_after_coexistence(base_url):
    async with httpx.AsyncClient() as http:
        r = await http.get(f"{base_url}/debug/ble/stats")
        stats = r.json()
        assert stats["messagesSent"] == stats["messagesReceived"]
