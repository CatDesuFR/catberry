# catberry-vskm: Vencord plugins for Vesktop. The Vencord build with the
# plugins in it (vencord/) is made by scripts/build-vencord.sh.
CXX ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra
PREFIX ?= $(HOME)/.local

PROGRAM = catberry-vskm
VERSION := $(shell sed -n 's/^pkgver=//p' packaging/arch/PKGBUILD)

$(PROGRAM): src/main.cpp third_party/json.hpp packaging/arch/PKGBUILD
	$(CXX) $(CXXFLAGS) -std=c++17 -DVSKM_VERSION='"$(VERSION)"' src/main.cpp -o $@

install: $(PROGRAM)
	install -Dm755 $(PROGRAM) $(DESTDIR)$(PREFIX)/bin/$(PROGRAM)
	for f in vencord/*; do install -Dm644 "$$f" "$(DESTDIR)$(PREFIX)/share/catberry-vskm/vencord/$$(basename "$$f")"; done

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(PROGRAM)
	for f in vencord/*; do rm -f "$(DESTDIR)$(PREFIX)/share/catberry-vskm/vencord/$$(basename "$$f")"; done

clean:
	rm -f $(PROGRAM)

.PHONY: install uninstall clean
