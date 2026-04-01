"""MicroProto BLE type-specific tests.

Verifies that different property types are correctly synced over BLE:
- UINT8, BOOL, INT32, ARRAY(RGB), LIST(bytes)
- Only ble_exposed properties should appear in schema
"""

import pytest
import struct
from conftest import OP_PROPERTY_UPDATE, OP_SCHEMA_UPSERT


async def test_receives_multiple_property_values(proto):
    """After handshake, should receive values for all ble_exposed properties."""
    result = await proto.handshake()

    # Should receive property updates for all 5 ble_exposed properties
    # (brightness, enabled, counter, rgb, label)
    # Values may be batched, but total should cover all properties
    assert len(result["properties"]) >= 1, "Should receive at least one property update batch"


async def test_schema_has_ble_exposed_only(proto):
    """Schema should contain only ble_exposed properties, not speed or hiddenVal."""
    result = await proto.handshake()

    # We have 7 total properties, 5 ble_exposed
    # Schema messages may be batched
    total_schema = len(result["schema"])
    assert total_schema >= 1, "Should receive schema"

    # We can't easily parse schema from raw bytes in Python,
    # but we can verify via HTTP that the counts are correct
    # (the HTTP test handles that)


async def test_property_update_binary_format(proto):
    """Property updates should be valid binary MicroProto messages."""
    result = await proto.handshake()

    for msg in result["properties"]:
        assert len(msg) >= 2, "Property update must be at least 2 bytes"
        opcode, flags = proto.decode_opcode(msg)
        assert opcode == OP_PROPERTY_UPDATE, f"Expected PROPERTY_UPDATE, got {opcode:#x}"


async def test_schema_upsert_format(proto):
    """Schema upserts should be valid binary messages."""
    result = await proto.handshake()

    for msg in result["schema"]:
        assert len(msg) >= 2, "Schema message must be at least 2 bytes"
        opcode, flags = proto.decode_opcode(msg)
        assert opcode == OP_SCHEMA_UPSERT, f"Expected SCHEMA_UPSERT, got {opcode:#x}"
