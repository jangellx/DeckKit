#include "MiraboxProtocol.h"

#include <algorithm>
#include <cstring>

namespace Mirabox {
	namespace {
		// The 293V3 board: 3 × 5 keys of 112 px, upside down. Image IDs run from the bottom row
		// up; input codes are the key index + 1. Tested in DeckBridge as the Mirabox 293V3
		// (0x6603:0x1005), the Fifine AmpliGame D6 rev. 2 and (with its own layout) the Ajazz
		// AKP153E rev. 2.
		constexpr Model board293V3( uint16_t vid, uint16_t pid, const char *name, bool tested, uint16_t packet = 1024 ) {
			return { vid, pid, name, Family::Mirabox, 3, 5, 112, Rotation::Rotate180, packet, 0, 8000, 10240, true, false, true, tested,
					 { 11, 12, 13, 14, 15, 6, 7, 8, 9, 10, 1, 2, 3, 4, 5 },
					 { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 } };
		}

		// The 293S board ("protocol version 1"): 3 × 6 keys of 85 px, turned a quarter, 512-byte
		// reports, and presses without releases. IDs run down each column from the fifth column
		// leftward; DeckBridge tested those 15 keys, and the rightmost column's 16 to 18 come
		// from opendeck-akp153 without a test.
		constexpr Model board293S( uint16_t vid, uint16_t pid, const char *name, bool tested ) {
			return { vid, pid, name, Family::Mirabox, 3, 6, 85, Rotation::Rotate270, 512, 0, 8000, 5120, false, false, false, tested,
					 { 13, 10, 7, 4, 1, 16, 14, 11, 8, 5, 2, 17, 15, 12, 9, 6, 3, 18 },
					 { 13, 10, 7, 4, 1, 16, 14, 11, 8, 5, 2, 17, 15, 12, 9, 6, 3, 18 } };
		}

		// The K1 Pro: 2 × 3 keys of 64 px and three dials (not used), report ID 4, a keepalive
		// every 2 s, and firmware that drops the last byte of every full 1024-byte piece.
		constexpr Model boardK1Pro( uint16_t vid, uint16_t pid, const char *name, bool tested ) {
			return { vid, pid, name, Family::Mirabox, 2, 3, 64, Rotation::Rotate90, 1024, 4, 2000, 4096, true, true, true, tested,
					 { 5, 3, 1, 6, 4, 2 },
					 { 5, 3, 1, 6, 4, 2 } };
		}

		// The AKP05: 2 × 5 keys of 112 px, upside down, with four dials and a touch strip (not
		// used). Image IDs 11 to 15 on top, 6 to 10 below (1 to 4 are the strip); input codes
		// 1 to 10 row by row.
		constexpr Model boardAKP05( uint16_t vid, uint16_t pid, const char *name, bool tested ) {
			return { vid, pid, name, Family::Akp05, 2, 5, 112, Rotation::Rotate180, 1024, 0, 10000, 0xFFFF, true, false, true, tested,
					 { 11, 12, 13, 14, 15, 6, 7, 8, 9, 10 },
					 { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 } };
		}

		// The AKP153E rev. 2: the 293V3 protocol with 95 px keys turned a quarter, numbered down
		// each column from the right. Tested in DeckBridge.
		constexpr Model akp153eRev2() {
			Model model = board293V3( 0x0300, 0x3010, "Ajazz AKP153E", true );
			model.keySize  = 95;
			model.rotation = Rotation::Rotate270;
			constexpr uint8_t ids[] = { 13, 10, 7, 4, 1, 14, 11, 8, 5, 2, 15, 12, 9, 6, 3 };
			for( size_t i = 0; i < sizeof( ids ); i++ ) {
				model.imageIDs[i]   = ids[i];
				model.inputCodes[i] = ids[i];
			}
			return model;
		}

		constexpr Model kModels[] = {
			board293V3( 0x6603, 0x1005, "Mirabox 293V3", true ),
			board293V3( 0x6603, 0x1006, "Mirabox 293V3", false ),
			board293V3( 0x6603, 0x1010, "Mirabox 293V3", false ),
			board293V3( 0x6603, 0x1014, "Mirabox 293S V3", false ),
			board293V3( 0x3142, 0x0060, "Fifine AmpliGame D6", true ),
			board293V3( 0x3142, 0x0007, "Fifine AmpliGame D6", false, 512 ),
			board293V3( 0x1200, 0x2012, "upHere 293V3", false ),
			board293V3( 0x0300, 0x3011, "Ajazz AKP153R", false ),
			akp153eRev2(),

			board293S( 0x5548, 0x6670, "Mirabox 293S", true ),
			board293S( 0x5548, 0x6674, "Ajazz AKP153", false ),
			board293S( 0x0300, 0x1010, "Ajazz AKP153E", false ),
			board293S( 0x0300, 0x1020, "Ajazz AKP153R", false ),
			board293S( 0x0B00, 0x1000, "Mars Gaming MSD-ONE", false ),
			board293S( 0x0C00, 0x1000, "Mad Dog GK150K", false ),
			board293S( 0x0A00, 0x1001, "Risemode Vision 01", false ),
			board293S( 0x0500, 0x1001, "TMICE Stream Controller", false ),

			boardK1Pro( 0x6603, 0x1015, "Mirabox K1 Pro", true ),
			boardK1Pro( 0x6603, 0x1019, "Mirabox K1 Pro", false ),
			boardK1Pro( 0x5548, 0x1025, "Mirabox K1 Pro", false ),

			boardAKP05( 0x0300, 0x3004, "Ajazz AKP05E", true ),
			boardAKP05( 0x0300, 0x3013, "Ajazz AKP05E Pro", false ),
			boardAKP05( 0x0300, 0x3014, "Ajazz AKP05CN Pro", false ),
			boardAKP05( 0x0200, 0x3001, "Redragon SS552", false ),
			boardAKP05( 0x6602, 0x1001, "Mirabox N4", false ),
			boardAKP05( 0x6603, 0x1007, "Mirabox N4E", false ),
			boardAKP05( 0x5548, 0x1008, "Mirabox N4 Pro", false ),
			boardAKP05( 0x5548, 0x1021, "Mirabox N4 Pro E", false ),
			boardAKP05( 0x5548, 0x1004, "VSDinside N4", false ),
			boardAKP05( 0x5548, 0x1023, "VSDinside N4 Pro", false ),
			boardAKP05( 0x0B00, 0x1003, "Mars Gaming MSD-PRO", false ),
			boardAKP05( 0x1500, 0x3002, "Soomfon CN003", false ),
			boardAKP05( 0x1500, 0x3007, "Streonor S10 Pro", false ),
		};

		constexpr uint8_t kPrefix[] = { 'C', 'R', 'T', 0, 0 };
	}

	const Model *find( uint16_t vid, uint16_t pid ) {
		for( const Model &model : kModels ) {
			if( model.vid == vid && model.pid == pid )
				return &model;
		}
		return nullptr;
	}

	// MARK: - Commands

	void command( uint8_t *out, size_t packet, const char *name, const uint8_t *args, size_t argCount ) {
		memset( out, 0, packet );
		size_t at = 0;
		for( uint8_t byte : kPrefix ) out[at++] = byte;
		for( const char *c = name; *c && at < packet; c++ ) out[at++] = (uint8_t)*c;
		for( size_t i = 0; i < argCount && at < packet; i++ ) out[at++] = args[i];
	}

	void brightness( uint8_t *out, size_t packet, uint8_t percent ) {
		const uint8_t args[] = { 0, 0, std::min<uint8_t>( percent, 100 ) };
		command( out, packet, "LIG", args, sizeof( args ) );
	}

	void clear( uint8_t *out, size_t packet, uint8_t imageID ) {
		const uint8_t args[] = { 0, 0, 0, imageID };
		command( out, packet, "CLE", args, sizeof( args ) );
	}

	void imageHeader( uint8_t *out, size_t packet, size_t length, uint8_t imageID ) {
		const uint8_t args[] = { 0, 0, (uint8_t)( length >> 8 ), (uint8_t)length, imageID };
		command( out, packet, "BAT", args, sizeof( args ) );
	}

	// MARK: - Images

	// Padded, every full piece is packet - 1 bytes of the image and a sacrificial zero, so the
	// byte the deck drops is never the image's.
	size_t sentLength( size_t length, size_t packet, bool pad ) {
		return pad ? length + length / ( packet - 1 ) : length;
	}

	size_t chunkCount( size_t length, size_t packet, bool pad ) {
		return ( sentLength( length, packet, pad ) + packet - 1 ) / packet;
	}

	void chunk( const uint8_t *image, size_t length, size_t packet, bool pad, size_t index, uint8_t *out ) {
		memset( out, 0, packet );
		size_t room  = pad ? packet - 1 : packet;   // image bytes in a full piece
		size_t start = index * room;
		if( start >= length )
			return;
		memcpy( out, image + start, std::min( room, length - start ) );
	}

	// MARK: - Input

	bool parseKey( const uint8_t *data, size_t length, uint8_t reportID, uint8_t &code, uint8_t &state ) {
		size_t at = reportID ? 1 : 0;
		if( length < at + 11 || ( reportID && data[0] != reportID ) )
			return false;
		if( data[at] != 'A' || data[at + 1] != 'C' || data[at + 2] != 'K' )
			return false;
		code  = data[at + 9];
		state = data[at + 10];
		return true;
	}

	int keyForInput( const Model &model, uint8_t code ) {
		for( uint8_t key = 0; key < model.keyCount(); key++ ) {
			if( model.inputCodes[key] == code )
				return key;
		}
		return -1;
	}

	// MARK: - Report descriptors

	namespace {
		// Calls fn( tag byte without its size bits, value ) for each short item.
		template <typename Fn>
		void forEachItem( const uint8_t *descriptor, size_t length, Fn fn ) {
			for( size_t at = 0; at < length; ) {
				uint8_t prefix = descriptor[at];
				if( prefix == 0xFE ) {   // a long item: its data size, then its tag, then the data
					if( at + 1 >= length ) return;
					at += 3 + descriptor[at + 1];
					continue;
				}
				size_t size = ( prefix & 0x03 ) == 3 ? 4 : prefix & 0x03;
				if( at + 1 + size > length )
					return;
				uint32_t value = 0;
				for( size_t i = 0; i < size; i++ )
					value |= (uint32_t)descriptor[at + 1 + i] << ( 8 * i );
				fn( (uint8_t)( prefix & 0xFC ), value );
				at += 1 + size;
			}
		}

		constexpr uint8_t kUsagePage   = 0x04;
		constexpr uint8_t kReportSize  = 0x74;
		constexpr uint8_t kReportID    = 0x84;
		constexpr uint8_t kReportCount = 0x94;
		constexpr uint8_t kOutput      = 0x90;
	}

	bool isVendorInterface( const uint8_t *descriptor, size_t length ) {
		bool vendor = false;
		forEachItem( descriptor, length, [&]( uint8_t tag, uint32_t value ) {
			if( tag == kUsagePage && value == 0xFFA0 )
				vendor = true;
		} );
		return vendor;
	}

	size_t largestOutputReport( const uint8_t *descriptor, size_t length ) {
		// Output bits per report ID (a deck has a handful at most).
		struct Report { uint8_t id; size_t bits; };
		Report   reports[8] = {};
		size_t   count      = 0;
		uint32_t size = 0, items = 0;
		uint8_t  id   = 0;
		forEachItem( descriptor, length, [&]( uint8_t tag, uint32_t value ) {
			switch( tag ) {
				case kReportSize:  size  = value; break;
				case kReportCount: items = value; break;
				case kReportID:    id    = (uint8_t)value; break;
				case kOutput: {
					Report *report = std::find_if( reports, reports + count, [&]( const Report &r ) { return r.id == id; } );
					if( report == reports + count ) {
						if( count == 8 ) return;
						reports[count++] = { id, 0 };
					}
					report->bits += (size_t)size * items;
					break;
				}
				default:
					break;
			}
		} );
		size_t largest = 0;
		for( size_t i = 0; i < count; i++ )
			largest = std::max( largest, reports[i].bits / 8 );
		return largest;
	}
}
