#!/bin/bash
# Builds timing_test.cpp with components/deckkit_util/Timing.h on the Mac and runs it.
set -euo pipefail
cd "$( dirname "$0" )"
WORK="$( mktemp -d )"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/include"
# Timing.h includes esp_timer.h for nowMillis(), which these checks don't use.
printf '#pragma once\n#include <cstdint>\ninline int64_t esp_timer_get_time() { return 0; }\n' > "$WORK/include/esp_timer.h"
clang++ -std=c++17 -Wall -Wextra -Wshadow -fsanitize=address,undefined -I../../components/deckkit_util -I"$WORK/include" timing_test.cpp -o "$WORK/timing_test"
"$WORK/timing_test"
