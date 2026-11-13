// The dev kit's RGB LED (a WS2812) as a status light, on plain ESP-IDF; the same colors as
// ESPDeck's:
//   setup mode              pulsing blue
//   looking for Wi-Fi       pulsing yellow
//   on Wi-Fi, no app        pulsing green (waiting for the app or bridge to connect)
//   connected               solid green at 25%, pulsing brighter while data moves
//   a key pressed or held   white, for at least a moment even on a quick tap
//   pairing                 blinking magenta, steady once confirmed on the deck
// While asleep, connected is off and everything else is very dim, so the light doesn't glow
// in a dark room. It runs on its own task, so pulses stay smooth whatever the caller is doing;
// everything here may be called from any task.
#pragma once

#include <atomic>
#include <cstdint>

class StatusLight {
public:
	// What the device is doing, which picks the color.
	enum class Mode : uint8_t {
		Off,
		Setup,
		Searching,
		OnWifi,
		Connected,
		Pairing,
		PairingConfirmed,
	};

	// Starts driving the LED on gpio (48 on the ESP32-S3-DevKitC-1 v1.0 and most clones, 38 on
	// v1.1; some clones connect it only once the solder jumper marked "RGB" is bridged), off.
	bool begin( int gpio );

	void setMode( Mode mode ) { mode_ = mode; }
	// Whether a key is down (white while it is), and whether the device is asleep (dim).
	void setKeyDown( bool down ) { keyDown_ = down; }
	void setAsleep( bool asleep ) { asleep_ = asleep; }

	// A key went down: white for at least kKeyFlash, even if it's released at once.
	void keyPressed();
	// Data went to or came from the app: the connected light pulses for kActivityHold.
	void activity();

private:
	static void task( void *arg );
	void        update();
	// Shows a full-scale color, dimmed while asleep.
	void        show( uint8_t red, uint8_t green, uint8_t blue );
	// Sends a color to the LED, unless it's already showing it.
	void        write( uint8_t red, uint8_t green, uint8_t blue );

	void                 *strip_ = nullptr;   // led_strip_handle_t
	uint32_t              written_ = 0xFFFFFFFF;   // last color sent, 0xRRGGBB
	std::atomic<Mode>     mode_ { Mode::Off };
	std::atomic<bool>     keyDown_ { false };
	std::atomic<bool>     asleep_ { false };
	std::atomic<uint32_t> keyUntil_ { 0 };        // ms
	std::atomic<uint32_t> activityUntil_ { 0 };   // ms
};
