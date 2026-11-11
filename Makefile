# catberry-cbmm: installs Catberry's Mod Menu into Geometry Dash.
# The compiled mod (geode/catdesu.cats-mod-menu.geode) is built into the program.
CXX ?= g++
WINCXX ?= x86_64-w64-mingw32-g++
CXXFLAGS ?= -O2 -Wall -Wextra
PREFIX ?= $(HOME)/.local

PROGRAM = catberry-cbmm
VERSION := $(shell sed -n 's/^pkgver=//p' packaging/arch/PKGBUILD)
GEODE = $(CURDIR)/geode/catdesu.cats-mod-menu.geode
DEFS = -DCBMM_VERSION='"$(VERSION)"' -DCBMM_GEODE='"$(GEODE)"'
WINDOWS_DIST = dist/$(PROGRAM)-windows

$(PROGRAM): src/main.cpp $(GEODE) packaging/arch/PKGBUILD
	$(CXX) $(CXXFLAGS) -std=c++17 $(DEFS) src/main.cpp -o $@

install: $(PROGRAM)
	install -Dm755 $(PROGRAM) $(DESTDIR)$(PREFIX)/bin/$(PROGRAM)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(PROGRAM)

# Windows build, cross-compiled with MinGW-w64: a folder with the exe,
# install.bat and uninstall.bat, plus a zip of it when zip is available.
windows: $(WINDOWS_DIST)/$(PROGRAM).exe $(WINDOWS_DIST)/install.bat $(WINDOWS_DIST)/uninstall.bat
	@if command -v zip >/dev/null; then \
		rm -f dist/$(PROGRAM)-windows.zip && cd dist && zip -qr $(PROGRAM)-windows.zip $(PROGRAM)-windows && \
		echo "Built dist/$(PROGRAM)-windows.zip"; \
	else echo "Built $(WINDOWS_DIST)/"; fi

$(WINDOWS_DIST)/$(PROGRAM).exe: src/main.cpp $(GEODE) packaging/arch/PKGBUILD
	mkdir -p $(@D)
	$(WINCXX) $(CXXFLAGS) -std=c++17 -static -s $(DEFS) src/main.cpp -o $@ -ladvapi32

# Batch files need Windows line endings.
$(WINDOWS_DIST)/%.bat: windows/%.bat
	mkdir -p $(@D)
	sed 's/\r*$$/\r/' $< > $@

clean:
	rm -f $(PROGRAM)
	rm -rf dist

.PHONY: install uninstall windows clean
