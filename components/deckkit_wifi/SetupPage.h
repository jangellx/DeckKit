// Wi-Fi setup from a phone, on plain ESP-IDF: setup mode runs a WPA2/WPA3 access point
// ("<prefix><suffix>", at 192.168.4.1) with a new random password each time, a DNS server
// that answers every name with that address, and a captive-portal page (SetupPageHTML.h) for
// choosing the Wi-Fi network, which WiFiStation saves once it has joined it. The station stays
// up the whole time, so a working connection survives. The device shows the access point's
// name and password, as QR codes say (see joinCode()), since nothing else knows them.
//
// Only phones on the access point get answers: requests from the home network are refused,
// the page's own requests must name 192.168.4.1 as their Host (so a page elsewhere can't reach
// it through DNS tricks), and POSTs from another origin are refused. Setup mode ends by itself
// after kIdleTimeout without a phone on the access point, once there's a saved network.
//
// The web server and DNS run on their own tasks; the rest may be called from any task.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include <mutex>

#include "esp_err.h"
#include "esp_netif.h"

class WiFiStation;
struct httpd_req;

class SetupPage {
public:
	struct Options {
		const char *product  = "";   // "TetherDeck", for the page's title
		const char *prefix   = "";   // the access point's name before the suffix: "TetherDeck-"
		const char *suffix   = "";   // "9570"
		const char *name     = "";   // the device's name, under the title
		const char *firmware = "";   // its version
	};

	// Creates the access point's interface; nothing runs until start(). Call after
	// WiFiStation::begin().
	bool begin( WiFiStation &wifi, const Options &options );

	// Starts or stops setup mode: the access point, DNS and the page.
	bool start();
	void stop();
	bool active() const { return active_; }

	// The access point's name, and its password while setup mode runs.
	const char *apSSID() const     { return apSSID_; }
	const char *apPassword() const { return apPassword_; }

	// The text of a QR code that joins the access point (WIFI:T:WPA;S:…;P:…;;), and of one
	// that opens the page. Valid while setup mode runs.
	const char *joinCode() const { return joinCode_; }
	static constexpr const char *kPageURL = "http://192.168.4.1/";

	// Leaving setup mode only makes sense with a network that works.
	bool canExit() const;

	// True once each: the page's Exit was pressed, or a network saved from it was joined
	// (kExitDelay later, so the page can show the new address); setup mode has been idle for
	// kIdleTimeout.
	bool takeExitRequest() { return exitRequested_.exchange( false ); }
	bool takeIdleTimeout() { return idleTimedOut_.exchange( false ); }

	static constexpr uint32_t kExitDelay  = 8000;     // ms
	static constexpr uint32_t kIdleTimeout = 900000;  // ms

private:
	static void dnsTask( void *arg );
	void        serveDNS();
	void        tick();
	void        makePassword();

	// Whether to answer a request (some are refused, others sent to the page); if not, it has
	// been answered already.
	bool allowRequest( httpd_req *request, bool post );

	// HTTP handlers.
	static esp_err_t handleRoot( httpd_req *request );
	static esp_err_t handleScan( httpd_req *request );
	static esp_err_t handleStatus( httpd_req *request );
	static esp_err_t handleSave( httpd_req *request );
	static esp_err_t handleExit( httpd_req *request );
	static esp_err_t handleOther( httpd_req *request );

	// Why the network didn't work, for the page.
	const char *errorText() const;

	WiFiStation *wifi_     = nullptr;
	Options      options_;
	esp_netif_t *apNetif_  = nullptr;
	void        *server_   = nullptr;   // httpd_handle_t
	int          dnsSocket_ = -1;
	char         apSSID_[33]     = {};
	char         apPassword_[16] = {};
	char         joinCode_[96]   = {};

	std::atomic<bool> active_ { false };
	std::atomic<bool> exitRequested_ { false };
	std::atomic<bool> idleTimedOut_ { false };
	std::atomic<uint32_t> lastActivity_ { 0 };   // ms: the last request, or a phone on the access point

	// A network chosen on the page, being tried.
	std::atomic<bool>     trying_ { false };
	std::atomic<bool>     joined_ { false };     // joined; leaving setup mode at joinedAt_ + kExitDelay
	std::atomic<uint32_t> joinedAt_ { 0 };
	char                  trySSID_[33]    = {};
	char                  saveError_[96]  = {};
	mutable std::mutex    textLock_;   // trySSID_ and saveError_
};
