//Please see LICENSE file.
#include "netutils.h"
#include <arpa/inet.h>
#include <algorithm>
#include <zlib.h>

namespace
{
using namespace std;
using boost::array;

}


SerialBuffer::SerialBuffer() : num(0)
{
	pos = arr.begin();
}


void SerialBuffer::clear()
{
	// no need to actually touch the data
	pos = arr.begin();
	num = 0;
}

void SerialBuffer::add(const unsigned char ch)
{
	*pos = ch;
	++pos;
	++num;
}

void SerialBuffer::add(const unsigned short sh)
{
	// make sure we write big-endian:
	unsigned short conv = htons(sh);
	// break it up to chars (this, IMO, is clean if inefficient):
	unsigned char b = conv & 0xFF;
	add(b);
	add((b = (conv >> 8) & 0xFF));
}

void SerialBuffer::add(const string &str)
{
	copy(str.begin(), str.end(), pos);
	pos += str.size();
	*pos = '\0';
	++pos;
	num += str.size()+1;
}

void SerialBuffer::write_compressed(char *buffer, const unsigned short len)
{
	uLongf res_size = BUFFER_SIZE; // actually this is not how much room there is, but
	// we'll never run out of room here anyway.

	// leave room in the beginning for the size short
	compress((Bytef*)(pos+2), &res_size, (Bytef*)buffer, len);
	add((unsigned short)res_size); //NOTE: pos has not been moved, so this writes right before the compressed data
	pos += res_size;
	num += res_size;
}


unsigned short SerialBuffer::read_sh()
{
	unsigned short sh = *(reinterpret_cast<unsigned short*>(pos));
	pos += 2;
	return ntohs(sh);
}

unsigned char SerialBuffer::read_ch()
{
	unsigned char ch = *(reinterpret_cast<unsigned char*>(pos));
	++pos;
	return ch;
}

void SerialBuffer::read_str(string &target)
{
	// If we trusted the other end blindly, we could just do:
#if 0
	target.assign(pos);
	pos += target.size()+1;
#endif
	/* But alas, there are mean people out there, who would abuse such a hole.
	 * Hence we enfore a maximum length of a string and read carefully
	 * one byte at a time (there might not be any '\0' in the buffer to end the
	 * string!) */
	target.clear();
	// Read (A) at most the maximum string length, (B) at most as much as there
	// is buffer left!
	short bs_read = 0;
	while(bs_read < MAXIMUM_STR_LEN && pos != arr.end())
	{
		if(*pos == '\0')
		{
			++pos;
			break;
		}
		target += *pos;
		++pos;
		++bs_read;
	}
}

bool SerialBuffer::read_compressed(char *buffer)
{
	// read the size of the block:
	unsigned short sh = read_sh();
	uLongf res_size = BUFFER_SIZE;
	sh = uncompress((Bytef*)buffer, &res_size, (Bytef*)pos, sh);
	if(sh == Z_BUF_ERROR || sh == Z_DATA_ERROR)
		return true;
	// Z_MEM_ERROR (insufficient memory) is highly unlikely.
	pos += res_size;
	return false;
}

char* SerialBuffer::getw()
{
	// calling get on receive implies there will be no more read calls on the same data:
	clear();
	return arr.c_array();
}

void SerialBuffer::set_amount(const short n)
{
	pos = arr.begin() + (num = n);
}


/* End notes:
 * The security issues of the above are mainly at the server end (clients
 * _probably_ don't need to worry about a "malignant server").
 *
 * The call to recvfrom(...) already enforces that the entire message won't
 * exceed the buffer we have for it. This leaves as the only problem
 * the possiblity that the data in the buffer is structured so that we
 * accidentally read past it (eg. a string is missing the ending '\0').
 *
 * We have enforced a maximum length of 250 bytes per string, and in
 * uncompressed packets there are at most a few strings, certainly not
 * too many to fill up the 2048 bytes we have from BUFFER_SIZE. This
 * _seems_ to me like it's sufficient and leaves the client with no
 * conceivable way of causing harm to the server by sending improper
 * packets.
 *
 * The actual data that we read should, when necessary, be checked
 * by the caller, of course. (When talking about numeric values.)
 */

