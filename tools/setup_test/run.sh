#!/bin/bash
# Builds setup_test.cpp with components/deckkit_wifi/SetupProtocol.cpp on the Mac and runs it.
set -euo pipefail
cd "$( dirname "$0" )"
WORK="$( mktemp -d )"
trap 'rm -rf "$WORK"' EXIT
clang++ -std=c++17 -Wall -Wextra -Wshadow -fsanitize=address,undefined -I../../components/deckkit_wifi setup_test.cpp ../../components/deckkit_wifi/SetupProtocol.cpp -o "$WORK/setup_test"
"$WORK/setup_test"
