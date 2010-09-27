// Please see LICENSE file.
#ifndef ENT_H
#define ENT_H

#include "../common/coords.h"

// Entity, fully abstract.
class Ent
{
public:
	Ent(const Coords &p, const char sym, const char cp);
	virtual ~Ent();
	virtual void update() = 0;

	void draw(char *target, const bool lit) const;
	void set_col(const char ncp) { cpair = ncp; }

	bool isvoid() const { return madevoid; }
	void makevoid() { madevoid = true; }

	Coords getpos() const { return pos; }
	void setpos(const Coords &c) { pos = c; }

protected:
	Coords pos;
	char symbol;
	char cpair;
	bool madevoid;
};

#endif
