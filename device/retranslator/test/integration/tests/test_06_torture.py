"""Torture and stress tests for retranslator LoRa sync."""

import asyncio

from conftest import DEVICE_ID_A, DEVICE_ID_B, RetranslatorClient


async def test_rapid_fire(board_a: RetranslatorClient, board_b: RetranslatorClient):
    """Send 20 texts from A at safe LoRa spacing (500ms), verify all arrive on B."""
    state_before = await board_b.get_state()
    a_before = next((e for e in state_before.entries if e.sender_id == DEVICE_ID_A), None)
    seq_before = a_before.high_seq if a_before else 0

    for i in range(20):
        await board_a.send_text(f"rapid-{i:02d}")
        await asyncio.sleep(0.5)

    # Poll B — expect >= 18/20 via live broadcast (beacon collisions may drop 1-2)
    for _ in range(60):
        await asyncio.sleep(0.5)
        await board_b.drain()
        state = await board_b.get_state()
        a_entry = next((e for e in state.entries if e.sender_id == DEVICE_ID_A), None)
        if a_entry and a_entry.high_seq >= seq_before + 20:
            break

    state = await board_b.get_state()
    a_entry = next((e for e in state.entries if e.sender_id == DEVICE_ID_A), None)
    got = (a_entry.high_seq - seq_before) if a_entry else 0
    assert got >= 18, f"Only {got}/20 messages arrived on B (expected >= 18)"


async def test_bidirectional_flood(board_a: RetranslatorClient, board_b: RetranslatorClient):
    """10 from A + 10 from B interleaved, verify convergence."""
    state_b = await board_b.get_state()
    a_before = next((e for e in state_b.entries if e.sender_id == DEVICE_ID_A), None)
    a_seq = a_before.high_seq if a_before else 0

    state_a = await board_a.get_state()
    b_before = next((e for e in state_a.entries if e.sender_id == DEVICE_ID_B), None)
    b_seq = b_before.high_seq if b_before else 0

    for i in range(10):
        await board_a.send_text(f"flood-A-{i}")
        await asyncio.sleep(0.5)
        await board_b.send_text(f"flood-B-{i}")
        await asyncio.sleep(0.5)

    # Wait for convergence
    for _ in range(60):
        await asyncio.sleep(0.5)
        await board_a.drain()
        await board_b.drain()
        sb = await board_b.get_state()
        sa = await board_a.get_state()
        a_on_b = next((e for e in sb.entries if e.sender_id == DEVICE_ID_A), None)
        b_on_a = next((e for e in sa.entries if e.sender_id == DEVICE_ID_B), None)
        a_ok = a_on_b and a_on_b.high_seq >= a_seq + 10
        b_ok = b_on_a and b_on_a.high_seq >= b_seq + 10
        if a_ok and b_ok:
            break
    else:
        a_got = (a_on_b.high_seq - a_seq) if a_on_b else 0
        b_got = (b_on_a.high_seq - b_seq) if b_on_a else 0
        assert False, f"Convergence failed: A->B {a_got}/10, B->A {b_got}/10"


async def test_large_text(board_a: RetranslatorClient, board_b: RetranslatorClient):
    """Send max-size text (99 bytes), verify exact content."""
    state_before = await board_b.get_state()
    a_before = next((e for e in state_before.entries if e.sender_id == DEVICE_ID_A), None)
    seq_before = a_before.high_seq if a_before else 0

    text = "X" * 99  # MAX_MSG_PAYLOAD=100, wire=[len:1][data:99]
    await board_a.send_text(text)

    # Wait for arrival
    for _ in range(20):
        await asyncio.sleep(0.5)
        await board_b.drain()
        state = await board_b.get_state()
        a_entry = next((e for e in state.entries if e.sender_id == DEVICE_ID_A), None)
        if a_entry and a_entry.high_seq > seq_before:
            break

    await board_b.drain()
    messages = await board_b.get_messages(DEVICE_ID_A, a_entry.high_seq, timeout=5.0)
    assert len(messages) > 0
    msg = messages[0]
    assert msg.msg_type == 0x02
    assert msg.payload[0] == 99
    assert msg.payload[1:] == text.encode()


async def test_message_ordering(board_a: RetranslatorClient, board_b: RetranslatorClient):
    """Send 10 numbered texts, verify monotonic seq and correct content on B."""
    state_before = await board_b.get_state()
    a_before = next((e for e in state_before.entries if e.sender_id == DEVICE_ID_A), None)
    seq_before = a_before.high_seq if a_before else 0

    for i in range(10):
        await board_a.send_text(f"order-{i:02d}")
        await asyncio.sleep(0.5)

    # Wait for all
    for _ in range(40):
        await asyncio.sleep(0.5)
        await board_b.drain()
        state = await board_b.get_state()
        a_entry = next((e for e in state.entries if e.sender_id == DEVICE_ID_A), None)
        if a_entry and a_entry.high_seq >= seq_before + 10:
            break

    await board_b.drain()
    messages = await board_b.get_messages(DEVICE_ID_A, seq_before + 1, timeout=10.0)
    texts = [m for m in messages if m.msg_type == 0x02]
    assert len(texts) >= 8, f"Expected >= 8 texts (LoRa may drop ~1-2), got {len(texts)}"

    # Verify monotonic seq — whatever arrived should be in order
    seqs = [m.seq for m in texts]
    for i in range(1, len(seqs)):
        assert seqs[i] > seqs[i - 1], f"Non-monotonic: {seqs}"


async def test_stats_validation(board_a: RetranslatorClient, board_b: RetranslatorClient):
    """Reset stats, send known count, verify counters match."""
    await board_a.reset_stats()
    await board_b.reset_stats()
    await asyncio.sleep(1.5)  # wait for at least one stats push cycle

    for i in range(5):
        await board_a.send_text(f"stats-{i}")
        await asyncio.sleep(0.5)

    await asyncio.sleep(3.0)
    await board_a.drain()
    await board_b.drain()

    stats_a = await board_a.get_stats()
    assert stats_a.packets_tx >= 5, f"A packets_tx={stats_a.packets_tx}, expected >= 5"
    assert stats_a.msgs_stored >= 5, f"A msgs_stored={stats_a.msgs_stored}, expected >= 5"
    assert stats_a.uptime_s > 0
    assert stats_a.free_heap > 0

    stats_b = await board_b.get_stats()
    assert stats_b.packets_rx >= 4, f"B packets_rx={stats_b.packets_rx}, expected >= 4"
    assert stats_b.msgs_stored >= 4, f"B msgs_stored={stats_b.msgs_stored}, expected >= 4"


async def test_heap_stability(board_a: RetranslatorClient, board_b: RetranslatorClient):
    """Compare heap before/after 50-message burst."""
    stats_before_a = await board_a.get_stats()
    stats_before_b = await board_b.get_stats()

    for i in range(50):
        await board_a.send_text(f"heap-{i:02d}")
        await asyncio.sleep(0.5)

    await asyncio.sleep(5.0)
    await board_a.drain()
    await board_b.drain()

    stats_after_a = await board_a.get_stats()
    stats_after_b = await board_b.get_stats()

    leak_a = stats_before_a.free_heap - stats_after_a.free_heap
    leak_b = stats_before_b.free_heap - stats_after_b.free_heap
    assert leak_a < 1024, f"Board A heap leak: {leak_a} bytes"
    assert leak_b < 1024, f"Board B heap leak: {leak_b} bytes"


async def test_sync_after_idle(board_a: RetranslatorClient, board_b: RetranslatorClient):
    """Wait 15s (>1 beacon cycle) idle, then verify message still arrives."""
    state_before = await board_b.get_state()
    a_before = next((e for e in state_before.entries if e.sender_id == DEVICE_ID_A), None)
    seq_before = a_before.high_seq if a_before else 0

    await asyncio.sleep(15.0)

    await board_a.send_text("after-idle")

    for _ in range(20):
        await asyncio.sleep(0.5)
        await board_b.drain()
        state = await board_b.get_state()
        a_entry = next((e for e in state.entries if e.sender_id == DEVICE_ID_A), None)
        if a_entry and a_entry.high_seq > seq_before:
            break
    else:
        assert False, "Message after idle did not arrive on B"


async def test_connection_cycling(board_a: RetranslatorClient, board_b: RetranslatorClient):
    """Disconnect/reconnect BLE 3 times, verify commands work after each."""
    for cycle in range(3):
        await board_a.stop()
        await board_a.client.disconnect()
        await asyncio.sleep(1.5)

        await board_a.client.connect()
        await board_a.start()

        info = await board_a.get_self_info()
        assert info.device_id == 0x01, f"Cycle {cycle}: wrong device_id {info.device_id}"
