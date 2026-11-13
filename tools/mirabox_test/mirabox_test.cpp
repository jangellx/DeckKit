// Checks components/deckkit_deck/MiraboxProtocol.cpp on the Mac: the commands' bytes, how
// images are cut into pieces (with the K1 Pro's padding), reading key reports, the model
// table, and the report descriptor checks. There's no Mirabox deck here to try them on, and a
// wrong byte in any of these fails silently (a black panel, keys that never register), so
// the expected values are DeckBridge's, which drives these decks on hardware. Run with
// run.sh; it prints each failure and exits non-zero if there are any.
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>

#include "MiraboxProtocol.h"

namespace {
	int failures = 0;

	void check( bool condition, const char *what ) {
		if( !condition ) {
			printf( "FAIL: %s\n", what );
			failures++;
		}
	}

	bool startsWith( const std::vector<uint8_t> &data, std::vector<uint8_t> prefix ) {
		return data.size() >= prefix.size() && memcmp( data.data(), prefix.data(), prefix.size() ) == 0;
	}

	bool zeroFrom( const std::vector<uint8_t> &data, size_t at ) {
		for( size_t i = at; i < data.size(); i++ )
			if( data[i] != 0 ) return false;
		return true;
	}
}

int main() {
	using namespace Mirabox;

	// Commands: "CRT\0\0", the name, the arguments, zeros to the packet size.
	std::vector<uint8_t> packet( 1024 );
	command( packet.data(), packet.size(), "CONNECT" );
	check( startsWith( packet, { 'C', 'R', 'T', 0, 0, 'C', 'O', 'N', 'N', 'E', 'C', 'T' } ) && zeroFrom( packet, 12 ), "CONNECT" );
	brightness( packet.data(), packet.size(), 60 );
	check( startsWith( packet, { 'C', 'R', 'T', 0, 0, 'L', 'I', 'G', 0, 0, 60 } ) && zeroFrom( packet, 11 ), "LIG 60" );
	brightness( packet.data(), packet.size(), 250 );
	check( packet[10] == 100, "LIG clamped to 100" );
	clear( packet.data(), packet.size(), 0xFF );
	check( startsWith( packet, { 'C', 'R', 'T', 0, 0, 'C', 'L', 'E', 0, 0, 0, 0xFF } ), "CLE all" );
	imageHeader( packet.data(), packet.size(), 0x1234, 11 );
	check( startsWith( packet, { 'C', 'R', 'T', 0, 0, 'B', 'A', 'T', 0, 0, 0x12, 0x34, 11 } ) && zeroFrom( packet, 13 ), "BAT: length big-endian, then the ID" );
	std::vector<uint8_t> small( 512, 0xEE );
	command( small.data(), small.size(), "STP" );
	check( startsWith( small, { 'C', 'R', 'T', 0, 0, 'S', 'T', 'P' } ) && zeroFrom( small, 8 ), "STP in a 512-byte packet" );

	// Images in pieces, unpadded: 2500 bytes in 1024-byte pieces is 3, the last zero-filled.
	std::vector<uint8_t> image( 2500 );
	for( size_t i = 0; i < image.size(); i++ ) image[i] = (uint8_t)( i % 251 + 1 );
	check( sentLength( 2500, 1024, false ) == 2500, "unpadded length" );
	check( chunkCount( 2500, 1024, false ) == 3, "unpadded pieces" );
	chunk( image.data(), image.size(), 1024, false, 2, packet.data() );
	check( memcmp( packet.data(), image.data() + 2048, 452 ) == 0 && zeroFrom( packet, 452 ), "last unpadded piece" );

	// Padded (the K1 Pro), as DeckBridge's padChunkBoundaries: 1023 image bytes and a zero per
	// full piece, so 2500 bytes go out as 2502.
	check( sentLength( 2500, 1024, true ) == 2502, "padded length" );
	check( chunkCount( 2500, 1024, true ) == 3, "padded pieces" );
	chunk( image.data(), image.size(), 1024, true, 1, packet.data() );
	check( memcmp( packet.data(), image.data() + 1023, 1023 ) == 0 && packet[1023] == 0, "second padded piece ends in the sacrificial zero" );
	chunk( image.data(), image.size(), 1024, true, 2, packet.data() );
	check( memcmp( packet.data(), image.data() + 2046, 454 ) == 0 && zeroFrom( packet, 454 ), "last padded piece" );
	check( sentLength( 2046, 1024, true ) == 2048 && chunkCount( 2046, 1024, true ) == 2, "exactly two padded pieces" );

	// Key reports: "ACK", the code at byte 9 and the state at 10, after the report ID if any.
	uint8_t report[64] = { 'A', 'C', 'K', 0, 0, 'O', 'K', 0, 0, 7, 1 };
	uint8_t code = 0, state = 0;
	check( parseKey( report, sizeof( report ), 0, code, state ) && code == 7 && state == 1, "key 7 down" );
	check( !parseKey( report, 10, 0, code, state ), "a report cut short isn't a key" );
	uint8_t numbered[64] = { 0x04, 'A', 'C', 'K', 0, 0, 'O', 'K', 0, 0, 3, 0 };
	check( parseKey( numbered, sizeof( numbered ), 0x04, code, state ) && code == 3 && state == 0, "key 3 up, with report ID 4" );
	check( !parseKey( report, sizeof( report ), 0x04, code, state ), "report ID missing" );

	// The model table.
	std::set<std::pair<uint16_t, uint16_t>> ids;
	for( uint16_t vid : { 0x6603, 0x3142, 0x1200, 0x0300, 0x5548, 0x0B00, 0x0C00, 0x0A00, 0x0500, 0x0200, 0x6602, 0x1500 } ) {
		for( uint32_t pid = 0; pid <= 0xFFFF; pid++ ) {
			const Model *model = find( vid, (uint16_t)pid );
			if( !model ) continue;
			check( ids.insert( { vid, (uint16_t)pid } ).second, "each vendor and product ID once" );
			check( strlen( model->name ) < 40, "name fits StreamDeck::Info::model" );
			check( model->keyCount() <= kMaxKeys, "keys fit the tables" );
			check( model->packet == 512 || model->packet == 1024, "packet is 512 or 1024" );
			std::set<uint8_t> images, inputs;
			for( uint8_t key = 0; key < model->keyCount(); key++ ) {
				images.insert( model->imageIDs[key] );
				inputs.insert( model->inputCodes[key] );
				check( keyForInput( *model, model->inputCodes[key] ) == key, "input codes map back to their key" );
			}
			check( images.size() == model->keyCount() && !images.count( 0 ), "image IDs distinct and set" );
			check( inputs.size() == model->keyCount() && !inputs.count( 0 ), "input codes distinct and set" );
		}
	}
	check( ids.size() == 33, "33 models" );

	// DeckBridge's hardware-verified layouts.
	const Model *v3 = find( 0x6603, 0x1005 );
	check( v3 && v3->imageIDs[0] == 11 && v3->imageIDs[14] == 5 && v3->inputCodes[0] == 1, "293V3: images from the bottom row, inputs row by row" );
	const Model *s = find( 0x5548, 0x6670 );
	check( s && s->rows == 3 && s->cols == 6 && !s->reportsKeyUp && s->packet == 512, "293S: 3 × 6, presses only, 512-byte reports" );
	check( s && s->imageIDs[0] == 13 && s->imageIDs[4] == 1 && s->imageIDs[16] == 3, "293S: down each column from the fifth leftward" );
	const Model *k1 = find( 0x6603, 0x1015 );
	check( k1 && k1->reportID == 4 && k1->padsChunks && k1->keepaliveMs == 2000, "K1 Pro: report ID 4, padded, 2 s keepalive" );
	check( k1 && keyForInput( *k1, 1 ) == 2 && keyForInput( *k1, 6 ) == 3, "K1 Pro: input 1 is the top right key, 6 the bottom left" );
	const Model *akp05 = find( 0x0300, 0x3004 );
	check( akp05 && akp05->family == Family::Akp05 && akp05->imageIDs[0] == 11 && akp05->imageIDs[9] == 10, "AKP05E: 11 to 15 on top, 6 to 10 below" );
	check( akp05 && keyForInput( *akp05, 0x37 ) == -1, "AKP05E: a dial press isn't a key" );
	check( !find( 0x0FD9, 0x0080 ), "an Elgato deck isn't in the table" );

	// Report descriptors: the vendor interface, and its output report size.
	const uint8_t vendor[] = {
		0x06, 0xA0, 0xFF,          // Usage Page (0xFFA0)
		0x09, 0x01,                // Usage (1)
		0xA1, 0x01,                // Collection (Application)
		0x09, 0x02, 0x75, 0x08,    //   Usage, Report Size (8)
		0x96, 0x00, 0x02,          //   Report Count (512)
		0x81, 0x02,                //   Input
		0x09, 0x03,
		0x96, 0x00, 0x04,          //   Report Count (1024)
		0x91, 0x02,                //   Output
		0xC0,
	};
	check( isVendorInterface( vendor, sizeof( vendor ) ), "vendor usage page" );
	check( largestOutputReport( vendor, sizeof( vendor ) ) == 1024, "1024-byte output report" );
	const uint8_t keyboard[] = { 0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x75, 0x01, 0x95, 0x05, 0x91, 0x02, 0x75, 0x03, 0x95, 0x01, 0x91, 0x01, 0xC0 };
	check( !isVendorInterface( keyboard, sizeof( keyboard ) ), "a keyboard isn't the deck" );
	check( largestOutputReport( keyboard, sizeof( keyboard ) ) == 1, "keyboard LEDs: one byte" );
	const uint8_t numberedOut[] = { 0x06, 0xA0, 0xFF, 0x85, 0x04, 0x75, 0x08, 0x96, 0x00, 0x04, 0x91, 0x02, 0x85, 0x05, 0x95, 0x10, 0x91, 0x02 };
	check( largestOutputReport( numberedOut, sizeof( numberedOut ) ) == 1024, "largest of two numbered output reports" );
	const uint8_t truncated[] = { 0x06, 0xA0 };
	check( !isVendorInterface( truncated, sizeof( truncated ) ) && largestOutputReport( truncated, sizeof( truncated ) ) == 0, "a cut-off descriptor" );

	if( failures == 0 )
		printf( "All Mirabox protocol checks passed.\n" );
	return failures == 0 ? 0 : 1;
}
