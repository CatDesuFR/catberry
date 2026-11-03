#include "../src/system.h"
#include "test.h"

namespace {
Family family(const char *osRelease) { return detectSystem(osRelease).family; }
} // namespace

void testSystem() {
    CHECK(family("NAME=\"Arch Linux\"\nID=arch\n") == Family::Arch);
    CHECK(family("NAME=\"CachyOS Linux\"\nID=cachyos\nID_LIKE=arch\n") == Family::Arch);
    CHECK(family("NAME=EndeavourOS\nID=\"endeavouros\"\nID_LIKE=\"arch\"\n") == Family::Arch);
    CHECK(family("ID=fedora\nVERSION_ID=44\n") == Family::Fedora);
    CHECK(family("ID=nobara\nID_LIKE=\"rhel centos fedora\"\n") == Family::Fedora);
    CHECK(family("ID=debian\nVERSION_ID=\"13\"\n") == Family::Debian13);
    CHECK(family("ID=debian\nVERSION_ID=\"12\"\n") == Family::Unsupported);
    CHECK(family("ID=ubuntu\nID_LIKE=debian\nVERSION_ID=\"26.04\"\nUBUNTU_CODENAME=resolute\n") == Family::Ubuntu);
    CHECK(family("ID=ubuntu\nID_LIKE=debian\nVERSION_ID=\"26.10\"\n") == Family::Ubuntu);
    CHECK(family("ID=ubuntu\nID_LIKE=debian\nVERSION_ID=\"24.04\"\nUBUNTU_CODENAME=noble\n") == Family::Unsupported);
    CHECK(family("ID=linuxmint\nID_LIKE=\"ubuntu debian\"\nVERSION_ID=\"23\"\nUBUNTU_CODENAME=resolute\n") ==
          Family::Ubuntu);
    CHECK(family("ID=linuxmint\nID_LIKE=\"ubuntu debian\"\nVERSION_ID=\"22\"\nUBUNTU_CODENAME=noble\n") ==
          Family::Unsupported);
    CHECK(family("ID=opensuse-tumbleweed\nID_LIKE=\"opensuse suse\"\n") == Family::Unsupported);
    CHECK(family("") == Family::Unsupported);
    CHECK(detectSystem("PRETTY_NAME=\"Debian GNU/Linux 12 (bookworm)\"\nID=debian\n").prettyName ==
          "Debian GNU/Linux 12 (bookworm)");
    CHECK(std::string(assetSuffix(Family::Arch)) == ".pkg.tar.zst");
    CHECK(std::string(assetSuffix(Family::Fedora)) == ".rpm");
    CHECK(std::string(assetSuffix(Family::Debian13)) == "+debian13_amd64.deb");
    CHECK(std::string(assetSuffix(Family::Ubuntu)) == "+ubuntu26.04_amd64.deb");
    CHECK(assetSuffix(Family::Unsupported) == nullptr);
}
