"""Tests for BLE scanning — PC advertises as HID peripheral, ESP32 finds it."""

import time
import pytest
from conftest import PERIPHERAL_NAME


def find_peripheral_in_results(results, hid_peripheral):
    """Find our bless peripheral in ESP32 scan results by address or name."""
    for d in results.get("devices", []):
        # Match by address if we know it
        if hid_peripheral.address and d["address"].upper() == hid_peripheral.address.upper():
            return d
        # Match by name
        if d["name"] and PERIPHERAL_NAME in d["name"]:
            return d
    return None


def scan_and_find(api, hid_peripheral, timeout=15):
    """Trigger scan and wait for our peripheral to appear."""
    api.scan()
    for _ in range(timeout * 2):
        time.sleep(0.5)
        results = api.scan_results()
        dev = find_peripheral_in_results(results, hid_peripheral)
        if dev:
            return dev
        if not results["scanning"]:
            break
    return None


def test_scan_finds_peripheral(api, hid_peripheral):
    """Trigger active scan, verify the PC's HID peripheral appears in results."""
    dev = scan_and_find(api, hid_peripheral)
    results = api.scan_results()
    assert dev is not None, (
        f"{PERIPHERAL_NAME} (addr={hid_peripheral.address}) not found in scan results. "
        f"Got: {[(d['name'], d['address']) for d in results['devices']]}"
    )


def test_scan_result_has_address(api, hid_peripheral):
    """Scanned device should have a valid BLE address."""
    dev = scan_and_find(api, hid_peripheral)
    assert dev is not None, f"{PERIPHERAL_NAME} not found"
    assert len(dev["address"]) == 17  # "XX:XX:XX:XX:XX:XX"


def test_scan_can_be_triggered_while_idle(api, hid_peripheral):
    """Starting a scan while idle should succeed."""
    result = api.scan()
    assert result.get("success") is True

    # Wait for it to finish
    for _ in range(25):
        time.sleep(0.5)
        if not api.scan_results()["scanning"]:
            break
