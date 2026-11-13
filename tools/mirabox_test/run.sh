#!/bin/bash
# Builds mirabox_test.cpp with components/deckkit_deck/MiraboxProtocol.cpp on the Mac and runs it.
set -euo pipefail
cd "$( dirname "$0" )"
WORK="$( mktemp -d )"
trap 'rm -rf "$WORK"' EXIT
clang++ -std=c++17 -Wall -Wextra -Wshadow -fsanitize=address,undefined -I../../components/deckkit_deck mirabox_test.cpp ../../components/deckkit_deck/MiraboxProtocol.cpp -o "$WORK/mirabox_test"
"$WORK/mirabox_test"
