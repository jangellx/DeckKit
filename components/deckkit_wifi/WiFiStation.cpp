#include "WiFiStation.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"

#include "Text.h"

static const char *TAG = "WiFi";

namespace {
	constexpr const char *kNamespace     = "deckkit_wifi";
	constexpr uint32_t    kRetryDelay    = 2000;   // ms after losing the saved network before joining again
	constexpr uint32_t    kTryRetryDelay = 1000;   // ms between attempts on a network being tried
	constexpr size_t      kMaxScanned    = 32;     // networks read from a scan
	constexpr size_t      kSafeText      = 40;     // a network name made printable, for the log

	esp_timer_handle_t timer( void *handle ) {
		return static_cast<esp_timer_handle_t>( handle );
	}

	void restart( void *handle, uint32_t milliseconds ) {
		esp_timer_stop( timer( handle ) );
		esp_timer_start_once( timer( handle ), (uint64_t)milliseconds * 1000 );
	}
}

bool WiFiStation::begin( const char *hostname ) {
	esp_err_t err = esp_netif_init();
	if( err != ESP_OK && err != ESP_ERR_INVALID_STATE )
		return false;
	err = esp_event_loop_create_default();
	if( err != ESP_OK && err != ESP_ERR_INVALID_STATE )
		return false;

	netif_ = esp_netif_create_default_wifi_sta();
	if( hostname )
		esp_netif_set_hostname( netif_, hostname );

	esp_timer_create_args_t args = {};
	args.arg      = this;
	args.callback = onRetry;
	args.name     = "wifi_retry";
	esp_timer_handle_t handle = nullptr;
	if( esp_timer_create( &args, &handle ) != ESP_OK )
		return false;
	retry_        = handle;
	args.callback = onTryTimeout;
	args.name     = "wifi_try";
	if( esp_timer_create( &args, &handle ) != ESP_OK )
		return false;
	tryTimer_ = handle;

	wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
	if( esp_wifi_init( &init ) != ESP_OK )
		return false;
	// The saved network lives in our own namespace, saved only once it works; not in Wi-Fi's.
	esp_wifi_set_storage( WIFI_STORAGE_RAM );
	esp_event_handler_register( WIFI_EVENT, ESP_EVENT_ANY_ID, onEvent, this );
	esp_event_handler_register( IP_EVENT, IP_EVENT_STA_GOT_IP, onEvent, this );

	loadSaved();
	esp_wifi_set_mode( WIFI_MODE_STA );
	// Power saving delays packets by up to a beacon interval; keys and acknowledgments are
	// small and want to go now.
	esp_wifi_set_ps( WIFI_PS_NONE );
	if( esp_wifi_start() != ESP_OK )
		return false;

	char safe[kSafeText];
	if( hasSaved() )
		ESP_LOGI( TAG, "Joining %s", Text::printable( ssid_, safe, sizeof( safe ) ) );
	else
		ESP_LOGW( TAG, "No network saved: set one up over Improv" );
	return true;
}

// MARK: - Joining

void WiFiStation::join( const char *ssid, const char *password ) {
	wifi_config_t config = {};
	strncpy( (char *)config.sta.ssid, ssid, sizeof( config.sta.ssid ) );
	memcpy( config.sta.password, password, std::min( strlen( password ), sizeof( config.sta.password ) ) );
	esp_wifi_disconnect();
	esp_wifi_set_config( WIFI_IF_STA, &config );
	esp_wifi_connect();
}

void WiFiStation::tryNetwork( const char *ssid, const char *password ) {
	strlcpy( trySSID_, ssid, sizeof( trySSID_ ) );
	strlcpy( tryPassword_, password, sizeof( tryPassword_ ) );
	char safe[kSafeText];
	ESP_LOGI( TAG, "Trying %s", Text::printable( trySSID_, safe, sizeof( safe ) ) );
	attempt_    = Attempt::Joining;
	connected_  = false;
	lastReason_ = 0;
	esp_timer_stop( timer( retry_ ) );
	restart( tryTimer_, kTryTimeout );
	join( trySSID_, tryPassword_ );
}

void WiFiStation::onRetry( void *arg ) {
	esp_wifi_connect();   // the network in the config: the one being tried, or the saved one
	(void)arg;
}

// The network being tried didn't work in time: back to the saved one.
void WiFiStation::onTryTimeout( void *arg ) {
	auto *self = static_cast<WiFiStation *>( arg );
	if( self->attempt_ != Attempt::Joining )
		return;
	char safe[kSafeText];
	ESP_LOGW( TAG, "Couldn't join %s", Text::printable( self->trySSID_, safe, sizeof( safe ) ) );
	memset( self->tryPassword_, 0, sizeof( self->tryPassword_ ) );
	self->attempt_ = Attempt::Failed;
	esp_timer_stop( timer( self->retry_ ) );
	if( self->hasSaved() )
		self->join( self->ssid_, self->password_ );
	else
		esp_wifi_disconnect();
}

void WiFiStation::onEvent( void *arg, esp_event_base_t base, int32_t id, void *data ) {
	auto *self = static_cast<WiFiStation *>( arg );
	if( base == WIFI_EVENT && id == WIFI_EVENT_STA_START ) {
		if( self->hasSaved() )
			self->join( self->ssid_, self->password_ );
	} else if( base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED ) {
		bool wasConnected = self->connected_.exchange( false );
		auto *info        = static_cast<wifi_event_sta_disconnected_t *>( data );
		self->lastReason_ = info->reason;
		if( self->attempt_ == Attempt::Joining ) {
			restart( self->retry_, kTryRetryDelay );   // until onTryTimeout() gives up
		} else if( self->hasSaved() ) {
			if( wasConnected )
				ESP_LOGW( TAG, "Disconnected (reason %u); joining again", info->reason );
			restart( self->retry_, kRetryDelay );
		}
	} else if( base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE ) {
		self->collectScan();
		self->scanning_ = false;
	} else if( base == IP_EVENT && id == IP_EVENT_STA_GOT_IP ) {
		auto *info = static_cast<ip_event_got_ip_t *>( data );
		self->connected_  = true;
		self->lastReason_ = 0;
		if( self->attempt_ == Attempt::Joining ) {
			esp_timer_stop( timer( self->tryTimer_ ) );
			self->save( self->trySSID_, self->tryPassword_ );
			memset( self->tryPassword_, 0, sizeof( self->tryPassword_ ) );
			self->attempt_ = Attempt::Joined;
		}
		char safe[kSafeText];
		ESP_LOGI( TAG, "On %s as " IPSTR, Text::printable( self->ssid_, safe, sizeof( safe ) ), IP2STR( &info->ip_info.ip ) );
	}
}

// MARK: - The saved network

void WiFiStation::loadSaved() {
	nvs_handle_t handle;
	if( nvs_open( kNamespace, NVS_READONLY, &handle ) != ESP_OK )
		return;
	size_t size = sizeof( ssid_ );
	if( nvs_get_str( handle, "ssid", ssid_, &size ) != ESP_OK )
		ssid_[0] = '\0';
	size = sizeof( password_ );
	if( nvs_get_str( handle, "password", password_, &size ) != ESP_OK )
		password_[0] = '\0';
	nvs_close( handle );
}

void WiFiStation::save( const char *ssid, const char *password ) {
	strlcpy( ssid_, ssid, sizeof( ssid_ ) );
	strlcpy( password_, password, sizeof( password_ ) );
	nvs_handle_t handle;
	esp_err_t    err = nvs_open( kNamespace, NVS_READWRITE, &handle );
	if( err == ESP_OK ) {
		err = nvs_set_str( handle, "ssid", ssid_ );
		if( err == ESP_OK )
			err = nvs_set_str( handle, "password", password_ );
		if( err == ESP_OK )
			err = nvs_commit( handle );
		nvs_close( handle );
	}
	char safe[kSafeText];
	if( err == ESP_OK )
		ESP_LOGI( TAG, "Saved %s", Text::printable( ssid_, safe, sizeof( safe ) ) );
	else
		ESP_LOGE( TAG, "Couldn't save the network: %s", esp_err_to_name( err ) );
}

// MARK: - Scanning

bool WiFiStation::startScan() {
	if( scanning_ )
		return true;
	if( esp_wifi_scan_start( nullptr, false ) != ESP_OK )
		return false;
	scanning_ = true;
	return true;
}

void WiFiStation::collectScan() {
	uint16_t count = kMaxScanned;
	auto    *records = static_cast<wifi_ap_record_t *>( calloc( kMaxScanned, sizeof( wifi_ap_record_t ) ) );
	if( !records ) {
		esp_wifi_clear_ap_list();
		return;
	}
	if( esp_wifi_scan_get_ap_records( &count, records ) != ESP_OK )
		count = 0;

	// One per name, at its strongest.
	std::lock_guard<std::mutex> lock( foundLock_ );
	foundCount_ = 0;
	for( uint16_t i = 0; i < count; i++ ) {
		const char *ssid = (const char *)records[i].ssid;
		if( !ssid[0] )
			continue;
		Network *same = std::find_if( found_, found_ + foundCount_, [&]( const Network &n ) { return strcmp( n.ssid, ssid ) == 0; } );
		if( same != found_ + foundCount_ ) {
			same->rssi = std::max( same->rssi, records[i].rssi );
		} else if( foundCount_ < kMaxFound ) {
			strlcpy( found_[foundCount_].ssid, ssid, sizeof( found_[foundCount_].ssid ) );
			found_[foundCount_].rssi   = records[i].rssi;
			found_[foundCount_].secure = records[i].authmode != WIFI_AUTH_OPEN;
			foundCount_++;
		}
	}
	free( records );
	std::sort( found_, found_ + foundCount_, []( const Network &a, const Network &b ) { return a.rssi > b.rssi; } );
}

size_t WiFiStation::scanResults( Network *out, size_t max ) {
	std::lock_guard<std::mutex> lock( foundLock_ );
	size_t count = std::min( max, foundCount_ );
	std::copy( found_, found_ + count, out );
	return count;
}
