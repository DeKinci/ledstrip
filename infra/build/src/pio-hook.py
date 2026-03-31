#!/usr/bin/env python3
"""
PlatformIO pre-build hook.
Runs the full build pipeline before firmware compilation:
  1. Generate test symlinks (test_generated/)
  2. Bundle web resources (web/ -> rsc_gen/)
  3. Compress resources for all libs and the device (rsc/ -> src/gen/)
"""

import os
import subprocess
import glob

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

    rel_src = os.path.relpath(src_dir, project_dir)
    device_dir = os.path.dirname(rel_src)

    # 1. Generate test symlinks
    run_tsx(project_dir, "infra/build/src/generate-test-symlinks.ts",
            "Generating test symlinks")

    # 2. Bundle web resources
    run_tsx(project_dir, "infra/build/src/bundle-web.ts",
            "Bundling web resources")

    # 3. Compress resources for all libs that have resources.txt
    compress = "infra/build/src/compress-resources.ts"
    for manifest in glob.glob(os.path.join(project_dir, "lib", "*", "rsc", "resources.txt")):
        lib_dir = os.path.relpath(os.path.dirname(os.path.dirname(manifest)), project_dir)
        run_tsx_with_args(project_dir, compress, [lib_dir],
                          f"Compressing resources for {lib_dir}")

    # 4. Compress resources for the device
    device_manifest = os.path.join(project_dir, device_dir, "rsc", "resources.txt")
    if os.path.exists(device_manifest):
        run_tsx_with_args(project_dir, compress, [device_dir],
                          f"Compressing resources for {device_dir}")

Import("env")
before_build(None, None, env)
