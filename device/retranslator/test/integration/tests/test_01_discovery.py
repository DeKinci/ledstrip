"""Test BLE discovery and basic identity of both retranslator boards."""

from conftest import DEVICE_ID_A, DEVICE_ID_B, RetranslatorClient


async def test_both_boards_found(both_boards):
    assert "a" in both_boards
    assert "b" in both_boards


async def test_board_a_identity(board_a: RetranslatorClient):
    info = await board_a.get_self_info()
    assert info.device_id == DEVICE_ID_A


async def test_board_b_identity(board_b: RetranslatorClient):
    info = await board_b.get_self_info()
    assert info.device_id == DEVICE_ID_B


async def test_get_state_works(board_a: RetranslatorClient, board_b: RetranslatorClient):
    state_a = await board_a.get_state()
    state_b = await board_b.get_state()
    assert isinstance(state_a.entries, list)
    assert isinstance(state_b.entries, list)
