#!/usr/bin/env bash
# Builds the CardputerZero Debian package (dist/NetTest_*_arm64.deb) inside a
# Debian container, so it works the same from macOS, Windows (WSL) or Linux.
#
# The README asks for a Debian-based host for packaging because CPack needs
# dpkg-shlibdeps/objdump/readelf; this script provides one with Docker or
# OrbStack and then runs the documented workflow preset unchanged:
#
#   cmake --workflow --preset cp0-cross-package
#
# Usage: ./build-deb.sh            (extra args are passed to the workflow)
set -euo pipefail

IMAGE="${BUILDER_IMAGE:-nettest-builder:trixie}"
WORKFLOW_PRESET="${WORKFLOW_PRESET:-cp0-cross-package}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if ! command -v docker >/dev/null 2>&1; then
  printf 'docker not found. Install OrbStack (https://orbstack.dev) or Docker Desktop first.\n' >&2
  exit 1
fi

# Debian 13 (trixie) matches the device OS. On an arm64 host (Apple Silicon)
# the native gcc already provides aarch64-linux-gnu-gcc; elsewhere the
# cross toolchain is installed instead.
printf '\n==> Preparing builder image %s\n' "${IMAGE}"
docker build --quiet -t "${IMAGE}" - <<'EOF' >/dev/null
FROM debian:trixie
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      cmake ninja-build git ca-certificates pkg-config file dpkg-dev binutils \
 && if [ "$(dpkg --print-architecture)" = "arm64" ]; then \
      apt-get install -y --no-install-recommends build-essential; \
    else \
      apt-get install -y --no-install-recommends crossbuild-essential-arm64 binutils-aarch64-linux-gnu; \
    fi \
 && rm -rf /var/lib/apt/lists/*
EOF

# Always mount at the same path: CMake caches absolute paths in build/ and .cache/.
printf '\n==> cmake --workflow --preset %s\n' "${WORKFLOW_PRESET}"
docker run --rm \
  -v "${ROOT}:/src" \
  -w /src \
  "${IMAGE}" \
  cmake --workflow --preset "${WORKFLOW_PRESET}" "$@"

shopt -s nullglob
packages=( "${ROOT}"/dist/*_arm64.deb )
if (( ${#packages[@]} == 0 )); then
  printf 'No package was produced in dist/\n' >&2
  exit 1
fi
printf '\n==> Package ready: %s\n' "$(ls -t "${packages[@]}" | head -n 1)"
