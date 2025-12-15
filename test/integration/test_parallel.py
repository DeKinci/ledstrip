"""
Parallel HTTP + WebSocket tests (MicroProto Protocol)
"""

import struct
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

# Protocol constants
OPCODE_HELLO = 0x00
OPCODE_PROPERTY_UPDATE_SHORT = 0x01
OPCODE_SCHEMA_UPSERT = 0x03
OPCODE_PING = 0x08
OPCODE_PONG = 0x09
PROTOCOL_VERSION = 1


def encode_hello(device_id: int = 0xDEADBEEF, max_packet: int = 4096) -> bytes:
    """Encode HELLO request"""
    return struct.pack('<BBHI', OPCODE_HELLO, PROTOCOL_VERSION, max_packet, device_id)


def encode_ping(payload: int) -> bytes:
    """Encode PING message"""
    return struct.pack('<BI', OPCODE_PING, payload)


async def do_handshake(ws) -> bool:
    """Perform MicroProto handshake, returns True on success"""
    await ws.send(encode_hello())

    for expected in ['HELLO', 'SCHEMA', 'VALUES']:
        try:
            msg = await asyncio.wait_for(ws.recv(), timeout=5.0)
            if not isinstance(msg, bytes) or len(msg) == 0:
                return False
            opcode = msg[0] & 0x0F
            if expected == 'HELLO' and opcode != OPCODE_HELLO:
                return False
            if expected == 'SCHEMA' and opcode != OPCODE_SCHEMA_UPSERT:
                return False
            if expected == 'VALUES' and opcode != OPCODE_PROPERTY_UPDATE_SHORT:
                return False
        except (asyncio.TimeoutError, Exception):
            return False
    return True


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
        ping_num = 0
        try:
            async with websockets.connect(ws_url, ping_interval=None) as ws:
                # Perform MicroProto handshake
                if not await do_handshake(ws):
                    ws_fail += 1
                    return

                while not stop_event.is_set():
                    ping_num += 1
                    try:
                        start = time.time()
                        await ws.send(encode_ping(ping_num))

                        # Wait for PONG (skip property broadcasts)
                        pong_received = False
                        attempts = 0
                        while not pong_received and attempts < 10:
                            attempts += 1
                            response = await asyncio.wait_for(ws.recv(), timeout=2.0)

                            if isinstance(response, bytes) and len(response) >= 1:
                                opcode = response[0] & 0x0F

                                # Skip property updates
                                if opcode == OPCODE_PROPERTY_UPDATE_SHORT:
                                    continue

                                if opcode == OPCODE_PONG and len(response) >= 5:
                                    resp_payload = struct.unpack('<I', response[1:5])[0]
                                    if resp_payload == ping_num:
                                        elapsed = (time.time() - start) * 1000
                                        ws_success += 1
                                        ws_times.append(elapsed)
                                        print(f"  {Colors.BLUE}[WS]  {Colors.RESET} {Colors.GREEN}OK{Colors.RESET} {elapsed:5.1f}ms")
                                        pong_received = True
                                    else:
                                        ws_fail += 1
                                        pong_received = True
                                else:
                                    ws_fail += 1
                                    pong_received = True

                        if not pong_received:
                            ws_fail += 1

                    except asyncio.TimeoutError:
                        ws_fail += 1
                    except Exception:
                        ws_fail += 1
                    await asyncio.sleep(0.5)  # Slower WS rate to avoid overwhelming
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