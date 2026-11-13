// Improv Wi-Fi over serial on plain ESP-IDF (the packets are ImprovProtocol.h's): lets ESP Web
// Tools in a browser, ESPDeck Bridge's USB Setup, or anything else that speaks Improv, over
// USB, ask for the device's details, list the networks it sees, and give it a network to join,
// which WiFiStation saves once it works.
//
// It listens on UART0 (the console, a dev kit's COM port) always, and on the native USB port's
// USB-Serial-JTAG if asked (only when a computer is there: a Stream Deck there needs the USB
// host). Log output keeps flowing on both; a lock around the log writer keeps a log line from
// landing inside an Improv packet. It runs on its own task.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "ImprovProtocol.h"

class WiFiStation;

class ImprovSerial {
public:
	// What Request Info and ESPDeck's Wi-Fi Network command answer. The strings must last.
	struct Info {
		const char *firmware = "";   // "TetherDeck"
		const char *version  = "";
		const char *chip     = "";   // "ESP32-S3"
		const char *name     = "";   // the device's name
		const char *hostname = "";   // its name on the network
	};

	// Installs the serial drivers and starts listening. usbSerial: also on USB-Serial-JTAG.
	bool begin( WiFiStation &wifi, const Info &info, bool usbSerial = false );

	// True once, after a network given over Improv has been joined and saved.
	bool takeProvisioned() { return provisioned_.exchange( false ); }

private:
	static void task( void *arg );
	void        run();
	void        receive( Improv::Parser &parser, const uint8_t *bytes, int count );
	void        handleCommand( uint8_t command, const uint8_t *args, size_t length );
	void        trackAttempt();
	void        trackScan();

	Improv::State currentState() const;
	void          sendState( Improv::State state );
	void          sendError( Improv::Error error );
	void          sendResult( uint8_t command, std::initializer_list<const char *> strings );
	void          sendPacket( uint8_t type, const uint8_t *data, size_t length );

	WiFiStation      *wifi_ = nullptr;
	Info              info_;
	bool              usbSerial_ = false;
	Improv::Parser    uartParser_;
	Improv::Parser    usbParser_;
	bool              joining_      = false;   // a network from Improv is being tried
	bool              scanWanted_   = false;   // a scan was asked for and hasn't been answered
	bool              scanStarted_  = false;   // and it's running
	uint32_t          scanDeadline_ = 0;       // ms by which the scan must have started
	uint32_t          scanTried_    = 0;       // ms of the last attempt to start it
	std::atomic<bool> provisioned_ { false };
};
