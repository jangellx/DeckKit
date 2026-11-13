#include "ImprovProtocol.h"

#include <cstring>

namespace Improv {

Parser::Result Parser::feed( uint8_t byte ) {
	// The header, one byte at a time; a mismatch starts over (on this byte, if it could begin one).
	if( used_ < kHeaderSize ) {
		if( byte == (uint8_t)kHeader[used_] )
			buffer_[used_++] = byte;
		else if( byte == (uint8_t)kHeader[0] )
			buffer_[0] = byte, used_ = 1;
		else
			used_ = 0;
		return Result::Pending;
	}

	buffer_[used_++] = byte;
	if( used_ < kHeaderSize + kFieldsSize )
		return Result::Pending;
	size_t dataLength = buffer_[kHeaderSize + 2];
	if( used_ < kHeaderSize + kFieldsSize + dataLength + 1 )
		return Result::Pending;

	used_ = 0;
	uint8_t sum = 0;
	for( size_t i = 0; i < kHeaderSize + kFieldsSize + dataLength; i++ )
		sum += buffer_[i];
	if( buffer_[kHeaderSize] != kVersion || sum != buffer_[kHeaderSize + kFieldsSize + dataLength] )
		return Result::Invalid;
	return Result::Packet;
}

bool readRPC( const uint8_t *data, size_t length, uint8_t &command, const uint8_t *&args, size_t &argsLength ) {
	if( length < 2 || data[1] != length - 2 )
		return false;
	command    = data[0];
	args       = data + 2;
	argsLength = length - 2;
	return true;
}

bool readWiFi( const uint8_t *args, size_t length, char ssid[33], char password[65] ) {
	if( length < 2 )
		return false;
	size_t ssidLength = args[0];
	if( ssidLength == 0 || ssidLength > 32 || length < 2 + ssidLength )
		return false;
	size_t passwordLength = args[1 + ssidLength];
	if( passwordLength > 64 || length != 2 + ssidLength + passwordLength )
		return false;
	memcpy( ssid, args + 1, ssidLength );
	ssid[ssidLength] = '\0';
	memcpy( password, args + 2 + ssidLength, passwordLength );
	password[passwordLength] = '\0';
	return true;
}

size_t writePacket( uint8_t *out, size_t size, uint8_t type, const uint8_t *data, size_t length ) {
	size_t total = kHeaderSize + kFieldsSize + length + 2;
	if( length > 255 || total > size )
		return 0;
	memcpy( out, kHeader, kHeaderSize );
	out[kHeaderSize]     = kVersion;
	out[kHeaderSize + 1] = type;
	out[kHeaderSize + 2] = (uint8_t)length;
	if( length )
		memcpy( out + kHeaderSize + kFieldsSize, data, length );
	uint8_t sum = 0;
	for( size_t i = 0; i < total - 2; i++ )
		sum += out[i];
	out[total - 2] = sum;
	out[total - 1] = '\n';
	return total;
}

size_t writeResult( uint8_t *out, size_t size, uint8_t command, const char *const *strings, size_t count ) {
	if( size < 2 )
		return 0;
	size_t limit  = size < 257 ? size : 257;   // the rest's length has to fit in a byte
	size_t length = 2;
	for( size_t i = 0; i < count; i++ ) {
		size_t text = strings[i] ? strlen( strings[i] ) : 0;
		if( text > 255 || length + 1 + text > limit )
			break;
		out[length++] = (uint8_t)text;
		if( text )
			memcpy( out + length, strings[i], text );
		length += text;
	}
	out[0] = command;
	out[1] = (uint8_t)( length - 2 );
	return length;
}

}
