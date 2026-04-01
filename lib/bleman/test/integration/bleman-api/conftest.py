"""Shared fixtures for BleMan integration tests.

Key fixtures:
  - ip, base_url: device HTTP access
  - hid_peripheral: PC acts as a BLE HID button via bless
  - api: helper for /bleman/* HTTP calls
  - debug: helper for /debug/bleman/* HTTP calls
"""

import os
import asyncio
import time
import requests
import pytest
import pytest_asyncio
from bless import BlessServer, BlessGATTCharacteristic, GATTCharacteristicProperties, GATTAttributePermissions


# Custom service UUIDs — must match firmware's TEST_BTN_SERVICE_UUID / TEST_BTN_REPORT_UUID.
# Uses custom UUIDs instead of HID 0x1812 because CoreBluetooth blocks reserved SIG UUIDs.
SERVICE_UUID = "bbb10001-f5a3-4aa0-b726-5d1be14a1d00"
REPORT_UUID  = "bbb10002-f5a3-4aa0-b726-5d1be14a1d00"


def device_ip():
    ip = os.environ.get("DEVICE_IP")
    if not ip:
        raise RuntimeError("DEVICE_IP not set")
    return ip


@pytest.fixture(scope="session")
def ip():
    return device_ip()


@pytest.fixture(scope="session")
def base_url(ip):
    return f"http://{ip}"


class BleManAPI:
    """Helper for /bleman/* HTTP endpoints."""

    def __init__(self, base_url: str):
        self.base = base_url

    def types(self):
        return requests.get(f"{self.base}/bleman/types").json()

    def scan(self):
        return requests.post(f"{self.base}/bleman/scan").json()

    def scan_results(self):
        return requests.get(f"{self.base}/bleman/scan/results").json()

    def known(self):
        return requests.get(f"{self.base}/bleman/known").json()

    def add_known(self, address, name="", icon="generic", type="", auto_connect=True):
        return requests.post(f"{self.base}/bleman/known", json={
            "address": address,
            "name": name,
            "icon": icon,
            "type": type,
            "autoConnect": auto_connect,
        }).json()

    def remove_known(self, address):
        return requests.delete(f"{self.base}/bleman/known/{address}").json()

    def connected(self):
        return requests.get(f"{self.base}/bleman/connected").json()

    def connect(self, address):
        return requests.post(f"{self.base}/bleman/connect/{address}").json()

    def disconnect(self, address):
        return requests.post(f"{self.base}/bleman/disconnect/{address}").json()


class DebugAPI:
    """Helper for /debug/bleman/* HTTP endpoints."""

    def __init__(self, base_url: str):
        self.base = base_url

    def state(self):
        return requests.get(f"{self.base}/debug/bleman/state").json()

    def actions(self):
        return requests.get(f"{self.base}/debug/bleman/actions").json()

    def reset(self):
        return requests.post(f"{self.base}/debug/bleman/reset").json()

    def heap(self):
        return requests.get(f"{self.base}/debug/bleman/heap").json()


@pytest.fixture(scope="session")
def api(base_url):
    return BleManAPI(base_url)


@pytest.fixture(scope="session")
def debug(base_url):
    return DebugAPI(base_url)


PERIPHERAL_NAME = "TBtn"


class HIDPeripheral:
    """PC acting as a BLE HID button device via bless.

    Advertises HID service (0x1812) with a Report characteristic.
    Tests can call press() / release() to send HID notifications.
    """

    def __init__(self, server: BlessServer, address: str):
        self.server = server
        self.address = address
        self.name = PERIPHERAL_NAME

    async def press(self):
        """Send button press (non-zero report)."""
        self.server.get_characteristic(REPORT_UUID).value = bytes([0x01])
        self.server.update_value(SERVICE_UUID, REPORT_UUID)

    async def release(self):
        """Send button release (zero report)."""
        self.server.get_characteristic(REPORT_UUID).value = bytes([0x00])
        self.server.update_value(SERVICE_UUID, REPORT_UUID)

    async def click(self, hold_ms=50):
        """Press and release with a short delay."""
        await self.press()
        await asyncio.sleep(hold_ms / 1000.0)
        await self.release()


def _find_peripheral_in_esp32_scan(ip, timeout=15):
    """Trigger ESP32 scan and look for our peripheral."""
    requests.post(f"http://{ip}/bleman/scan")
    for _ in range(timeout * 2):
        time.sleep(0.5)
        try:
            data = requests.get(f"http://{ip}/bleman/scan/results", timeout=3).json()
        except Exception:
            continue
        for d in data.get("devices", []):
            if d.get("name") and PERIPHERAL_NAME in d["name"]:
                return d["address"]
        if not data.get("scanning", True):
            break
    return None


@pytest_asyncio.fixture(scope="session", loop_scope="session")
async def hid_peripheral(ip):
    """Start a BLE HID peripheral on the PC.

    Advertises as "TBtn" with custom service UUID.
    Verifies the ESP32 can discover it before proceeding.
    Retries bless start up to 3 times if not discoverable.
    """
    server = BlessServer(name=PERIPHERAL_NAME)

    await server.add_new_service(SERVICE_UUID)
    await server.add_new_characteristic(
        SERVICE_UUID,
        REPORT_UUID,
        GATTCharacteristicProperties.notify,
        None,
        GATTAttributePermissions.readable,
    )

    await server.start()
    await asyncio.sleep(1.0)

    # Verify bless thinks it's advertising
    is_adv = await server.is_advertising()
    print(f"bless is_advertising: {is_adv}")
    if not is_adv:
        # Wait and check again
        await asyncio.sleep(2.0)
        is_adv = await server.is_advertising()
        print(f"bless is_advertising (retry): {is_adv}")

    assert is_adv, "bless failed to start advertising"

    # Verify ESP32 can discover us — retry scans
    address = None
    for attempt in range(5):
        address = _find_peripheral_in_esp32_scan(ip)
        if address:
            print(f"Peripheral discoverable at {address} (scan attempt {attempt + 1})")
            break
        print(f"Peripheral not found by ESP32 (scan attempt {attempt + 1}), retrying...")
        await asyncio.sleep(2.0)

    assert address is not None, (
        f"Peripheral {PERIPHERAL_NAME} not discoverable by ESP32 after 5 scan attempts. "
        f"bless is_advertising={is_adv}. Check CoreBluetooth/bless state."
    )

    peripheral = HIDPeripheral(server, address)

    yield peripheral

    await server.stop()


@pytest.fixture(autouse=True)
def reset_actions(debug):
    """Reset the action log before each test."""
    debug.reset()
