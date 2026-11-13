// Improv Wi-Fi's serial packets (https://www.improv-wifi.com/serial/, version 1): reading them
// out of a byte stream and writing them. No ESP-IDF, so tools/improv_test builds exactly this
// file on the Mac; ImprovSerial.cpp carries the packets over the serial ports.
//
// A packet is "IMPROV", the version (1), a type, the data's length, the data, and a checksum:
// the low byte of the sum of every byte before it.
#pragma once

#include <cstddef>
#include <cstdint>

namespace Improv {
	constexpr char    kHeader[]   = "IMPROV";
	constexpr size_t  kHeaderSize = 6;
	constexpr size_t  kFieldsSize = 3;   // version, type, length
	constexpr uint8_t kVersion    = 1;
	// The most a packet takes on the wire: header, fields, 255 bytes of data, checksum, newline.
	constexpr size_t  kMaxPacket  = kHeaderSize + kFieldsSize + 255 + 2;

	// Packet types.
	constexpr uint8_t kTypeState  = 0x01;
	constexpr uint8_t kTypeError  = 0x02;
	constexpr uint8_t kTypeRPC    = 0x03;
	constexpr uint8_t kTypeResult = 0x04;

	// RPC commands.
	constexpr uint8_t kSendWiFi     = 0x01;
	constexpr uint8_t kRequestState = 0x02;
	constexpr uint8_t kRequestInfo  = 0x03;
	constexpr uint8_t kRequestScan  = 0x04;
	constexpr uint8_t kDeviceName   = 0x06;   // get (no data) or set (the name's bytes)
	// ESPDeck's own, which ESPDeck Bridge's USB Setup asks: the saved network's name (never its
	// password), whether it's on it, and its hostname. The spec reserves nothing for vendors,
	// so these count down from the top of the range.
	constexpr uint8_t kWiFiNetwork  = 0xFE;

	// The current-state values.
	enum class State : uint8_t {
		Ready        = 0x02,   // "authorized"
		Provisioning = 0x03,
		Provisioned  = 0x04,
	};

	// The error-state values.
	enum class Error : uint8_t {
		None            = 0x00,
		InvalidRPC      = 0x01,
		UnknownCommand  = 0x02,
		UnableToConnect = 0x03,
		Unknown         = 0xFF,
	};

	// Reads packets out of a byte stream. Anything that isn't one (a terminal's keystrokes,
	// log lines echoed back) is skipped.
	class Parser {
	public:
		enum class Result : uint8_t {
			Pending,   // more bytes needed
			Packet,    // a complete, valid packet: type() and data()
			Invalid,   // a complete packet with the wrong version or checksum
		};

		Result feed( uint8_t byte );

		uint8_t        type() const   { return buffer_[kHeaderSize + 1]; }
		const uint8_t *data() const   { return buffer_ + kHeaderSize + kFieldsSize; }
		size_t         length() const { return buffer_[kHeaderSize + 2]; }

	private:
		uint8_t buffer_[kHeaderSize + kFieldsSize + 255 + 1] = {};
		size_t  used_ = 0;
	};

	// An RPC packet's data is the command, the length of its own data, then that data. False
	// if the lengths don't agree.
	bool readRPC( const uint8_t *data, size_t length, uint8_t &command, const uint8_t *&args, size_t &argsLength );

	// Send Wi-Fi's data: the network name's length and bytes, then the password's. ssid gets
	// up to 32 bytes and password up to 64, both NUL-terminated. False if it's malformed.
	bool readWiFi( const uint8_t *args, size_t length, char ssid[33], char password[65] );

	// Writes a packet into out (kMaxPacket bytes is always enough), with a newline after it so
	// log lines that follow start on their own line. Returns its length, or 0 if it won't fit.
	size_t writePacket( uint8_t *out, size_t size, uint8_t type, const uint8_t *data, size_t length );

	// Writes an RPC result's data into out: the command, the length of the rest, then each
	// string as its length and bytes (strings that don't fit in 255 bytes are left off).
	// Returns its length.
	size_t writeResult( uint8_t *out, size_t size, uint8_t command, const char *const *strings, size_t count );
}
