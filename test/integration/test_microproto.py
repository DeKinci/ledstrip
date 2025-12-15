"""
MicroProto WebSocket Protocol Test

Tests the binary protocol handshake and property sync.
"""

import struct
import asyncio
import websockets
from . import Colors, TestResult

# Protocol constants
PROTOCOL_VERSION = 1
OPCODE_HELLO = 0x00
OPCODE_PROPERTY_UPDATE_SHORT = 0x01
OPCODE_SCHEMA_UPSERT = 0x03
OPCODE_ERROR = 0x07
OPCODE_PING = 0x08
OPCODE_PONG = 0x09

# Type IDs
TYPE_BOOL = 0x01
TYPE_INT8 = 0x02
TYPE_UINT8 = 0x03
TYPE_INT32 = 0x04
TYPE_FLOAT32 = 0x05


def encode_hello_request(device_id: int = 0x12345678, max_packet: int = 4096) -> bytes:
    """Encode a client HELLO request"""
    return struct.pack('<BBHI',
        OPCODE_HELLO,      # header (opcode=0, flags=0, batch=0)
        PROTOCOL_VERSION,  # version
        max_packet,        # max_packet_size (little-endian)
        device_id          # device_id (little-endian)
    )


def decode_hello_response(data: bytes) -> dict:
    """Decode server HELLO response"""
    if len(data) < 12:
        return None
    header, version, max_packet, session_id, timestamp = struct.unpack('<BBHII', data[:12])
    if (header & 0x0F) != OPCODE_HELLO:
        return None
    return {
        'version': version,
        'max_packet': max_packet,
        'session_id': session_id,
        'timestamp': timestamp
    }


def decode_op_header(byte: int) -> tuple:
    """Decode operation header byte"""
    opcode = byte & 0x0F
    flags = (byte >> 4) & 0x07
    batch = (byte >> 7) & 0x01
    return opcode, flags, batch


def decode_schema_item(data: bytes, offset: int) -> tuple:
    """Decode a schema property item, returns (property_dict, new_offset)"""
    if offset >= len(data):
        return None, offset

    item_type = data[offset]
    offset += 1

    prop_type = item_type & 0x0F
    readonly = bool(item_type & 0x10)
    persistent = bool(item_type & 0x20)
    hidden = bool(item_type & 0x40)

    if prop_type != 1:  # PROPERTY
        return None, offset

    # Property level flags
    level_flags = data[offset]
    offset += 1
    level = level_flags & 0x03
    ble_exposed = bool(level_flags & 0x04)

    # Group ID if GROUP level
    group_id = 0
    if level == 1:  # GROUP
        group_id = data[offset]
        offset += 1

    # Item ID
    item_id = data[offset]
    offset += 1

    # Namespace ID
    namespace_id = data[offset]
    offset += 1

    # Name
    name_len = data[offset]
    offset += 1
    name = data[offset:offset+name_len].decode('ascii')
    offset += name_len

    # Description (varint length)
    desc_len, varint_bytes = decode_varint(data, offset)
    offset += varint_bytes
    offset += desc_len  # Skip description

    # Type ID
    type_id = data[offset]
    offset += 1

    # Validation flags (skip)
    offset += 1

    # Default value
    value_size = get_type_size(type_id)
    default_value = data[offset:offset+value_size]
    offset += value_size

    # UI hints (skip)
    offset += 1

    return {
        'id': item_id,
        'name': name,
        'type_id': type_id,
        'readonly': readonly,
        'persistent': persistent,
        'level': level
    }, offset


def decode_varint(data: bytes, offset: int) -> tuple:
    """Decode varint, returns (value, bytes_consumed)"""
    result = 0
    shift = 0
    bytes_consumed = 0

    while offset < len(data):
        byte = data[offset]
        offset += 1
        bytes_consumed += 1
        result |= (byte & 0x7F) << shift
        if (byte & 0x80) == 0:
            break
        shift += 7
        if bytes_consumed > 5:
            break

    return result, bytes_consumed


def get_type_size(type_id: int) -> int:
    """Get size of a type in bytes"""
    sizes = {
        TYPE_BOOL: 1,
        TYPE_INT8: 1,
        TYPE_UINT8: 1,
        TYPE_INT32: 4,
        TYPE_FLOAT32: 4
    }
    return sizes.get(type_id, 0)


def decode_property_value(data: bytes, type_id: int) -> any:
    """Decode a property value"""
    if type_id == TYPE_BOOL:
        return bool(data[0])
    elif type_id == TYPE_INT8:
        return struct.unpack('<b', data[:1])[0]
    elif type_id == TYPE_UINT8:
        return data[0]
    elif type_id == TYPE_INT32:
        return struct.unpack('<i', data[:4])[0]
    elif type_id == TYPE_FLOAT32:
        return struct.unpack('<f', data[:4])[0]
    return None


async def test_microproto_handshake(ip: str) -> TestResult:
    """Test MicroProto WebSocket handshake"""
    print(f"\n{Colors.CYAN}{Colors.BOLD}[TEST] MicroProto Handshake{Colors.RESET}")
    result = TestResult("MicroProto Handshake", True)

    uri = f"ws://{ip}:81"

    try:
        async with websockets.connect(uri, subprotocols=[]) as ws:
            # Send HELLO
            hello = encode_hello_request()
            print(f"  Sending HELLO ({len(hello)} bytes)")
            await ws.send(hello)

            # Receive HELLO response
            response = await asyncio.wait_for(ws.recv(), timeout=5.0)
            if isinstance(response, str):
                print(f"  {Colors.RED}Got text instead of binary{Colors.RESET}")
                result.fail_count += 1
                result.passed = False
                return result

            hello_resp = decode_hello_response(response)
            if hello_resp:
                print(f"  {Colors.GREEN}HELLO response:{Colors.RESET} version={hello_resp['version']}, session={hello_resp['session_id']}")
                result.success_count += 1
            else:
                print(f"  {Colors.RED}Invalid HELLO response{Colors.RESET}")
                result.fail_count += 1
                result.passed = False
                return result

            # Receive SCHEMA_UPSERT
            schema_msg = await asyncio.wait_for(ws.recv(), timeout=5.0)
            opcode, flags, batch = decode_op_header(schema_msg[0])

            if opcode == OPCODE_SCHEMA_UPSERT:
                if batch:
                    count = schema_msg[1] + 1
                    print(f"  {Colors.GREEN}SCHEMA_UPSERT:{Colors.RESET} {count} properties (batched)")

                    # Decode properties
                    offset = 2
                    properties = []
                    for i in range(count):
                        prop, offset = decode_schema_item(schema_msg, offset)
                        if prop:
                            properties.append(prop)
                            print(f"    - {prop['name']} (id={prop['id']}, type={prop['type_id']})")

                    result.success_count += 1
                else:
                    print(f"  {Colors.YELLOW}SCHEMA_UPSERT (non-batched){Colors.RESET}")
                    result.success_count += 1
            else:
                print(f"  {Colors.RED}Expected SCHEMA_UPSERT, got opcode {opcode}{Colors.RESET}")
                result.fail_count += 1

            # Receive PROPERTY_UPDATE
            values_msg = await asyncio.wait_for(ws.recv(), timeout=5.0)
            opcode, flags, batch = decode_op_header(values_msg[0])

            if opcode == OPCODE_PROPERTY_UPDATE_SHORT:
                if batch:
                    count = values_msg[1] + 1
                    print(f"  {Colors.GREEN}PROPERTY_UPDATE:{Colors.RESET} {count} values")
                    result.success_count += 1
                else:
                    print(f"  {Colors.GREEN}PROPERTY_UPDATE (single){Colors.RESET}")
                    result.success_count += 1
            else:
                print(f"  {Colors.RED}Expected PROPERTY_UPDATE, got opcode {opcode}{Colors.RESET}")
                result.fail_count += 1

            result.passed = result.fail_count == 0

    except asyncio.TimeoutError:
        print(f"  {Colors.RED}Timeout waiting for response{Colors.RESET}")
        result.error = "Timeout"
        result.passed = False
    except Exception as e:
        print(f"  {Colors.RED}Error: {e}{Colors.RESET}")
        result.error = str(e)
        result.passed = False

    return result


def test_microproto(ip: str) -> TestResult:
    """Run MicroProto handshake test (sync wrapper)"""
    return asyncio.run(test_microproto_handshake(ip))


def encode_property_update(prop_id: int, value: int, type_id: int = TYPE_UINT8) -> bytes:
    """Encode a PROPERTY_UPDATE_SHORT message"""
    if type_id == TYPE_UINT8:
        return struct.pack('<BBBB', OPCODE_PROPERTY_UPDATE_SHORT, prop_id, 0, value)
    elif type_id == TYPE_BOOL:
        return struct.pack('<BBBB', OPCODE_PROPERTY_UPDATE_SHORT, prop_id, 0, 1 if value else 0)
    elif type_id == TYPE_INT32:
        return struct.pack('<BBBi', OPCODE_PROPERTY_UPDATE_SHORT, prop_id, 0, value)
    elif type_id == TYPE_FLOAT32:
        return struct.pack('<BBBf', OPCODE_PROPERTY_UPDATE_SHORT, prop_id, 0, value)
    return b''


async def test_microproto_stress_impl(ip: str, duration: int = 5) -> TestResult:
    """Stress test MicroProto WebSocket - rapid property updates"""
    print(f"\n{Colors.CYAN}{Colors.BOLD}[TEST] MicroProto Stress ({duration}s){Colors.RESET}")
    result = TestResult("MicroProto Stress", True)

    uri = f"ws://{ip}:81"
    updates_sent = 0
    updates_received = 0
    errors = 0
    latencies = []

    try:
        async with websockets.connect(uri, subprotocols=[]) as ws:
            # Handshake first
            hello = encode_hello_request()
            await ws.send(hello)

            # Wait for HELLO + SCHEMA + VALUES
            for _ in range(3):
                await asyncio.wait_for(ws.recv(), timeout=5.0)

            print(f"  Handshake complete, starting stress test...")

            # Find brightness property ID (from earlier test, it's id=3 or id=0)
            brightness_id = 3  # From main.cpp

            start_time = asyncio.get_event_loop().time()
            end_time = start_time + duration

            # Send rapid updates and listen for broadcasts
            send_interval = 0.05  # 20 updates/sec
            last_send = 0
            value = 0

            while asyncio.get_event_loop().time() < end_time:
                now = asyncio.get_event_loop().time()

                # Send update
                if now - last_send >= send_interval:
                    value = (value + 10) % 256
                    msg = encode_property_update(brightness_id, value, TYPE_UINT8)
                    send_time = now
                    await ws.send(msg)
                    updates_sent += 1
                    last_send = now

                # Check for incoming messages (non-blocking)
                try:
                    response = await asyncio.wait_for(ws.recv(), timeout=0.01)
                    if isinstance(response, bytes) and len(response) > 0:
                        opcode = response[0] & 0x0F
                        if opcode == OPCODE_PROPERTY_UPDATE_SHORT:
                            updates_received += 1
                            # Calculate latency if this is our echo
                            latency = (asyncio.get_event_loop().time() - send_time) * 1000
                            latencies.append(latency)
                except asyncio.TimeoutError:
                    pass
                except Exception as e:
                    errors += 1

            elapsed = asyncio.get_event_loop().time() - start_time

            # Results
            result.success_count = updates_sent
            result.fail_count = errors

            rate = updates_sent / elapsed if elapsed > 0 else 0
            avg_latency = sum(latencies) / len(latencies) if latencies else 0
            max_latency = max(latencies) if latencies else 0

            print(f"  Sent: {updates_sent} updates ({rate:.1f}/sec)")
            print(f"  Received: {updates_received} broadcasts")
            print(f"  Errors: {errors}")
            if latencies:
                print(f"  Latency: avg={avg_latency:.1f}ms, max={max_latency:.1f}ms")

            result.times = latencies
            result.passed = errors == 0 and updates_sent > 0

    except Exception as e:
        print(f"  {Colors.RED}Error: {e}{Colors.RESET}")
        result.error = str(e)
        result.passed = False

    return result


async def test_microproto_reconnect_impl(ip: str, count: int = 5) -> TestResult:
    """Test rapid reconnection to MicroProto server"""
    print(f"\n{Colors.CYAN}{Colors.BOLD}[TEST] MicroProto Reconnect ({count}x){Colors.RESET}")
    result = TestResult("MicroProto Reconnect", True)

    uri = f"ws://{ip}:81"
    successes = 0
    failures = 0
    times = []

    for i in range(count):
        start = asyncio.get_event_loop().time()
        try:
            async with websockets.connect(uri, subprotocols=[], close_timeout=1) as ws:
                # Send HELLO
                await ws.send(encode_hello_request(device_id=0x1000 + i))

                # Wait for full handshake
                hello_resp = await asyncio.wait_for(ws.recv(), timeout=2.0)
                schema = await asyncio.wait_for(ws.recv(), timeout=2.0)
                values = await asyncio.wait_for(ws.recv(), timeout=2.0)

                elapsed = (asyncio.get_event_loop().time() - start) * 1000
                times.append(elapsed)

                # Verify responses
                if (hello_resp[0] & 0x0F) == OPCODE_HELLO and \
                   (schema[0] & 0x0F) == OPCODE_SCHEMA_UPSERT and \
                   (values[0] & 0x0F) == OPCODE_PROPERTY_UPDATE_SHORT:
                    successes += 1
                    print(f"  [{i+1}/{count}] {Colors.GREEN}OK{Colors.RESET} ({elapsed:.0f}ms)")
                else:
                    failures += 1
                    print(f"  [{i+1}/{count}] {Colors.RED}Bad response{Colors.RESET}")

        except Exception as e:
            failures += 1
            print(f"  [{i+1}/{count}] {Colors.RED}Error: {e}{Colors.RESET}")

        # Small delay between connections
        await asyncio.sleep(0.1)

    result.success_count = successes
    result.fail_count = failures
    result.times = times
    result.passed = failures == 0

    if times:
        avg = sum(times) / len(times)
        print(f"  Average handshake: {avg:.0f}ms")

    return result


def test_microproto_stress(ip: str, duration: int = 5) -> TestResult:
    """Run MicroProto stress test (sync wrapper)"""
    return asyncio.run(test_microproto_stress_impl(ip, duration))


def test_microproto_reconnect(ip: str, count: int = 5) -> TestResult:
    """Run MicroProto reconnect test (sync wrapper)"""
    return asyncio.run(test_microproto_reconnect_impl(ip, count))


def encode_ping(payload: int) -> bytes:
    """Encode a PING message"""
    return struct.pack('<BI', OPCODE_PING, payload)


async def test_microproto_ping_impl(ip: str, count: int = 10) -> TestResult:
    """Test PING/PONG heartbeat"""
    print(f"\n{Colors.CYAN}{Colors.BOLD}[TEST] MicroProto Ping ({count}x){Colors.RESET}")
    result = TestResult("MicroProto Ping", True)

    uri = f"ws://{ip}:81"
    successes = 0
    failures = 0
    latencies = []

    try:
        async with websockets.connect(uri, subprotocols=[]) as ws:
            # Handshake first
            await ws.send(encode_hello_request())
            for _ in range(3):
                await asyncio.wait_for(ws.recv(), timeout=5.0)

            print(f"  Handshake complete, sending {count} pings...")

            for i in range(count):
                payload = i + 1
                ping_msg = encode_ping(payload)

                start = asyncio.get_event_loop().time()
                await ws.send(ping_msg)

                # Wait for PONG (skip any property broadcasts)
                try:
                    pong_received = False
                    attempts = 0
                    while not pong_received and attempts < 10:
                        attempts += 1
                        response = await asyncio.wait_for(ws.recv(), timeout=2.0)
                        elapsed = (asyncio.get_event_loop().time() - start) * 1000

                        if isinstance(response, bytes) and len(response) >= 1:
                            opcode = response[0] & 0x0F

                            # Skip property updates, wait for PONG
                            if opcode == OPCODE_PROPERTY_UPDATE_SHORT:
                                continue

                            if opcode == OPCODE_PONG and len(response) >= 5:
                                resp_payload = struct.unpack('<I', response[1:5])[0]
                                if resp_payload == payload:
                                    successes += 1
                                    latencies.append(elapsed)
                                    print(f"  [{i+1}/{count}] {Colors.GREEN}PONG{Colors.RESET} payload={payload}, RTT={elapsed:.1f}ms")
                                    pong_received = True
                                else:
                                    failures += 1
                                    print(f"  [{i+1}/{count}] {Colors.RED}Bad payload{Colors.RESET} expected={payload}, got={resp_payload}")
                                    pong_received = True  # Don't retry
                            else:
                                failures += 1
                                print(f"  [{i+1}/{count}] {Colors.RED}Unexpected opcode{Colors.RESET} opcode={opcode}")
                                pong_received = True  # Don't retry

                    if not pong_received:
                        failures += 1
                        print(f"  [{i+1}/{count}] {Colors.RED}No PONG after {attempts} messages{Colors.RESET}")

                except asyncio.TimeoutError:
                    failures += 1
                    print(f"  [{i+1}/{count}] {Colors.RED}Timeout{Colors.RESET}")

                await asyncio.sleep(0.1)

            result.success_count = successes
            result.fail_count = failures
            result.times = latencies
            result.passed = failures == 0

            if latencies:
                avg = sum(latencies) / len(latencies)
                min_lat = min(latencies)
                max_lat = max(latencies)
                print(f"  RTT: avg={avg:.1f}ms, min={min_lat:.1f}ms, max={max_lat:.1f}ms")

    except Exception as e:
        print(f"  {Colors.RED}Error: {e}{Colors.RESET}")
        result.error = str(e)
        result.passed = False

    return result


def test_microproto_ping(ip: str, count: int = 10) -> TestResult:
    """Run MicroProto ping test (sync wrapper)"""
    return asyncio.run(test_microproto_ping_impl(ip, count))