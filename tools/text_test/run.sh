#!/bin/bash
# Builds text_test.cpp with components/deckkit_util/Text.cpp on the Mac and runs it.
set -euo pipefail
cd "$( dirname "$0" )"
WORK="$( mktemp -d )"
trap 'rm -rf "$WORK"' EXIT
clang++ -std=c++17 -Wall -Wextra -Wshadow -fsanitize=address,undefined -I../../components/deckkit_util text_test.cpp ../../components/deckkit_util/Text.cpp -o "$WORK/text_test"
"$WORK/text_test"
