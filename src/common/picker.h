// The point picker of quick click: a see-through layer over the whole
// desktop. Shows set.points to start with; on Enter (at least one point)
// stores the points and the desktop size in `set` and returns true; on Esc
// returns false and leaves `set` as it was.
#pragma once

#include "quickclick.h"

bool pickPoints(QuickSet &set);
