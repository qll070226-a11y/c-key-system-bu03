#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$repo_root/tests/build"
mkdir -p "$build_dir"

gcc -std=c17 -Wall -Wextra -Werror -pedantic \
  -I"$repo_root/components/c_key_core/include" \
  "$repo_root/components/c_key_core/c_key_core.c" \
  "$repo_root/components/c_key_core/c_key_pipeline.c" \
  "$repo_root/components/c_key_core/c_key_display.c" \
  "$repo_root/components/c_key_core/bu03_uart2.c" \
  "$repo_root/components/c_key_core/bu03_twr_usb.c" \
  "$repo_root/components/c_key_core/bu04_pdoa.c" \
  "$repo_root/components/c_key_core/c_key_bu03_bridge.c" \
  "$repo_root/components/c_key_core/c_key_telemetry.c" \
  "$repo_root/tests/test_core.c" \
  "$repo_root/tests/test_pipeline.c" \
  "$repo_root/tests/test_display.c" \
  "$repo_root/tests/test_bu03_uart2.c" \
  "$repo_root/tests/test_bu03_twr_usb.c" \
  "$repo_root/tests/test_bu04_pdoa.c" \
  "$repo_root/tests/test_bu03_bridge.c" \
  "$repo_root/tests/test_contest_scenario.c" \
  "$repo_root/tests/test_telemetry.c" \
  -lm -o "$build_dir/test_core"

"$build_dir/test_core"
