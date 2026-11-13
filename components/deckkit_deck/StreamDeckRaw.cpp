// An Elgato deck's HID reports as raw bytes: StreamDeck's passthrough (see StreamDeck.h, "Raw
// reports"). They go out through the same output transfer and HID class requests as
// StreamDeck's own key images and brightness, under the same mutex, so the two can't
// interleave.
#include "StreamDeck.h"

#include <cstring>

#include "esp_log.h"

static const char *TAG = "StreamDeck";

void StreamDeck::setRawInputHandler( RawInputHandler handler, void *context ) {
	rawContext_ = context;
	rawInput_   = handler;
}

esp_err_t StreamDeck::sendOutputReport( const uint8_t *report, size_t length ) {
	if( !report || length == 0 )
		return ESP_ERR_INVALID_ARG;
	if( !mutex_ )
		return ESP_ERR_INVALID_STATE;

	xSemaphoreTake( mutex_, portMAX_DELAY );
	esp_err_t err = ESP_ERR_INVALID_STATE;
	if( handle_ && !mirabox_ ) {
		if( length > outputSize() ) {
			ESP_LOGW( TAG, "Output report of %u bytes is longer than the deck's %u", (unsigned)length, (unsigned)outputSize() );
			err = ESP_ERR_INVALID_SIZE;
		} else {
			memcpy( report_, report, length );
			err = sendReport( length );
		}
	}
	xSemaphoreGive( mutex_ );
	return err;
}

esp_err_t StreamDeck::setFeatureReport( const uint8_t *report, size_t length ) {
	if( !report || length == 0 )
		return ESP_ERR_INVALID_ARG;
	if( !mutex_ )
		return ESP_ERR_INVALID_STATE;

	xSemaphoreTake( mutex_, portMAX_DELAY );
	esp_err_t err = ESP_ERR_INVALID_STATE;
	if( handle_ && !mirabox_ )
		err = hid_class_request_set_report( handle_, HID_REPORT_TYPE_FEATURE, report[0], const_cast<uint8_t *>( report ), length );
	xSemaphoreGive( mutex_ );
	return err;
}

esp_err_t StreamDeck::getFeatureReport( uint8_t reportID, uint8_t *out, size_t &length ) {
	if( !out || length == 0 )
		return ESP_ERR_INVALID_ARG;
	if( !mutex_ )
		return ESP_ERR_INVALID_STATE;

	xSemaphoreTake( mutex_, portMAX_DELAY );
	esp_err_t err = ESP_ERR_INVALID_STATE;
	if( handle_ && !mirabox_ ) {
		out[0] = reportID;
		err    = hid_class_request_get_report( handle_, HID_REPORT_TYPE_FEATURE, reportID, out, &length );
	}
	xSemaphoreGive( mutex_ );
	if( err != ESP_OK )
		length = 0;
	return err;
}
