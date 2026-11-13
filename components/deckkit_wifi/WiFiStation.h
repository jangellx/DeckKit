// The Wi-Fi station on plain ESP-IDF: joins the saved network and keeps it joined, tries a new
// one (from Improv, say) and saves it only once it works, and scans. The network is kept in
// NVS (namespace "deckkit_wifi"). As in ESPDeck, a new device's first network is saved into
// encrypted NVS (deckkit_storage's SecureStorage burns its eFuse key first), unless Standard
// storage was chosen for its setup; NVS must be set up with SecureStorage::init().
//
// Its callbacks run on ESP-IDF's event task; everything else may be called from any task.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "esp_netif.h"

class WiFiStation {
public:
	// A network seen by scan().
	struct Network {
		char    ssid[33];
		int8_t  rssi;
		bool    secure;
	};

	// How tryNetwork() is going.
	enum class Attempt : uint8_t {
		None,
		Joining,
		Joined,   // saved
		Failed,   // back on the saved network, if any
	};

	// Starts Wi-Fi as a station named hostname (DHCP and mDNS), and joins the saved network.
	// NVS must be set up first. Creates ESP-IDF's default event loop if there isn't one.
	bool begin( const char *hostname );

	// The saved network's name ("" if none).
	const char *savedSSID() const { return ssid_; }
	bool        hasSaved() const  { return ssid_[0] != '\0'; }

	// Standard storage (plain NVS) was chosen for setting up this device, so its first network
	// doesn't burn a key and encrypt. Kept, in NVS, until it's erased.
	bool standardStorage() const { return standard_; }
	void setStandardStorage( bool standard );
	// Whether there's still a choice to make: a new device (no network saved) with plain storage
	// and a free eFuse key block.
	bool storageChoiceOpen() const;
	// Whether saving the first network will encrypt: the choice is open and Standard wasn't chosen.
	bool encryptsAtSetup() const { return storageChoiceOpen() && !standard_; }

	// On a network, with an address.
	bool isConnected() const { return connected_; }

	// Why the station last lost or failed to join a network (a wifi_err_reason_t), 0 once it
	// has an address.
	uint8_t lastReason() const { return lastReason_; }

	// Joins ssid with password, giving up after kTryTimeout. Saved only once it has an address;
	// until then (and if it fails) the saved network stays.
	void    tryNetwork( const char *ssid, const char *password );
	Attempt attempt() const { return attempt_; }

	// Starts a scan; false if the radio won't scan now (it refuses while joining a network).
	bool   startScan();
	bool   scanning() const { return scanning_; }
	// The networks the last scan found, strongest first, one per name; they stay until the next
	// scan finishes. Returns how many.
	size_t scanResults( Network *out, size_t max );

	esp_netif_t *netif() const { return netif_; }

	static constexpr uint32_t kTryTimeout = 20000;   // ms

private:
	static void onEvent( void *arg, esp_event_base_t base, int32_t id, void *data );
	// Saves a network that tryNetwork() joined, encrypting NVS first on a new device; on its own
	// task, since that takes longer and more stack than the event task has.
	static void saveTask( void *arg );
	static void onRetry( void *arg );
	static void onTryTimeout( void *arg );

	void join( const char *ssid, const char *password );
	// Reads a finished scan's networks into found_ (ESP-IDF hands them out only once).
	void collectScan();
	void loadSaved();
	void save( const char *ssid, const char *password );

	esp_netif_t *netif_      = nullptr;
	void        *retry_      = nullptr;   // esp_timer_handle_t
	void        *tryTimer_   = nullptr;   // esp_timer_handle_t
	char         ssid_[33]   = {};
	char         password_[65] = {};
	// The network tryNetwork() is trying.
	char         trySSID_[33] = {};
	char         tryPassword_[65] = {};
	bool         standard_   = false;
	void        *saver_      = nullptr;   // TaskHandle_t of saveTask()

	static constexpr size_t kMaxFound = 32;
	Network                 found_[kMaxFound] = {};
	size_t                  foundCount_ = 0;
	std::mutex              foundLock_;   // found_ and foundCount_

	std::atomic<bool>    connected_ { false };
	std::atomic<uint8_t> lastReason_ { 0 };
	std::atomic<bool>    scanning_ { false };
	std::atomic<Attempt> attempt_ { Attempt::None };
};
