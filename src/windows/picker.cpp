// The Windows point picker comes next (Task 6); until then -quickpick says so.
#include "../common/picker.h"

#include <cstdio>

bool pickPoints(QuickSet &) {
    std::fprintf(stderr, "catberry: the point picker is not available on Windows yet\n");
    return false;
}
