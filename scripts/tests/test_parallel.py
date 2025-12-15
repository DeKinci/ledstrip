"""
Parallel HTTP + WebSocket tests
"""

import asyncio
import time
from . import Colors, TestResult

try:
    import websockets
    HAS_WEBSOCKETS = True
except ImportError:
    HAS_WEBSOCKETS = False

try:
    import aiohttp
    HAS_AIOHTTP = True
except ImportError:
    HAS_AIOHTTP = False


async def _test_parallel(ip: str, duration: int) -> TestResult:
    """Test HTTP and WebSocket simultaneously."""
    result = TestResult("Parallel HTTP+WS", True)
    http_times = []
    ws_times = []
    http_success = 0
    http_fail = 0
    ws_success = 0
    ws_fail = 0
    stop_event = asyncio.Event()

    async def http_worker():
        nonlocal http_success, http_fail
        url = f"http://{ip}/ping"
        async with aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=5)) as session:
            while not stop_event.is_set():
                try:
                    start = time.time()
                    async with session.get(url) as resp:
                        await resp.text()
                        elapsed = (time.time() - start) * 1000
                        if resp.status == 200:
                            http_success += 1
                            http_times.append(elapsed)
                            print(f"  {Colors.CYAN}[HTTP]{Colors.RESET} {Colors.GREEN}OK{Colors.RESET} {elapsed:5.1f}ms")
                        else:
                            http_fail += 1
                except Exception:
                    http_fail += 1
                await asyncio.sleep(0.2)

    async def ws_worker():
        nonlocal ws_success, ws_fail
        ws_url = f"ws://{ip}:81"
        msg_num = 0
        try:
            async with websockets.connect(ws_url, ping_interval=None) as ws:
                while not stop_event.is_set():
                    msg_num += 1
                    try:
                        start = time.time()
                        await ws.send(f"parallel_{msg_num}")
                        await asyncio.wait_for(ws.recv(), timeout=2.0)
                        elapsed = (time.time() - start) * 1000
                        ws_success += 1
                        ws_times.append(elapsed)
                        print(f"  {Colors.BLUE}[WS]  {Colors.RESET} {Colors.GREEN}OK{Colors.RESET} {elapsed:5.1f}ms")
                    except Exception:
                        ws_fail += 1
                    await asyncio.sleep(0.1)
        except Exception:
            pass

    http_task = asyncio.create_task(http_worker())
    ws_task = asyncio.create_task(ws_worker())

    await asyncio.sleep(duration)
    stop_event.set()
    await asyncio.sleep(0.5)

    http_task.cancel()
    ws_task.cancel()
    try:
        await http_task
    except asyncio.CancelledError:
        pass
    try:
        await ws_task
    except asyncio.CancelledError:
        pass

    result.success_count = http_success + ws_success
    result.fail_count = http_fail + ws_fail
    result.times = http_times + ws_times
    result.passed = (http_fail == 0 and ws_fail == 0)

    print(f"\n  HTTP: {http_success}/{http_success+http_fail}, WS: {ws_success}/{ws_success+ws_fail}")

    return result


def test_parallel(ip: str, duration: int = 5) -> TestResult:
    """Test HTTP and WebSocket in parallel."""
    if not HAS_AIOHTTP or not HAS_WEBSOCKETS:
        missing = []
        if not HAS_AIOHTTP:
            missing.append("aiohttp")
        if not HAS_WEBSOCKETS:
            missing.append("websockets")
        return TestResult("Parallel HTTP+WS", False, error=f"{', '.join(missing)} not installed")

    print(f"\n{Colors.MAGENTA}{Colors.BOLD}[TEST] Parallel HTTP + WebSocket ({duration}s){Colors.RESET}")
    return asyncio.run(_test_parallel(ip, duration))