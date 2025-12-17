"""
LED API HTTP endpoint tests

Tests the shader CRUD and animation control endpoints.
"""

import time
from . import Colors, TestResult

try:
    import requests
    HAS_REQUESTS = True
except ImportError:
    HAS_REQUESTS = False


def test_shader_list(ip: str) -> TestResult:
    """Test GET /api/shader - list all shaders."""
    if not HAS_REQUESTS:
        return TestResult("Shader List", False, error="requests not installed")

    print(f"\n{Colors.CYAN}{Colors.BOLD}[TEST] Shader List API{Colors.RESET}")
    url = f"http://{ip}/api/shader"

    result = TestResult("Shader List", True)

    try:
        start = time.time()
        response = requests.get(url, timeout=5)
        elapsed = (time.time() - start) * 1000

        if response.status_code != 200:
            result.fail_count += 1
            result.passed = False
            print(f"  {Colors.RED}FAIL{Colors.RESET} HTTP {response.status_code}")
            return result

        data = response.json()

        if 'shader' not in data:
            result.fail_count += 1
            result.passed = False
            print(f"  {Colors.RED}FAIL{Colors.RESET} Missing 'shader' field")
        elif not isinstance(data['shader'], list):
            result.fail_count += 1
            result.passed = False
            print(f"  {Colors.RED}FAIL{Colors.RESET} 'shader' is not a list")
        else:
            result.success_count += 1
            result.times.append(elapsed)
            print(f"  {Colors.GREEN}OK{Colors.RESET} {elapsed:6.1f}ms - {len(data['shader'])} shader(s)")
            for name in data['shader'][:5]:  # Show up to 5
                print(f"      - {name}")

    except Exception as e:
        result.fail_count += 1
        result.passed = False
        print(f"  {Colors.RED}Error: {str(e)[:50]}{Colors.RESET}")

    return result


def test_shader_crud(ip: str) -> TestResult:
    """Test shader CRUD operations (add, get, delete)."""
    if not HAS_REQUESTS:
        return TestResult("Shader CRUD", False, error="requests not installed")

    print(f"\n{Colors.CYAN}{Colors.BOLD}[TEST] Shader CRUD{Colors.RESET}")

    result = TestResult("Shader CRUD", True)
    test_shader_name = "__test_shader__"
    test_shader_code = "return hsv(t * 0.1, 1.0, brightness)"

    try:
        # POST /api/shader - Add a test shader
        print(f"\n  1. Add shader '{test_shader_name}'")
        start = time.time()
        response = requests.post(
            f"http://{ip}/api/shader",
            json={"name": test_shader_name, "shader": test_shader_code},
            timeout=5
        )
        elapsed = (time.time() - start) * 1000

        if response.status_code != 200:
            result.fail_count += 1
            result.passed = False
            print(f"     {Colors.RED}FAIL{Colors.RESET} HTTP {response.status_code}")
            try:
                print(f"     Response: {response.json()}")
            except:
                pass
            return result

        data = response.json()
        if data.get('success'):
            result.success_count += 1
            result.times.append(elapsed)
            print(f"     {Colors.GREEN}OK{Colors.RESET} {elapsed:6.1f}ms")
        else:
            result.fail_count += 1
            result.passed = False
            print(f"     {Colors.RED}FAIL{Colors.RESET} {data.get('error', 'unknown error')}")
            return result

        # GET /api/shader/{name} - Retrieve the shader
        print(f"\n  2. Get shader '{test_shader_name}'")
        start = time.time()
        response = requests.get(f"http://{ip}/api/shader/{test_shader_name}", timeout=5)
        elapsed = (time.time() - start) * 1000

        if response.status_code != 200:
            result.fail_count += 1
            result.passed = False
            print(f"     {Colors.RED}FAIL{Colors.RESET} HTTP {response.status_code}")
        else:
            data = response.json()
            if 'shader' in data and test_shader_code in data['shader']:
                result.success_count += 1
                result.times.append(elapsed)
                print(f"     {Colors.GREEN}OK{Colors.RESET} {elapsed:6.1f}ms - {len(data['shader'])} bytes")
            else:
                result.fail_count += 1
                result.passed = False
                print(f"     {Colors.RED}FAIL{Colors.RESET} Shader content mismatch")

        # Verify it's in the list
        print(f"\n  3. Verify shader in list")
        response = requests.get(f"http://{ip}/api/shader", timeout=5)
        data = response.json()
        if test_shader_name in data.get('shader', []):
            result.success_count += 1
            print(f"     {Colors.GREEN}OK{Colors.RESET} Found in list")
        else:
            result.fail_count += 1
            result.passed = False
            print(f"     {Colors.RED}FAIL{Colors.RESET} Not in list")

        # DELETE /api/shader/{name} - Remove the shader
        print(f"\n  4. Delete shader '{test_shader_name}'")
        start = time.time()
        response = requests.delete(f"http://{ip}/api/shader/{test_shader_name}", timeout=5)
        elapsed = (time.time() - start) * 1000

        if response.status_code != 200:
            result.fail_count += 1
            result.passed = False
            print(f"     {Colors.RED}FAIL{Colors.RESET} HTTP {response.status_code}")
        else:
            data = response.json()
            if data.get('success'):
                result.success_count += 1
                result.times.append(elapsed)
                print(f"     {Colors.GREEN}OK{Colors.RESET} {elapsed:6.1f}ms")
            else:
                result.fail_count += 1
                result.passed = False
                print(f"     {Colors.RED}FAIL{Colors.RESET} {data.get('error', 'unknown error')}")

        # Verify it's gone
        print(f"\n  5. Verify shader removed")
        response = requests.get(f"http://{ip}/api/shader", timeout=5)
        data = response.json()
        if test_shader_name not in data.get('shader', []):
            result.success_count += 1
            print(f"     {Colors.GREEN}OK{Colors.RESET} No longer in list")
        else:
            result.fail_count += 1
            result.passed = False
            print(f"     {Colors.RED}FAIL{Colors.RESET} Still in list")

    except Exception as e:
        result.fail_count += 1
        result.passed = False
        print(f"  {Colors.RED}Error: {str(e)[:50]}{Colors.RESET}")

        # Try to clean up
        try:
            requests.delete(f"http://{ip}/api/shader/{test_shader_name}", timeout=2)
        except:
            pass

    return result


def test_animation_status(ip: str) -> TestResult:
    """Test GET /api/show - get current animation info."""
    if not HAS_REQUESTS:
        return TestResult("Animation Status", False, error="requests not installed")

    print(f"\n{Colors.CYAN}{Colors.BOLD}[TEST] Animation Status API{Colors.RESET}")
    url = f"http://{ip}/api/show"

    result = TestResult("Animation Status", True)

    try:
        start = time.time()
        response = requests.get(url, timeout=5)
        elapsed = (time.time() - start) * 1000

        if response.status_code != 200:
            result.fail_count += 1
            result.passed = False
            print(f"  {Colors.RED}FAIL{Colors.RESET} HTTP {response.status_code}")
            return result

        data = response.json()
        required_fields = ['name', 'ledLimit', 'shaderCount']
        missing = [f for f in required_fields if f not in data]

        if missing:
            result.fail_count += 1
            result.passed = False
            print(f"  {Colors.RED}FAIL{Colors.RESET} Missing fields: {missing}")
        else:
            result.success_count += 1
            result.times.append(elapsed)
            print(f"  {Colors.GREEN}OK{Colors.RESET} {elapsed:6.1f}ms")
            print(f"      name={data['name']}, ledLimit={data['ledLimit']}, shaders={data['shaderCount']}")

    except Exception as e:
        result.fail_count += 1
        result.passed = False
        print(f"  {Colors.RED}Error: {str(e)[:50]}{Colors.RESET}")

    return result


def test_ble_scan(ip: str) -> TestResult:
    """Test BLE scan endpoints."""
    if not HAS_REQUESTS:
        return TestResult("BLE Scan", False, error="requests not installed")

    print(f"\n{Colors.CYAN}{Colors.BOLD}[TEST] BLE Scan API{Colors.RESET}")

    result = TestResult("BLE Scan", True)

    try:
        # POST /api/ble/scan - Trigger scan
        print(f"\n  1. Trigger BLE scan")
        start = time.time()
        response = requests.post(f"http://{ip}/api/ble/scan", timeout=5)
        elapsed = (time.time() - start) * 1000

        if response.status_code != 200:
            result.fail_count += 1
            result.passed = False
            print(f"     {Colors.RED}FAIL{Colors.RESET} HTTP {response.status_code}")
            return result

        data = response.json()
        if data.get('success'):
            result.success_count += 1
            result.times.append(elapsed)
            print(f"     {Colors.GREEN}OK{Colors.RESET} {elapsed:6.1f}ms - scan started")
        else:
            print(f"     {Colors.YELLOW}WARN{Colors.RESET} {data.get('message', 'unknown')}")
            result.success_count += 1  # Still counts as success

        # GET /api/ble/scan/results - Get results (may be empty or scanning)
        print(f"\n  2. Get scan results")
        time.sleep(2)  # Wait for scan to complete
        start = time.time()
        response = requests.get(f"http://{ip}/api/ble/scan/results", timeout=5)
        elapsed = (time.time() - start) * 1000

        if response.status_code != 200:
            result.fail_count += 1
            result.passed = False
            print(f"     {Colors.RED}FAIL{Colors.RESET} HTTP {response.status_code}")
        else:
            data = response.json()
            scanning = data.get('scanning', False)
            devices = data.get('devices', [])
            result.success_count += 1
            result.times.append(elapsed)
            print(f"     {Colors.GREEN}OK{Colors.RESET} {elapsed:6.1f}ms - scanning={scanning}, devices={len(devices)}")
            for dev in devices[:3]:  # Show up to 3
                print(f"        - {dev.get('name', '?')} ({dev.get('address', '?')})")

    except Exception as e:
        result.fail_count += 1
        result.passed = False
        print(f"  {Colors.RED}Error: {str(e)[:50]}{Colors.RESET}")

    return result


def test_ble_known_devices(ip: str) -> TestResult:
    """Test BLE known devices endpoints."""
    if not HAS_REQUESTS:
        return TestResult("BLE Known Devices", False, error="requests not installed")

    print(f"\n{Colors.CYAN}{Colors.BOLD}[TEST] BLE Known Devices API{Colors.RESET}")

    result = TestResult("BLE Known Devices", True)
    test_address = "00:00:00:00:00:FF"  # Fake address for testing

    try:
        # GET /api/ble/known - List known devices
        print(f"\n  1. List known devices")
        start = time.time()
        response = requests.get(f"http://{ip}/api/ble/known", timeout=5)
        elapsed = (time.time() - start) * 1000

        if response.status_code != 200:
            result.fail_count += 1
            result.passed = False
            print(f"     {Colors.RED}FAIL{Colors.RESET} HTTP {response.status_code}")
            return result

        data = response.json()
        devices = data.get('devices', [])
        result.success_count += 1
        result.times.append(elapsed)
        print(f"     {Colors.GREEN}OK{Colors.RESET} {elapsed:6.1f}ms - {len(devices)} known device(s)")

        # POST /api/ble/known - Add test device
        print(f"\n  2. Add test device")
        start = time.time()
        response = requests.post(
            f"http://{ip}/api/ble/known",
            json={"address": test_address, "name": "TestDevice", "icon": "test"},
            timeout=5
        )
        elapsed = (time.time() - start) * 1000

        if response.status_code != 200:
            result.fail_count += 1
            print(f"     {Colors.RED}FAIL{Colors.RESET} HTTP {response.status_code}")
        else:
            data = response.json()
            if data.get('success'):
                result.success_count += 1
                result.times.append(elapsed)
                print(f"     {Colors.GREEN}OK{Colors.RESET} {elapsed:6.1f}ms")
            else:
                result.fail_count += 1
                print(f"     {Colors.RED}FAIL{Colors.RESET} {data.get('error', 'unknown')}")

        # DELETE /api/ble/known/{addr} - Remove test device
        print(f"\n  3. Remove test device")
        start = time.time()
        response = requests.delete(f"http://{ip}/api/ble/known/{test_address}", timeout=5)
        elapsed = (time.time() - start) * 1000

        if response.status_code != 200:
            result.fail_count += 1
            print(f"     {Colors.RED}FAIL{Colors.RESET} HTTP {response.status_code}")
        else:
            data = response.json()
            if data.get('success'):
                result.success_count += 1
                result.times.append(elapsed)
                print(f"     {Colors.GREEN}OK{Colors.RESET} {elapsed:6.1f}ms")
            else:
                # 404 is ok if device wasn't added
                print(f"     {Colors.YELLOW}WARN{Colors.RESET} {data.get('error', 'not found')}")
                result.success_count += 1

    except Exception as e:
        result.fail_count += 1
        result.passed = False
        print(f"  {Colors.RED}Error: {str(e)[:50]}{Colors.RESET}")

        # Try to clean up
        try:
            requests.delete(f"http://{ip}/api/ble/known/{test_address}", timeout=2)
        except:
            pass

    return result


def test_ble_connected(ip: str) -> TestResult:
    """Test BLE connected devices endpoint."""
    if not HAS_REQUESTS:
        return TestResult("BLE Connected", False, error="requests not installed")

    print(f"\n{Colors.CYAN}{Colors.BOLD}[TEST] BLE Connected API{Colors.RESET}")
    url = f"http://{ip}/api/ble/connected"

    result = TestResult("BLE Connected", True)

    try:
        start = time.time()
        response = requests.get(url, timeout=5)
        elapsed = (time.time() - start) * 1000

        if response.status_code != 200:
            result.fail_count += 1
            result.passed = False
            print(f"  {Colors.RED}FAIL{Colors.RESET} HTTP {response.status_code}")
            return result

        data = response.json()
        devices = data.get('devices', [])
        result.success_count += 1
        result.times.append(elapsed)
        print(f"  {Colors.GREEN}OK{Colors.RESET} {elapsed:6.1f}ms - {len(devices)} connected device(s)")
        for dev in devices:
            print(f"      - {dev.get('name', '?')} ({dev.get('address', '?')})")

    except Exception as e:
        result.fail_count += 1
        result.passed = False
        print(f"  {Colors.RED}Error: {str(e)[:50]}{Colors.RESET}")

    return result