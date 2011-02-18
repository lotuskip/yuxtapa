//Please see LICENSE file.
#ifndef COMMON_CONSTANTS_H
#define COMMON_CONSTANTS_H

#define PACKAGE "yux+apa"
#define VERSION "4a"

const unsigned char VIEWSIZE = 23;
const unsigned char MSG_WIN_X = 47;
const unsigned char MSG_WIN_Y = 15;

// The architecture of yuxtapa is such that most changes take place
// independently on the client and server sides, without modifications
// in the interaction between the two. Hence, in addition to the version
// of the game, we have an "interaction version". For a server and a client
// to be compatible, only their interaction versions need to match.
const unsigned short INTR_VERSION = 1;

const unsigned short BUFFER_SIZE = 2048; /* this is plenty for most purposes,
	but can't be much lower for some */

const unsigned char MAX_NICK_LEN = 10;

const unsigned short MAXIMUM_STR_LEN = 350; /* In _valid_ communication,
	the longest strings sent are the chat messages. This should be plenty
	for those, even if it's multibyte UTF-8. */

#endif
