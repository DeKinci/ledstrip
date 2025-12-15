"""
PlatformIO integration for running device tests.

Adds a custom target: pio run -t test_device
"""

Import("env")

import subprocess
import sys
import time
from pathlib import Path


def run_integration_tests(source, target, env):
    """Run integration tests on the device."""
    project_dir = Path(env.get("PROJECT_DIR", "."))
    venv_python = project_dir / ".venv" / "bin" / "python"
    test_script = project_dir / "scripts" / "run_integration_tests.py"

    print("\n" + "=" * 60)
    print("Running Integration Tests")
    print("=" * 60 + "\n")

    # Determine python executable
    if venv_python.exists():
        python = str(venv_python)
    else:
        python = sys.executable

    # Wait for device to boot after upload
    print("Waiting 3 seconds for device to boot...")
    time.sleep(3)

    # Run tests
    result = subprocess.run(
        [python, str(test_script)],
        cwd=str(project_dir)
    )

    if result.returncode != 0:
        env.Exit(1)


# Register custom target
env.AddCustomTarget(
    name="test_device",
    dependencies=None,
    actions=[run_integration_tests],
    title="Test Device",
    description="Run HTTP/WebSocket integration tests (auto-discovers device)"
)
