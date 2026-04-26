"""Test live LoRa message relay: send on board A, verify board B receives it."""

import asyncio

from conftest import DEVICE_ID_A, RetranslatorClient


async def test_text_a_to_b(board_a: RetranslatorClient, board_b: RetranslatorClient):
    """Send text on A, B should receive it via LoRa live broadcast."""
    # Record B's state before
    state_before = await board_b.get_state()
    a_before = next((e for e in state_before.entries if e.sender_id == DEVICE_ID_A), None)
    seq_before = a_before.high_seq if a_before else 0

    text = "hello from A"
    await board_a.send_text(text)

    # Wait for B's state to update — live broadcast + digest sync fallback
    for _ in range(60):
        await asyncio.sleep(0.5)
        await board_b.drain()
        state = await board_b.get_state()
        a_entry = next((e for e in state.entries if e.sender_id == DEVICE_ID_A), None)
        if a_entry and a_entry.high_seq > seq_before:
            break
    else:
        assert False, "Board B did not receive text from A within 30s"

    # Verify the message content
    await board_b.drain()
    messages = await board_b.get_messages(DEVICE_ID_A, a_entry.high_seq, timeout=5.0)
    assert len(messages) > 0, "Could not retrieve message from B"
    msg = messages[0]
    assert msg.sender_id == DEVICE_ID_A
    assert msg.msg_type == 0x02  # Text
    assert msg.payload[0] == len(text.encode())
    assert msg.payload[1:] == text.encode()


async def test_location_a_to_b(board_a: RetranslatorClient, board_b: RetranslatorClient):
    """Send location on A, B should receive it via LoRa."""
    state_before = await board_b.get_state()
    a_before = next((e for e in state_before.entries if e.sender_id == DEVICE_ID_A), None)
    seq_before = a_before.high_seq if a_before else 0

    await board_a.set_location(5, 6)

    # Wait for state update on B
    for _ in range(60):
        await asyncio.sleep(0.5)
        await board_b.drain()
        state = await board_b.get_state()
        a_entry = next((e for e in state.entries if e.sender_id == DEVICE_ID_A), None)
        if a_entry and a_entry.high_seq > seq_before:
            break
    else:
        assert False, "Board B did not receive location from A within 30s"

    # Verify location in state
    assert a_entry.node_a == 5
    assert a_entry.node_b == 6


async def test_message_stored_on_b(board_a: RetranslatorClient, board_b: RetranslatorClient):
    """After relay, B's state should show A as a sender, messages retrievable."""
    text = "stored check"
    await board_a.send_text(text)

    # Wait for live delivery
    await board_b.recv_or_none(timeout=5.0)
    await asyncio.sleep(0.5)

    # Verify state
    state = await board_b.get_state()
    a_entry = next((e for e in state.entries if e.sender_id == DEVICE_ID_A), None)
    assert a_entry is not None, f"Board B state has no entry for A. Entries: {state.entries}"
    assert a_entry.high_seq > 0

    # Retrieve messages
    await board_b.drain(timeout=0.5)
    messages = await board_b.get_messages(DEVICE_ID_A, 1, timeout=5.0)
    assert len(messages) > 0, "No messages from A on board B"
    texts = [m for m in messages if m.msg_type == 0x02]
    assert len(texts) > 0
