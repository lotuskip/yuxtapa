//Please see LICENSE file.
#ifndef COMMON_CONSTANTS_H
#define COMMON_CONSTANTS_H

const unsigned char VIEWSIZE = 23;
const unsigned char MSG_WIN_X = 47;
const unsigned char MSG_WIN_Y = 15;

// The architecture of yuxtapa is such that most changes take place in
// the server code. Hence we don't necessarily require the client
// version to *match* the server version, but instead keep a track
// of backward-compatability, so that players don't need to update
// their clients unnecessarily every time the server is updated.
const unsigned short GAME_VERSION = 1; // The current version
// The oldest version (of the client) this version is still compatible with:
const unsigned short REQ_CLIENT_VERSION = 1;

const unsigned short BUFFER_SIZE = 2048; /* this is plenty for most purposes,
	but can't be much lower for some */

const unsigned char MAX_NICK_LEN = 10;

const unsigned short MAXIMUM_STR_LEN = 250; /* In _valid_ communication,
	the longest strings sent are the chat messages. This should be plenty
	for those, even if it's multibyte UTF-8. */

#endif
