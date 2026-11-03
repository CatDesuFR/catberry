#include "../src/common/platform.h"
#include "../src/common/quickclick.h"
#include "test.h"

#include <fstream>

void testQuickClick() {
    QuickSet set;
    set.desktopW = 1920;
    set.desktopH = 1080;
    set.hotkey = parseKey("F7");
    set.gapUs = 250;
    set.points = {{192, 108, ClickType::Left}, {960, 540, ClickType::Double}, {5, 6, ClickType::Right}};
    CHECK(writeQuickSet("qa", set));
    QuickSet back;
    std::string error;
    CHECK(readQuickSet("qa", back, error));
    CHECK(back.desktopW == 1920 && back.desktopH == 1080 && back.hotkey == parseKey("F7") && back.gapUs == 250);
    CHECK(back.points.size() == 3 && back.points[1].x == 960 && back.points[1].type == ClickType::Double &&
          back.points[2].type == ClickType::Right);

    // A gap outside the range is brought into it.
    set.gapUs = 5;
    CHECK(writeQuickSet("qb", set));
    CHECK(readQuickSet("qb", back, error) && back.gapUs == kMinGapUs);

    // A broken point line is refused, naming it.
    {
        std::ofstream out(configDir() / "quickclicks" / "qbad.conf");
        out << "desktop=1920x1080\nhotkey=F7\ngap_us=100\npoint=10,20,sideways\n";
    }
    CHECK(!readQuickSet("qbad", back, error) && error.find("point=10,20,sideways") != std::string::npos);

    CHECK((quickSetNames() == std::vector<std::string>{"qa", "qb", "qbad"}));
    CHECK(deleteQuickSet("qbad") && !deleteQuickSet("qbad"));
    CHECK(!writeQuickSet("bad name", set));

    QuickPoint p = scaledPoint({960, 540, ClickType::Left}, 1920, 1080, 3840, 2160);
    CHECK(p.x == 1920 && p.y == 1080 && p.type == ClickType::Left);
    p = scaledPoint({960, 540, ClickType::Middle}, 1920, 1080, 1920, 1080);
    CHECK(p.x == 960 && p.y == 540);
    p = scaledPoint({960, 540, ClickType::Middle}, 0, 0, 1920, 1080);
    CHECK(p.x == 960 && p.y == 540);

    CHECK(quickPidName("x") == "quick.x");
    ClickType t;
    CHECK(parseClickType("double", t) && t == ClickType::Double && std::string(clickTypeName(t)) == "double");
    CHECK(!parseClickType("triple", t));
}
