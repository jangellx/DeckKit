// Mirabox decks over USB: StreamDeck's half of MiraboxProtocol.h. Experimental; see there.
//
// They come through the same HID host and output transfer as Elgato decks. What differs:
// they're found by vendor and product ID and their interface's vendor usage page (some also
// have a keyboard interface), every command is one output report of the size their report
// descriptor declares, key images go out as BAT, the image in pieces, then STP or ULEND,
// key presses arrive as ACK reports, and they need a keepalive.
#include "StreamDeck.h"
#include "MiraboxProtocol.h"

#include <cstring>

#include "esp_log.h"

static const char *TAG = "StreamDeck";

namespace {
	StreamDeck::Transform transformFor( Mirabox::Rotation rotation ) {
		switch( rotation ) {
			case Mirabox::Rotation::Rotate90:  return StreamDeck::Transform::Rotate90;
			case Mirabox::Rotation::Rotate270: return StreamDeck::Transform::Rotate270;
			case Mirabox::Rotation::Rotate180: return StreamDeck::Transform::Rotate180;
			default:                           return StreamDeck::Transform::None;
		}
	}
}

// MARK: - Finding one

bool StreamDeck::identifyMirabox( hid_host_device_handle_t handle, uint16_t vid, uint16_t pid, Info &info, const Mirabox::Model *&model, uint16_t &packet ) {
	model = Mirabox::find( vid, pid );
	if( !model )
		return false;

	size_t         length     = 0;
	const uint8_t *descriptor = hid_host_get_report_descriptor( handle, &length );
	if( !descriptor || !Mirabox::isVendorInterface( descriptor, length ) ) {
		ESP_LOGI( TAG, "%s: skipping an interface that isn't the deck's own", model->name );
		model = nullptr;
		return false;
	}

	// Commands are framed to the packet size, and a wrong one fails silently (a black panel),
	// so the size the deck declares wins over the table's.
	size_t declared = Mirabox::largestOutputReport( descriptor, length );
	packet = model->packet;
	if( ( declared == 512 || declared == 1024 ) && declared != packet ) {
		ESP_LOGW( TAG, "%s declares %u-byte reports, not %u; using %u", model->name, (unsigned)declared, packet, (unsigned)declared );
		packet = (uint16_t)declared;
	}

	info.vid        = vid;
	info.pid        = pid;
	strlcpy( info.model, model->name, sizeof( info.model ) );
	info.protocol   = model->family == Mirabox::Family::Akp05 ? Protocol::Akp05 : Protocol::Mirabox;
	info.rows       = model->rows;
	info.cols       = model->cols;
	info.keySize    = model->keySize;
	info.format     = Format::JPEG;
	info.transform  = transformFor( model->rotation );
	info.reversed   = false;
	info.imageLimit = model->imageLimit;
	info.pressesOnly = !model->reportsKeyUp;
	ESP_LOGW( TAG, "%s (%04X:%04X): experimental support%s", model->name, vid, pid,
			  model->tested ? "" : ", and not tested on this model by DeckBridge either" );
	return true;
}

// MARK: - Commands

template <typename Fill>
esp_err_t StreamDeck::sendMiraboxCommand( Fill fill ) {
	uint8_t id = mirabox_->reportID;
	if( id )
		report_[0] = id;
	fill( report_ + ( id ? 1 : 0 ) );
	return sendReport( outputSize() );
}

// Wakes the panel (nothing works before DIS), sets the brightness and clears every key, as
// DeckBridge opens one. The keepalive starts here.
void StreamDeck::prepareMirabox() {
	xSemaphoreTake( mutex_, portMAX_DELAY );
	if( handle_ && mirabox_ ) {
		size_t    packet = miraboxPacket_;
		bool      akp05  = mirabox_->family == Mirabox::Family::Akp05;
		esp_err_t err    = ESP_OK;
		if( akp05 )
			err = sendMiraboxCommand( [&]( uint8_t *out ) { Mirabox::command( out, packet, "VER" ); } );
		if( err == ESP_OK )
			err = sendMiraboxCommand( [&]( uint8_t *out ) { Mirabox::command( out, packet, "DIS" ); } );
		if( err == ESP_OK )
			err = sendMiraboxCommand( [&]( uint8_t *out ) { Mirabox::brightness( out, packet, brightness_ ); } );
		if( err == ESP_OK )
			err = sendMiraboxCommand( [&]( uint8_t *out ) { Mirabox::clear( out, packet, 0xFF ); } );
		if( err == ESP_OK && ( akp05 || mirabox_->stpAfterClear ) )
			err = sendMiraboxCommand( [&]( uint8_t *out ) { Mirabox::command( out, packet, "STP" ); } );
		if( err != ESP_OK )
			ESP_LOGW( TAG, "Couldn't set up the %s: %s", mirabox_->name, esp_err_to_name( err ) );
		keepaliveMs_ = mirabox_->keepaliveMs;
	}
	xSemaphoreGive( mutex_ );
}

// CONNECT; the AKP05 also needs DIS and its brightness again, or it stops taking images.
void StreamDeck::sendKeepalive() {
	xSemaphoreTake( mutex_, portMAX_DELAY );
	if( handle_ && mirabox_ ) {
		size_t    packet = miraboxPacket_;
		esp_err_t err    = ESP_OK;
		if( mirabox_->family == Mirabox::Family::Akp05 ) {
			err = sendMiraboxCommand( [&]( uint8_t *out ) { Mirabox::command( out, packet, "DIS" ); } );
			if( err == ESP_OK )
				err = sendMiraboxCommand( [&]( uint8_t *out ) { Mirabox::brightness( out, packet, brightness_ ); } );
		}
		if( err == ESP_OK )
			err = sendMiraboxCommand( [&]( uint8_t *out ) { Mirabox::command( out, packet, "CONNECT" ); } );
		if( err != ESP_OK )
			ESP_LOGW( TAG, "Keepalive failed: %s", esp_err_to_name( err ) );
	}
	xSemaphoreGive( mutex_ );
}

// BAT with the length as sent, the image in packet-sized pieces, then STP (or ULEND) to show
// it. Caller holds mutex_.
esp_err_t StreamDeck::sendMiraboxImage( uint8_t key, const uint8_t *image, size_t length ) {
	if( mirabox_->imageLimit && length > mirabox_->imageLimit )
		return ESP_ERR_INVALID_SIZE;

	size_t  packet = miraboxPacket_;
	bool    pad    = mirabox_->padsChunks;
	uint8_t id     = mirabox_->imageIDs[key];
	size_t  sent   = Mirabox::sentLength( length, packet, pad );
	if( sent > 0xFFFF )
		return ESP_ERR_INVALID_SIZE;

	esp_err_t err = sendMiraboxCommand( [&]( uint8_t *out ) { Mirabox::imageHeader( out, packet, sent, id ); } );
	size_t    count = Mirabox::chunkCount( length, packet, pad );
	for( size_t index = 0; err == ESP_OK && index < count; index++ )
		err = sendMiraboxCommand( [&]( uint8_t *out ) { Mirabox::chunk( image, length, packet, pad, index, out ); } );
	if( err == ESP_OK ) {
		const char *end = mirabox_->family == Mirabox::Family::Akp05 ? "ULEND" : "STP";
		err = sendMiraboxCommand( [&]( uint8_t *out ) { Mirabox::command( out, packet, end ); } );
	}
	return err;
}

// LIG; also what sleep is (0). Caller holds mutex_.
esp_err_t StreamDeck::sendMiraboxBrightness( uint8_t percent ) {
	brightness_ = percent;
	size_t packet = miraboxPacket_;
	return sendMiraboxCommand( [&]( uint8_t *out ) { Mirabox::brightness( out, packet, percent ); } );
}

// MARK: - Keys

// An ACK report: a key's code and whether it's down. Decks that only report presses get their
// release straight after. Codes that aren't keys (dials, a touch strip) are ignored.
void StreamDeck::handleMiraboxInput( const uint8_t *data, size_t length ) {
	const Mirabox::Model *model = mirabox_;
	uint8_t code = 0, state = 0;
	if( !model || !Mirabox::parseKey( data, length, model->reportID, code, state ) )
		return;
	int key = Mirabox::keyForInput( *model, code );
	if( key < 0 )
		return;

	if( !model->reportsKeyUp ) {
		// Every report is a press, whatever its state byte, as DeckBridge reads them.
		post( EventType::KeyDown, (uint8_t)key );
		post( EventType::KeyUp, (uint8_t)key );
		return;
	}
	uint8_t down = state == 0x01 ? 1 : 0;
	if( down != keyStates_[key] ) {
		keyStates_[key] = down;
		post( down ? EventType::KeyDown : EventType::KeyUp, (uint8_t)key );
	}
}
