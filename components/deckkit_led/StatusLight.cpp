#include "StatusLight.h"

#include <cmath>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

#include "Timing.h"

static const char *TAG = "StatusLight";

namespace {
	// The board's WS2812 is very bright; these are full-scale values for each state.
	constexpr uint8_t  kPulsePeak      = 110;
	constexpr uint8_t  kPulseFloor     = 6;
	constexpr uint8_t  kConnectedLevel = 64;    // 25%
	constexpr uint8_t  kKeyLevel       = 140;
	constexpr uint8_t  kPairingLevel   = 120;
	constexpr uint32_t kPairingBlink   = 250;   // ms on, then off
	constexpr uint32_t kPulsePeriod    = 2000;  // ms
	constexpr uint32_t kKeyFlash       = 150;   // ms of white, at least, per key press
	constexpr uint32_t kActivityHold   = 300;   // ms the data pulse lasts after the last message
	constexpr uint32_t kActivityPeriod = 400;   // ms, a quicker pulse than the searching ones
	constexpr uint8_t  kActivityPeak   = 160;
	// Asleep, the pulse peak scales down to this: visible in a dark room, but no more.
	constexpr uint8_t  kSleepPeak      = 8;

	constexpr uint32_t    kFrame        = 20;     // ms between updates
	constexpr uint32_t    kTaskStack    = 3072;   // bytes
	constexpr UBaseType_t kTaskPriority = 2;

	// Scales a channel for sleep, keeping anything that was lit at least barely lit.
	uint8_t dimmed( uint8_t level ) {
		if( level == 0 )
			return 0;
		unsigned scaled = ( (unsigned)level * kSleepPeak + kPulsePeak / 2 ) / kPulsePeak;
		return (uint8_t)( scaled < 1 ? 1 : scaled > 255 ? 255 : scaled );
	}

	// 0…1…0 over the period, eased so it breathes rather than blinks.
	float pulse( uint32_t now, uint32_t period = kPulsePeriod ) {
		float phase = (float)( now % period ) / period;
		return 0.5f - 0.5f * cosf( phase * 2.0f * (float)M_PI );
	}

	// The slow pulse (setup, Wi-Fi) as a channel level, from kPulseFloor to kPulsePeak.
	uint8_t pulseLevel( uint32_t now ) {
		return (uint8_t)( kPulseFloor + ( kPulsePeak - kPulseFloor ) * pulse( now ) );
	}
}

bool StatusLight::begin( int gpio ) {
	led_strip_config_t config = {};
	config.strip_gpio_num         = gpio;
	config.max_leds               = 1;
	config.led_model              = LED_MODEL_WS2812;
	config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
	led_strip_rmt_config_t rmt = {};
	rmt.clk_src       = RMT_CLK_SRC_DEFAULT;
	rmt.resolution_hz = 10 * 1000 * 1000;
	led_strip_handle_t strip = nullptr;
	esp_err_t err = led_strip_new_rmt_device( &config, &rmt, &strip );
	if( err != ESP_OK ) {
		ESP_LOGE( TAG, "Couldn't start the LED on GPIO %d: %s", gpio, esp_err_to_name( err ) );
		return false;
	}
	strip_ = strip;
	write( 0, 0, 0 );
	return xTaskCreate( task, "status_light", kTaskStack, this, kTaskPriority, nullptr ) == pdPASS;
}

void StatusLight::keyPressed() {
	keyUntil_ = Timing::nowMillis() + kKeyFlash;
}

void StatusLight::activity() {
	activityUntil_ = Timing::nowMillis() + kActivityHold;
}

void StatusLight::task( void *arg ) {
	auto *self = static_cast<StatusLight *>( arg );
	while( true ) {
		self->update();
		vTaskDelay( pdMS_TO_TICKS( kFrame ) );
	}
}

void StatusLight::update() {
	uint32_t now = Timing::nowMillis();
	if( keyDown_ || !Timing::reached( keyUntil_, now ) ) {
		show( kKeyLevel, kKeyLevel, kKeyLevel );
		return;
	}

	Mode mode = mode_;
	if( asleep_ && mode == Mode::Connected ) {
		show( 0, 0, 0 );
		return;
	}

	switch( mode ) {
		case Mode::Off:
			show( 0, 0, 0 );
			break;
		case Mode::Setup: {
			uint8_t level = pulseLevel( now );
			show( 0, level / 4, level );                       // Wi-Fi blue
			break;
		}
		case Mode::Searching: {
			uint8_t level = pulseLevel( now );
			show( level, (uint8_t)( level * 3 / 4 ), 0 );      // yellow (a touch less green reads as yellow on WS2812s)
			break;
		}
		case Mode::OnWifi: {
			uint8_t level = pulseLevel( now );
			show( level / 16, level, level / 16 );             // green, barely paler (more reads as the setup blue)
			break;
		}
		case Mode::Pairing:
			if( ( now / kPairingBlink ) % 2 == 0 )
				show( kPairingLevel, 0, kPairingLevel );
			else
				show( 0, 0, 0 );
			break;
		case Mode::PairingConfirmed:
			show( kPairingLevel, 0, kPairingLevel );
			break;
		case Mode::Connected:
			if( !Timing::reached( activityUntil_, now ) )
				show( 0, (uint8_t)( kConnectedLevel + ( kActivityPeak - kConnectedLevel ) * pulse( now, kActivityPeriod ) ), 0 );
			else
				show( 0, kConnectedLevel, 0 );
			break;
	}
}

void StatusLight::show( uint8_t red, uint8_t green, uint8_t blue ) {
	if( asleep_ )
		write( dimmed( red ), dimmed( green ), dimmed( blue ) );
	else
		write( red, green, blue );
}

void StatusLight::write( uint8_t red, uint8_t green, uint8_t blue ) {
	uint32_t color = (uint32_t)red << 16 | (uint32_t)green << 8 | blue;
	if( color == written_ || !strip_ )
		return;
	written_ = color;
	auto *strip = static_cast<led_strip_handle_t>( strip_ );
	led_strip_set_pixel( strip, 0, red, green, blue );
	led_strip_refresh( strip );
}
