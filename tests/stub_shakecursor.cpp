// The unit tests build without Qt: Shake Cursor is not paused there.
#include "../src/common/shakecursor.h"

ShakeCursorPause::ShakeCursorPause() {}
ShakeCursorPause::~ShakeCursorPause() {}
void ShakeCursorPause::pause() {}
void ShakeCursorPause::resume() {}
