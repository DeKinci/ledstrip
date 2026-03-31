"""
PlatformIO integration for running device tests.

Adds a custom target: pio run -t test_device
Delegates to the TS test runner at infra/test-runner/
"""

Import("env")

import subprocess
import time
from pathlib import Path


def run_integration_tests(source, target, env):
    """Run integration tests on the device."""
    project_dir = Path(env.get("PROJECT_DIR", "."))
    cli = project_dir / "infra" / "test-runner" / "src" / "cli.ts"

    if not cli.exists():
        print(f"Test runner not found at {cli}")
        env.Exit(1)
        return

    print("\n" + "=" * 60)
    print("Running Integration Tests")
    print("=" * 60 + "\n")

    # Wait for device to boot after upload
    print("Waiting 3 seconds for device to boot...")
    time.sleep(3)

    result = subprocess.run(
        ["npx", "tsx", str(cli), "all", "--skip-flash"],
        cwd=str(project_dir),
    )

    if result.returncode != 0:
        env.Exit(1)


# Register custom target
env.AddCustomTarget(
    name="test_device",
    dependencies=None,
    actions=[run_integration_tests],
    title="Test Device",
    description="Run integration tests against device (auto-discovers)"
)
