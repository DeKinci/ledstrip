"""
ESP32 Integration Test Suite - Shared utilities
"""

import subprocess
import re
import time
from dataclasses import dataclass, field
from typing import Optional, List


class Colors:
    GREEN = '\033[92m'
    RED = '\033[91m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    CYAN = '\033[96m'
    MAGENTA = '\033[95m'
    RESET = '\033[0m'
    BOLD = '\033[1m'


@dataclass
class TestResult:
    """Result of a single test."""
    name: str
    passed: bool
    success_count: int = 0
    fail_count: int = 0
    times: List[float] = field(default_factory=list)
    error: Optional[str] = None

    @property
    def total(self) -> int:
        return self.success_count + self.fail_count

    @property
    def success_rate(self) -> float:
        return (self.success_count / self.total * 100) if self.total > 0 else 0


# Espressif MAC address prefixes (OUI)
ESPRESSIF_MACS = [
    '24:0a:c4', '24:6f:28', '24:62:ab', '30:ae:a4', 'b8:f8:62',
    '3c:71:bf', '40:f5:20', '48:3f:da', '54:43:b2',
    '5c:cf:7f', '60:01:94', '68:c6:3a', '70:03:9f',
    '7c:9e:bd', '80:7d:3a', '84:0d:8e', '84:cc:a8',
    '84:f3:eb', '8c:aa:b5', '94:3c:c6', '94:b5:55',
    '94:b9:7e', 'a0:20:a6', 'a4:cf:12', 'a4:e5:7c',
    'ac:0b:fb', 'ac:67:b2', 'b4:e6:2d', 'bc:dd:c2',
    'c4:4f:33', 'c4:5b:be', 'c8:2b:96', 'cc:50:e3',
    'd4:f9:8d', 'dc:4f:22', 'e0:98:06', 'e8:68:e7',
    'ec:fa:bc', 'f0:08:d1', 'f4:12:fa', 'f4:cf:a2',
    'fc:f5:c4', '08:3a:f2', '08:b6:1f', '08:d1:f9',
    '10:52:1c', '10:97:bd', '34:94:54', '34:85:18',
    '34:86:5d', '34:ab:95', '48:27:e2', '48:e7:29',
    '58:bf:25', '64:b7:08', '78:21:84', '78:e3:6d',
    '98:cd:ac', 'a0:76:4e', 'a8:03:2a', 'a8:42:e3',
    'b0:a7:32', 'b8:d6:1a', 'c0:49:ef', 'c8:f0:9e',
    'd8:bf:c0', 'd8:13:2a', 'e0:5a:1b', 'e8:31:cd',
    'e8:6b:ea', 'e8:9f:6d', 'ec:62:60', 'ec:64:c9',
    'f0:9e:9e', 'f4:65:a6',
]


def discover_esp32() -> Optional[str]:
    """Discover ESP32 IP address using ARP table."""
    print(f"{Colors.BLUE}Discovering ESP32 device...{Colors.RESET}")

    try:
        result = subprocess.run(['arp', '-a'], capture_output=True, text=True, timeout=5)
        pattern = r'\((\d+\.\d+\.\d+\.\d+)\)\s+at\s+([0-9a-fA-F:]+)'

        for match in re.finditer(pattern, result.stdout):
            ip = match.group(1)
            mac = match.group(2).lower()
            mac_prefix = ':'.join(mac.split(':')[:3])

            if mac_prefix in ESPRESSIF_MACS:
                print(f"{Colors.GREEN}Found ESP32:{Colors.RESET} {ip} ({mac})")
                return ip

        # Try scanning network
        print(f"{Colors.YELLOW}Not in ARP cache, scanning...{Colors.RESET}")
        result = subprocess.run(['ifconfig'], capture_output=True, text=True, timeout=5)
        ip_match = re.search(r'inet\s+(\d+\.\d+\.\d+)\.\d+', result.stdout)

        if ip_match:
            subnet = ip_match.group(1)
            subprocess.run(['ping', '-c', '1', '-t', '1', f'{subnet}.255'],
                           capture_output=True, timeout=3)
            time.sleep(0.5)

            result = subprocess.run(['arp', '-a'], capture_output=True, text=True, timeout=5)
            for match in re.finditer(pattern, result.stdout):
                ip = match.group(1)
                mac = match.group(2).lower()
                mac_prefix = ':'.join(mac.split(':')[:3])
                if mac_prefix in ESPRESSIF_MACS:
                    print(f"{Colors.GREEN}Found ESP32:{Colors.RESET} {ip} ({mac})")
                    return ip

        print(f"{Colors.RED}No ESP32 device found{Colors.RESET}")
        return None
    except Exception as e:
        print(f"{Colors.RED}Discovery error: {e}{Colors.RESET}")
        return None