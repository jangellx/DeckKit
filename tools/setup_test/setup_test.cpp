// Checks components/deckkit_wifi/SetupProtocol.cpp on the Mac: decoding the setup page's form
// fields and answering DNS questions. Both read whatever a phone on the setup network sends.

#include "SetupProtocol.h"

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

static bool field( const std::string &body, const char *key, char *out, size_t size ) {
	return SetupProtocol::formValue( body.data(), body.size(), key, out, size );
}

static void form() {
	char value[65];

	// What the page sends: URLSearchParams encodes spaces as '+' and the rest as %XX.
	std::string body = "ssid=Caf%C3%A9+Wi-Fi&password=p%40ss+w%26rd%3D1";
	CHECK( field( body, "ssid", value, sizeof( value ) ) && strcmp( value, "Caf\xC3\xA9 Wi-Fi" ) == 0 );
	CHECK( field( body, "password", value, sizeof( value ) ) && strcmp( value, "p@ss w&rd=1" ) == 0 );

	// Empty values, missing keys, and keys that only start the same way.
	CHECK( field( "ssid=Home&password=", "password", value, sizeof( value ) ) && value[0] == '\0' );
	CHECK( !field( "ssid=Home", "password", value, sizeof( value ) ) );
	CHECK( !field( "ssidx=Home", "ssid", value, sizeof( value ) ) );
	CHECK( !field( "ssid", "ssid", value, sizeof( value ) ) );
	CHECK( field( "a=1&ssid=Two", "ssid", value, sizeof( value ) ) && strcmp( value, "Two" ) == 0 );

	// Bad escapes, a NUL, and values that don't fit.
	CHECK( !field( "ssid=ab%2", "ssid", value, sizeof( value ) ) );
	CHECK( !field( "ssid=ab%", "ssid", value, sizeof( value ) ) );
	CHECK( !field( "ssid=ab%zz", "ssid", value, sizeof( value ) ) );
	CHECK( !field( "ssid=a%00b", "ssid", value, sizeof( value ) ) );
	CHECK( !field( "ssid=abcd", "ssid", value, 4 ) );
	CHECK( field( "ssid=abc", "ssid", value, 4 ) && strcmp( value, "abc" ) == 0 );
	// A % right before the next field's '&' doesn't borrow from it.
	CHECK( !field( "ssid=ab%4&password=x", "ssid", value, sizeof( value ) ) );
}

// A DNS query for name with the given type, as a phone's resolver sends it.
static std::vector<uint8_t> query( const char *name, uint16_t type, uint8_t flags = 0x01 ) {
	std::vector<uint8_t> q = { 0x12, 0x34, flags, 0x00, 0, 1, 0, 0, 0, 0, 0, 0 };
	for( const char *label = name; *label; ) {
		const char *dot    = strchr( label, '.' );
		size_t      length = dot ? (size_t)( dot - label ) : strlen( label );
		q.push_back( (uint8_t)length );
		q.insert( q.end(), label, label + length );
		label += length + ( dot ? 1 : 0 );
	}
	q.push_back( 0 );
	q.push_back( (uint8_t)( type >> 8 ) );
	q.push_back( (uint8_t)type );
	q.push_back( 0 );
	q.push_back( 1 );   // IN
	return q;
}

static void dns() {
	const uint8_t address[4] = { 192, 168, 4, 1 };
	uint8_t       out[600];

	// An A question: same ID and question, one answer pointing at the setup page.
	auto   a      = query( "captive.apple.com", 1 );
	size_t length = SetupProtocol::dnsAnswer( a.data(), a.size(), address, out, sizeof( out ) );
	CHECK( length == a.size() + 16 );
	CHECK( out[0] == 0x12 && out[1] == 0x34 && out[2] == 0x85 && out[5] == 1 && out[7] == 1 );
	CHECK( memcmp( out + 12, a.data() + 12, a.size() - 12 ) == 0 );
	CHECK( memcmp( out + a.size(), "\xC0\x0C\x00\x01\x00\x01", 6 ) == 0 && memcmp( out + length - 4, address, 4 ) == 0 );

	// AAAA: answered with no records, so the phone falls back to IPv4.
	auto aaaa = query( "example.com", 28 );
	length    = SetupProtocol::dnsAnswer( aaaa.data(), aaaa.size(), address, out, sizeof( out ) );
	CHECK( length == aaaa.size() && out[7] == 0 );

	// Ignored: a response, two questions, a compression pointer, a name past the end, too short.
	auto response = query( "a.b", 1, 0x81 );
	CHECK( SetupProtocol::dnsAnswer( response.data(), response.size(), address, out, sizeof( out ) ) == 0 );
	auto two = query( "a.b", 1 );
	two[5]   = 2;
	CHECK( SetupProtocol::dnsAnswer( two.data(), two.size(), address, out, sizeof( out ) ) == 0 );
	auto pointer = query( "a.b", 1 );
	pointer[12]  = 0xC0;
	CHECK( SetupProtocol::dnsAnswer( pointer.data(), pointer.size(), address, out, sizeof( out ) ) == 0 );
	auto cut = query( "example.com", 1 );
	cut.resize( cut.size() - 3 );
	CHECK( SetupProtocol::dnsAnswer( cut.data(), cut.size(), address, out, sizeof( out ) ) == 0 );
	auto longLabel = query( "a.b", 1 );
	longLabel[12]  = 60;   // a label claiming to run past the end
	CHECK( SetupProtocol::dnsAnswer( longLabel.data(), longLabel.size(), address, out, sizeof( out ) ) == 0 );
	CHECK( SetupProtocol::dnsAnswer( a.data(), 10, address, out, sizeof( out ) ) == 0 );
	// No room for the answer.
	CHECK( SetupProtocol::dnsAnswer( a.data(), a.size(), address, out, a.size() ) == 0 );
}

int main() {
	form();
	dns();
	if( failures ) {
		printf( "Setup: %d check(s) failed\n", failures );
		return 1;
	}
	printf( "Setup: all checks passed\n" );
	return 0;
}
