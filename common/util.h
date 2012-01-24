//Please see LICENSE file.
#ifndef UTIL_H
#define UTIL_H

#include <string>

// randor0(N) is the equivalent of random()%N, but works with N==1, too (it just
// returns 0 in the case N==1).
short randor0(const short size);

// replacement for boost::algorithm::trim
void trim_str(std::string &s);

// replacements for boost::lexical_cast<string>
// Yes, we could use a template here, but that would generate separate functions
// for char, unsigned char, short, unsigned short, etc. etc. and all we want are
// unsigned short and float.
std::string lex_cast(const unsigned short n);
std::string lex_cast_fl(const float f);

#endif
