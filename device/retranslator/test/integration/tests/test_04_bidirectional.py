"""Test bidirectional message sync: both boards send, both should converge."""

import asyncio

from conftest import DEVICE_ID_A, DEVICE_ID_B, RetranslatorClient


async def _wait_sender_seq(board: RetranslatorClient, sender_id: int, min_seq: int, timeout: float = 30.0):
    """Poll board state until sender's highSeq >= min_seq."""
    for _ in range(int(timeout * 2)):
        await asyncio.sleep(0.5)
        await board.drain()
        state = await board.get_state()
        entry = next((e for e in state.entries if e.sender_id == sender_id), None)
        if entry and entry.high_seq >= min_seq:
            return entry
    state = await board.get_state()
    entry = next((e for e in state.entries if e.sender_id == sender_id), None)
    return entry


async def test_bidirectional_text(board_a: RetranslatorClient, board_b: RetranslatorClient):
    """Send text on each board, verify both have each other's messages."""
    state_b = await board_b.get_state()
    a_before = next((e for e in state_b.entries if e.sender_id == DEVICE_ID_A), None)
    a_seq = a_before.high_seq if a_before else 0

    state_a = await board_a.get_state()
    b_before = next((e for e in state_a.entries if e.sender_id == DEVICE_ID_B), None)
    b_seq = b_before.high_seq if b_before else 0

    await board_a.send_text("from A bidir")
    await asyncio.sleep(1.0)
    await board_b.send_text("from B bidir")

    # Wait for both directions
    a_on_b = await _wait_sender_seq(board_b, DEVICE_ID_A, a_seq + 1)
    assert a_on_b is not None, "Board B has no entry for sender A"

    b_on_a = await _wait_sender_seq(board_a, DEVICE_ID_B, b_seq + 1)
    assert b_on_a is not None, "Board A has no entry for sender B"


async def test_multiple_messages_converge(board_a: RetranslatorClient, board_b: RetranslatorClient):
    """Send multiple messages each way, verify all arrive."""
    state_b = await board_b.get_state()
    a_before = next((e for e in state_b.entries if e.sender_id == DEVICE_ID_A), None)
    a_seq = a_before.high_seq if a_before else 0

    state_a = await board_a.get_state()
    b_before = next((e for e in state_a.entries if e.sender_id == DEVICE_ID_B), None)
    b_seq = b_before.high_seq if b_before else 0

    # Send 3 from A, 2 from B — space out to avoid LoRa collisions
    for i in range(3):
        await board_a.send_text(f"multi-A-{i}")
        await asyncio.sleep(0.5)
    for i in range(2):
        await board_b.send_text(f"multi-B-{i}")
        await asyncio.sleep(0.5)

    a_on_b = await _wait_sender_seq(board_b, DEVICE_ID_A, a_seq + 3)
    assert a_on_b is not None
    assert a_on_b.high_seq >= a_seq + 3, (
        f"Expected A highSeq >= {a_seq + 3} on B, got {a_on_b.high_seq}"
    )

    b_on_a = await _wait_sender_seq(board_a, DEVICE_ID_B, b_seq + 2)
    assert b_on_a is not None
    assert b_on_a.high_seq >= b_seq + 2, (
        f"Expected B highSeq >= {b_seq + 2} on A, got {b_on_a.high_seq}"
    )
