#include "SetupPage.h"

#include <cstdio>
#include <cstring>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include "SetupPageHTML.h"
#include "SecureStorage.h"
#include "SetupProtocol.h"
#include "Text.h"
#include "Timing.h"
#include "WiFiStation.h"

static const char *TAG = "Setup";

namespace {
	constexpr uint8_t     kAddress[4]    = { 192, 168, 4, 1 };
	constexpr const char *kHost          = "192.168.4.1";      // the Host the page's requests name,
	constexpr const char *kHostPort      = "192.168.4.1:80";   // with or without the port
	constexpr const char *kOrigin        = "http://192.168.4.1";
	constexpr uint16_t    kDNSPort       = 53;
	constexpr uint8_t     kAPChannel     = 1;
	constexpr int         kAPMaxStations = 4;
	constexpr size_t      kMaxBody       = 512;    // bytes of a POST
	constexpr size_t      kMaxNetworks   = 24;     // networks the page lists
	constexpr size_t      kSafeText      = 40;     // a network name made printable, for the log
	constexpr uint32_t    kTaskStack     = 4096;
	constexpr UBaseType_t kTaskPriority  = 3;
	constexpr int         kTickInterval  = 1000;   // ms between looks at the attempt and the phones

	// No 0/O, 1/l/i, in case the password is read off a phone and typed. 12 characters (59
	// bits) still fit the smallest keys' QR code (version 3).
	constexpr const char *kPasswordAlphabet = "abcdefghjkmnpqrstuvwxyz23456789";
	constexpr size_t      kPasswordLength   = 12;

	// An IPv4 address from a socket address (IPv4, or IPv4-mapped IPv6); false otherwise.
	bool ipv4( const sockaddr_storage &address, uint8_t out[4] ) {
		if( address.ss_family == AF_INET ) {
			memcpy( out, &reinterpret_cast<const sockaddr_in &>( address ).sin_addr.s_addr, 4 );
			return true;
		}
#if CONFIG_LWIP_IPV6
		if( address.ss_family == AF_INET6 ) {
			const auto &v6 = reinterpret_cast<const sockaddr_in6 &>( address );
			const uint8_t *bytes = reinterpret_cast<const uint8_t *>( &v6.sin6_addr );
			static const uint8_t kMapped[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF };
			if( memcmp( bytes, kMapped, sizeof( kMapped ) ) == 0 ) {
				memcpy( out, bytes + 12, 4 );
				return true;
			}
		}
#endif
		return false;
	}

	// Whether the request came in on the access point's address from a phone on it.
	bool fromAccessPoint( httpd_req_t *request ) {
		int              fd = httpd_req_to_sockfd( request );
		sockaddr_storage local = {}, peer = {};
		socklen_t        size  = sizeof( local );
		uint8_t          localIP[4], peerIP[4];
		if( getsockname( fd, (sockaddr *)&local, &size ) != 0 || !ipv4( local, localIP ) )
			return false;
		size = sizeof( peer );
		if( getpeername( fd, (sockaddr *)&peer, &size ) != 0 || !ipv4( peer, peerIP ) )
			return false;
		return memcmp( localIP, kAddress, 4 ) == 0 && memcmp( peerIP, kAddress, 3 ) == 0;
	}

	esp_err_t refuse( httpd_req_t *request ) {
		httpd_resp_set_status( request, "403 Forbidden" );
		return httpd_resp_send( request, "", 0 );
	}

	// Sends the phone to the page.
	esp_err_t redirect( httpd_req_t *request ) {
		httpd_resp_set_status( request, "302 Found" );
		httpd_resp_set_hdr( request, "Location", SetupPage::kPageURL );
		httpd_resp_set_hdr( request, "Cache-Control", "no-store" );
		return httpd_resp_send( request, "", 0 );
	}

	esp_err_t sendJSON( httpd_req_t *request, const char *status, const char *json ) {
		httpd_resp_set_status( request, status );
		httpd_resp_set_type( request, "application/json" );
		httpd_resp_set_hdr( request, "Cache-Control", "no-store" );
		return httpd_resp_sendstr( request, json );
	}

	// The same for a cJSON object, which it deletes.
	esp_err_t sendJSON( httpd_req_t *request, cJSON *json ) {
		char     *text = cJSON_PrintUnformatted( json );
		esp_err_t err  = sendJSON( request, "200 OK", text ? text : "{}" );
		cJSON_free( text );
		cJSON_Delete( json );
		return err;
	}

	// Backslash-escapes the characters the WIFI: QR format reserves, into out.
	void appendEscaped( char *out, size_t size, const char *text ) {
		size_t used = strlen( out );
		for( ; *text && used + 2 < size; text++ ) {
			if( strchr( "\\;,\":", *text ) )
				out[used++] = '\\';
			out[used++] = *text;
		}
		out[used] = '\0';
	}
}

bool SetupPage::begin( WiFiStation &wifi, const Options &options ) {
	wifi_    = &wifi;
	options_ = options;
	snprintf( apSSID_, sizeof( apSSID_ ), "%s%s", options.prefix, options.suffix );
	apNetif_ = esp_netif_create_default_wifi_ap();
	return apNetif_ != nullptr;
}

bool SetupPage::canExit() const {
	return wifi_ && wifi_->hasSaved();
}

// MARK: - Start and stop

bool SetupPage::start() {
	if( active_ )
		return true;

	// AP+STA keeps the station connected (or trying) alongside the access point.
	if( esp_wifi_set_mode( WIFI_MODE_APSTA ) != ESP_OK )
		return false;
	makePassword();   // with the radio on, esp_random() is truly random

	wifi_config_t config = {};
	strlcpy( (char *)config.ap.ssid, apSSID_, sizeof( config.ap.ssid ) );
	config.ap.ssid_len       = (uint8_t)strlen( apSSID_ );
	strlcpy( (char *)config.ap.password, apPassword_, sizeof( config.ap.password ) );
	config.ap.channel        = kAPChannel;
	config.ap.authmode       = WIFI_AUTH_WPA2_WPA3_PSK;
	config.ap.max_connection = kAPMaxStations;
	config.ap.pmf_cfg.capable = true;
	if( esp_wifi_set_config( WIFI_IF_AP, &config ) != ESP_OK ) {
		ESP_LOGE( TAG, "Starting the access point failed" );
		esp_wifi_set_mode( WIFI_MODE_STA );
		return false;
	}

	// DHCP hands out this address as the DNS server, and (option 114, RFC 8910) points newer
	// phones straight at the page.
	esp_netif_dhcps_stop( apNetif_ );
	uint8_t offerDNS = 0x02;   // OFFER_DNS
	esp_netif_dhcps_option( apNetif_, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offerDNS, sizeof( offerDNS ) );
	esp_netif_dns_info_t dns = {};
	dns.ip.type             = ESP_IPADDR_TYPE_V4;
	dns.ip.u_addr.ip4.addr  = ESP_IP4TOADDR( kAddress[0], kAddress[1], kAddress[2], kAddress[3] );
	esp_netif_set_dns_info( apNetif_, ESP_NETIF_DNS_MAIN, &dns );
	esp_netif_dhcps_option( apNetif_, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, (void *)kPageURL, strlen( kPageURL ) );
	esp_netif_dhcps_start( apNetif_ );

	snprintf( joinCode_, sizeof( joinCode_ ), "WIFI:T:WPA;S:" );
	appendEscaped( joinCode_, sizeof( joinCode_ ), apSSID_ );
	strlcat( joinCode_, ";P:", sizeof( joinCode_ ) );
	appendEscaped( joinCode_, sizeof( joinCode_ ), apPassword_ );
	strlcat( joinCode_, ";;", sizeof( joinCode_ ) );

	// The web server: the page's routes, then everything else to the page.
	httpd_config_t http = HTTPD_DEFAULT_CONFIG();
	http.max_uri_handlers = 8;
	// Phones open several connections at once; the oldest is closed for a new one (LRU purge)
	// rather than the device running out of sockets.
	http.max_open_sockets = 5;
	http.uri_match_fn     = httpd_uri_match_wildcard;
	http.lru_purge_enable = true;
	http.stack_size       = 6144;
	httpd_handle_t server = nullptr;
	if( httpd_start( &server, &http ) != ESP_OK ) {
		ESP_LOGE( TAG, "Starting the web server failed" );
	} else {
		server_ = server;
		const httpd_uri_t routes[] = {
			{ "/", HTTP_GET, handleRoot, this },
			{ "/scan", HTTP_GET, handleScan, this },
			{ "/status", HTTP_GET, handleStatus, this },
			{ "/save", HTTP_POST, handleSave, this },
			{ "/exit", HTTP_POST, handleExit, this },
			{ "/*", HTTP_GET, handleOther, this },
			{ "/*", HTTP_POST, handleOther, this },
		};
		for( const httpd_uri_t &route : routes )
			httpd_register_uri_handler( server, &route );
	}

	// The DNS server, on the access point's address only.
	dnsSocket_ = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	sockaddr_in address = {};
	address.sin_family = AF_INET;
	address.sin_port   = htons( kDNSPort );
	memcpy( &address.sin_addr.s_addr, kAddress, 4 );
	if( dnsSocket_ < 0 || bind( dnsSocket_, (sockaddr *)&address, sizeof( address ) ) != 0 )
		ESP_LOGE( TAG, "Starting the DNS server failed" );

	active_        = true;
	exitRequested_ = false;
	idleTimedOut_  = false;
	trying_        = false;
	joined_        = false;
	lastActivity_  = Timing::nowMillis();
	{
		std::lock_guard<std::mutex> lock( textLock_ );
		saveError_[0] = '\0';
	}
	wifi_->startScan();
	xTaskCreate( dnsTask, "setup", kTaskStack, this, kTaskPriority, nullptr );
	ESP_LOGI( TAG, "Setup mode: join %s and open %s", apSSID_, kPageURL );
	return true;
}

void SetupPage::stop() {
	if( !active_.exchange( false ) )
		return;
	if( server_ )
		httpd_stop( static_cast<httpd_handle_t>( server_ ) );
	server_ = nullptr;
	if( dnsSocket_ >= 0 )
		shutdown( dnsSocket_, SHUT_RDWR );   // the DNS task closes it as it ends
	esp_wifi_set_mode( WIFI_MODE_STA );
	memset( apPassword_, 0, sizeof( apPassword_ ) );
	memset( joinCode_, 0, sizeof( joinCode_ ) );
	ESP_LOGI( TAG, "Left setup mode" );
}

// Rejection sampling keeps every character equally likely.
void SetupPage::makePassword() {
	size_t alphabet = strlen( kPasswordAlphabet );
	size_t limit    = 256 - 256 % alphabet;
	for( size_t i = 0; i < kPasswordLength; ) {
		uint8_t random;
		esp_fill_random( &random, 1 );
		if( random < limit )
			apPassword_[i++] = kPasswordAlphabet[random % alphabet];
	}
	apPassword_[kPasswordLength] = '\0';
}

// MARK: - DNS, and keeping track

void SetupPage::dnsTask( void *arg ) {
	static_cast<SetupPage *>( arg )->serveDNS();
	vTaskDelete( nullptr );
}

void SetupPage::serveDNS() {
	int fd = dnsSocket_;
	while( active_ ) {
		if( fd >= 0 ) {
			fd_set readable;
			FD_ZERO( &readable );
			FD_SET( fd, &readable );
			timeval timeout = { kTickInterval / 1000, ( kTickInterval % 1000 ) * 1000 };
			if( select( fd + 1, &readable, nullptr, nullptr, &timeout ) > 0 && FD_ISSET( fd, &readable ) ) {
				uint8_t          query[512], answer[600];
				sockaddr_storage from = {};
				socklen_t        size = sizeof( from );
				int length = recvfrom( fd, query, sizeof( query ), 0, (sockaddr *)&from, &size );
				size_t reply = length > 0 ? SetupProtocol::dnsAnswer( query, (size_t)length, kAddress, answer, sizeof( answer ) ) : 0;
				if( reply )
					sendto( fd, answer, reply, 0, (sockaddr *)&from, size );
			}
		} else {
			vTaskDelay( pdMS_TO_TICKS( kTickInterval ) );
		}
		tick();
	}
	if( fd >= 0 )
		close( fd );
	if( dnsSocket_ == fd )
		dnsSocket_ = -1;
}

// Follows a network chosen on the page, asks to leave kExitDelay after it's joined, and
// notices kIdleTimeout without a phone on the access point.
void SetupPage::tick() {
	uint32_t now = Timing::nowMillis();
	if( trying_ ) {
		WiFiStation::Attempt attempt = wifi_->attempt();
		if( attempt == WiFiStation::Attempt::Joined ) {
			trying_   = false;
			joined_   = true;
			joinedAt_ = now;
		} else if( attempt == WiFiStation::Attempt::Failed ) {
			trying_ = false;
			std::lock_guard<std::mutex> lock( textLock_ );
			snprintf( saveError_, sizeof( saveError_ ), "Couldn't join that network: %s The previous network is kept.", errorText() );
		}
	}
	if( joined_ && Timing::reached( joinedAt_ + kExitDelay, now ) ) {
		joined_        = false;
		exitRequested_ = true;
	}

	wifi_sta_list_t stations = {};
	bool            phone    = esp_wifi_ap_get_sta_list( &stations ) == ESP_OK && stations.num > 0;
	if( phone || trying_ || joined_ ) {
		lastActivity_ = now;
	} else if( Timing::reached( lastActivity_ + kIdleTimeout, now ) && canExit() ) {
		ESP_LOGI( TAG, "No phone on the access point for %u minutes", (unsigned)( kIdleTimeout / 60000 ) );
		lastActivity_ = now;
		idleTimedOut_ = true;
	}
}

// Why the station isn't connected, for the page ("" if it is, or there's nothing to say).
const char *SetupPage::errorText() const {
	switch( wifi_->lastReason() ) {
		case 0:
		case WIFI_REASON_ASSOC_LEAVE:   // our own disconnect before joining another network
			return trying_ || wifi_->isConnected() ? "" : "Check the network name and password.";
		case WIFI_REASON_AUTH_FAIL:
		case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
		case WIFI_REASON_HANDSHAKE_TIMEOUT:
		case WIFI_REASON_MIC_FAILURE:
			return "Wrong password.";
		case WIFI_REASON_NO_AP_FOUND:
		case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
		case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
		case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
			return "Network not found. Only 2.4 GHz networks can be used.";
		default:
			return "Couldn't connect to the network.";
	}
}

// MARK: - HTTP

// Requests from the home network, from pages that reached 192.168.4.1 by another name, and
// POSTs from other origins are refused or sent to the page.
bool SetupPage::allowRequest( httpd_req *request, bool post ) {
	if( !fromAccessPoint( request ) ) {
		refuse( request );
		return false;
	}
	lastActivity_ = Timing::nowMillis();

	char host[48] = {};
	if( httpd_req_get_hdr_value_str( request, "Host", host, sizeof( host ) ) != ESP_OK
		|| ( strcmp( host, kHost ) != 0 && strcmp( host, kHostPort ) != 0 ) ) {
		if( post )
			refuse( request );
		else
			redirect( request );
		return false;
	}
	char origin[48] = {};
	if( post && httpd_req_get_hdr_value_len( request, "Origin" ) > 0
		&& ( httpd_req_get_hdr_value_str( request, "Origin", origin, sizeof( origin ) ) != ESP_OK || strcmp( origin, kOrigin ) != 0 ) ) {
		refuse( request );
		return false;
	}
	return true;
}

esp_err_t SetupPage::handleRoot( httpd_req *request ) {
	auto *self = static_cast<SetupPage *>( request->user_ctx );
	if( !self->allowRequest( request, false ) )
		return ESP_OK;
	httpd_resp_set_type( request, "text/html; charset=utf-8" );
	httpd_resp_set_hdr( request, "Cache-Control", "no-store" );
	return httpd_resp_send( request, SetupPageHTML::kPage, sizeof( SetupPageHTML::kPage ) - 1 );
}

// The networks found, strongest first; `refresh` starts a new scan.
esp_err_t SetupPage::handleScan( httpd_req *request ) {
	auto *self = static_cast<SetupPage *>( request->user_ctx );
	if( !self->allowRequest( request, false ) )
		return ESP_OK;
	char query[32] = {};
	if( httpd_req_get_url_query_str( request, query, sizeof( query ) ) == ESP_OK && strstr( query, "refresh" ) )
		self->wifi_->startScan();

	static WiFiStation::Network networks[kMaxNetworks];   // only the web server's task uses it
	size_t count = self->wifi_->scanResults( networks, kMaxNetworks );
	cJSON *json  = cJSON_CreateObject();
	cJSON_AddBoolToObject( json, "scanning", self->wifi_->scanning() );
	cJSON *list = cJSON_AddArrayToObject( json, "networks" );
	for( size_t i = 0; i < count; i++ ) {
		cJSON *item = cJSON_CreateObject();
		cJSON_AddStringToObject( item, "ssid", networks[i].ssid );
		cJSON_AddNumberToObject( item, "rssi", networks[i].rssi );
		cJSON_AddBoolToObject( item, "secure", networks[i].secure );
		cJSON_AddItemToArray( list, item );
	}
	return sendJSON( request, json );
}

// Everything the page shows, polled.
esp_err_t SetupPage::handleStatus( httpd_req *request ) {
	auto *self = static_cast<SetupPage *>( request->user_ctx );
	if( !self->allowRequest( request, false ) )
		return ESP_OK;
	bool trying    = self->trying_;
	bool connected = self->wifi_->isConnected() && !trying;
	char ip[16]    = {};
	esp_netif_ip_info_t info = {};
	if( connected && esp_netif_get_ip_info( self->wifi_->netif(), &info ) == ESP_OK )
		snprintf( ip, sizeof( ip ), IPSTR, IP2STR( &info.ip ) );

	cJSON *json = cJSON_CreateObject();
	{
		std::lock_guard<std::mutex> lock( self->textLock_ );
		cJSON_AddStringToObject( json, "ssid", trying ? self->trySSID_ : self->wifi_->savedSSID() );
		cJSON_AddStringToObject( json, "saveError", self->saveError_ );
	}
	cJSON_AddStringToObject( json, "product", self->options_.product );
	cJSON_AddStringToObject( json, "name", self->options_.name );
	cJSON_AddStringToObject( json, "firmware", self->options_.firmware );
	cJSON_AddBoolToObject( json, "configured", self->wifi_->hasSaved() );
	cJSON_AddBoolToObject( json, "connecting", trying );
	cJSON_AddBoolToObject( json, "connected", connected );
	cJSON_AddStringToObject( json, "ip", ip );
	cJSON_AddStringToObject( json, "error", connected ? "" : self->errorText() );
	cJSON_AddBoolToObject( json, "canExit", self->canExit() );
	// A new device with plain storage: saving its first network encrypts storage, unless the
	// page's checkbox says Standard.
	cJSON_AddBoolToObject( json, "encryptOffered", self->wifi_->storageChoiceOpen() );
	cJSON_AddBoolToObject( json, "standardStorage", self->wifi_->standardStorage() );
	cJSON_AddStringToObject( json, "storage", SecureStorage::stateName() );
	cJSON_AddBoolToObject( json, "leaving", self->joined_ || self->exitRequested_ );
	return sendJSON( request, json );
}

// Save & Connect: the network is tried, and saved once it has been joined.
esp_err_t SetupPage::handleSave( httpd_req *request ) {
	auto *self = static_cast<SetupPage *>( request->user_ctx );
	if( !self->allowRequest( request, true ) )
		return ESP_OK;

	char   body[kMaxBody + 1] = {};
	size_t length = 0;
	if( request->content_len > kMaxBody )
		return sendJSON( request, "400 Bad Request", "{\"ok\":false,\"error\":\"Too long.\"}" );
	while( length < request->content_len ) {
		int got = httpd_req_recv( request, body + length, request->content_len - length );
		if( got <= 0 )
			return ESP_FAIL;
		length += (size_t)got;
	}

	char ssid[33] = {}, password[65] = {};
	const char *error = nullptr;
	if( !SetupProtocol::formValue( body, length, "ssid", ssid, sizeof( ssid ) ) || !ssid[0] )
		error = "Choose a network (network names have at most 32 characters).";
	else if( SetupProtocol::formValue( body, length, "password", password, sizeof( password ) )
			 && password[0] && ( strlen( password ) < 8 || strlen( password ) > 64 ) )
		error = "Wi-Fi passwords have 8 to 64 characters.";
	char encrypt[4]    = {};
	bool encryptChosen = SetupProtocol::formValue( body, length, "encrypt", encrypt, sizeof( encrypt ) );
	memset( body, 0, sizeof( body ) );
	if( error ) {
		memset( password, 0, sizeof( password ) );
		cJSON *json = cJSON_CreateObject();
		cJSON_AddBoolToObject( json, "ok", false );
		cJSON_AddStringToObject( json, "error", error );
		char *text = cJSON_PrintUnformatted( json );
		esp_err_t err = sendJSON( request, "400 Bad Request", text ? text : "{}" );
		cJSON_free( text );
		cJSON_Delete( json );
		return err;
	}

	// The checkbox, on a new device: applies when the network is saved, once it works.
	if( encryptChosen && self->wifi_->storageChoiceOpen() )
		self->wifi_->setStandardStorage( strcmp( encrypt, "1" ) != 0 );
	{
		std::lock_guard<std::mutex> lock( self->textLock_ );
		strlcpy( self->trySSID_, ssid, sizeof( self->trySSID_ ) );
		self->saveError_[0] = '\0';
	}
	self->joined_ = false;
	self->trying_ = true;
	self->wifi_->tryNetwork( ssid, password );
	memset( password, 0, sizeof( password ) );
	char safe[kSafeText];
	ESP_LOGI( TAG, "Trying %s from the setup page", Text::printable( ssid, safe, sizeof( safe ) ) );
	return sendJSON( request, "200 OK", "{\"ok\":true}" );
}

// Exit Setup, allowed once there's a network that works.
esp_err_t SetupPage::handleExit( httpd_req *request ) {
	auto *self = static_cast<SetupPage *>( request->user_ctx );
	if( !self->allowRequest( request, true ) )
		return ESP_OK;
	if( !self->canExit() )
		return sendJSON( request, "409 Conflict", "{\"ok\":false,\"error\":\"Set up Wi-Fi first.\"}" );
	self->exitRequested_ = true;
	return sendJSON( request, "200 OK", "{\"ok\":true}" );
}

// Captive-network probes (/hotspot-detect.html, /generate_204, …) get a redirect instead of
// the answer they expect, which makes phones open the page.
esp_err_t SetupPage::handleOther( httpd_req *request ) {
	auto *self = static_cast<SetupPage *>( request->user_ctx );
	if( !fromAccessPoint( request ) )
		return refuse( request );
	self->lastActivity_ = Timing::nowMillis();
	return redirect( request );
}
