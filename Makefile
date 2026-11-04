# catberry-roofce: the catberry wallpaper manager (Roof Carton Engine).
# Thin wrapper around CMake so it builds like the other catberry tools.
PREFIX ?= $(HOME)/.local
BUILD = build

catberry-roofce: CMakeLists.txt $(wildcard src/*.cpp src/*.h)
	cmake -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$(PREFIX) >/dev/null
	cmake --build $(BUILD) -j2
	cp $(BUILD)/catberry-roofce $@

install: catberry-roofce
	install -Dm755 catberry-roofce $(DESTDIR)$(PREFIX)/bin/catberry-roofce
	install -Dm644 data/catberry-roofce.desktop $(DESTDIR)$(PREFIX)/share/applications/catberry-roofce.desktop

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/catberry-roofce $(DESTDIR)$(PREFIX)/share/applications/catberry-roofce.desktop

clean:
	rm -rf $(BUILD) catberry-roofce dist

# Unit tests of the online sources' page readers.
test:
	cmake -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$(PREFIX) >/dev/null
	cmake --build $(BUILD) --target roofce-tests -j$$(nproc)
	$(BUILD)/roofce-tests

.PHONY: install uninstall clean test
