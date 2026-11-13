#include "SetupProtocol.h"

#include <cstring>

namespace SetupProtocol {

namespace {
	int hexValue( char c ) {
		if( c >= '0' && c <= '9' )
			return c - '0';
		if( c >= 'a' && c <= 'f' )
			return c - 'a' + 10;
		if( c >= 'A' && c <= 'F' )
			return c - 'A' + 10;
		return -1;
	}
}

bool formValue( const char *body, size_t length, const char *key, char *out, size_t size ) {
	if( !body || !key || !out || size == 0 )
		return false;
	size_t keyLength = strlen( key );
	size_t start     = 0;
	while( start <= length ) {
		size_t end = start;
		while( end < length && body[end] != '&' )
			end++;
		// This field: key=value.
		if( end - start > keyLength && memcmp( body + start, key, keyLength ) == 0 && body[start + keyLength] == '=' ) {
			size_t written = 0;
			for( size_t i = start + keyLength + 1; i < end; i++ ) {
				char c = body[i];
				if( c == '+' ) {
					c = ' ';
				} else if( c == '%' ) {
					if( i + 2 >= end )
						return false;   // fewer than two characters after the %
					int high = hexValue( body[i + 1] );
					int low  = hexValue( body[i + 2] );
					if( high < 0 || low < 0 )
						return false;
					c  = (char)( high << 4 | low );
					i += 2;
				}
				if( c == '\0' || written + 1 >= size )
					return false;
				out[written++] = c;
			}
			out[written] = '\0';
			return true;
		}
		if( end == length )
			break;
		start = end + 1;
	}
	return false;
}

size_t dnsAnswer( const uint8_t *query, size_t length, const uint8_t address[4], uint8_t *out, size_t size ) {
	constexpr size_t kHeader    = 12;
	constexpr size_t kMaxLength = 512;   // DNS over UDP
	if( !query || length < kHeader + 5 || length > kMaxLength )
		return 0;
	// A standard query (QR 0, opcode 0) with exactly one question.
	if( ( query[2] & 0xF8 ) != 0 || query[4] != 0 || query[5] != 1 )
		return 0;

	size_t end = kHeader;
	while( end < length && query[end] != 0 ) {
		if( query[end] & 0xC0 )
			return 0;   // no compression pointers in a question
		end += query[end] + 1;
	}
	if( end + 5 > length )
		return 0;
	size_t   questionEnd = end + 5;   // the name's terminator, type and class
	uint16_t type        = (uint16_t)( query[end + 1] << 8 | query[end + 2] );
	bool     answer      = type == 1 || type == 255;   // A or ANY

	// The answer: a pointer to the question's name, A, IN, TTL 60 s, the address.
	const uint8_t record[] = { 0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, address[0], address[1], address[2], address[3] };
	size_t        total    = questionEnd + ( answer ? sizeof( record ) : 0 );
	if( total > size )
		return 0;

	memset( out, 0, kHeader );
	memcpy( out, query, 2 );                             // ID
	out[2] = (uint8_t)( 0x84 | ( query[2] & 0x01 ) );   // response, authoritative, RD copied
	out[5] = 1;                                          // one question
	out[7] = answer ? 1 : 0;                             // and its answer, if any
	memcpy( out + kHeader, query + kHeader, questionEnd - kHeader );
	if( answer )
		memcpy( out + questionEnd, record, sizeof( record ) );
	return total;
}

}
