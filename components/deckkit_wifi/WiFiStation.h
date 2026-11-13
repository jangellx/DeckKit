// The Wi-Fi station on plain ESP-IDF: joins the saved network and keeps it joined, tries a new
// one (from Improv, say) and saves it only once it works, and scans. The network is kept in
// NVS (namespace "deckkit_wifi") in plain flash: anyone holding the board can read it.
//
// Its callbacks run on ESP-IDF's event task; everything else may be called from any task.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

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

	// On a network, with an address.
	bool isConnected() const { return connected_; }

	// Joins ssid with password, giving up after kTryTimeout. Saved only once it has an address;
	// until then (and if it fails) the saved network stays.
	void    tryNetwork( const char *ssid, const char *password );
	Attempt attempt() const { return attempt_; }

	// Starts a scan; false if the radio won't scan now (it refuses while joining a network).
	bool   startScan();
	bool   scanning() const { return scanning_; }
	// After a scan: the networks found, strongest first, one per name. Returns how many.
	size_t scanResults( Network *out, size_t max );

	esp_netif_t *netif() const { return netif_; }

	static constexpr uint32_t kTryTimeout = 20000;   // ms

private:
	static void onEvent( void *arg, esp_event_base_t base, int32_t id, void *data );
	static void onRetry( void *arg );
	static void onTryTimeout( void *arg );

	void join( const char *ssid, const char *password );
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

	std::atomic<bool>    connected_ { false };
	std::atomic<bool>    scanning_ { false };
	std::atomic<Attempt> attempt_ { Attempt::None };
};
