// Please see LICENSE file.
#include "settings.h"
#include "../common/utfstr.h"
#include "../common/confdir.h"
#include "../common/constants.h"
#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <cstdlib>
#include <algorithm>
#include <map>
#include <boost/algorithm/string/trim.hpp>

namespace
{
using namespace std;

const char MIN_ANIM_LEN = 2;
const char MAX_ANIM_LEN = 15;

const string default_key_bds = "896321475 CscTXuwt+-lQo"; // cf. enum e_Key_binding
const string default_nick = "player";
const string default_anim = "-\\|/";

const string str_ctrl_error = "Config error: cannot use ^? where ? is not a letter!";

string nick = default_nick;
string anim = default_anim;
string key_bindings = default_key_bds;
string serverip = "";
string qshout[MAX_QUICK_SHOUTS] = { "", "", "", "", "", "", "", "", "", "", "", "" };
string ouch_str = "";
bool ouching = false;

string confdir_str = "";

map<string,string> aliases;
}

void Config::read_config(const string servername)
{
	// figure out config  directory of current user:
	string s;
	confdir_str = s = get_conf_dir();
	s += "client.conf";

	ifstream file(s.c_str());
	string keyw;
	char ch;
	string first_server = "";
	while(file)
	{
		// This is very crude parsing; if the file is not right it probably is read all wrong
		getline(file, s);
		if(s.empty() || s[0] == '#')
			continue;
		stringstream ss(s, ios_base::in);
		ss >> keyw;
		if(keyw == "nick")
		{
			keyw = s;
			keyw.erase(0, keyw.find(' '));
			boost::algorithm::trim(keyw);
			if(!keyw.empty())
			{
				if(num_syms(keyw) > MAX_NICK_LEN)
				{
					del_syms(keyw, MAX_NICK_LEN);
					cerr << "Warning: nick given in config file is too long.";
				}
				nick = keyw;
				continue;
			}
		}
		else if(keyw == "server")
		{
			ss >> keyw; // server name
			string tmp = "";
			ss >> tmp; // server IP
			if(!keyw.empty() && !tmp.empty())
			{
				/* if a server was given, we only match against it;
				 * otherwise pick first server */
				if(first_server.empty())
					first_server = tmp;
				if(serverip.empty() && servername == keyw)
					serverip = tmp;
				continue;
			}
		}
		else if(keyw == "key")
		{
			keyw = ss.str();
			if(keyw.size() < 6) // must be at least "key XY"
			{
				cerr << "Faulty line in config: \'" << keyw << "\'." << endl;
				continue;
			}
			keyw = keyw.substr(4); // drop "key "; leaving keyw.size() >= 2
			// Check for ^[x]: ("^XA", for instance)
			if(keyw[0] == '^' && keyw.size() > 2)
			{
				if(!isalpha(keyw[1]))
				{
					cerr << str_ctrl_error << endl;
					continue;
				} // else:
				ch = tolower(keyw[1]) - 96; // 96 == 'a'-1
				keyw.erase(0,1); // remove '^', forcing the replacement to be at index 1
			}
			else
				ch = keyw[0];
			e_Key_binding kb = convert_key(ch);
			if(kb != MAX_KEY_BINDING)
			{
				// The replacement could also be ^[x]:
				if(keyw[1] == '^' && keyw.size() > 2)
				{
					if(!isalpha(keyw[2]))
					{
						cerr << str_ctrl_error << endl;
						continue;
					} // else:
					ch = tolower(keyw[2]) - 96;
				}
				else if((ch = keyw[1]) == '?')
				{
					// '?' is reserved for help view!
					cerr << "Config error: key '?' is reserved!" << endl;
					continue;
				}
				key_bindings[kb] = ch;
			}
			else
				cerr << "Unknown original keybinding \'" << ch << "\' in config." << endl;
			continue;
		}
		else if(keyw == "anim")
		{
			keyw = s;
			keyw.erase(0, keyw.find(' '));
			if(!keyw.empty())
				keyw.erase(0, 1);
			if(num_syms(keyw) < MIN_ANIM_LEN || num_syms(keyw) > MAX_ANIM_LEN)
				cerr << "Warning, an animation of invalid length ignored." << endl;
			else
				anim = keyw;
			continue;
		}
		else if(keyw == "alias")
		{
			ss >> keyw; // alias string
			string tmps = s;
			tmps.erase(0, tmps.find(' ', 6)); // get rid of "alias [key]"
			if(!tmps.empty())
			{
				tmps.erase(0,1);
				aliases[keyw] = tmps;
				continue;
			}
		}
		else if(keyw == "qshout")
		{
			short sh;
			ss >> sh;
			if(sh < 1 || sh > MAX_QUICK_SHOUTS)
				cerr << "Invalid quick shout index: " << sh << endl;
			else
			{
				qshout[sh-1] = ss.str();
				qshout[sh-1].erase(0, 8 + int(sh > 9)); // 8 if sh is single-digit, 9 if not
				if(qshout[sh-1].empty())
				{
					cerr << "Config warning: empty \'qshout " << sh << "\'?" << endl;
					continue;
				}
				qshout[sh-1].erase(0,1); // remove ' '
				if(num_syms(qshout[sh-1]) >= MSG_WIN_X)
				{
					cerr << "Quick shout " << sh << " is too long!" << endl;
					del_syms(qshout[sh-1], MSG_WIN_X);
				}
			}
			continue;
		}
		else if(keyw == "ouch")
		{
			ouch_str = ss.str();
			if(ouch_str.size() < 5)
			{
				cerr << "Config warning: \'ouch\' without content?" << endl;
				continue;
			}
			ouch_str.erase(0, 5); // erase "ouch "
			if(num_syms(ouch_str) >= MSG_WIN_X)
			{
				cerr << "The ouch string in the config is too long!" << endl;
				del_syms(ouch_str, MSG_WIN_X);
			}
			continue;
		}
		else if(keyw == "ouch_on")
		{
			ss >> ouching;
			continue;
		}
		// if here, not all is okay:
		cerr << "Warning, could not parse the following line in config file:"
			<< endl << "\"" << s << "\"" << endl;
	}

	// everything extracted (or failed altogether), check:
	if(serverip.empty()) // no match for desired server
	{
		if(!servername.empty()) // some server *was* requested
			serverip = servername; // try to connect to it "as-is"
		else if(first_server.empty()) // no server given in file, either?
		{
			cerr << "Warning, no server given in config, defaulting to localhost!" << endl;
			serverip = "127.0.0.1:12360";
		}
		else // No server requested; use first one found in config
			serverip = first_server;
	}
	// check for key-binding conflicts:
	keyw = key_bindings; // tmp copy
	sort(keyw.begin(), keyw.end());
	unique(keyw.begin(), keyw.end());
	if(keyw.size() < key_bindings.size())
	{
		cerr << "Config resulted in key binding conflicts; reverting to original keys." << endl;
		key_bindings = default_key_bds;
	}
	// check that ouching is off if ouch string is empty:
	if(ouch_str.empty() && ouching)
	{
		ouching = false;
		cerr << "Config error: the ouch feature requires you to define a non-empty message!" << endl;
	}
}


string &Config::get_server_ip() { return serverip; }
string &Config::get_nick() { return nick; }
string &Config::get_anim() { return anim; }
string &Config::quick_shout(const char index) { return qshout[index]; }
string &Config::get_ouch() { return ouch_str; }
bool Config::do_ouch() { return ouching; }
string &Config::configdir() { return confdir_str; }


e_Key_binding Config::convert_key(const char key)
{
	string::size_type index = key_bindings.find(key);
	if(index == string::npos) // no such key mapped
		return MAX_KEY_BINDING;
	else return e_Key_binding(index);
}


void Config::do_aliasing(string &s)
{
	map<string,string>::iterator it = aliases.find(s);
	if(it != aliases.end())
		s = it->second;
}


bool Config::toggle_ouch()
{
	if(ouch_str.empty())
		return false; // cannot use the feature without a string!
	return (ouching = !ouching);
}

