// Please see LICENSE file.
#ifndef LOG_H
#define LOG_H

#include <string>

void to_log(const std::string &s);
void timed_log(const std::string &s); // same as above, but adds the time

bool check_date_change(const int d); 

#endif
