"""
WebSocket Reliability Test (MicroProto Protocol)

Tests WebSocket connection reliability using proper MicroProto binary protocol.
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

# Protocol constants (shared with test_microproto.py)
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

    # Expect: HELLO response, SCHEMA_UPSERT, PROPERTY_UPDATE
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


async def _test_websocket_reliability(ip: str, count: int) -> TestResult:
    """Test WebSocket reliability using PING/PONG messages."""
    ws_url = f"ws://{ip}:81"
    result = TestResult("WebSocket Reliability", True)

    try:
        async with websockets.connect(ws_url, ping_interval=None) as ws:
            # Proper MicroProto handshake
            if not await do_handshake(ws):
                result.passed = False
                result.error = "Handshake failed"
                print(f"  {Colors.RED}Handshake failed{Colors.RESET}")
                return result

            print(f"  Handshake OK, sending {count} pings...")

            for i in range(count):
                try:
                    payload = i + 1
                    start = time.time()

                    # Send PING
                    await ws.send(encode_ping(payload))

                    # Wait for PONG (skip any property broadcasts)
                    pong_received = False
                    attempts = 0
                    while not pong_received and attempts < 10:
                        attempts += 1
                        response = await asyncio.wait_for(ws.recv(), timeout=2.0)
                        elapsed = (time.time() - start) * 1000

                        if isinstance(response, bytes) and len(response) >= 1:
                            opcode = response[0] & 0x0F

                            # Skip property updates
                            if opcode == OPCODE_PROPERTY_UPDATE_SHORT:
                                continue

                            if opcode == OPCODE_PONG and len(response) >= 5:
                                resp_payload = struct.unpack('<I', response[1:5])[0]
                                if resp_payload == payload:
                                    result.success_count += 1
                                    result.times.append(elapsed)
                                    print(f"  {i+1:2d}. {Colors.GREEN}OK{Colors.RESET} {elapsed:6.1f}ms")
                                    pong_received = True
                                else:
                                    result.fail_count += 1
                                    result.passed = False
                                    print(f"  {i+1:2d}. {Colors.RED}Bad payload{Colors.RESET} (expected={payload}, got={resp_payload})")
                                    pong_received = True
                            else:
                                result.fail_count += 1
                                result.passed = False
                                print(f"  {i+1:2d}. {Colors.RED}Unexpected{Colors.RESET} (opcode={opcode})")
                                pong_received = True

                    if not pong_received:
                        result.fail_count += 1
                        result.passed = False
                        print(f"  {i+1:2d}. {Colors.RED}No PONG{Colors.RESET}")

                except asyncio.TimeoutError:
                    result.fail_count += 1
                    result.passed = False
                    print(f"  {i+1:2d}. {Colors.RED}Timeout{Colors.RESET}")
                except Exception as e:
                    result.fail_count += 1
                    result.passed = False
                    print(f"  {i+1:2d}. {Colors.RED}Error: {e}{Colors.RESET}")

                await asyncio.sleep(0.05)  # Small delay between pings

    except Exception as e:
        result.passed = False
        result.error = str(e)
        print(f"  {Colors.RED}Connection failed: {e}{Colors.RESET}")

    # Print summary
    if result.times:
        avg = sum(result.times) / len(result.times)
        min_t = min(result.times)
        max_t = max(result.times)
        print(f"  RTT: avg={avg:.1f}ms, min={min_t:.1f}ms, max={max_t:.1f}ms")

    return result


def test_websocket_reliability(ip: str, count: int = 20) -> TestResult:
    """Test WebSocket reliability using MicroProto PING/PONG."""
    if not HAS_WEBSOCKETS:
        return TestResult("WebSocket Reliability", False, error="websockets not installed")

    print(f"\n{Colors.BLUE}{Colors.BOLD}[TEST] WebSocket Reliability ({count} pings){Colors.RESET}")
    return asyncio.run(_test_websocket_reliability(ip, count))