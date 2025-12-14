#!/usr/bin/env python3
"""
ESP32 WebSocket Reliability Test
Tests WebSocket connection stability, message round-trip times, and reliability.
"""

import asyncio
import websockets
import time
import statistics
import sys

# Configuration
ESP32_IP = "10.0.0.96"
WS_PORT = 81
NUM_MESSAGES = 50
MESSAGE_INTERVAL = 0.1  # seconds between messages
TIMEOUT = 5.0  # seconds

class Colors:
    GREEN = '\033[92m'
    RED = '\033[91m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    RESET = '\033[0m'
    BOLD = '\033[1m'

def print_header():
    print(f"\n{Colors.BOLD}ESP32 WebSocket Reliability Test{Colors.RESET}")
    print("=" * 60)
    print(f"Device IP:  {ESP32_IP}")
    print(f"WS Port:    {WS_PORT}")
    print(f"Messages:   {NUM_MESSAGES}")
    print(f"Interval:   {MESSAGE_INTERVAL}s")
    print(f"Timeout:    {TIMEOUT}s")
    print("=" * 60)

async def test_websocket():
    ws_url = f"ws://{ESP32_IP}:{WS_PORT}"
    print(f"\nConnecting to {ws_url}...")

    results = {
        'sent': 0,
        'received': 0,
        'failed': 0,
        'times': [],
        'errors': []
    }

    try:
        async with websockets.connect(ws_url, ping_interval=None) as websocket:
            # Wait for welcome message
            try:
                welcome = await asyncio.wait_for(websocket.recv(), timeout=TIMEOUT)
                print(f"{Colors.GREEN}✓{Colors.RESET} Connected! Welcome: {welcome}\n")
            except asyncio.TimeoutError:
                print(f"{Colors.YELLOW}⚠{Colors.RESET} No welcome message received\n")

            print("Sending messages...\n")

            # Send messages and measure round-trip time
            for i in range(1, NUM_MESSAGES + 1):
                message = f"Test message {i}"

                try:
                    start_time = time.time()

                    # Send message
                    await websocket.send(message)
                    results['sent'] += 1

                    # Wait for response
                    response = await asyncio.wait_for(websocket.recv(), timeout=TIMEOUT)

                    elapsed_ms = (time.time() - start_time) * 1000
                    results['times'].append(elapsed_ms)
                    results['received'] += 1

                    status = f"{Colors.GREEN}✓{Colors.RESET}"
                    print(f"Message {i:3d}: {status} {elapsed_ms:7.1f}ms - {response[:50]}")

                except asyncio.TimeoutError:
                    results['failed'] += 1
                    elapsed_ms = TIMEOUT * 1000
                    results['errors'].append((i, 'timeout'))
                    status = f"{Colors.RED}✗ TIMEOUT{Colors.RESET}"
                    print(f"Message {i:3d}: {status} {elapsed_ms:7.1f}ms")

                except Exception as e:
                    results['failed'] += 1
                    results['errors'].append((i, str(e)))
                    status = f"{Colors.RED}✗ ERROR{Colors.RESET}"
                    print(f"Message {i:3d}: {status} {str(e)}")

                # Wait before next message
                if i < NUM_MESSAGES:
                    await asyncio.sleep(MESSAGE_INTERVAL)

            # Wait a bit for any broadcast messages
            print(f"\n{Colors.BLUE}Waiting for broadcast messages...{Colors.RESET}")
            try:
                while True:
                    msg = await asyncio.wait_for(websocket.recv(), timeout=2.0)
                    print(f"  📢 Broadcast: {msg}")
            except asyncio.TimeoutError:
                pass

    except websockets.exceptions.WebSocketException as e:
        print(f"\n{Colors.RED}WebSocket Error: {e}{Colors.RESET}")
        return None
    except Exception as e:
        print(f"\n{Colors.RED}Error: {e}{Colors.RESET}")
        return None

    return results

def print_results(results):
    if results is None:
        print("\n" + "=" * 60)
        print(f"{Colors.RED}TEST FAILED - Could not establish connection{Colors.RESET}")
        print("=" * 60)
        return

    print("\n" + "=" * 60)
    print(f"{Colors.BOLD}RESULTS{Colors.RESET}")
    print("=" * 60)

    success_rate = (results['received'] / results['sent'] * 100) if results['sent'] > 0 else 0

    print(f"Total messages sent: {results['sent']}")
    print(f"Successful:          {results['received']} ({success_rate:.1f}%)")
    print(f"Failed:              {results['failed']} ({100-success_rate:.1f}%)")

    if results['times']:
        print(f"\nRound-trip times (successful messages only):")
        print(f"  Min:             {min(results['times']):.1f}ms")
        print(f"  Max:             {max(results['times']):.1f}ms")
        print(f"  Average:         {statistics.mean(results['times']):.1f}ms")
        print(f"  Median:          {statistics.median(results['times']):.1f}ms")
        print(f"  Std Dev:         {statistics.stdev(results['times']):.1f}ms" if len(results['times']) > 1 else "  Std Dev:         N/A")

        # Distribution
        fast = sum(1 for t in results['times'] if t < 100)
        medium = sum(1 for t in results['times'] if 100 <= t < 1000)
        slow = sum(1 for t in results['times'] if t >= 1000)

        print(f"\nRound-trip time distribution:")
        print(f"  < 100ms:         {fast} ({fast/len(results['times'])*100:.1f}%)")
        print(f"  100ms - 1s:      {medium} ({medium/len(results['times'])*100:.1f}%)")
        print(f"  > 1s:            {slow} ({slow/len(results['times'])*100:.1f}%)")

    if results['errors']:
        print(f"\n{Colors.YELLOW}Errors:{Colors.RESET}")
        for msg_num, error in results['errors'][:5]:  # Show first 5 errors
            print(f"  Message {msg_num}: {error}")
        if len(results['errors']) > 5:
            print(f"  ... and {len(results['errors']) - 5} more")

    print("=" * 60)

def main():
    print_header()

    try:
        results = asyncio.run(test_websocket())
        print_results(results)
    except KeyboardInterrupt:
        print(f"\n\n{Colors.YELLOW}Test interrupted by user{Colors.RESET}")
        sys.exit(1)

if __name__ == "__main__":
    main()
