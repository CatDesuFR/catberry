# catberry-ac: the catberry autoclicker.
CXX ?= g++
WINCXX ?= x86_64-w64-mingw32-g++
CXXFLAGS ?= -O2 -Wall -Wextra
PREFIX ?= $(HOME)/.local

PROGRAM = catberry-ac
COMMON_SRCS = src/main.cpp $(wildcard src/common/*.cpp)
WINDOWS_SRCS = $(COMMON_SRCS) $(wildcard src/windows/*.cpp)
HEADERS = $(wildcard src/*/*.h)
WINDOWS_DIST = dist/$(PROGRAM)-windows
BUILD = build

# Linux: Qt 6, built with CMake (make, make install, make uninstall).
$(PROGRAM): CMakeLists.txt $(wildcard src/*.cpp src/*/*.cpp) $(HEADERS)
	cmake -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$(PREFIX) >/dev/null
	cmake --build $(BUILD) -j$$(nproc)
	cp $(BUILD)/$(PROGRAM) $@

install: $(PROGRAM)
	install -Dm755 $(PROGRAM) $(DESTDIR)$(PREFIX)/bin/$(PROGRAM)
	install -Dm644 data/$(PROGRAM).desktop $(DESTDIR)$(PREFIX)/share/applications/$(PROGRAM).desktop

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(PROGRAM) $(DESTDIR)$(PREFIX)/share/applications/$(PROGRAM).desktop

# Windows build, cross-compiled with MinGW-w64: a folder with the exe,
# install.bat and uninstall.bat, plus a zip of it when zip is available.
windows: $(WINDOWS_DIST)/$(PROGRAM).exe $(WINDOWS_DIST)/install.bat $(WINDOWS_DIST)/uninstall.bat
	@if command -v zip >/dev/null; then \
		rm -f dist/$(PROGRAM)-windows.zip && cd dist && zip -qr $(PROGRAM)-windows.zip $(PROGRAM)-windows && \
		echo "Built dist/$(PROGRAM)-windows.zip"; \
	else echo "Built $(WINDOWS_DIST)/"; fi

$(WINDOWS_DIST)/$(PROGRAM).exe: $(WINDOWS_SRCS) $(HEADERS)
	mkdir -p $(@D)
	$(WINCXX) $(CXXFLAGS) -std=c++17 -static -s $(WINDOWS_SRCS) -o $@ \
		-luser32 -lgdi32 -lwinmm -lshell32 -lole32 -luuid

# Batch files need Windows line endings.
$(WINDOWS_DIST)/%.bat: windows/%.bat
	mkdir -p $(@D)
	sed 's/\r*$$/\r/' $< > $@

clean:
	rm -rf $(PROGRAM) $(BUILD) dist

.PHONY: install uninstall windows clean
