"""Shared fixtures for retranslator two-board integration tests.

Discovers two retranslator boards via BLE, connects to both, and provides
high-level command/response helpers for the retranslator BLE protocol.
"""

import asyncio
import struct
import time
from dataclasses import dataclass, field

import pytest
import pytest_asyncio
from bleak import BleakClient, BleakScanner

# --- BLE UUIDs (Nordic UART Service) ---

SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
RX_CHAR_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # app -> device (write)
TX_CHAR_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # device -> app (notify)
STATS_CHAR_UUID = "6e400004-b5a3-f393-e0a9-e50e24dcca9e"  # device -> app (notify, stats)

# --- Fragment header (microble BleMessageService) ---

FRAG_START = 0x80
FRAG_END = 0x40
FRAG_COMPLETE = FRAG_START | FRAG_END

# --- BLE commands (app -> device) ---

CMD_SET_CLOCK = 0x01
CMD_SET_LOCATION = 0x02
CMD_SEND_TEXT = 0x03
CMD_GET_STATE = 0x04
CMD_GET_MESSAGES = 0x05
CMD_GET_SELF_INFO = 0x06
CMD_RESET_STATS = 0x08
CMD_START_SPEEDTEST = 0x09
CMD_GET_SPEEDTEST_RESULTS = 0x0A

# --- BLE responses (device -> app) ---

RESP_STATE = 0x80
RESP_MESSAGE = 0x81
NOTIFY_INCOMING = 0x82
NOTIFY_PRESENCE = 0x83
RESP_SELF_INFO = 0x84
RESP_SPEEDTEST_RESULTS = 0x86

# --- Expected device IDs ---

DEVICE_ID_A = 0x01
DEVICE_ID_B = 0x02

# --- Response dataclasses ---


@dataclass
class SelfInfo:
    device_id: int
    clock: int
    active_senders: int
    boot_count: int


@dataclass
class SenderEntry:
    sender_id: int
    high_seq: int
    loc_seq: int
    node_a: int
    node_b: int
    presence: int  # 0=Online, 1=Stale, 2=Offline


@dataclass
class StateResp:
    entries: list[SenderEntry] = field(default_factory=list)


@dataclass
class MessageEntry:
    sender_id: int
    seq: int
    timestamp: int
    msg_type: int
    payload: bytes


@dataclass
class SpeedtestResults:
    count: int
    interval_ms: int
    payload_size: int
    total_sent: int
    total_received: int
    total_lost: int
    loss_rate_x10: int  # x10: 125 = 12.5%
    rtt_min: int
    rtt_max: int
    rtt_avg: int
    rtt_p1: int
    rtt_p5: int
    rtt_p10: int
    rtt_p25: int
    rtt_p50: int
    rtt_p75: int
    rtt_p90: int
    rtt_p95: int
    rtt_p99: int
    test_duration_ms: int
    actual_interval_avg_ms: int


@dataclass
class Stats:
    packets_rx: int
    packets_tx: int
    tx_failures: int
    decode_failures: int
    sync_started: int
    sync_completed: int
    sync_timeout: int
    msgs_stored: int
    dups_rejected: int
    beacons_sent: int
    beacons_rx: int
    hash_mismatches: int
    uptime_s: int
    free_heap: int
    loop_time_us: int


# --- BLE client ---


class RetranslatorClient:
    """BLE client for the retranslator command protocol."""

    def __init__(self, client: BleakClient):
        self.client = client
        self._rx_queue: asyncio.Queue[bytes] = asyncio.Queue()
        self._chunks: list[bytes] = []

    async def start(self):
        await self.client.start_notify(TX_CHAR_UUID, self._on_notify)

    async def stop(self):
        try:
            await self.client.stop_notify(TX_CHAR_UUID)
        except Exception:
            pass

    def _on_notify(self, _sender, data: bytearray):
        if len(data) < 1:
            return
        header = data[0]
        payload = bytes(data[1:])

        if header & FRAG_START:
            self._chunks.clear()
        self._chunks.append(payload)

        if header & FRAG_END:
            msg = b"".join(self._chunks)
            self._chunks.clear()
            self._rx_queue.put_nowait(msg)

    async def send_cmd(self, data: bytes):
        mtu = self.client.mtu_size - 3
        chunk_size = mtu - 1  # fragment header

        if len(data) <= chunk_size:
            frag = bytes([FRAG_COMPLETE]) + data
            await self.client.write_gatt_char(RX_CHAR_UUID, frag, response=True)
        else:
            offset = 0
            while offset < len(data):
                remaining = len(data) - offset
                this_chunk = min(remaining, chunk_size)
                is_first = offset == 0
                is_last = offset + this_chunk >= len(data)

                hdr = 0
                if is_first:
                    hdr |= FRAG_START
                if is_last:
                    hdr |= FRAG_END

                frag = bytes([hdr]) + data[offset : offset + this_chunk]
                await self.client.write_gatt_char(RX_CHAR_UUID, frag, response=is_last)
                offset += this_chunk

    async def recv(self, timeout: float = 10.0) -> bytes:
        return await asyncio.wait_for(self._rx_queue.get(), timeout=timeout)

    async def recv_or_none(self, timeout: float = 2.0) -> bytes | None:
        try:
            return await asyncio.wait_for(self._rx_queue.get(), timeout=timeout)
        except asyncio.TimeoutError:
            return None

    async def drain(self, timeout: float = 0.3):
        while True:
            try:
                self._rx_queue.get_nowait()
            except asyncio.QueueEmpty:
                break
        # also drain anything arriving shortly after
        while await self.recv_or_none(timeout=timeout) is not None:
            pass

    async def collect(self, timeout: float = 5.0, match_type: int | None = None) -> list[bytes]:
        msgs = []
        deadline = asyncio.get_event_loop().time() + timeout
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            msg = await self.recv_or_none(timeout=remaining)
            if msg is None:
                break
            if match_type is None or (len(msg) > 0 and msg[0] == match_type):
                msgs.append(msg)
        return msgs

    # --- High-level commands ---

    async def set_clock(self, unix_time: int):
        await self.send_cmd(bytes([CMD_SET_CLOCK]) + struct.pack(">I", unix_time))

    async def set_location(self, node_a: int, node_b: int):
        await self.send_cmd(bytes([CMD_SET_LOCATION, node_a, node_b]))

    async def send_text(self, text: str):
        data = text.encode("utf-8")
        await self.send_cmd(bytes([CMD_SEND_TEXT, len(data)]) + data)

    async def _recv_type(self, resp_type: int, timeout: float = 5.0) -> bytes:
        """Wait for a message with a specific response type byte, discarding others."""
        deadline = asyncio.get_event_loop().time() + timeout
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                raise asyncio.TimeoutError(f"No response 0x{resp_type:02X} within {timeout}s")
            msg = await self.recv(timeout=remaining)
            if len(msg) > 0 and msg[0] == resp_type:
                return msg

    async def get_self_info(self) -> SelfInfo:
        await self.send_cmd(bytes([CMD_GET_SELF_INFO]))
        msg = await self._recv_type(RESP_SELF_INFO, timeout=5.0)
        return self._parse_self_info(msg)

    async def get_state(self) -> StateResp:
        await self.send_cmd(bytes([CMD_GET_STATE]))
        msg = await self._recv_type(RESP_STATE, timeout=5.0)
        return self._parse_state(msg)

    async def get_stats(self, timeout: float = 3.0) -> Stats:
        """Subscribe to stats characteristic, wait for one push, unsubscribe."""
        q: asyncio.Queue[bytes] = asyncio.Queue()

        def on_stats(_sender, data: bytearray):
            q.put_nowait(bytes(data))

        await self.client.start_notify(STATS_CHAR_UUID, on_stats)
        try:
            data = await asyncio.wait_for(q.get(), timeout=timeout)
        finally:
            try:
                await self.client.stop_notify(STATS_CHAR_UUID)
            except Exception:
                pass
        return self._parse_stats(data)

    async def reset_stats(self):
        await self.send_cmd(bytes([CMD_RESET_STATS]))

    async def start_speedtest(self, count: int = 20, interval_ms: int = 500, payload_size: int = 10):
        await self.send_cmd(
            bytes([CMD_START_SPEEDTEST])
            + struct.pack(">HH", count, interval_ms)
            + bytes([payload_size])
        )

    async def get_speedtest_results(self) -> SpeedtestResults | None:
        await self.send_cmd(bytes([CMD_GET_SPEEDTEST_RESULTS]))
        msg = await self._recv_type(RESP_SPEEDTEST_RESULTS, timeout=5.0)
        if len(msg) < 44:
            return None  # status response (idle/running), not full results
        return self._parse_speedtest_results(msg)

    async def get_messages(self, sender_id: int, from_seq: int, timeout: float = 5.0) -> list[MessageEntry]:
        await self.send_cmd(bytes([CMD_GET_MESSAGES, sender_id]) + struct.pack(">H", from_seq))
        # Firmware streams messages as NOTIFY_INCOMING (0x82), not RESP_MESSAGE (0x81)
        msgs = await self.collect(timeout=timeout, match_type=NOTIFY_INCOMING)
        return [self._parse_message(m) for m in msgs]

    # --- Parsers ---

    @staticmethod
    def _parse_self_info(data: bytes) -> SelfInfo:
        assert len(data) >= 11 and data[0] == RESP_SELF_INFO
        device_id = data[1]
        clock = struct.unpack(">I", data[2:6])[0]
        active_senders = data[6]
        boot_count = struct.unpack(">I", data[7:11])[0]
        return SelfInfo(device_id, clock, active_senders, boot_count)

    @staticmethod
    def _parse_state(data: bytes) -> StateResp:
        assert len(data) >= 2 and data[0] == RESP_STATE
        count = data[1]
        entries = []
        pos = 2
        for _ in range(count):
            if pos + 8 > len(data):
                break
            sender_id = data[pos]
            high_seq = struct.unpack(">H", data[pos + 1 : pos + 3])[0]
            loc_seq = struct.unpack(">H", data[pos + 3 : pos + 5])[0]
            node_a = data[pos + 5]
            node_b = data[pos + 6]
            presence = data[pos + 7]
            entries.append(SenderEntry(sender_id, high_seq, loc_seq, node_a, node_b, presence))
            pos += 8
        return StateResp(entries)

    @staticmethod
    def _parse_speedtest_results(data: bytes) -> SpeedtestResults:
        assert len(data) >= 44 and data[0] == RESP_SPEEDTEST_RESULTS
        d = data[1:]  # skip type byte
        count, interval_ms = struct.unpack(">HH", d[0:4])
        payload_size = d[4]
        total_sent, total_received, total_lost, loss_x10 = struct.unpack(">HHHH", d[5:13])
        rtt = struct.unpack(">12H", d[13:37])
        test_dur = struct.unpack(">I", d[37:41])[0]
        avg_interval = struct.unpack(">H", d[41:43])[0]
        return SpeedtestResults(
            count, interval_ms, payload_size,
            total_sent, total_received, total_lost, loss_x10,
            *rtt,  # min, max, avg, p1, p5, p10, p25, p50, p75, p90, p95, p99
            test_dur, avg_interval,
        )

    @staticmethod
    def _parse_stats(data: bytes) -> Stats:
        assert len(data) >= 60, f"Stats too short: {len(data)} bytes"
        fields = struct.unpack(">15I", data[:60])
        return Stats(*fields)

    @staticmethod
    def _parse_message(data: bytes) -> MessageEntry:
        assert len(data) >= 9 and data[0] in (RESP_MESSAGE, NOTIFY_INCOMING)
        sender_id = data[1]
        seq = struct.unpack(">H", data[2:4])[0]
        timestamp = struct.unpack(">I", data[4:8])[0]
        msg_type = data[8]
        payload = data[9:]
        return MessageEntry(sender_id, seq, timestamp, msg_type, payload)


# --- Fixtures ---


def _has_nus(d, adv):
    return SERVICE_UUID.lower() in [u.lower() for u in (adv.service_uuids or [])]


@pytest_asyncio.fixture(scope="session", loop_scope="session")
async def both_boards():
    """Discover two retranslator boards via BLE, connect, identify by device ID."""
    matches = []
    seen = set()

    def _detection(d, adv):
        if _has_nus(d, adv) and d.address not in seen:
            seen.add(d.address)
            matches.append(d)

    scanner = BleakScanner(detection_callback=_detection)
    await scanner.start()
    # Wait until we find 2 or timeout
    for _ in range(30):
        if len(matches) >= 2:
            break
        await asyncio.sleep(0.5)
    await scanner.stop()

    assert len(matches) >= 2, (
        f"Need 2 retranslator boards advertising NUS UUID, found {len(matches)}: "
        f"{[d.name for d in matches]}"
    )

    clients = {}
    ble_clients = []

    for device in matches[:2]:
        client = BleakClient(device)
        await client.connect()
        rc = RetranslatorClient(client)
        await rc.start()
        info = await rc.get_self_info()
        clients[info.device_id] = rc
        ble_clients.append(client)

    assert DEVICE_ID_A in clients, f"Board A (ID 0x{DEVICE_ID_A:02X}) not found"
    assert DEVICE_ID_B in clients, f"Board B (ID 0x{DEVICE_ID_B:02X}) not found"

    yield {"a": clients[DEVICE_ID_A], "b": clients[DEVICE_ID_B]}

    for rc in clients.values():
        await rc.stop()
    for c in ble_clients:
        try:
            await c.disconnect()
        except Exception:
            pass


@pytest_asyncio.fixture(scope="session", loop_scope="session")
async def board_a(both_boards) -> RetranslatorClient:
    return both_boards["a"]


@pytest_asyncio.fixture(scope="session", loop_scope="session")
async def board_b(both_boards) -> RetranslatorClient:
    return both_boards["b"]


@pytest_asyncio.fixture(autouse=True, loop_scope="session")
async def drain_before_test(both_boards):
    """Clear stale notifications before each test."""
    await both_boards["a"].drain()
    await both_boards["b"].drain()
