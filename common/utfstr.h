//Please see LICENSE file.
#ifndef COLSTR_H
#define COLSTR_H

#include <string>

// UTF-8 string manipulation. There exist some macros for this, too,
// but we do what we need the way we like to do it.

// del the symbol at index n (index in symbols, not chars!)
void del(std::string &s, const int n);
// insert wchar c at index n (again, in symbols, not chars)
void ins(std::string &s, const unsigned int c, const int n);

// returns the number of symbols
short num_syms(const std::string &s);

// returns the nth symbol as a string (possibly a multibyte symbol)
std::string sym_at(const std::string &s, const int n);

// del starting at symbol [start_at] (assumes there are that many symbols)
void del_syms(std::string &s, const short start_at);

// gives the index, in chars, of the nth symbol:
short ind_of_sym(const std::string &s, int n);

// Returns the substring starting at the 'beg'th symbol
// and 'len' symbols in length
std::string mb_substr(const std::string &s, const short beg, short len);

// If "s.substr(0,n)" would cut the string in the middle of a multi-byte
// symbol, returns the last index m<=n so that "s.substr(0,m)" is valid utf8.
// Assumes that s[n-1] is safe and that the whole 's' is valid utf8.
short low_bound(const std::string &s, const short n);

#endif
