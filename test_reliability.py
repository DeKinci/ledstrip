#!/usr/bin/env python3
"""
Test HTTP endpoint reliability for ESP32 device
"""

import requests
import time
import statistics
from typing import List, Tuple

# Configuration
DEVICE_IP = "10.0.0.96"
ENDPOINT = "/ping"
NUM_REQUESTS = 20
TIMEOUT = 15  # seconds

def test_endpoint(url: str, num_requests: int) -> Tuple[List[float], int, int]:
    """
    Test an endpoint multiple times and collect statistics
    Returns: (response_times, success_count, failure_count)
    """
    response_times = []
    success_count = 0
    failure_count = 0

    print(f"Testing {url}")
    print(f"Making {num_requests} requests...\n")

    for i in range(num_requests):
        try:
            start = time.time()
            response = requests.get(url, timeout=TIMEOUT)
            elapsed = time.time() - start

            if response.status_code == 200:
                response_times.append(elapsed)
                success_count += 1
                status = "✓"
            else:
                failure_count += 1
                status = f"✗ (HTTP {response.status_code})"

            print(f"Request {i+1:2d}: {status:20s} {elapsed*1000:7.1f}ms")

        except requests.exceptions.Timeout:
            failure_count += 1
            print(f"Request {i+1:2d}: ✗ TIMEOUT          {TIMEOUT*1000:7.1f}ms")

        except requests.exceptions.RequestException as e:
            failure_count += 1
            print(f"Request {i+1:2d}: ✗ ERROR            {str(e)[:30]}")

        # Small delay between requests
        time.sleep(0.1)

    return response_times, success_count, failure_count

def print_statistics(response_times: List[float], success_count: int, failure_count: int):
    """Print statistics about the test results"""
    total = success_count + failure_count

    print("\n" + "="*60)
    print("RESULTS")
    print("="*60)
    print(f"Total requests:    {total}")
    print(f"Successful:        {success_count} ({success_count/total*100:.1f}%)")
    print(f"Failed:            {failure_count} ({failure_count/total*100:.1f}%)")

    if response_times:
        print(f"\nResponse times (successful requests only):")
        print(f"  Min:             {min(response_times)*1000:.1f}ms")
        print(f"  Max:             {max(response_times)*1000:.1f}ms")
        print(f"  Average:         {statistics.mean(response_times)*1000:.1f}ms")
        print(f"  Median:          {statistics.median(response_times)*1000:.1f}ms")

        if len(response_times) > 1:
            print(f"  Std Dev:         {statistics.stdev(response_times)*1000:.1f}ms")

        # Categorize response times
        fast = sum(1 for t in response_times if t < 0.1)
        medium = sum(1 for t in response_times if 0.1 <= t < 1.0)
        slow = sum(1 for t in response_times if t >= 1.0)

        print(f"\nResponse time distribution:")
        print(f"  < 100ms:         {fast} ({fast/len(response_times)*100:.1f}%)")
        print(f"  100ms - 1s:      {medium} ({medium/len(response_times)*100:.1f}%)")
        print(f"  > 1s:            {slow} ({slow/len(response_times)*100:.1f}%)")

    print("="*60)

def main():
    url = f"http://{DEVICE_IP}{ENDPOINT}"

    print("ESP32 HTTP Reliability Test")
    print("="*60)
    print(f"Device IP:  {DEVICE_IP}")
    print(f"Endpoint:   {ENDPOINT}")
    print(f"Requests:   {NUM_REQUESTS}")
    print(f"Timeout:    {TIMEOUT}s")
    print("="*60 + "\n")

    # Run the test
    response_times, success_count, failure_count = test_endpoint(url, NUM_REQUESTS)

    # Print statistics
    print_statistics(response_times, success_count, failure_count)

    # Recommendation
    if failure_count / (success_count + failure_count) > 0.1:
        print("\n⚠️  HIGH FAILURE RATE - Check WiFi signal, interference, or router")
    elif response_times and statistics.mean(response_times) > 1.0:
        print("\n⚠️  SLOW RESPONSES - Possible congestion or processing delays")
    else:
        print("\n✓ Connection appears stable")

if __name__ == "__main__":
    main()
