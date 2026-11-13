// What SetupPage reads from phones on its network, in plain C++ so tools/setup_test builds
// exactly this on the Mac: form fields from a POST body, and DNS questions it answers (every
// name is the setup page's address, which is what makes phones open it).
#pragma once

#include <cstddef>
#include <cstdint>

namespace SetupProtocol {
	// The value of `key` in an application/x-www-form-urlencoded body (key=value&…), decoded:
	// '+' is a space, %XX a byte. False if the key isn't there, the value has a bad escape or
	// a NUL, or it doesn't fit in out (size counts the NUL).
	bool formValue( const char *body, size_t length, const char *key, char *out, size_t size );

	// The answer to a DNS query: the same ID and question, and for an A (or ANY) question one
	// record giving address (4 bytes, network order) with a 60-second TTL; other types get no
	// records. Returns its length in out, or 0 if the query isn't a standard query with one
	// question that fits (it's ignored then).
	size_t dnsAnswer( const uint8_t *query, size_t length, const uint8_t address[4], uint8_t *out, size_t size );
}
