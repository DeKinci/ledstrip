#!/usr/bin/env python3
"""Simple WebSocket connection test with detailed error output"""

import asyncio
import websockets
import traceback

ESP32_IP = "10.0.0.96"
WS_PORT = 81

async def test():
    ws_url = f"ws://{ESP32_IP}:{WS_PORT}"
    print(f"Attempting to connect to: {ws_url}")

    try:
        async with websockets.connect(
            ws_url,
            ping_interval=None,
            close_timeout=10,
            open_timeout=10
        ) as ws:
            print("✓ Connected successfully!")

            # Try to receive welcome message
            msg = await asyncio.wait_for(ws.recv(), timeout=5)
            print(f"Received: {msg}")

            # Send a test message
            await ws.send("Hello ESP32")
            print("Sent: Hello ESP32")

            # Get response
            response = await asyncio.wait_for(ws.recv(), timeout=5)
            print(f"Response: {response}")

    except Exception as e:
        print(f"\n✗ Connection failed!")
        print(f"Error type: {type(e).__name__}")
        print(f"Error message: {e}")
        print(f"\nFull traceback:")
        traceback.print_exc()

if __name__ == "__main__":
    asyncio.run(test())
