// Millisecond clocks and deadlines that stay right across millis()' 49-day wrap.
#pragma once

#include <cstdint>

#include "esp_timer.h"

namespace Timing {
	// Milliseconds since boot, from a clock that's safe on any task (no Arduino needed).
	inline uint32_t nowMillis() {
		return (uint32_t)( esp_timer_get_time() / 1000 );
	}

	// Whether `deadline` has come by `now`. Only for a deadline set within the last 24.8 days:
	// one set (or left at 0) longer ago than that looks like the future again.
	inline bool reached( uint32_t deadline, uint32_t now ) {
		return (int32_t)( now - deadline ) >= 0;
	}

	// Whether a deadline that's only ever set up to `span` ms ahead has come: anything not in
	// (now, now + span] has. So one never set (0), or set long ago, counts as come, however
	// long the device has been up; reached() would call it the future for half of every 49 days.
	inline bool due( uint32_t deadline, uint32_t now, uint32_t span ) {
		uint32_t ahead = deadline - now;
		return ahead == 0 || ahead > span;
	}
}
