# catberry: the launcher that runs the catberry tools (catberry-ac and
# catberry-mtools, built from the ac and mtools branches).
CXX ?= g++
WINCXX ?= x86_64-w64-mingw32-g++
CXXFLAGS ?= -O2 -Wall -Wextra
PREFIX ?= $(HOME)/.local

PROGRAM = catberry
WINDOWS_DIST = dist/$(PROGRAM)-windows

$(PROGRAM): src/main.cpp
	$(CXX) $(CXXFLAGS) -std=c++17 $< -o $@

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

$(WINDOWS_DIST)/$(PROGRAM).exe: src/main.cpp
	mkdir -p $(@D)
	$(WINCXX) $(CXXFLAGS) -std=c++17 -static -s $< -o $@

# Batch files need Windows line endings.
$(WINDOWS_DIST)/%.bat: windows/%.bat
	mkdir -p $(@D)
	sed 's/\r*$$/\r/' $< > $@

clean:
	rm -rf $(PROGRAM) dist

.PHONY: install uninstall windows clean
