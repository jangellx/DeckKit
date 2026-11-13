// Stream Dock-style decks from Mirabox and the brands that sell its boards (Ajazz, Fifine,
// upHere, Mars Gaming, Redragon and others): their models and USB reports. Experimental: none
// of this has been tried on hardware from ESPDeck. It follows DeckBridge
// (https://github.com/lukasMega/DeckBridge), which drives several of these decks on hardware;
// each model says whether DeckBridge has.
//
// Plain C++ without ESP-IDF, so tools/mirabox_test can check it on the Mac; StreamDeck (in
// StreamDeckMirabox.cpp) does the USB.
//
// Every command is one output report of `packet` bytes (512 or 1024, after the report ID if
// the deck uses one): "CRT", two zero bytes, the command's name, then its arguments.
//   DIS                   wake the panel; nothing else works before it
//   LIG 0 0 percent       brightness
//   CLE 0 0 0 id          clear a key (0xFF: every key)
//   BAT 0 0 lenHi lenLo id  a key image of len bytes follows, in packet-sized pieces
//   STP                   show what was sent (Mirabox family)
//   ULEND                 end of one image (AKP05 family)
//   CONNECT               keepalive: without it the deck drops the host
//   VER                   asks for the firmware version (AKP05 family)
// Key presses come back as input reports: "ACK", then the key's code at byte 9 and its state
// at byte 10 (1 down, 0 up; after the report ID if there is one).
//
// Model tables derived from DeckBridge, Copyright (c) 2026 Lukas, MIT License; see
// THIRD-PARTY-NOTICES.md.
#pragma once

#include <cstddef>
#include <cstdint>

namespace Mirabox {
	// Which command set a deck takes.
	enum class Family : uint8_t {
		Mirabox,   // 293V3, 293S, K1 Pro and their rebadges: STP after images
		Akp05,     // Ajazz AKP05 and its rebadges: ULEND after each image, a fuller keepalive
	};

	// How a key image is turned before it's sent: StreamDeck::Transform's values.
	enum class Rotation : uint8_t {
		None,
		Rotate90,    // clockwise
		Rotate270,   // counterclockwise
		Rotate180,
	};

	constexpr uint8_t kMaxKeys = 18;   // the 293S's 3 × 6

	// One deck model.
	struct Model {
		uint16_t    vid;
		uint16_t    pid;
		const char *name;
		Family      family;
		uint8_t     rows;
		uint8_t     cols;
		uint16_t    keySize;         // pixels, square
		Rotation    rotation;        // from an upright image
		uint16_t    packet;          // output report size, without the report ID
		uint8_t     reportID;        // 0 if the deck's reports don't have one
		uint16_t    keepaliveMs;
		uint16_t    imageLimit;      // the largest key image the deck takes, in bytes
		bool        reportsKeyUp;    // false: it only reports presses, so the release is made up
		bool        padsChunks;      // the deck drops the last byte of every full piece (K1 Pro)
		bool        stpAfterClear;
		bool        tested;          // DeckBridge drives this model on hardware
		// Key index (row-major from the top-left, as seen) → the deck's image ID and input code.
		uint8_t     imageIDs[kMaxKeys];
		uint8_t     inputCodes[kMaxKeys];

		uint8_t keyCount() const { return rows * cols; }
	};

	// The model with this vendor and product ID, or nullptr.
	const Model *find( uint16_t vid, uint16_t pid );

	// Fills `out` (packet bytes) with a command: "CRT\0\0", its name, then `args`.
	void command( uint8_t *out, size_t packet, const char *name, const uint8_t *args = nullptr, size_t argCount = 0 );
	void brightness( uint8_t *out, size_t packet, uint8_t percent );
	void clear( uint8_t *out, size_t packet, uint8_t imageID );
	// The header before an image of `length` bytes (as sent: see sentLength()).
	void imageHeader( uint8_t *out, size_t packet, size_t length, uint8_t imageID );

	// An image's length on the wire: with a padding byte after every packet - 1 bytes when
	// the deck drops the last byte of each full piece, otherwise as it is.
	size_t sentLength( size_t length, size_t packet, bool pad );
	// How many packets an image takes, and the contents of each (zero-filled past the end).
	size_t chunkCount( size_t length, size_t packet, bool pad );
	void   chunk( const uint8_t *image, size_t length, size_t packet, bool pad, size_t index, uint8_t *out );

	// A key report's code and state, if `data` (an input report, with the report ID first if
	// the deck uses one) is one.
	bool parseKey( const uint8_t *data, size_t length, uint8_t reportID, uint8_t &code, uint8_t &state );
	// The key index for an input code, or -1 for a code that isn't a key (a dial, the touch strip).
	int keyForInput( const Model &model, uint8_t code );

	// Whether an interface's report descriptor is the deck's own (vendor usage page 0xFFA0), not
	// the keyboard interface some of these decks also have.
	bool isVendorInterface( const uint8_t *descriptor, size_t length );
	// The largest output report the descriptor declares, in bytes without the report ID; 0 if none.
	size_t largestOutputReport( const uint8_t *descriptor, size_t length );
}
