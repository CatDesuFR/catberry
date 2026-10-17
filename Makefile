CXX ?= g++
WINCXX ?= x86_64-w64-mingw32-g++
CXXFLAGS ?= -O2 -Wall -Wextra
PREFIX ?= $(HOME)/.local

COMMON_SRCS = src/main.cpp $(wildcard src/common/*.cpp)
LINUX_SRCS = $(COMMON_SRCS) $(wildcard src/linux/*.cpp)
WINDOWS_SRCS = $(COMMON_SRCS) $(wildcard src/windows/*.cpp)
HEADERS = $(wildcard src/*/*.h)
WINDOWS_DIST = dist/catberry-windows

catberry: $(LINUX_SRCS) $(HEADERS)
	$(CXX) $(CXXFLAGS) -std=c++17 -pthread $(LINUX_SRCS) -o $@ -lX11 -lXtst

install: catberry
	install -Dm755 catberry $(DESTDIR)$(PREFIX)/bin/catberry

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/catberry

# Windows build, cross-compiled with MinGW-w64. Produces a folder with
# catberry.exe, install.bat and uninstall.bat, plus a zip of it when zip
# is available.
windows: $(WINDOWS_DIST)/catberry.exe $(WINDOWS_DIST)/install.bat $(WINDOWS_DIST)/uninstall.bat
	@if command -v zip >/dev/null; then \
		rm -f dist/catberry-windows.zip && cd dist && zip -qr catberry-windows.zip catberry-windows && \
		echo "Built dist/catberry-windows.zip"; \
	else echo "Built $(WINDOWS_DIST)/"; fi

$(WINDOWS_DIST)/catberry.exe: $(WINDOWS_SRCS) $(HEADERS)
	mkdir -p $(@D)
	$(WINCXX) $(CXXFLAGS) -std=c++17 -static -s $(WINDOWS_SRCS) -o $@ \
		-luser32 -lgdi32 -lwinmm -lshell32 -lole32 -luuid

# Batch files need Windows line endings.
$(WINDOWS_DIST)/%.bat: windows/%.bat
	mkdir -p $(@D)
	sed 's/\r*$$/\r/' $< > $@

clean:
	rm -rf catberry dist

.PHONY: install uninstall windows clean
