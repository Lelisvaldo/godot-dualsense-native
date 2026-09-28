#!/usr/bin/env bash
# Builds the GDExtension into example/addons/dualsense/bin using the pinned Docker toolchain.
# Usage: ./build.sh [linux|windows|all]   (default: all)
set -euo pipefail
cd "$(dirname "$0")"
IMAGE=godot-dualsense-native-toolchain:1
target="${1:-all}"
case "$target" in
  linux|windows) platforms=("$target") ;;
  all) platforms=(linux windows) ;;
  *) echo "usage: $0 [linux|windows|all]" >&2; exit 2 ;;
esac
docker build -t "$IMAGE" -f docker/Dockerfile docker
for p in "${platforms[@]}"; do
  docker run --rm --network none -u "$(id -u):$(id -g)" -v "$PWD:/work" "$IMAGE" \
    scons -C /work/native -j2 platform="$p"
done
ls -la example/addons/dualsense/bin
