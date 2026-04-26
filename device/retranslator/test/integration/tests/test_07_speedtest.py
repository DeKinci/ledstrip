"""LoRa radio diagnostic speedtest — ping-pong RTT and loss measurement."""

import asyncio

from conftest import RetranslatorClient


async def test_speedtest_basic(board_a: RetranslatorClient, board_b: RetranslatorClient):
    """Run a 20-ping speedtest and verify results structure."""
    await board_a.start_speedtest(count=20, interval_ms=500, payload_size=10)

    # Wait: 20 * 500ms sending + 5s pong timeout + margin
    await asyncio.sleep(18)

    results = await board_a.get_speedtest_results()
    assert results is not None, "No speedtest results (test still running or idle)"
    assert results.total_sent == 20
    assert results.count == 20
    assert results.interval_ms == 500
    assert results.payload_size == 10
    assert results.total_received >= 10, f"Only {results.total_received}/20 pongs received"
    assert results.total_lost == results.total_sent - results.total_received
    assert results.test_duration_ms > 0

    print(f"\n  === Speedtest Results (20 pings, 500ms, 10B payload) ===")
    print(f"  Sent: {results.total_sent}  Received: {results.total_received}  Lost: {results.total_lost}")
    print(f"  Loss rate: {results.loss_rate_x10 / 10:.1f}%")
    print(f"  RTT min={results.rtt_min}ms max={results.rtt_max}ms avg={results.rtt_avg}ms")
    print(f"  RTT p1={results.rtt_p1} p5={results.rtt_p5} p50={results.rtt_p50} "
          f"p95={results.rtt_p95} p99={results.rtt_p99}")
    print(f"  Duration: {results.test_duration_ms}ms  Avg interval: {results.actual_interval_avg_ms}ms")


async def test_speedtest_fast_interval(board_a: RetranslatorClient, board_b: RetranslatorClient):
    """Run at aggressive 200ms interval — expect higher loss, measures LoRa limits."""
    await board_a.start_speedtest(count=50, interval_ms=200, payload_size=10)

    await asyncio.sleep(18)

    results = await board_a.get_speedtest_results()
    assert results is not None
    assert results.total_sent == 50
    assert results.total_received > 0, "Zero pongs — radio link may be down"

    print(f"\n  === Fast Speedtest (50 pings, 200ms) ===")
    print(f"  Received: {results.total_received}/50  Loss: {results.loss_rate_x10 / 10:.1f}%")
    print(f"  RTT p50={results.rtt_p50}ms p95={results.rtt_p95}ms p99={results.rtt_p99}ms")


async def test_speedtest_large_payload(board_a: RetranslatorClient, board_b: RetranslatorClient):
    """Test with max payload to measure impact on RTT and loss."""
    await board_a.start_speedtest(count=20, interval_ms=500, payload_size=90)

    await asyncio.sleep(18)

    results = await board_a.get_speedtest_results()
    assert results is not None
    assert results.total_sent == 20
    assert results.payload_size == 90
    assert results.total_received >= 5, f"Only {results.total_received}/20 with large payload"

    print(f"\n  === Large Payload Speedtest (20 pings, 500ms, 90B) ===")
    print(f"  Received: {results.total_received}/20  Loss: {results.loss_rate_x10 / 10:.1f}%")
    print(f"  RTT p50={results.rtt_p50}ms p99={results.rtt_p99}ms")


async def test_speedtest_rtt_sanity(board_a: RetranslatorClient, board_b: RetranslatorClient):
    """Verify RTT percentiles are ordered correctly."""
    await board_a.start_speedtest(count=30, interval_ms=500, payload_size=10)

    await asyncio.sleep(22)

    results = await board_a.get_speedtest_results()
    assert results is not None
    assert results.total_received >= 10, "Not enough pongs for percentile validation"

    # Percentiles must be monotonically non-decreasing
    pcts = [
        results.rtt_p1, results.rtt_p5, results.rtt_p10, results.rtt_p25,
        results.rtt_p50, results.rtt_p75, results.rtt_p90, results.rtt_p95, results.rtt_p99,
    ]
    for i in range(1, len(pcts)):
        assert pcts[i] >= pcts[i - 1], f"Non-monotonic percentiles: {pcts}"

    assert results.rtt_min <= results.rtt_p1
    assert results.rtt_p99 <= results.rtt_max
    assert results.rtt_min <= results.rtt_avg <= results.rtt_max


async def test_speedtest_status_while_running(board_a: RetranslatorClient, board_b: RetranslatorClient):
    """Query results while test is still running — should get status response."""
    await board_a.start_speedtest(count=50, interval_ms=500, payload_size=10)

    # Query immediately — test is still running
    await asyncio.sleep(1.0)
    results = await board_a.get_speedtest_results()
    assert results is None, "Expected None (running status), got full results"

    # Wait for test to complete
    await asyncio.sleep(30)

    results = await board_a.get_speedtest_results()
    assert results is not None, "Test should be done by now"
    assert results.total_sent == 50

    print(f"\n  === Status Check Test (50 pings, 500ms) ===")
    print(f"  Received: {results.total_received}/50  Loss: {results.loss_rate_x10 / 10:.1f}%")
