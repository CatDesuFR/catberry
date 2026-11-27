# catberry-scan: PC check for cheats (Roblox, CS2, Easy Anti-Cheat games).
# Windows only: cross-compiled with MinGW-w64. The official release is built
# with SCAN_KEY (64 hex digits, kept out of the source) to sign its reports.
WINCXX ?= x86_64-w64-mingw32-g++
CXXFLAGS ?= -O2 -Wall -Wextra

PROGRAM = catberry-scan
VERSION := $(shell sed -n 's/^pkgver=//p' packaging/arch/PKGBUILD)
DEFS = -DSCAN_VERSION='"$(VERSION)"' $(if $(SCAN_KEY),-DSCAN_KEY_HEX='"$(SCAN_KEY)"',)
WINDOWS_DIST = dist/$(PROGRAM)-windows

all:
	@echo "catberry scan is Windows only: make windows (needs MinGW-w64)"

# A folder with the exe, install.bat and uninstall.bat, plus a zip of it.
windows: $(WINDOWS_DIST)/$(PROGRAM).exe $(WINDOWS_DIST)/install.bat $(WINDOWS_DIST)/uninstall.bat
	@if command -v zip >/dev/null; then \
		rm -f dist/$(PROGRAM)-windows.zip && cd dist && zip -qr $(PROGRAM)-windows.zip $(PROGRAM)-windows && \
		echo "Built dist/$(PROGRAM)-windows.zip"; \
	else echo "Built $(WINDOWS_DIST)/"; fi

$(WINDOWS_DIST)/$(PROGRAM).exe: src/main.cpp src/signatures.h packaging/arch/PKGBUILD
	mkdir -p $(@D)
	@echo "$(WINCXX) ... src/main.cpp -o $@$(if $(SCAN_KEY), (with the signing key),)"
	@$(WINCXX) $(CXXFLAGS) -std=c++17 -municode -static -s $(DEFS) src/main.cpp -o $@ \
		-lbcrypt -lpsapi -lwintrust -lshell32 -lole32 -luuid -ladvapi32

# Batch files need Windows line endings.
$(WINDOWS_DIST)/%.bat: windows/%.bat
	mkdir -p $(@D)
	sed 's/\r*$$/\r/' $< > $@

clean:
	rm -rf dist

.PHONY: all windows clean
