"""
HTTP endpoint tests
"""

import time
from . import Colors, TestResult

try:
    import requests
    HAS_REQUESTS = True
except ImportError:
    HAS_REQUESTS = False


def test_http_reliability(ip: str, count: int = 20) -> TestResult:
    """Test HTTP endpoint reliability with multiple requests."""
    if not HAS_REQUESTS:
        return TestResult("HTTP Reliability", False, error="requests not installed")

    print(f"\n{Colors.CYAN}{Colors.BOLD}[TEST] HTTP Reliability ({count} requests){Colors.RESET}")
    url = f"http://{ip}/ping"

    result = TestResult("HTTP Reliability", True)

    for i in range(count):
        try:
            start = time.time()
            response = requests.get(url, timeout=5)
            elapsed = (time.time() - start) * 1000

            if response.status_code == 200:
                result.success_count += 1
                result.times.append(elapsed)
                status = f"{Colors.GREEN}OK{Colors.RESET}"
            else:
                result.fail_count += 1
                status = f"{Colors.RED}HTTP {response.status_code}{Colors.RESET}"
                result.passed = False

            print(f"  {i+1:2d}. {status} {elapsed:6.1f}ms")
        except Exception as e:
            result.fail_count += 1
            result.passed = False
            print(f"  {i+1:2d}. {Colors.RED}Error: {str(e)[:30]}{Colors.RESET}")

        time.sleep(0.1)

    return result


def test_http_post(ip: str) -> TestResult:
    """Test HTTP POST with echo endpoint."""
    if not HAS_REQUESTS:
        return TestResult("HTTP POST", False, error="requests not installed")

    print(f"\n{Colors.CYAN}{Colors.BOLD}[TEST] HTTP POST Echo{Colors.RESET}")
    url = f"http://{ip}/echo"
    test_data = '{"test": "data", "number": 42}'

    result = TestResult("HTTP POST", True)

    for i in range(5):
        try:
            start = time.time()
            response = requests.post(url, data=test_data, timeout=5)
            elapsed = (time.time() - start) * 1000

            if response.status_code == 200 and response.text == test_data:
                result.success_count += 1
                result.times.append(elapsed)
                print(f"  {i+1}. {Colors.GREEN}OK{Colors.RESET} {elapsed:6.1f}ms")
            else:
                result.fail_count += 1
                result.passed = False
                print(f"  {i+1}. {Colors.RED}Mismatch{Colors.RESET}")
        except Exception as e:
            result.fail_count += 1
            result.passed = False
            print(f"  {i+1}. {Colors.RED}Error: {str(e)[:30]}{Colors.RESET}")

        time.sleep(0.1)

    return result