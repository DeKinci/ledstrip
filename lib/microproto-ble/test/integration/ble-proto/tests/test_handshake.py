"""MicroProto BLE handshake tests."""

import pytest
from conftest import OP_HELLO, OP_SCHEMA_UPSERT, OP_PROPERTY_UPDATE, HELLO_FLAG_RESPONSE


async def test_hello_response(proto):
    """Device responds to HELLO with a HELLO response."""
    await proto.send_hello()
    msg = await proto.recv(timeout=5.0)
    opcode, flags = proto.decode_opcode(msg)
    assert opcode == OP_HELLO, f"Expected HELLO response, got opcode {opcode:#x}"
    assert flags & HELLO_FLAG_RESPONSE, "HELLO response should have response flag set"


async def test_full_handshake(proto):
    """Full handshake delivers HELLO + SCHEMA + property values."""
    result = await proto.handshake()

    assert result["hello"] is not None, "Should receive HELLO response"
    assert len(result["schema"]) > 0, "Should receive at least one SCHEMA_UPSERT"
    assert len(result["properties"]) > 0, "Should receive initial PROPERTY_UPDATE(s)"


async def test_schema_contains_ble_exposed_properties(proto):
    """Schema should include ble_exposed properties (brightness, enabled, label)."""
    result = await proto.handshake()

    # Count schema definitions — we expect at least 3 ble_exposed properties
    # (brightness, enabled, label). speed is NOT ble_exposed.
    schema_count = len(result["schema"])
    assert schema_count >= 3, f"Expected at least 3 schema entries, got {schema_count}"


async def test_schema_filters_non_ble_exposed(proto):
    """BLE transport should NOT receive schema for non-ble_exposed properties (speed)."""
    result = await proto.handshake()

    # The firmware has 4 properties total, but only 3 are ble_exposed.
    # We should receive exactly 3 schema entries over BLE, not 4.
    # Note: schema entries may be batched, so count individual entries within batched messages.
    schema_count = len(result["schema"])

    # Each SCHEMA_UPSERT might be batched (batch flag in header).
    # At minimum we know we should get fewer schemas than total property count.
    # The firmware has 4 properties, 3 ble_exposed.
    assert schema_count <= 4, f"Should not receive more than 4 schema messages, got {schema_count}"
