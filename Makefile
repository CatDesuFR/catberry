# catberry-mtools: the catberry macro recorder and hotkey macro configs.
CXX ?= g++
WINCXX ?= x86_64-w64-mingw32-g++
CXXFLAGS ?= -O2 -Wall -Wextra
PREFIX ?= $(HOME)/.local

PROGRAM = catberry-mtools
COMMON_SRCS = src/main.cpp $(wildcard src/common/*.cpp)
LINUX_SRCS = $(COMMON_SRCS) $(wildcard src/linux/*.cpp)
WINDOWS_SRCS = $(COMMON_SRCS) $(wildcard src/windows/*.cpp)
HEADERS = $(wildcard src/*/*.h)
WINDOWS_DIST = dist/$(PROGRAM)-windows

$(PROGRAM): $(LINUX_SRCS) $(HEADERS)
	$(CXX) $(CXXFLAGS) -std=c++17 -pthread $(LINUX_SRCS) -o $@ -lX11 -lXtst

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

$(WINDOWS_DIST)/$(PROGRAM).exe: $(WINDOWS_SRCS) $(HEADERS)
	mkdir -p $(@D)
	$(WINCXX) $(CXXFLAGS) -std=c++17 -static -s $(WINDOWS_SRCS) -o $@ \
		-luser32 -lgdi32 -lwinmm -lshell32 -lole32 -luuid

# Batch files need Windows line endings.
$(WINDOWS_DIST)/%.bat: windows/%.bat
	mkdir -p $(@D)
	sed 's/\r*$$/\r/' $< > $@

clean:
	rm -rf $(PROGRAM) dist tests/run-tests

# Unit tests of the non-window logic: make test
TEST_SRCS = $(filter-out src/main.cpp src/common/gui.cpp src/common/mtools.cpp,$(COMMON_SRCS)) \
	$(filter-out src/linux/host.cpp,$(wildcard src/linux/*.cpp)) $(wildcard tests/*.cpp)
test: $(TEST_SRCS) $(HEADERS) tests/test.h
	$(CXX) $(CXXFLAGS) -std=c++17 -pthread $(TEST_SRCS) -o tests/run-tests -lX11 -lXtst
	./tests/run-tests

.PHONY: install uninstall windows clean test
