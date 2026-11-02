# catberry install / uninstall / versions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `catberry install|uninstall|versions <tool>` (both word orders) installs, changes the version of, and removes any catberry tool on Arch, Fedora, Debian 13, Ubuntu 26.04+ and Windows.

**Architecture:** The launcher (`main` branch, plain C++17, no libraries) gains small focused modules: pure logic (versions, release list, SHA256SUMS, os-release, argument parsing) with unit tests, and a thin layer that runs `curl`, `sha256sum`/`certutil`, `tar` and the package managers as programs. Real behaviour is tested in podman containers of each distro.

**Tech Stack:** C++17, g++ / MinGW-w64, make, podman, the release pipeline from project 1 (`ci/release-local.sh`).

**Spec:** `docs/superpowers/specs/2026-11-02-install-uninstall-design.md`

## Global Constraints

- Repo `CatDesuFR/catberry`; tags `<tool>-v<version>`; asset URL `https://github.com/CatDesuFR/catberry/releases/download/<tag>/<name>`; release list `https://api.github.com/repos/CatDesuFR/catberry/releases?per_page=100`.
- Tools: `ac`→`catberry-ac`, `mtools`→`catberry-mtools`, `roofce`→`catberry-roofce` (Linux only), `catberry`→`catberry`.
- Asset suffixes: Arch `.pkg.tar.zst`, Fedora `.rpm`, Debian 13 `+debian13_amd64.deb`, Ubuntu `+ubuntu26.04_amd64.deb`, Windows `-windows.zip`.
- A file belongs to a program when its name is `<program>` + (`-` or `_`) + a digit (so `catberry-ac-1.1.2…` is not the launcher's) and ends with the suffix; Windows: exactly `<program>-windows.zip`.
- Every user-facing sentence is copied from the spec verbatim (already installed, unknown version, unsupported system, damaged download, uninstall prompts).
- Prompts default to No; no tty on stdin = No.
- `sudo` only when not root (`geteuid() != 0`).
- No new libraries; external programs only: `curl`, `sha256sum`, `certutil`, `tar`, `pacman`/`dnf`/`apt`/`rpm`/`dpkg`, `pkill`/`taskkill`.
- Launcher version 1.3.0 (`kVersion` in `src/main.cpp`, `pkgver` in `packaging/arch/PKGBUILD`).
- Commits on `main`, authored `CatDesuFR <stefan.zara.tudor@gmail.com>`, ending with the Co-Authored-By line.

## Review Focus

- `catberry-ac` files must never match a lookup for the launcher `catberry` (prefix rule above). Pinned in Task 2's tests.
- Versions compare number by number (`1.10.0 > 1.9.2`, `1.2 == 1.2.0`). Task 1.
- Uninstall when the tool is not installed but personal files remain: still offers deletion (spec step 5). Task 7's container test.
- A failed download or hash mismatch leaves nothing installed and removes the temp folder. Task 5's test (bad hash) and Task 8's container run.
- `catberry roofce gui` and every other existing tool command still reach the tool unchanged. Task 4 tests.

---

### Task 1: Build split, test harness, versions

**Files:**
- Modify: `Makefile` (build every `src/*.cpp`; Windows build too; new `test` target building `tests/*.cpp` + `src/*.cpp` except `src/main.cpp`, then running it)
- Create: `src/version.h`, `src/version.cpp`, `tests/test.h` (a `CHECK(expr)` macro counting failures, printing `file:line`), `tests/main.cpp` (runs all test functions, prints `N/N passed`, exit 1 on any failure), `tests/test_version.cpp`

**Interfaces — Produces:**
- `struct Version { std::vector<int> parts; };`
- `bool parseVersion(const std::string &text, Version &out);` — accepts `1.2.2`, `v1.2.2`; rejects empty, letters, `1..2`.
- `int compareVersions(const Version &a, const Version &b);` — -1/0/1, missing parts are 0.
- `std::string versionString(const Version &v);`
- `std::vector<std::string> sortNewestFirst(std::vector<std::string> versions);` (skips unparsable)

- [ ] **Step 1:** Write `tests/test_version.cpp`: `1.10.0 > 1.9.2`; `1.2 == 1.2.0`; `v1.2.2` parses to `1.2.2`; `""`, `1.a`, `1..2` rejected; `sortNewestFirst({"1.1.0","1.10.0","1.9.2","x"}) == {"1.10.0","1.9.2","1.1.0"}`.
- [ ] **Step 2:** `make test` → fails to build (no `version.h`).
- [ ] **Step 3:** Implement; Makefile changes so `make catberry`, `make windows`, `make test` all work.
- [ ] **Step 4:** `make test` → all pass; `make catberry && ./catberry -v` still prints `catberry 1.2.0`; `packaging/stage.sh` check from project 1 still lists its 3 files.
- [ ] **Step 5:** Commit `launcher: split into modules, add make test and version parsing`.

### Task 2: Release list and SHA256SUMS

**Files:** Create `src/releases.h`, `src/releases.cpp`, `tests/test_releases.cpp`, `tests/data/releases.json` (a saved API response: `gh api 'repos/CatDesuFR/catberry/releases?per_page=100' > tests/data/releases.json`).

**Interfaces — Produces:**
- `std::vector<std::string> toolVersions(const std::string &releasesJson, const std::string &tool);` — every `"tag_name": "<tool>-v<ver>"`, newest first (uses `sortNewestFirst`). Must not return `ac` tags for `catberry`, nor the old `v1.0.2`.
- `struct SumEntry { std::string sha256, name; };`
- `bool findAsset(const std::string &sums, const std::string &program, const std::string &suffix, SumEntry &out);` — prefix rule from Global Constraints; for suffix `-windows.zip` the name must equal `<program>-windows.zip`.

- [ ] **Step 1:** Tests: from the saved JSON, `toolVersions(json,"catberry") == {"1.2.0"}`, `"ac" → {"1.1.2"}`, `"roofce" → {"1.2.2"}`. `findAsset` on a SUMS text containing both `catberry-1.2.0-1-x86_64.pkg.tar.zst` and `catberry-ac-1.1.2-1-x86_64.pkg.tar.zst`: program `catberry` + `.pkg.tar.zst` returns the launcher's; program `catberry-ac` returns ac's; `catberry_1.2.0-1+debian13_amd64.deb` found for suffix `+debian13_amd64.deb`; missing suffix → false.
- [ ] **Step 2:** `make test` → fails (undefined).
- [ ] **Step 3:** Implement (string scan for `"tag_name"`, no JSON library).
- [ ] **Step 4:** `make test` → pass.
- [ ] **Step 5:** Commit `launcher: read release versions and SHA256SUMS`.

### Task 3: System detection

**Files:** Create `src/system.h`, `src/system.cpp`, `tests/test_system.cpp`.

**Interfaces — Produces:**
- `enum class Family { Arch, Fedora, Debian13, Ubuntu, Unsupported };`
- `struct SystemInfo { Family family; std::string prettyName; };`
- `SystemInfo detectSystem(const std::string &osRelease);` — rules in the spec's table. Ubuntu: `ID=ubuntu` → `VERSION_ID` ≥ 26.04. Derivative (`ID_LIKE` has ubuntu): `UBUNTU_CODENAME` in {focal, jammy, noble, oracular, plucky, questing} → Unsupported; anything else (incl. `resolute`) → Ubuntu.
- `const char *assetSuffix(Family);` (nullptr for Unsupported)

- [ ] **Step 1:** Tests with inline os-release texts: Arch (`ID=arch`), CachyOS (`ID=cachyos`, `ID_LIKE=arch`), EndeavourOS (`ID_LIKE=arch`), Fedora (`ID=fedora`), Nobara (`ID_LIKE="rhel centos fedora"`), Debian 13 (`VERSION_ID="13"`) → Debian13, Debian 12 → Unsupported, Ubuntu 26.04 → Ubuntu, Ubuntu 24.04 → Unsupported, Mint with `UBUNTU_CODENAME=resolute` → Ubuntu, Mint with `noble` → Unsupported; `prettyName` from `PRETTY_NAME` with quotes removed.
- [ ] **Steps 2–5:** fail → implement → pass → commit `launcher: detect the Linux system`.

### Task 4: Command parsing

**Files:** Create `src/args.h`, `src/args.cpp`, `tests/test_args.cpp`; modify `src/main.cpp` to use it.

**Interfaces — Produces:**
- `struct Command { enum Kind { Help, Versions_, RunTool, Install, Uninstall, ListVersions, Unknown } kind; std::string tool, version; bool reinstall = false; int toolArgStart = 0; };` (`Versions_` = `-v`)
- `Command parseCommand(int argc, char **argv);` — both word orders; `install` with no version → `"latest"`; `--reinstall` anywhere after the tool; tool names `ac mtools roofce catberry`; `catberry <tool> <other>` → `RunTool` with `toolArgStart` pointing at `<other>`; `-config*` → RunTool mtools as today.

- [ ] **Step 1:** Tests: `install roofce` → Install/roofce/latest; `roofce install 1.2.1` → version `1.2.1`; `install ac --reinstall`; `ac uninstall` and `uninstall ac` → Uninstall; `versions mtools`, `mtools versions`; `roofce gui` → RunTool roofce, args start at `gui`; `install nope` → Unknown; `catberry install catberry` → Install/catberry; `-configload X` → RunTool mtools.
- [ ] **Steps 2–5:** fail → implement, wire `main()` to it (behaviour for existing commands unchanged) → pass + `./catberry roofce -v` still runs roofce → commit `launcher: parse install/uninstall/versions commands`.

### Task 5: Running programs, downloads and hashes

**Files:** Create `src/process.h`, `src/process.cpp` (Linux + `#ifdef _WIN32`), `src/fetch.h`, `src/fetch.cpp`, `tests/test_fetch.cpp`.

**Interfaces — Produces:**
- `int runProgram(const std::vector<std::string> &argv);` — runs with the terminal attached, waits, returns exit code (127 if it cannot start). Linux fork/execvp; Windows CreateProcess with quoting as in `main.cpp`.
- `bool captureOutput(const std::vector<std::string> &argv, std::string &out);` — stdout of a program, true on exit 0.
- `std::vector<std::string> withSudo(std::vector<std::string> argv);` — prefixes `sudo` unless root.
- `class TempDir { public: TempDir(); ~TempDir(); const std::string &path() const; };` — mkdtemp / `GetTempPath`+unique name; removes itself recursively.
- `bool download(const std::string &url, const std::string &file);` — `curl -fsSL -o`.
- `bool sha256Matches(const std::string &file, const std::string &expected);` — `sha256sum` / `certutil -hashfile <f> SHA256`, compares lower-case hex.

- [ ] **Step 1:** Tests (Linux host): write a file with known content (`"abc"` → `ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad`); `sha256Matches` true for it, false for a wrong hash; `TempDir` path exists inside scope and is gone after; `captureOutput({"echo","hi"})` gives `"hi\n"`; `runProgram({"false"}) == 1`; `runProgram({"no-such-program-xyz"}) == 127`.
- [ ] **Steps 2–5:** fail → implement → pass; `make windows` compiles → commit `launcher: run programs, download, check hashes`.

### Task 6: versions and install (Linux)

**Files:** Create `src/manage.h`, `src/manage.cpp`; modify `src/main.cpp` dispatch.

**Interfaces:**
- Consumes: Tasks 1–5.
- Produces: `int listVersions(const std::string &tool);`, `int installTool(const std::string &tool, const std::string &version, bool reinstall);`, plus helpers `std::string installedVersion(const std::string &program);` (the tool's `-v`, last word; "" if absent) and `bool installedAsPackage(Family, const std::string &program);` (`pacman -Q` / `rpm -q` / `dpkg -s`).

Behaviour exactly as spec "versions" and "Install" steps 1–7 (incl. launcher first for ac/mtools, `dnf downgrade` when older, roofce restart hint). Fetch the release JSON with `download` into the TempDir.

- [ ] **Step 1:** Build the 1.3.0-candidate package for Arch locally (`podman` + `ci/build.sh arch`) and write `ci/test-install.sh <target>`: in a fresh container with the built launcher package installed, run and check (by exit code and `grep` on output / `pacman -Q`, `rpm -q`, `dpkg -s`):
  `catberry versions roofce` lists `1.2.2`; `catberry install ac` → ac installed; again → "ac 1.1.2 is already installed." exit 0; `--reinstall` → exit 0; `catberry install ac 9.9` → "ac has no version 9.9"; in `debian:12` → the unsupported message, exit 1.
- [ ] **Step 2:** Run it on Arch → fails (commands unknown).
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Passes on arch, fedora, debian13, ubuntu26.04; debian:12 case passes.
- [ ] **Step 5:** Commit `launcher: catberry install and versions`.

### Task 7: uninstall (Linux)

**Files:** Modify `src/manage.h/.cpp`, `ci/test-install.sh`.

**Produces:** `int uninstallTool(const std::string &tool);`

Behaviour exactly as spec "Uninstall" 1–5 and "uninstall catberry" (prompt texts verbatim; XDG variables; empty `catberry` folders removed; tools before launcher).

- [ ] **Step 1:** Extend `ci/test-install.sh`: create `~/.config/catberry/ac.conf`, `printf '\n' | catberry uninstall ac` → package gone, `ac.conf` still there, output has "Your settings and files were kept."; `printf 'y\n' | catberry uninstall ac` (not installed now) → "ac is not installed." and `ac.conf` deleted; install ac + mtools, `printf 'y\ny\n' | catberry uninstall catberry` → all three packages gone; roofce: put a fake `~/.local/bin/catberry-roofce` and autostart file, uninstall → both gone.
- [ ] **Steps 2–5:** fail → implement → pass on all four distros → commit `launcher: catberry uninstall`.

### Task 8: Windows paths

**Files:** Modify `src/manage.cpp` (`#ifdef _WIN32` sections) or create `src/manage_windows.cpp`.

Behaviour exactly as spec "Windows" and the Windows columns of Uninstall (zip via `curl.exe`, `certutil`, `tar.exe -xf`, `%LOCALAPPDATA%\Programs\catberry`, PATH add like `install.bat`, `catberry.old.exe` rename and cleanup on start, `taskkill`, roofce refused).

- [ ] **Step 1:** Unit test for the shared pieces it uses (`findAsset` with `-windows.zip`; done in Task 2).
- [ ] **Step 2:** `make windows` → builds without warnings.
- [ ] **Step 3:** Commit `launcher: install and uninstall on Windows`. Ruling to ledger: not run (no Windows here).

### Task 9: 1.3.0, help, READMEs, release

**Files:** Modify `src/main.cpp` (`kVersion = "1.3.0"`, help text), `packaging/arch/PKGBUILD` (`pkgver=1.3.0`), `README.md` on main; README install sections on `ac`, `mtools`, `roof-carton-engine` (lead with `catberry install <tool>`, links to Releases).

- [ ] **Step 1:** `make test` and `ci/test-install.sh` on all four distros pass with the 1.3.0 build.
- [ ] **Step 2:** Commit and push all four branches.
- [ ] **Step 3:** `ci/release-local.sh catberry` → `catberry-v1.3.0` published, all smoke tests OK.
- [ ] **Step 4:** On this PC: user runs `sudo pacman -U` of the 1.3.0 package; `catberry versions roofce` and `catberry install roofce` (→ 1.2.2 replaces the local 1.2.1 build).
