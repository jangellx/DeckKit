#!/bin/bash
# Builds improv_test.cpp with components/deckkit_wifi/ImprovProtocol.cpp on the Mac and runs it.
set -euo pipefail
cd "$( dirname "$0" )"
WORK="$( mktemp -d )"
trap 'rm -rf "$WORK"' EXIT
clang++ -std=c++17 -Wall -Wextra -Wshadow -fsanitize=address,undefined -I../../components/deckkit_wifi improv_test.cpp ../../components/deckkit_wifi/ImprovProtocol.cpp -o "$WORK/improv_test"
"$WORK/improv_test"
