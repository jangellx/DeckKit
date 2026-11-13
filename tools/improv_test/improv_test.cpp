// Checks components/deckkit_wifi/ImprovProtocol.cpp on the Mac: reading packets out of a noisy
// stream, the checksum, and the bounds on what arrives (it comes from whoever is at the serial
// port), against packets built the way ESP Web Tools builds them.

#include "ImprovProtocol.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int failures = 0;

#define CHECK( condition )                                                        \
	do {                                                                          \
		if( !( condition ) ) {                                                    \
			printf( "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition );         \
			failures++;                                                           \
		}                                                                         \
	} while( 0 )

// A packet as a client builds it, independently of writePacket().
static std::vector<uint8_t> clientPacket( uint8_t type, const std::vector<uint8_t> &data ) {
	std::vector<uint8_t> packet = { 'I', 'M', 'P', 'R', 'O', 'V', 1, type, (uint8_t)data.size() };
	packet.insert( packet.end(), data.begin(), data.end() );
	uint8_t sum = 0;
	for( uint8_t byte : packet )
		sum += byte;
	packet.push_back( sum );
	return packet;
}

// The Send Wi-Fi RPC's data.
static std::vector<uint8_t> sendWiFi( const std::string &ssid, const std::string &password ) {
	std::vector<uint8_t> args = { (uint8_t)ssid.size() };
	args.insert( args.end(), ssid.begin(), ssid.end() );
	args.push_back( (uint8_t)password.size() );
	args.insert( args.end(), password.begin(), password.end() );
	std::vector<uint8_t> data = { Improv::kSendWiFi, (uint8_t)args.size() };
	data.insert( data.end(), args.begin(), args.end() );
	return data;
}

// Feeds bytes and returns the packets that came out valid, and how many were invalid.
struct Parsed {
	std::vector<std::pair<uint8_t, std::vector<uint8_t>>> packets;
	int invalid = 0;
};
static Parsed feed( Improv::Parser &parser, const std::vector<uint8_t> &bytes ) {
	Parsed parsed;
	for( uint8_t byte : bytes ) {
		switch( parser.feed( byte ) ) {
			case Improv::Parser::Result::Packet:
				parsed.packets.push_back( { parser.type(), std::vector<uint8_t>( parser.data(), parser.data() + parser.length() ) } );
				break;
			case Improv::Parser::Result::Invalid:
				parsed.invalid++;
				break;
			default:
				break;
		}
	}
	return parsed;
}

static void parsing() {
	Improv::Parser parser;

	// A packet in the middle of noise, including a false start ("IMPR") and a lone "I".
	auto                 wifi   = clientPacket( Improv::kTypeRPC, sendWiFi( "Home", "secret pass" ) );
	std::vector<uint8_t> stream = { 'h', 'e', 'l', 'l', 'o', '\n', 'I', 'M', 'P', 'R', 'I' };
	stream.insert( stream.end(), wifi.begin(), wifi.end() );
	stream.push_back( '\n' );
	Parsed parsed = feed( parser, stream );
	CHECK( parsed.packets.size() == 1 && parsed.invalid == 0 );
	if( parsed.packets.size() == 1 ) {
		CHECK( parsed.packets[0].first == Improv::kTypeRPC );
		uint8_t        command = 0;
		const uint8_t *args    = nullptr;
		size_t         length  = 0;
		auto          &data    = parsed.packets[0].second;
		CHECK( Improv::readRPC( data.data(), data.size(), command, args, length ) );
		CHECK( command == Improv::kSendWiFi );
		char ssid[33], password[65];
		CHECK( Improv::readWiFi( args, length, ssid, password ) );
		CHECK( strcmp( ssid, "Home" ) == 0 && strcmp( password, "secret pass" ) == 0 );
	}

	// A bad checksum and a wrong version: reported, not returned, and the next packet still reads.
	auto bad = clientPacket( Improv::kTypeRPC, { Improv::kRequestState, 0 } );
	bad.back() ^= 0xFF;
	auto version = clientPacket( Improv::kTypeRPC, { Improv::kRequestState, 0 } );
	version[6]   = 2;
	version.back() += 1;   // keep the checksum right, so only the version is wrong
	auto good    = clientPacket( Improv::kTypeRPC, { Improv::kRequestInfo, 0 } );
	std::vector<uint8_t> mixed;
	for( auto *packet : { &bad, &version, &good } )
		mixed.insert( mixed.end(), packet->begin(), packet->end() );
	parsed = feed( parser, mixed );
	CHECK( parsed.invalid == 2 && parsed.packets.size() == 1 );
	CHECK( parsed.packets.size() == 1 && parsed.packets[0].second[0] == Improv::kRequestInfo );

	// Data at the 255-byte limit.
	std::vector<uint8_t> big( 255, 0x41 );
	parsed = feed( parser, clientPacket( Improv::kTypeRPC, big ) );
	CHECK( parsed.packets.size() == 1 && parsed.packets[0].second.size() == 255 );
}

static void bounds() {
	uint8_t        command;
	const uint8_t *args;
	size_t         length;
	char           ssid[33], password[65];

	// RPC lengths that don't agree.
	const uint8_t shortRPC[] = { Improv::kSendWiFi, 5, 1, 2 };
	CHECK( !Improv::readRPC( shortRPC, sizeof( shortRPC ), command, args, length ) );
	CHECK( !Improv::readRPC( shortRPC, 1, command, args, length ) );

	// Network names and passwords out of range, or lengths past the end.
	auto check = []( const std::vector<uint8_t> &data, char *s, char *p ) {
		return Improv::readWiFi( data.data(), data.size(), s, p );
	};
	CHECK( !check( { 0, 0 }, ssid, password ) );                                   // no name
	std::vector<uint8_t> longName = { 33 };
	longName.insert( longName.end(), 33, 'a' );
	longName.push_back( 0 );
	CHECK( !check( longName, ssid, password ) );                                   // name over 32
	std::vector<uint8_t> longPassword = { 1, 'a', 65 };
	longPassword.insert( longPassword.end(), 65, 'p' );
	CHECK( !check( longPassword, ssid, password ) );                               // password over 64
	CHECK( !check( { 4, 'a', 'b' }, ssid, password ) );                            // name past the end
	CHECK( !check( { 1, 'a', 5, 'x' }, ssid, password ) );                         // password past the end
	CHECK( !check( { 1, 'a', 1, 'x', 'y' }, ssid, password ) );                    // extra bytes

	// The longest of each, and an open network (no password).
	auto full = sendWiFi( std::string( 32, 'n' ), std::string( 64, 'p' ) );
	CHECK( check( std::vector<uint8_t>( full.begin() + 2, full.end() ), ssid, password ) );
	CHECK( strlen( ssid ) == 32 && strlen( password ) == 64 );
	auto open = sendWiFi( "Cafe", "" );
	CHECK( check( std::vector<uint8_t>( open.begin() + 2, open.end() ), ssid, password ) && password[0] == '\0' );
}

static void writing() {
	// A result as ESP Web Tools reads it: command, length of the rest, then length-prefixed strings.
	const char *info[] = { "TetherDeck", "0.1.0", "ESP32-S3", "TetherDeck 9570" };
	uint8_t     data[255];
	size_t      length = Improv::writeResult( data, sizeof( data ), Improv::kRequestInfo, info, 4 );
	CHECK( length == 2 + 1 + 10 + 1 + 5 + 1 + 8 + 1 + 15 );
	CHECK( data[0] == Improv::kRequestInfo && data[1] == length - 2 && data[2] == 10 && memcmp( data + 3, "TetherDeck", 10 ) == 0 );

	// No strings: the empty result that ends a scan.
	CHECK( Improv::writeResult( data, sizeof( data ), Improv::kRequestScan, nullptr, 0 ) == 2 && data[1] == 0 );

	// Strings past 255 bytes in all are left off.
	std::string  long1( 200, 'x' ), long2( 100, 'y' );
	const char  *longOnes[] = { long1.c_str(), long2.c_str() };
	length = Improv::writeResult( data, sizeof( data ), Improv::kRequestScan, longOnes, 2 );
	CHECK( length == 2 + 1 + 200 && data[1] == 201 );

	// A packet, read back by the parser.
	uint8_t packet[Improv::kMaxPacket];
	size_t  size = Improv::writePacket( packet, sizeof( packet ), Improv::kTypeResult, data, length );
	CHECK( size == Improv::kHeaderSize + Improv::kFieldsSize + length + 2 && packet[size - 1] == '\n' );
	Improv::Parser parser;
	Parsed         parsed = feed( parser, std::vector<uint8_t>( packet, packet + size ) );
	CHECK( parsed.packets.size() == 1 && parsed.packets[0].first == Improv::kTypeResult && parsed.packets[0].second.size() == length );

	// The state packet a client waits for.
	uint8_t state = (uint8_t)Improv::State::Provisioned;
	size = Improv::writePacket( packet, sizeof( packet ), Improv::kTypeState, &state, 1 );
	auto expected = clientPacket( Improv::kTypeState, { state } );
	CHECK( size == expected.size() + 1 && memcmp( packet, expected.data(), expected.size() ) == 0 );
	CHECK( Improv::writePacket( packet, 10, Improv::kTypeState, &state, 1 ) == 0 );
}

int main() {
	parsing();
	bounds();
	writing();
	if( failures ) {
		printf( "Improv: %d check(s) failed\n", failures );
		return 1;
	}
	printf( "Improv: all checks passed\n" );
	return 0;
}
