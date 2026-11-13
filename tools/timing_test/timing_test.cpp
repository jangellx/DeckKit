// Checks components/deckkit_util/Timing.h on the Mac: deadlines across millis()' wrap. The
// firmware once ignored PlatformIO uploads and showed a stuck white light from day 24.8 to day
// 49.7 of uptime, because deadlines left at 0 looked like the future to reached() then. Run
// with run.sh; it prints each failure and exits non-zero if there are any.
#include <cstdio>

#include "Timing.h"

namespace {
	int failures = 0;

	void check( bool condition, const char *what ) {
		if( !condition ) {
			printf( "FAIL: %s\n", what );
			failures++;
		}
	}
}

int main() {
	constexpr uint32_t kHalf = 0x80000000u;   // 24.8 days in ms

	// reached(): right within 24.8 days of the deadline, either side of the wrap.
	check( Timing::reached( 100, 100 ), "reached at the deadline" );
	check( !Timing::reached( 101, 100 ), "not before it" );
	check( Timing::reached( 0xFFFFFFF0u, 5 ), "reached across the wrap" );
	check( !Timing::reached( 5, 0xFFFFFFF0u ), "not yet, across the wrap" );
	check( !Timing::reached( 0, kHalf ), "a deadline left at 0 looks like the future after 24.8 days (why due() exists)" );

	// due(): a deadline only ever set up to `span` ahead.
	check( Timing::due( 0, 10, 300 ), "never set, early on" );
	check( Timing::due( 0, kHalf, 300 ), "never set, after 24.8 days" );
	check( Timing::due( 0, kHalf + 1000, 300 ), "never set, after 24.8 days and a bit" );
	// The one limit: in the last `span` ms before the wrap, a deadline never set looks that far
	// ahead, so it waits at most `span` once every 49 days.
	check( !Timing::due( 0, 0xFFFFFF00u, 300 ), "never set, 256 ms before the wrap: waits that long" );
	check( Timing::due( 0, 0xFFFFFE00u, 300 ), "never set, 512 ms before the wrap" );
	check( !Timing::due( 1150, 1000, 300 ), "set 150 ahead: not yet" );
	check( Timing::due( 1000, 1000, 300 ), "at the deadline" );
	check( Timing::due( 1150, 1200, 300 ), "after it" );
	check( !Timing::due( 100, 0xFFFFFFF0u, 300 ), "set across the wrap: not yet" );
	check( Timing::due( 100, 101, 300 ), "set across the wrap: come" );
	check( Timing::due( 1000, 1000 + kHalf + 5, 300 ), "set long ago" );

	if( failures == 0 )
		printf( "Timing: all checks passed.\n" );
	return failures == 0 ? 0 : 1;
}
