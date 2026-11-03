// Quick click: a saved set of screen points, clicked in order as fast as
// the desktop reliably takes it, each time the set's hotkey is pressed.
#pragma once

#include "keys.h"

#include <functional>
#include <string>
#include <vector>

enum class ClickType { Left, Right, Middle, Double };

struct QuickPoint {
    int x, y; // desktop pixels (all screens together, top-left is 0,0)
    ClickType type;
};

// Below 100 µs between points the desktop drops clicks (measured on KDE).
const int kMinGapUs = 100;
const int kMaxGapUs = 1000000;

struct QuickSet {
    std::vector<QuickPoint> points;
    int desktopW = 0, desktopH = 0; // the desktop size the points were picked on
    int hotkey = -1;
    int gapUs = kMinGapUs;
};

const char *clickTypeName(ClickType type); // "left", "right", "middle", "double"
bool parseClickType(const std::string &text, ClickType &type);

// In configDir()/quickclicks/<name>.conf; names as for hotkey configs.
bool writeQuickSet(const std::string &name, const QuickSet &set);
bool readQuickSet(const std::string &name, QuickSet &set, std::string &error);
std::vector<std::string> quickSetNames();
bool deleteQuickSet(const std::string &name);
// The name of a loaded set's pid file ("quick.<name>": never a config name).
std::string quickPidName(const std::string &name);

// A point picked on a desktop of fromW x fromH, on one of toW x toH.
QuickPoint scaledPoint(const QuickPoint &p, int fromW, int fromH, int toW, int toH);

class KeyWatcher;
class Output;
// Runs a loaded set: waits for the hotkey and clicks the points, until asked
// to stop. `ready` is called once: "" when setup worked, otherwise why not.
int serveQuickSet(const std::string &name, const QuickSet &set,
                  const std::function<void(const std::string &error)> &ready);
// One run of the points (pressing the hotkey again on `keys` stops it).
// Positions go out as fractions of the desktop they were picked on, and the
// absolute pointer maps fractions onto the desktop as it is now: a changed
// resolution scales them by itself.
void runQuickSet(const QuickSet &set, Output &output, KeyWatcher &keys);
