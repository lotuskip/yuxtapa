//Please see LICENSE file.
#ifndef COLSTR_H
#define COLSTR_H

#include <string>

// UTF-8 string manipulation. There exist some macros for this, too,
// but we do what we need the way we like to do it.

// del a symbol at index n (index in symbols, not chars!)
char del(std::string &s, const int n);
// insert wchar c at index n (again, in symbols, not chars)
char ins(std::string &s, const unsigned int c, const int n);
// The above return the number of chars added/removed

// returns the number of symbols
short num_syms(const std::string &s);

// returns the nth symbol as a string (possibly a multibyte symbol)
std::string sym_at(const std::string &s, const int n);

// del starting at symbol [start_at] (assumes there are that many symbols)
void del_syms(std::string &s, const short start_at);

// gives the index, in chars, of the nth symbol:
short ind_of_sym(const std::string &s, int n);

// Returns, as a C-string, the substring starting at the begth *symbol*
// and *len* symbols in length
const char* mb_substr(const std::string &s, const short beg, short len);

#endif
