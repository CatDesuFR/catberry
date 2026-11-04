// "Find the fastest gap": tries a quick-click set at gaps from fast to slow
// in the real app (a game menu, say). The user presses the set's hotkey to
// run each try, Enter when one worked, Esc to stop. A small strip at the top
// of the screen, see-through to clicks, says what to do.
#pragma once

#include "../common/quickclick.h"

class GlobalInput;
class QWidget;

// The gap (µs) the user confirmed, or -1 (stopped, or none worked).
int findFastestGap(const QuickSet &set, GlobalInput *input, QWidget *parent);
