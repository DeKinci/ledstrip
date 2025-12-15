"""
WiFiMan API integration tests
"""

import time
from . import Colors, TestResult

try:
    import requests
    HAS_REQUESTS = True
except ImportError:
    HAS_REQUESTS = False


def test_wifiman_status(ip: str) -> TestResult:
    """Test WiFiMan status endpoint returns valid JSON."""
    if not HAS_REQUESTS:
        return TestResult("WiFiMan Status", False, error="requests not installed")

    print(f"\n{Colors.CYAN}{Colors.BOLD}[TEST] WiFiMan Status{Colors.RESET}")
    url = f"http://{ip}/wifiman/status"

    result = TestResult("WiFiMan Status", True)

    try:
        start = time.time()
        response = requests.get(url, timeout=5)
        elapsed = (time.time() - start) * 1000

        if response.status_code != 200:
            result.fail_count += 1
            result.passed = False
            print(f"  {Colors.RED}FAIL{Colors.RESET} HTTP {response.status_code}")
            return result

        data = response.json()
        required_fields = ['state', 'connected', 'ssid', 'ip', 'apMode']
        missing = [f for f in required_fields if f not in data]

        if missing:
            result.fail_count += 1
            result.passed = False
            print(f"  {Colors.RED}FAIL{Colors.RESET} Missing fields: {missing}")
        else:
            result.success_count += 1
            result.times.append(elapsed)
            print(f"  {Colors.GREEN}OK{Colors.RESET} {elapsed:6.1f}ms")
            print(f"      state={data['state']}, connected={data['connected']}, ssid={data['ssid']}")

    except Exception as e:
        result.fail_count += 1
        result.passed = False
        print(f"  {Colors.RED}Error: {str(e)[:50]}{Colors.RESET}")

    return result


def test_wifiman_list(ip: str) -> TestResult:
    """Test WiFiMan saved networks list endpoint."""
    if not HAS_REQUESTS:
        return TestResult("WiFiMan List", False, error="requests not installed")

    print(f"\n{Colors.CYAN}{Colors.BOLD}[TEST] WiFiMan Saved Networks{Colors.RESET}")
    url = f"http://{ip}/wifiman/list"

    result = TestResult("WiFiMan List", True)

    try:
        start = time.time()
        response = requests.get(url, timeout=5)
        elapsed = (time.time() - start) * 1000

        if response.status_code != 200:
            result.fail_count += 1
            result.passed = False
            print(f"  {Colors.RED}FAIL{Colors.RESET} HTTP {response.status_code}")
            return result

        data = response.json()

        if 'networks' not in data:
            result.fail_count += 1
            result.passed = False
            print(f"  {Colors.RED}FAIL{Colors.RESET} Missing 'networks' field")
        elif not isinstance(data['networks'], list):
            result.fail_count += 1
            result.passed = False
            print(f"  {Colors.RED}FAIL{Colors.RESET} 'networks' is not a list")
        else:
            result.success_count += 1
            result.times.append(elapsed)
            print(f"  {Colors.GREEN}OK{Colors.RESET} {elapsed:6.1f}ms - {len(data['networks'])} saved network(s)")
            for net in data['networks'][:3]:  # Show up to 3
                print(f"      - {net.get('ssid', '?')} (priority={net.get('priority', '?')})")

    except Exception as e:
        result.fail_count += 1
        result.passed = False
        print(f"  {Colors.RED}Error: {str(e)[:50]}{Colors.RESET}")

    return result


def test_wifiman_scan(ip: str) -> TestResult:
    """Test WiFiMan scan endpoint."""
    if not HAS_REQUESTS:
        return TestResult("WiFiMan Scan", False, error="requests not installed")

    print(f"\n{Colors.CYAN}{Colors.BOLD}[TEST] WiFiMan Scan{Colors.RESET}")
    url = f"http://{ip}/wifiman/scan"

    result = TestResult("WiFiMan Scan", True)

    try:
        # First request might start a scan
        start = time.time()
        response = requests.get(url, timeout=5)
        elapsed = (time.time() - start) * 1000

        if response.status_code != 200:
            result.fail_count += 1
            result.passed = False
            print(f"  {Colors.RED}FAIL{Colors.RESET} HTTP {response.status_code}")
            return result

        data = response.json()

        if 'status' in data and data['status'] == 'scanning':
            print(f"  {Colors.YELLOW}Scan in progress{Colors.RESET}, waiting...")
            time.sleep(3)
            # Retry
            start = time.time()
            response = requests.get(url, timeout=5)
            elapsed = (time.time() - start) * 1000
            data = response.json()

        if 'networks' not in data:
            result.fail_count += 1
            result.passed = False
            print(f"  {Colors.RED}FAIL{Colors.RESET} Missing 'networks' field")
        elif not isinstance(data['networks'], list):
            result.fail_count += 1
            result.passed = False
            print(f"  {Colors.RED}FAIL{Colors.RESET} 'networks' is not a list")
        else:
            result.success_count += 1
            result.times.append(elapsed)
            print(f"  {Colors.GREEN}OK{Colors.RESET} {elapsed:6.1f}ms - {len(data['networks'])} network(s) found")
            for net in data['networks'][:5]:  # Show up to 5
                encrypted = "encrypted" if net.get('encrypted') else "open"
                print(f"      - {net.get('ssid', '?')} ({net.get('rssi', '?')} dBm, {encrypted})")

    except Exception as e:
        result.fail_count += 1
        result.passed = False
        print(f"  {Colors.RED}Error: {str(e)[:50]}{Colors.RESET}")

    return result


def test_wifiman_page(ip: str) -> TestResult:
    """Test WiFiMan HTML page loads."""
    if not HAS_REQUESTS:
        return TestResult("WiFiMan Page", False, error="requests not installed")

    print(f"\n{Colors.CYAN}{Colors.BOLD}[TEST] WiFiMan Page{Colors.RESET}")
    url = f"http://{ip}/wifiman"

    result = TestResult("WiFiMan Page", True)

    try:
        start = time.time()
        response = requests.get(url, timeout=5)
        elapsed = (time.time() - start) * 1000

        if response.status_code != 200:
            result.fail_count += 1
            result.passed = False
            print(f"  {Colors.RED}FAIL{Colors.RESET} HTTP {response.status_code}")
        elif '<!DOCTYPE html>' not in response.text and '<html' not in response.text:
            result.fail_count += 1
            result.passed = False
            print(f"  {Colors.RED}FAIL{Colors.RESET} Not HTML content")
        else:
            result.success_count += 1
            result.times.append(elapsed)
            print(f"  {Colors.GREEN}OK{Colors.RESET} {elapsed:6.1f}ms - {len(response.text)} bytes")

    except Exception as e:
        result.fail_count += 1
        result.passed = False
        print(f"  {Colors.RED}Error: {str(e)[:50]}{Colors.RESET}")

    return result


def test_wifiman_add_remove(ip: str) -> TestResult:
    """Test adding and removing a network (non-destructive)."""
    if not HAS_REQUESTS:
        return TestResult("WiFiMan Add/Remove", False, error="requests not installed")

    print(f"\n{Colors.CYAN}{Colors.BOLD}[TEST] WiFiMan Add/Remove Network{Colors.RESET}")

    result = TestResult("WiFiMan Add/Remove", True)
    test_ssid = "__test_network_12345__"

    try:
        # Add a test network
        start = time.time()
        response = requests.post(
            f"http://{ip}/wifiman/add",
            json={"ssid": test_ssid, "password": "testpass", "priority": 1},
            timeout=5
        )
        elapsed = (time.time() - start) * 1000

        if response.status_code != 200:
            result.fail_count += 1
            result.passed = False
            print(f"  Add: {Colors.RED}FAIL{Colors.RESET} HTTP {response.status_code}")
            return result

        data = response.json()
        if not data.get('success'):
            result.fail_count += 1
            result.passed = False
            print(f"  Add: {Colors.RED}FAIL{Colors.RESET} {data.get('error', 'unknown error')}")
            return result

        result.success_count += 1
        result.times.append(elapsed)
        print(f"  Add: {Colors.GREEN}OK{Colors.RESET} {elapsed:6.1f}ms")

        # Verify it's in the list
        response = requests.get(f"http://{ip}/wifiman/list", timeout=5)
        data = response.json()
        found = any(n.get('ssid') == test_ssid for n in data.get('networks', []))

        if found:
            result.success_count += 1
            print(f"  Verify: {Colors.GREEN}OK{Colors.RESET} Network found in list")
        else:
            result.fail_count += 1
            result.passed = False
            print(f"  Verify: {Colors.RED}FAIL{Colors.RESET} Network not in list")

        # Remove the test network
        start = time.time()
        response = requests.post(
            f"http://{ip}/wifiman/remove",
            json={"ssid": test_ssid},
            timeout=5
        )
        elapsed = (time.time() - start) * 1000

        if response.status_code != 200:
            result.fail_count += 1
            result.passed = False
            print(f"  Remove: {Colors.RED}FAIL{Colors.RESET} HTTP {response.status_code}")
            return result

        data = response.json()
        if not data.get('success'):
            result.fail_count += 1
            result.passed = False
            print(f"  Remove: {Colors.RED}FAIL{Colors.RESET} {data.get('error', 'unknown error')}")
            return result

        result.success_count += 1
        result.times.append(elapsed)
        print(f"  Remove: {Colors.GREEN}OK{Colors.RESET} {elapsed:6.1f}ms")

        # Verify it's gone
        response = requests.get(f"http://{ip}/wifiman/list", timeout=5)
        data = response.json()
        found = any(n.get('ssid') == test_ssid for n in data.get('networks', []))

        if not found:
            result.success_count += 1
            print(f"  Cleanup: {Colors.GREEN}OK{Colors.RESET} Network removed from list")
        else:
            result.fail_count += 1
            result.passed = False
            print(f"  Cleanup: {Colors.RED}FAIL{Colors.RESET} Network still in list")

    except Exception as e:
        result.fail_count += 1
        result.passed = False
        print(f"  {Colors.RED}Error: {str(e)[:50]}{Colors.RESET}")

        # Try to clean up
        try:
            requests.post(f"http://{ip}/wifiman/remove", json={"ssid": test_ssid}, timeout=2)
        except:
            pass

    return result