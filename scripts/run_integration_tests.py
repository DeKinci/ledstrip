#!/usr/bin/env python3
"""
ESP32 Integration Test Runner

Auto-discovers ESP32 device via ARP and runs a full test suite.

Usage:
    python scripts/run_integration_tests.py              # Auto-discover and run all
    python scripts/run_integration_tests.py 192.168.1.100  # Use specific IP
    pio run -t test_device                               # Via PlatformIO
"""

import subprocess
import sys
import statistics
from pathlib import Path
from typing import List

# Ensure we can import from test/integration
sys.path.insert(0, str(Path(__file__).parent.parent / 'test'))

from integration import Colors, TestResult, discover_esp32
from integration.test_http import test_http_reliability, test_http_post
from integration.test_websocket import test_websocket_reliability
from integration.test_parallel import test_parallel
from integration.test_wifiman import (
    test_wifiman_status,
    test_wifiman_list,
    test_wifiman_scan,
    test_wifiman_page,
    test_wifiman_add_remove,
)
from integration.test_microproto import (
    test_microproto,
    test_microproto_stress,
    test_microproto_reconnect,
    test_microproto_ping,
    test_schema_constraints,
    test_constraint_validation,
    test_container_updates,
)
from integration.test_led_api import (
    test_shader_list,
    test_shader_crud,
    test_animation_status,
    test_ble_scan,
    test_ble_known_devices,
    test_ble_connected,
)


def run_test_suite(ip: str) -> List[TestResult]:
    """Run all integration tests."""
    results = []

    # HTTP Tests
    results.append(test_http_reliability(ip, count=15))
    results.append(test_http_post(ip))

    # WebSocket Tests
    results.append(test_websocket_reliability(ip, count=15))

    # Parallel Tests
    results.append(test_parallel(ip, duration=5))

    # WiFiMan Tests
    results.append(test_wifiman_status(ip))
    results.append(test_wifiman_list(ip))
    results.append(test_wifiman_scan(ip))
    results.append(test_wifiman_page(ip))
    results.append(test_wifiman_add_remove(ip))

    # MicroProto Tests
    results.append(test_microproto(ip))
    results.append(test_microproto_ping(ip, count=10))
    results.append(test_schema_constraints(ip))
    results.append(test_constraint_validation(ip))
    results.append(test_container_updates(ip))
    results.append(test_microproto_stress(ip, duration=5))
    results.append(test_microproto_reconnect(ip, count=5))

    # LED API Tests
    results.append(test_shader_list(ip))
    results.append(test_shader_crud(ip))
    results.append(test_animation_status(ip))

    # BLE API Tests
    results.append(test_ble_scan(ip))
    results.append(test_ble_known_devices(ip))
    results.append(test_ble_connected(ip))

    return results


def print_summary(results: List[TestResult]) -> bool:
    """Print test suite summary. Returns True if all passed."""
    print(f"\n{'='*60}")
    print(f"{Colors.BOLD}TEST SUITE SUMMARY{Colors.RESET}")
    print('='*60)

    all_passed = True
    total_success = 0
    total_fail = 0

    for result in results:
        if result.error:
            status = f"{Colors.YELLOW}SKIP{Colors.RESET}"
            detail = result.error
        elif result.passed:
            status = f"{Colors.GREEN}PASS{Colors.RESET}"
            detail = f"{result.success_count}/{result.total}"
            if result.times:
                detail += f" (avg {statistics.mean(result.times):.1f}ms)"
        else:
            status = f"{Colors.RED}FAIL{Colors.RESET}"
            detail = f"{result.success_count}/{result.total}"
            all_passed = False

        print(f"  {status}  {result.name:25s} {detail}")
        total_success += result.success_count
        total_fail += result.fail_count

    print('='*60)
    print(f"Total: {total_success} passed, {total_fail} failed")

    if all_passed:
        print(f"\n{Colors.GREEN}{Colors.BOLD}ALL TESTS PASSED{Colors.RESET}")
    else:
        print(f"\n{Colors.RED}{Colors.BOLD}SOME TESTS FAILED{Colors.RESET}")

    return all_passed


def ensure_venv():
    """Ensure we're running in the venv, restart if not."""
    venv_python = Path(__file__).parent.parent / '.venv' / 'bin' / 'python'

    if hasattr(sys, 'real_prefix') or (hasattr(sys, 'base_prefix') and sys.base_prefix != sys.prefix):
        return

    if venv_python.exists():
        print(f"{Colors.YELLOW}Restarting in venv...{Colors.RESET}")
        import os
        os.execv(str(venv_python), [str(venv_python)] + sys.argv)
    else:
        print(f"{Colors.YELLOW}Warning: venv not found at {venv_python}{Colors.RESET}")
        print("Run: python -m venv .venv && .venv/bin/pip install requests websockets aiohttp")


def main():
    """Main entry point."""
    ensure_venv()

    print(f"\n{Colors.BOLD}ESP32 Integration Test Suite{Colors.RESET}")
    print('='*60)

    # Get IP from args or auto-discover
    if len(sys.argv) > 1:
        ip = sys.argv[1]
        print(f"Using provided IP: {ip}")
    else:
        ip = discover_esp32()
        if not ip:
            print(f"\n{Colors.RED}Cannot run tests without device IP{Colors.RESET}")
            print("Usage: python scripts/run_integration_tests.py [IP_ADDRESS]")
            sys.exit(1)

    # Connectivity check
    print(f"\nChecking connectivity to {ip}...")
    result = subprocess.run(['ping', '-c', '1', '-t', '2', ip], capture_output=True)
    if result.returncode != 0:
        print(f"{Colors.RED}Device not responding{Colors.RESET}")
        sys.exit(1)
    print(f"{Colors.GREEN}Device is reachable{Colors.RESET}")

    # Run test suite
    results = run_test_suite(ip)

    # Print summary
    all_passed = print_summary(results)

    sys.exit(0 if all_passed else 1)


if __name__ == "__main__":
    main()