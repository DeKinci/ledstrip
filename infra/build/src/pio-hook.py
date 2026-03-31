#!/usr/bin/env python3
"""
PlatformIO pre-build hook.
Runs the full build pipeline before firmware compilation:
  1. Generate test symlinks (test_generated/)
  2. Bundle web resources (web/ -> rsc_gen/)
  3. Compress resources into C++ headers (rsc/ + rsc_gen/ -> src/gen/)
"""

import os
import subprocess

def run_tsx(project_dir, script, label):
    path = os.path.join(project_dir, script)
    if not os.path.exists(path):
        return
    print(f"[build] {label}")
    try:
        subprocess.run(
            ["npx", "tsx", path],
            cwd=project_dir,
            check=True,
            timeout=60,
        )
    except Exception as e:
        print(f"[build] WARNING: {label} failed: {e}")

def run_tsx_with_args(project_dir, script, args, label):
    path = os.path.join(project_dir, script)
    if not os.path.exists(path):
        return
    print(f"[build] {label}")
    try:
        subprocess.run(
            ["npx", "tsx", path] + args,
            cwd=project_dir,
            check=True,
            timeout=60,
        )
    except Exception as e:
        print(f"[build] WARNING: {label} failed: {e}")

def before_build(source, target, env):
    project_dir = env.get("PROJECT_DIR", os.getcwd())
    src_dir = env.get("PROJECT_SRC_DIR", os.path.join(project_dir, "src"))

    # Determine which device we're building by checking src_dir
    # e.g. /path/to/ledstrip/device/ledstrip/src -> device/ledstrip
    rel_src = os.path.relpath(src_dir, project_dir)
    device_dir = os.path.dirname(rel_src)  # device/ledstrip

    # 1. Generate test symlinks
    run_tsx(project_dir, "infra/build/src/generate-test-symlinks.ts",
            "Generating test symlinks")

    # 2. Bundle web resources (lib/microproto-web/web/ -> rsc_gen/)
    run_tsx(project_dir, "infra/build/src/bundle-web.ts",
            "Bundling web resources")

    # 3. Compress resources into C++ headers
    manifest = os.path.join(project_dir, device_dir, "resources.txt")
    if os.path.exists(manifest):
        run_tsx_with_args(
            project_dir,
            "infra/build/src/compress-resources.ts",
            [device_dir],
            f"Compressing resources for {device_dir}")

# PlatformIO hook entry point
Import("env")
env.AddPreAction("buildprog", before_build)
