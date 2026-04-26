"""Test presence detection via beacon exchange."""

import asyncio

from conftest import DEVICE_ID_A, DEVICE_ID_B, RetranslatorClient


async def test_board_b_sees_a_online(board_a: RetranslatorClient, board_b: RetranslatorClient):
    """After beacons exchange, B should report A as Online."""
    # Beacons fire every 10s — wait up to 15s for presence to propagate
    for _ in range(30):
        state = await board_b.get_state()
        a_entry = next((e for e in state.entries if e.sender_id == DEVICE_ID_A), None)
        if a_entry and a_entry.presence == 0:  # Online
            break
        await asyncio.sleep(0.5)
        await board_b.drain()
    else:
        state = await board_b.get_state()
        a_entry = next((e for e in state.entries if e.sender_id == DEVICE_ID_A), None)
        assert a_entry is not None, f"Board B has no entry for A. State: {state.entries}"
        assert a_entry.presence == 0, f"Expected Online (0), got {a_entry.presence}"


async def test_board_a_sees_b_online(board_a: RetranslatorClient, board_b: RetranslatorClient):
    """Symmetric: A should also report B as Online."""
    for _ in range(30):
        state = await board_a.get_state()
        b_entry = next((e for e in state.entries if e.sender_id == DEVICE_ID_B), None)
        if b_entry and b_entry.presence == 0:
            break
        await asyncio.sleep(0.5)
        await board_a.drain()
    else:
        state = await board_a.get_state()
        b_entry = next((e for e in state.entries if e.sender_id == DEVICE_ID_B), None)
        assert b_entry is not None, f"Board A has no entry for B. State: {state.entries}"
        assert b_entry.presence == 0, f"Expected Online (0), got {b_entry.presence}"
