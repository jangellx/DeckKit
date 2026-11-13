#include "ImprovSerial.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "SecureStorage.h"
#include "Text.h"
#include "Timing.h"
#include "WiFiStation.h"

static const char *TAG = "Improv";

namespace {
	constexpr uint32_t    kScanStartTimeout  = 10000;   // ms to get a scan going
	constexpr uint32_t    kScanRetry         = 500;     // ms between attempts to start one
	constexpr uint32_t    kPollInterval      = 20;      // ms between looks at the serial ports
	constexpr size_t      kMaxNetworks       = 24;      // networks a scan answers with
	constexpr size_t      kSafeText          = 40;      // a network name made printable, for the log
	constexpr uint32_t    kTaskStack         = 4096;    // bytes
	constexpr UBaseType_t kTaskPriority      = 3;
	// Serial driver buffers, bytes.
	constexpr int         kUARTReceiveBuffer = 512;
	constexpr uint32_t    kUSBReceiveBuffer  = 512;
	constexpr uint32_t    kUSBTransmitBuffer = 1024;

	// Serial output lock: log lines and Improv packets each go out whole.
	SemaphoreHandle_t outputLock = nullptr;

	// Whether taking outputLock is possible here (not in an interrupt, nor before the scheduler).
	bool canLock() {
		return !xPortInIsrContext() && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
	}

	// The log writer: each line goes out whole, between Improv packets.
	int lockedVprintf( const char *format, va_list args ) {
		if( !canLock() )
			return vprintf( format, args );
		xSemaphoreTakeRecursive( outputLock, portMAX_DELAY );
		int written = vprintf( format, args );
		xSemaphoreGiveRecursive( outputLock );
		return written;
	}
}

bool ImprovSerial::begin( WiFiStation &wifi, const Info &info, bool usbSerial ) {
	wifi_ = &wifi;
	info_ = info;
	outputLock = xSemaphoreCreateRecursiveMutex();
	if( !outputLock )
		return false;
	esp_log_set_vprintf( lockedVprintf );

	// A receive-only driver on the console UART. Logs keep going straight to the UART's FIFO;
	// with no transmit buffer, uart_write_bytes() also writes the FIFO directly, so the two
	// stay in order.
	if( !uart_is_driver_installed( UART_NUM_0 ) ) {
		esp_err_t err = uart_driver_install( UART_NUM_0, kUARTReceiveBuffer, 0, 0, nullptr, 0 );
		if( err != ESP_OK ) {
			ESP_LOGE( TAG, "UART driver: %s", esp_err_to_name( err ) );
			return false;
		}
	}

	// On the USB-Serial-JTAG port, logs go through the driver too, so both share its buffer.
	if( usbSerial && !usb_serial_jtag_is_driver_installed() ) {
		usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
		config.rx_buffer_size = kUSBReceiveBuffer;
		config.tx_buffer_size = kUSBTransmitBuffer;
		esp_err_t err = usb_serial_jtag_driver_install( &config );
		if( err == ESP_OK )
			usb_serial_jtag_vfs_use_driver();
		else
			ESP_LOGE( TAG, "USB-Serial-JTAG driver: %s", esp_err_to_name( err ) );
		usbSerial_ = err == ESP_OK;
	}

	ESP_LOGI( TAG, "Listening on UART0%s", usbSerial_ ? " and USB" : "" );
	return xTaskCreate( task, "improv", kTaskStack, this, kTaskPriority, nullptr ) == pdPASS;
}

void ImprovSerial::task( void *arg ) {
	static_cast<ImprovSerial *>( arg )->run();
}

void ImprovSerial::run() {
	uint8_t bytes[64];
	while( true ) {
		int count;
		while( ( count = uart_read_bytes( UART_NUM_0, bytes, sizeof( bytes ), 0 ) ) > 0 )
			receive( uartParser_, bytes, count );
		while( usbSerial_ && ( count = usb_serial_jtag_read_bytes( bytes, sizeof( bytes ), 0 ) ) > 0 )
			receive( usbParser_, bytes, count );
		trackAttempt();
		trackScan();
		vTaskDelay( pdMS_TO_TICKS( kPollInterval ) );
	}
}

// MARK: - Receiving

void ImprovSerial::receive( Improv::Parser &parser, const uint8_t *bytes, int count ) {
	for( int i = 0; i < count; i++ ) {
		switch( parser.feed( bytes[i] ) ) {
			case Improv::Parser::Result::Invalid:
				ESP_LOGW( TAG, "Bad packet" );
				sendError( Improv::Error::InvalidRPC );
				break;
			case Improv::Parser::Result::Packet: {
				// Only RPCs are acted on.
				if( parser.type() != Improv::kTypeRPC )
					break;
				uint8_t        command;
				const uint8_t *args;
				size_t         length;
				if( !Improv::readRPC( parser.data(), parser.length(), command, args, length ) ) {
					sendError( Improv::Error::InvalidRPC );
					break;
				}
				sendError( Improv::Error::None );
				handleCommand( command, args, length );
				break;
			}
			default:
				break;
		}
	}
}

void ImprovSerial::handleCommand( uint8_t command, const uint8_t *args, size_t length ) {
	switch( command ) {
		case Improv::kSendWiFi: {
			char ssid[33], password[65];
			if( !Improv::readWiFi( args, length, ssid, password ) ) {
				sendError( Improv::Error::InvalidRPC );
				return;
			}
			sendState( Improv::State::Provisioning );
			joining_ = true;
			wifi_->tryNetwork( ssid, password );
			memset( password, 0, sizeof( password ) );
			break;
		}

		case Improv::kRequestState: {
			Improv::State state = currentState();
			sendState( state );
			if( state == Improv::State::Provisioned )
				sendResult( Improv::kSendWiFi, {} );   // what a successful provisioning answers
			break;
		}

		case Improv::kRequestInfo:
			sendResult( Improv::kRequestInfo, { info_.firmware, info_.version, info_.chip, info_.name } );
			break;

		case Improv::kDeviceName:
			// Renaming isn't supported: answer with the name in effect.
			sendResult( Improv::kDeviceName, { info_.name } );
			break;

		case Improv::kWiFiNetwork: {
			char ssid[33];
			Text::displayable( wifi_->savedSSID(), ssid, sizeof( ssid ) );
			sendResult( Improv::kWiFiNetwork, { ssid, wifi_->hasSaved() && wifi_->isConnected() ? "YES" : "NO", info_.hostname } );
			break;
		}

		case Improv::kStorage: {
			if( length > 1 || ( length == 1 && args[0] > 1 ) ) {
				sendError( Improv::Error::InvalidRPC );
				return;
			}
			if( length == 1 && wifi_->storageChoiceOpen() )
				wifi_->setStandardStorage( args[0] == 0 );
			const char *setup = !wifi_->storageChoiceOpen() ? "none" : wifi_->standardStorage() ? "standard" : "encrypt";
			sendResult( Improv::kStorage, { SecureStorage::stateName(), setup } );
			break;
		}

		case Improv::kRequestScan:
			if( !scanWanted_ ) {
				uint32_t now  = Timing::nowMillis();
				scanWanted_   = true;
				scanStarted_  = false;
				scanDeadline_ = now + kScanStartTimeout;
				scanTried_    = now - kScanRetry;
			}
			break;

		default:
			sendError( Improv::Error::UnknownCommand );
			break;
	}
}

// MARK: - Joining and scanning

// Provisioned means on the saved network.
Improv::State ImprovSerial::currentState() const {
	if( joining_ )
		return Improv::State::Provisioning;
	if( wifi_->hasSaved() && wifi_->isConnected() )
		return Improv::State::Provisioned;
	return Improv::State::Ready;
}

void ImprovSerial::trackAttempt() {
	if( !joining_ )
		return;
	WiFiStation::Attempt attempt = wifi_->attempt();
	if( attempt == WiFiStation::Attempt::Joined ) {
		joining_ = false;
		sendState( Improv::State::Provisioned );
		// No URL: the device's own pages, if it has any, are for setup only.
		sendResult( Improv::kSendWiFi, {} );
		provisioned_ = true;
	} else if( attempt == WiFiStation::Attempt::Failed ) {
		joining_ = false;
		sendError( Improv::Error::UnableToConnect );
		sendState( Improv::State::Ready );
	}
}

// Starts a requested scan (the radio refuses while it's joining a network, so keep trying for
// a while), then answers with one result per network, strongest first, and an empty one.
void ImprovSerial::trackScan() {
	if( !scanWanted_ )
		return;
	if( !scanStarted_ ) {
		uint32_t now = Timing::nowMillis();
		if( !Timing::reached( scanTried_ + kScanRetry, now ) )
			return;
		scanTried_   = now;
		scanStarted_ = wifi_->startScan();
		if( !scanStarted_ && Timing::reached( scanDeadline_, now ) ) {
			ESP_LOGW( TAG, "Couldn't scan" );
			scanWanted_ = false;
			sendResult( Improv::kRequestScan, {} );
		}
		return;
	}
	if( wifi_->scanning() )
		return;

	scanStarted_ = false;
	scanWanted_ = false;
	static WiFiStation::Network networks[kMaxNetworks];
	size_t count = wifi_->scanResults( networks, kMaxNetworks );
	for( size_t i = 0; i < count; i++ ) {
		char rssi[8];
		snprintf( rssi, sizeof( rssi ), "%d", networks[i].rssi );
		sendResult( Improv::kRequestScan, { networks[i].ssid, rssi, networks[i].secure ? "YES" : "NO" } );
	}
	sendResult( Improv::kRequestScan, {} );
}

// MARK: - Sending

void ImprovSerial::sendState( Improv::State state ) {
	uint8_t value = (uint8_t)state;
	sendPacket( Improv::kTypeState, &value, 1 );
}

void ImprovSerial::sendError( Improv::Error error ) {
	uint8_t value = (uint8_t)error;
	sendPacket( Improv::kTypeError, &value, 1 );
}

void ImprovSerial::sendResult( uint8_t command, std::initializer_list<const char *> strings ) {
	uint8_t data[257];
	size_t  length = Improv::writeResult( data, sizeof( data ), command, strings.begin(), strings.size() );
	sendPacket( Improv::kTypeResult, data, length );
}

void ImprovSerial::sendPacket( uint8_t type, const uint8_t *data, size_t length ) {
	uint8_t packet[Improv::kMaxPacket];
	size_t  size = Improv::writePacket( packet, sizeof( packet ), type, data, length );
	if( !size )
		return;
	xSemaphoreTakeRecursive( outputLock, portMAX_DELAY );
	fflush( stdout );
	uart_write_bytes( UART_NUM_0, packet, size );
	uart_wait_tx_done( UART_NUM_0, pdMS_TO_TICKS( 100 ) );
	if( usbSerial_ )
		usb_serial_jtag_write_bytes( packet, size, pdMS_TO_TICKS( 50 ) );
	xSemaphoreGiveRecursive( outputLock );
}
