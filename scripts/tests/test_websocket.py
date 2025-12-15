"""
WebSocket tests
"""

import asyncio
import time
from . import Colors, TestResult

try:
    import websockets
    HAS_WEBSOCKETS = True
except ImportError:
    HAS_WEBSOCKETS = False


async def _test_websocket_reliability(ip: str, count: int) -> TestResult:
    """Test WebSocket reliability with multiple messages."""
    ws_url = f"ws://{ip}:81"
    result = TestResult("WebSocket Reliability", True)

    try:
        async with websockets.connect(ws_url, ping_interval=None) as ws:
            # Skip welcome if any
            try:
                await asyncio.wait_for(ws.recv(), timeout=1.0)
            except asyncio.TimeoutError:
                pass

            for i in range(count):
                try:
                    msg = f"test_msg_{i}"
                    start = time.time()
                    await ws.send(msg)
                    response = await asyncio.wait_for(ws.recv(), timeout=2.0)
                    elapsed = (time.time() - start) * 1000

                    result.success_count += 1
                    result.times.append(elapsed)
                    print(f"  {i+1:2d}. {Colors.GREEN}OK{Colors.RESET} {elapsed:6.1f}ms")
                except asyncio.TimeoutError:
                    result.fail_count += 1
                    result.passed = False
                    print(f"  {i+1:2d}. {Colors.RED}Timeout{Colors.RESET}")
                except Exception as e:
                    result.fail_count += 1
                    result.passed = False
                    print(f"  {i+1:2d}. {Colors.RED}Error{Colors.RESET}")

                await asyncio.sleep(0.1)
    except Exception as e:
        result.passed = False
        result.error = str(e)
        print(f"  {Colors.RED}Connection failed: {e}{Colors.RESET}")

    return result


def test_websocket_reliability(ip: str, count: int = 20) -> TestResult:
    """Test WebSocket reliability."""
    if not HAS_WEBSOCKETS:
        return TestResult("WebSocket Reliability", False, error="websockets not installed")

    print(f"\n{Colors.BLUE}{Colors.BOLD}[TEST] WebSocket Reliability ({count} messages){Colors.RESET}")
    return asyncio.run(_test_websocket_reliability(ip, count))