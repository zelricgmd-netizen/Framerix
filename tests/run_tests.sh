#!/usr/bin/env bash
# Builds and runs the host unit tests (no Geode SDK needed).
set -euo pipefail
cd "$(dirname "$0")/.."
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
  -Isrc/core tests/test_core.cpp -o /tmp/framefix_tests
/tmp/framefix_tests
