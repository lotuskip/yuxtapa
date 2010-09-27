// Please see LICENSE file.
#include "ent.h"
#include "../common/col_codes.h"

Ent::Ent(const Coords &p, const char sym, const char cp) :
	pos(p), symbol(sym), cpair(cp), madevoid(false)
{
}

Ent::~Ent() {}

void Ent::draw(char *target, const bool lit) const
{
	char cp = cpair;
	if(lit)
		cp += NUM_NORM_COLS;
	*target = cp;
	*(++target) = symbol;
}
