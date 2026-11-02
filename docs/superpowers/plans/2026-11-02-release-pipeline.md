# Release Pipeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** One `gh workflow run` publishes a version of any catberry tool as a GitHub Release with Arch, Fedora, Debian 13, Ubuntu 26.04 and (except roofce) Windows packages.

**Architecture:** Each tool branch gets a `packaging/stage.sh` that builds the program and lays out its files under a given root, plus an RPM spec and a Debian control template that call it. One workflow on `main` checks out the chosen branch, builds each format in its distro's container, smoke-tests each package in a fresh container, and only then tags and publishes.

**Tech Stack:** GitHub Actions, Docker containers (`archlinux:latest`, `fedora:latest`, `debian:13`, `ubuntu:26.04`), makepkg, rpmbuild (`--build-in-place`), dpkg-deb + dpkg-shlibdeps, MinGW-w64, `gh`.

**Spec:** `docs/superpowers/specs/2026-11-02-release-pipeline-design.md`

## Global Constraints

- Tools and branches: `catberry`→`main`, `ac`→`ac`, `mtools`→`mtools`, `roofce`→`roof-carton-engine`. Programs: `catberry`, `catberry-ac`, `catberry-mtools`, `catberry-roofce`.
- Version = `pkgver`/`pkgrel` from the branch's `packaging/arch/PKGBUILD`; nothing else hard-codes it.
- Tag = `<tool>-v<pkgver>` (e.g. `ac-v1.1.2`). An existing tag or release fails the run; nothing is overwritten.
- Publish only after every build and smoke test passed. Releases are created with `--latest=false`.
- File names: `<program>-<ver>-<rel>-x86_64.pkg.tar.zst`, `<program>-<ver>-<rel>.fc<N>.x86_64.rpm`, `<program>_<ver>-<rel>+debian13_amd64.deb`, `<program>_<ver>-<rel>+ubuntu26.04_amd64.deb`, `<program>-windows.zip`, `SHA256SUMS`.
- Package contents match today's PKGBUILD `package()` exactly, same `/usr` paths.
- ac and mtools packages depend on the launcher package `catberry`. roofce only suggests it (its PKGBUILD has it as an optdepend): Debian `Suggests`, RPM `Suggests`. So roofce's smoke test does not install the launcher.
- Beyond the spec's file list, each branch also gets `packaging/stage.sh`, shared by the RPM and Debian builds so the install layout is written once per tool.
- Existing PKGBUILDs, Makefiles and `release/` folders are not changed.
- Commits authored `CatDesuFR <stefan.zara.tudor@gmail.com>` via `git -c user.name=... -c user.email=...`, ending with the Co-Authored-By line.

## Review Focus

- Fedora has no `ffmpeg` package (it's in RPM Fusion); roofce must require `/usr/bin/ffmpeg` so either `ffmpeg-free` or RPM Fusion's `ffmpeg` satisfies it. Pinned in Task 4's smoke test (install succeeds on stock Fedora).
- First run for a tool before any `catberry-v*` release exists: smoke tests for ac/mtools/roofce need the launcher. Publish `catberry` first; if no launcher release exists the smoke job fails with a clear "publish catberry first" message (Task 5).
- Running the workflow twice for the same version: must fail before building, at the tag check (Task 5, step "re-run").
- makepkg refuses to run as root in the Arch container: build as a created `builder` user (Task 5).
- A `.deb` missing library dependencies installs but crashes: `Depends` comes from `dpkg-shlibdeps`, and the smoke test runs `-v` in a fresh container that has only what apt pulled in (Task 1–5).

---

### Task 1: Launcher packaging files (`main`)

**Files:**
- Create: `packaging/stage.sh`
- Create: `packaging/fedora/catberry.spec`
- Create: `packaging/debian/control`, `packaging/debian/postinst`

**Interfaces:**
- Produces: `packaging/stage.sh <root>` — run from the repo root; builds (`make catberry`) and installs exactly the PKGBUILD's files under `<root>/usr/...`; exits non-zero on failure. Every branch's `stage.sh` has this same contract.
- Produces: `packaging/debian/control` — a template with the placeholders `@VERSION@` and `@SHLIBS@`; fields `Package`, `Version`, `Architecture: amd64`, `Maintainer: CatDesuFR <stefan.zara.tudor@gmail.com>`, `Depends`, optional `Suggests`, `Description`.
- Produces: the spec reads `%{pkgver}` and `%{pkgrel}` passed by `rpmbuild --define`, and calls `packaging/stage.sh %{buildroot}` in `%install`.

- [ ] **Step 1: Write `stage.sh`** — `set -eu`; `make catberry`; `install -Dm755 catberry "$1/usr/bin/catberry"`; the udev rule to `$1/usr/lib/udev/rules.d/70-catberry-uinput.rules`; `README.md` to `$1/usr/share/doc/catberry/README.md`.
- [ ] **Step 2: Check it** — Run: `bash packaging/stage.sh "$SCRATCH/root" && find "$SCRATCH/root" -type f | sort`. Expected: exactly the three files above.
- [ ] **Step 3: Write the spec** — `Name: catberry`, `Version: %{pkgver}`, `Release: %{pkgrel}%{?dist}`, `BuildRequires: gcc-c++ make`, `%post` and `%postun`: `udevadm control --reload-rules || :` and `udevadm trigger --name-match=uinput || :`; `%files` lists the three files.
- [ ] **Step 4: Write `control` and `postinst`** — `Depends: @SHLIBS@`; `postinst` does the same udev reload as the Arch `.install`, `exit 0`.
- [ ] **Step 5: Commit** on `main`: `packaging: stage script, Fedora spec and Debian control for the launcher`.

### Task 2: ac packaging files (`ac` branch)

**Files:** Create `packaging/stage.sh`, `packaging/fedora/catberry-ac.spec`, `packaging/debian/control`, `packaging/debian/postinst` in `/home/catdesu/projects/cpp/catberry-ac`.

**Interfaces:** Same contracts as Task 1.

- [ ] **Step 1: `stage.sh`** — `make catberry-ac`; installs `/usr/bin/catberry-ac` and `/usr/share/doc/catberry-ac/README.md`.
- [ ] **Step 2: Check it** — same command as Task 1. Expected: those two files.
- [ ] **Step 3: Spec** — `BuildRequires: gcc-c++ make libX11-devel libXtst-devel`, `Requires: catberry`; `%post` echoes the same two hints as `catberry-ac.install` (input group).
- [ ] **Step 4: `control`** — `Depends: catberry, @SHLIBS@`; `postinst` prints the same hints.
- [ ] **Step 5: Commit** on `ac`.

### Task 3: mtools packaging files (`mtools` branch)

Identical to Task 2 with `catberry-mtools` (same build deps, same hints from `catberry-mtools.install`). Commit on `mtools`.

### Task 4: roofce packaging files (`roof-carton-engine` branch)

**Files:** Create `packaging/stage.sh`, `packaging/fedora/catberry-roofce.spec`, `packaging/debian/control` in `/home/catdesu/projects/cpp/catberry-roof-carton-engine`.

- [ ] **Step 1: `stage.sh`** — `cmake -S . -B build-pkg -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr`, `cmake --build build-pkg`; installs `/usr/bin/catberry-roofce`, `/usr/share/applications/catberry-roofce.desktop`, `/usr/share/doc/catberry-roofce/README.md`. Add `/build-pkg/` to `.gitignore`.
- [ ] **Step 2: Check it locally** (cmake and Qt are installed here) — Expected: those three files.
- [ ] **Step 3: Spec** — `BuildRequires: cmake gcc-c++ qt6-qtbase-devel qt6-qtmultimedia-devel layer-shell-qt-devel`; `Requires: qt6-qtmultimedia /usr/bin/ffmpeg unzip`; `Suggests: catberry`.
- [ ] **Step 4: `control`** — `Depends: @SHLIBS@, ffmpeg, unzip`; `Suggests: catberry`. Build deps for Debian/Ubuntu live in the workflow (Task 5): `cmake g++ qt6-base-dev qt6-multimedia-dev liblayershellqtinterface-dev`.
- [ ] **Step 5: Commit** on `roof-carton-engine`.

### Task 5: The release workflow (`main`)

**Files:** Create `.github/workflows/release.yml`.

**Interfaces:**
- Consumes: every branch's `packaging/stage.sh <root>`, `packaging/fedora/<program>.spec`, `packaging/debian/control` (+ optional `postinst`), `packaging/arch/PKGBUILD`, `make windows`.
- Produces: `gh workflow run release.yml -f tool=<catberry|ac|mtools|roofce>`.

Jobs (all `runs-on: ubuntu-latest`, `permissions: contents: write`):

- [ ] **Step 1: `meta`** — map tool→branch/program; check out the branch; read `pkgver`/`pkgrel` with `sed -n 's/^pkgver=//p'`; fail with `"<tag> is already released"` if `gh release view <tag>` or `git ls-remote --tags origin <tag>` finds it. Outputs: `branch`, `program`, `version`, `rel`, `tag`, `sha`.
- [ ] **Step 2: `arch`** — `container: archlinux:latest`; `pacman -Syu --noconfirm base-devel git` plus the PKGBUILD's makedepends/depends; create user `builder`, `chown` the checkout, `su builder -c 'cd packaging/arch && makepkg -s --noconfirm'` (sudo for builder without password for `-s`). Upload the `.pkg.tar.zst`.
- [ ] **Step 3: `fedora`** — `container: fedora:latest`; `dnf install -y rpm-build` + BuildRequires via `dnf builddep -y packaging/fedora/<program>.spec`; `rpmbuild -bb --build-in-place --define "pkgver $V" --define "pkgrel $R" --define "_topdir $PWD/rpmbuild" packaging/fedora/<program>.spec`. Upload the `.rpm`.
- [ ] **Step 4: `deb`** — matrix `{debian: debian:13, ubuntu: ubuntu:26.04}` with suffix `debian13` / `ubuntu26.04`; install per-tool build deps (table in the workflow); `packaging/stage.sh root`; `@SHLIBS@` = output of `dpkg-shlibdeps -O` on the staged binary (needs a minimal `debian/control` with `Source:` in the working dir; strip `shlibs:Depends=`); fill `@VERSION@` = `$V-$R+<suffix>`; copy `postinst` if present to `root/DEBIAN/` (0755); `dpkg-deb --build --root-owner-group root <program>_$V-$R+<suffix>_amd64.deb`. Upload.
- [ ] **Step 5: `windows`** — skipped when tool is `roofce`; `apt-get install -y g++-mingw-w64-x86-64 zip`; `make windows WINCXX=x86_64-w64-mingw32-g++-posix`; upload `dist/<program>-windows.zip`.
- [ ] **Step 6: Smoke tests** — one job per Linux artifact (`needs` its build job), in a fresh container of the same image: install the launcher first when tool ≠ catberry and tool ≠ roofce (download the matching format from the newest `catberry-v*` release with `gh release download`; if none exists, fail with `"publish catberry first"`); install the package (`pacman -U --noconfirm`, `dnf install -y ./x.rpm`, `apt-get install -y ./x.deb`); run `<program> -v` and require it to contain `$V`. roofce `-v` must not need a display: run it with `QT_QPA_PLATFORM=offscreen`.
- [ ] **Step 7: `publish`** — `needs` all smoke jobs and windows; download all artifacts; `sha256sum * > SHA256SUMS`; `gh release create <tag> --target <sha> --title "<tool> <version>" --notes "<program> <version>" --latest=false <files>`.
- [ ] **Step 8: Commit** on `main`: `Release workflow: build, test and publish any tool to GitHub Releases`.

### Task 6: Publish the current versions and check

- [ ] **Step 1: Push** `main`, `ac`, `mtools`, `roof-carton-engine`.
- [ ] **Step 2: Run for the launcher** — `gh workflow run release.yml -f tool=catberry`; watch with `gh run watch`. Expected: release `catberry-v1.2.0` with 4 Linux packages, 1 zip, `SHA256SUMS`. Fix and re-run on failure (nothing is published on failure).
- [ ] **Step 3: Re-run check** — run it again for `catberry`. Expected: fails in `meta` with "catberry-v1.2.0 is already released".
- [ ] **Step 4: Run for `ac`, `mtools`, `roofce`** — Expected: `ac-v1.1.2`, `mtools-v1.1.0`, `roofce-v1.2.1` (roofce without a zip).
- [ ] **Step 5: Local Arch check** — download the four `.pkg.tar.zst` from the releases and have the user run `sudo pacman -U` on them; then `catberry -v` lists all four versions.
