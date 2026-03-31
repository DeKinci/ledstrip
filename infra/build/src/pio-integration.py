"""
PlatformIO integration for device tests.

Custom targets:
  pio run -t test_device           Run all integration test suites
  pio run -t test_device_list      List available test suites
"""

Import("env")

import subprocess
import time
from pathlib import Path


def _run_cli(env, *args):
    project_dir = Path(env.get("PROJECT_DIR", "."))
    cli = project_dir / "infra" / "test-runner" / "src" / "cli.ts"
    if not cli.exists():
        print(f"Test runner not found at {cli}")
        env.Exit(1)
        return
    result = subprocess.run(
        ["npx", "tsx", str(cli)] + list(args),
        cwd=str(project_dir),
    )
    if result.returncode != 0:
        env.Exit(1)


def run_all_tests(source, target, env):
    print("\n" + "=" * 60)
    print("Running Integration Tests")
    print("=" * 60 + "\n")
    print("Waiting 3 seconds for device to boot...")
    time.sleep(3)
    _run_cli(env, "all", "--skip-flash")


def list_suites(source, target, env):
    _run_cli(env, "list")


env.AddCustomTarget(
    name="test_device",
    dependencies=None,
    actions=[run_all_tests],
    title="Test Device",
    description="Run all integration tests against device",
)

env.AddCustomTarget(
    name="test_device_list",
    dependencies=None,
    actions=[list_suites],
    title="List Test Suites",
    description="List available integration test suites",
)
