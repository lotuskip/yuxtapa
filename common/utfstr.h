//Please see LICENSE file.
#ifndef COLSTR_H
#define COLSTR_H

#include <string>

// UTF-8 string manipulation. There exist some macros for this, too,
// but we do what we need the way we like to do it.

// del a symbol at index n (index in symbols, not chars!)
void del(std::string &s, int n);
// insert wchar c at index n (again, in symbols, not chars)
void ins(std::string &s, const unsigned int c, int n);

// returns the number of symbols
short num_syms(const std::string &s);

// returns the nth symbol as a string (possibly a multibyte symbol)
std::string sym_at(const std::string &s, int n);

// del starting at symbol [start_at] (assumes there are that many symbols)
void del_syms(std::string &s, const short start_at);

#endif
