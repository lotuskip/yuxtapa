//Please see LICENSE file.
#include "utfstr.h"
#ifdef DEBUG
#include <iostream>
#endif

namespace
{
using namespace std;

#ifdef DEBUG
const string inv_utf8_warning = "Invalid UTF-8! Expect trouble...";
#endif


char seq_len(char leadch) // determine the length of the sequence starting with leadch
{
	unsigned char lead = static_cast<unsigned char>(leadch);
	if(lead < 0x80)
		return 1;
	if(lead >= 0xC2 && lead <= 0xDF)
		return 2;
	if(lead >= 0xE0 && lead <= 0xEF)
		return 3;
	if(lead >= 0xF0 && lead <= 0xF4)
		return 4;
#ifdef DEBUG
	cerr << inv_utf8_warning << endl;
#endif
	return 1; // invalid lead!
}

bool invalid_cont_byte(const char ch)
{
	unsigned char test = static_cast<unsigned char>(ch);
	return (test < 0x80 || test > 0xBF); // outside the only allowed 2nd, 3rd or 4th byte of a multi-byte seq
}


// move iterator forward by a complete symbol
void advance(string::iterator &i, string &s, short num)
{
	char l;
	for(; num > 0; --num)
	{
		l = seq_len(*i);
		do { ++i; --l; }
		while(l > 0 // sequence is ready,
			&& i != s.end() // premature end of string,
			&& !invalid_cont_byte(*i)); // non-first byte invalid for continuation

#ifdef DEBUG
		if(l > 0) // if was broken for some other reason than this, something is wrong
		{
			cerr << inv_utf8_warning << endl;
			return;
		}
#endif
	}
}

} // end local namespace


char del(string &s, const int n)
{
	string::iterator i = s.begin();
	advance(i, s, n);
	char l = seq_len(*i);
	for(char c = 0; c < l; ++c)
		i = s.erase(i); // NOTE: unsafe if string is improper utf8...
	return l;
}


char ins(string &s, const unsigned int c, const int n)
{
	// First interpret "n symbols" in terms of a string iterator:
	string::iterator i = s.begin();
	advance(i, s, n);
	if(c < 0x80) // single byte
	{
		s.insert(i, static_cast<unsigned char>(c));
		return 1;
	}
	if(c < 0x800) // 2 bytes
	{
		i = s.insert(i, static_cast<unsigned char>((c >> 6) | 0xc0));
		s.insert(++i, static_cast<unsigned char>((c & 0x3f) | 0x80));
		return 2;
	}
	if(c < 0x10000) // 3
	{
		i = s.insert(i, static_cast<unsigned char>((c >> 12) | 0xe0));
		i = s.insert(++i, static_cast<unsigned char>(((c >> 6) & 0x3f) | 0x80));
		s.insert(++i, static_cast<unsigned char>((c & 0x3f) | 0x80));
		return 3;
	}
	// else 4
	i = s.insert(i, static_cast<unsigned char>((c >> 18) | 0xf0));
	i = s.insert(++i, static_cast<unsigned char>(((c >> 12) & 0x3f) | 0x80));
	i = s.insert(++i, static_cast<unsigned char>(((c >> 6) & 0x3f) | 0x80));
	s.insert(++i, static_cast<unsigned char>((c & 0x3f) | 0x80));
	return 4;
}


short num_syms(const string &s)
{
	// we assume the string is valid UTF-8, and can hence simply count the non-continuation bytes
	int n = 0;
	for(string::const_iterator i = s.begin(); i != s.end(); ++i)
	{
		if(static_cast<unsigned char>(*i) < 0x80 || static_cast<unsigned char>(*i) > 0xBF)
			++n;
	}
	return n;
}


string sym_at(const string &s, const int n)
{
	short b = ind_of_sym(s, n);
	return s.substr(b, seq_len(s[b]));
}


void del_syms(std::string &s, const short start_at)
{
	string::iterator i = s.begin();
	advance(i, s, start_at);
	for(char l = seq_len(*i); l > 0; --l)
		i = s.erase(i); // unsafe if string is improper utf8...
}


short ind_of_sym(const string &s, int n)
{
	short b = 0;
	for(; n > 0; --n)
		b += seq_len(s[b]);
	return b;
}


const char* mb_substr(const string &s, const short beg, short len)
{
	short b = ind_of_sym(s, beg);
	short e = 0;
	for(; len > 0; --len)
		e += seq_len(s[b+e]);
	return s.substr(b, e).c_str();
}


short low_bound(const std::string &s, short n)
{
	while(!invalid_cont_byte(s[n-1]))
		--n;
	return n;
}
