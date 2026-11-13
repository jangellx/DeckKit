// Optional encryption of NVS (the Wi-Fi password and the other settings), with ESP-IDF's
// HMAC-based NVS encryption, from ESPDeck's SecureNVS: the XTS keys that encrypt
// NVS are derived by the chip's HMAC peripheral from a 256-bit key in an eFuse key block
// (purpose HMAC_UP, read- and write-protected), which no software can read back. It doesn't
// need flash encryption. Burning that key can't be undone, so the project decides when: ESPDeck
// and TetherDeck burn it when a new device (nothing secret stored yet) saves its first Wi-Fi
// network, unless Standard storage was chosen for its setup (encryptForSetup()), and ESPDeck
// also when its bridge asks (encrypt()). A device without the key keeps plain NVS.
//
// Call init() in place of nvs_flash_init(): it opens NVS plain without the key and encrypted
// with it. CONFIG_NVS_ENCRYPTION stays off: with it, nvs_flash_init() burns a key on every
// device that doesn't have one. A plain nvs_flash_init() on encrypted NVS would erase every
// entry (they fail their CRC). The marker for an interrupted move keeps SecureNVS's name, so a
// device moved between ESPDeck and a DeckKit project reads it the same way.
#pragma once

#include <cstdint>

#include "esp_err.h"

namespace SecureStorage {
	// How NVS is kept.
	enum class State : uint8_t {
		Plain,         // not encrypted, and encrypt() can
		Encrypted,     // with the eFuse key (burned, whether or not NVS has moved over yet)
		Unsupported,   // not encrypted, and there's no free eFuse key block to do it with
	};

	// In place of nvs_flash_init(). With the key, it also finishes a move to encryption that a
	// power cut interrupted (see encrypt()). After ESP_ERR_NVS_NO_FREE_PAGES or
	// ESP_ERR_NVS_NEW_VERSION_FOUND, erase NVS (nvs_flash_erase()) and call it again, as for
	// plain NVS.
	esp_err_t init();

	// NVS was set up here.
	bool ready();

	// How NVS is kept now.
	State       state();
	// "plain", "encrypted" or "unsupported", as ESPDeck's protocol and Improv command say it.
	const char *stateName();

	// What encrypt() and encryptForSetup() did.
	enum class Outcome : uint8_t {
		Refused,     // nothing changed; `error` says why
		Encrypted,   // restart now
		Failed,      // the key is burned but the move didn't finish; restart now (init() sorts it out)
	};

	// Burns a new key and moves every NVS entry over to encrypted NVS. The steps, and what a
	// power cut during each one leaves at the next start:
	//   1. Copy every entry to RAM.        Nothing has changed.
	//   2. Burn the key.                   Plain NVS with a key: init() finds the unencrypted
	//                                      entries and finishes the move (encrypted, all kept).
	//   3. Erase NVS (~0.2 s).             As 2, with the entries not yet erased; those already
	//                                      erased are lost (all of them, at worst).
	//   4. Write the entries back          init() finds the move's marker and erases NVS: empty,
	//      encrypted, verify (~0.1 s).     so the device starts in setup mode, unpaired.
	// NVS is unusable afterwards until the restart.
	Outcome encrypt( const char *&error );

	// The same for a new device saving its first Wi-Fi network (WiFiStation does it),
	// before the credentials are stored, so they never touch plain flash. The entries so far
	// (its name, say) move across. There's no restart: once it returns Encrypted, NVS is
	// encrypted and in use, though every handle opened before is invalid (open it again).
	// If the move fails after the burn (Failed), NVS is set up plain again with the entries,
	// so the setup can go on; the next start encrypts them (init()), and restartWanted() asks
	// for that start. Refused leaves NVS plain and as it was.
	Outcome encryptForSetup( const char *&error );

	// The key is burned but NVS is still plain until the next start (encryptForSetup() fell
	// back); restart once nothing is in the middle of something.
	bool restartWanted();
}
