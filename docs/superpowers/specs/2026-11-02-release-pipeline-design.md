# Release pipeline: GitHub Releases for every catberry tool

Project 1 of 3. Next: `catberry install` / `uninstall` (project 2), then
ac and mtools rewritten for Wayland in Qt 6 (project 3). Project 2 needs
this one: it reads versions and downloads files from GitHub Releases.

## Goal

Every version of every catberry program is published as a GitHub Release
with ready-to-install packages for Arch-based distros, Fedora, Debian,
Ubuntu and (where the tool exists there) Windows. Publishing one version
is one command.

## Programs and branches

| Tool name | Program | Branch | Current version |
| --- | --- | --- | --- |
| `catberry` | `catberry` (launcher) | `main` | 1.2.0 |
| `ac` | `catberry-ac` | `ac` | 1.1.2 |
| `mtools` | `catberry-mtools` | `mtools` | 1.1.0 |
| `roofce` | `catberry-roofce` | `roof-carton-engine` | 1.2.1 |

The version of a tool is the `pkgver` (and `pkgrel`) in that branch's
`packaging/arch/PKGBUILD`. It is the single source of the version; the
other packaging files read it or are checked against it.

## How a release happens

- One workflow, `.github/workflows/release.yml`, on `main` only. GitHub
  runs a manually started workflow from the branch it lives on, so the
  tool branches carry no CI files.
- Started by hand: `gh workflow run release.yml -f tool=<tool name>`
  (`workflow_dispatch` with a `tool` choice input).
- Steps:
  1. Check out the tool's branch.
  2. Read `pkgver`/`pkgrel` from its PKGBUILD. Tag = `<tool name>-v<pkgver>`,
     e.g. `ac-v1.1.2`, `catberry-v1.2.0`.
  3. If that tag or release already exists: fail with a clear message.
     A published version is never overwritten.
  4. Build every package (below), in parallel jobs.
  5. Smoke-test every Linux package (below).
  6. Only if all builds and tests pass: create the tag on the branch's
     checked-out commit and a GitHub Release with all files and a
     `SHA256SUMS` file. The newest release of a tool is not marked as the
     repository's "Latest" (there are four tools; "Latest" means nothing).

## What gets built

Each Linux package is built in a fresh container of its distro, so it links
against that distro's libraries and declares that distro's package names.

| Tool | Arch | Fedora (newest) | Debian 13 | Ubuntu 26.04 | Windows |
| --- | --- | --- | --- | --- | --- |
| catberry | yes | yes | yes | yes | yes |
| ac | yes | yes | yes | yes | yes |
| mtools | yes | yes | yes | yes | yes |
| roofce | yes | yes | yes | yes | no (Linux only) |

File names (example for ac 1.1.2, Fedora 44):

- `catberry-ac-1.1.2-1-x86_64.pkg.tar.zst`
- `catberry-ac-1.1.2-1.fc44.x86_64.rpm`
- `catberry-ac_1.1.2-1+debian13_amd64.deb`
- `catberry-ac_1.1.2-1+ubuntu26.04_amd64.deb`
- `catberry-ac-windows.zip`
- `SHA256SUMS`

Debian and Ubuntu get separate `.deb` files because their Qt and
layer-shell package names and versions differ. Ubuntu releases older than
26.04 are not targeted: they lack the Qt 6 layer-shell library roofce
needs.

### Per distro

- **Arch**: `archlinux:latest`; the existing `packaging/arch/PKGBUILD`,
  run with `makepkg` as a non-root build user.
- **Fedora**: `fedora:latest`; new `packaging/fedora/<program>.spec`,
  built with `rpmbuild`. Version fields filled from the PKGBUILD by the
  workflow.
- **Debian / Ubuntu**: `debian:13` and `ubuntu:26.04`; new
  `packaging/debian/control` template (name, dependencies, description).
  The workflow installs the build dependencies, builds with the same
  commands as the PKGBUILD, lays out the same files under a staging root
  and runs `dpkg-deb --build`. Version filled from the PKGBUILD.
- **Windows**: `make windows` (MinGW-w64 cross-compile) on an Ubuntu
  runner, exactly as today; the resulting `<program>-windows.zip`.

### Package contents (all Linux formats)

The same files the PKGBUILD installs today, same paths under `/usr`:

- launcher: `/usr/bin/catberry`, the uinput udev rule
  (`/usr/lib/udev/rules.d/70-catberry-uinput.rules`), README; on install,
  reload udev rules (Arch `.install`, RPM `%post`, Debian `postinst`).
- ac / mtools: the program, README; they depend on the launcher package.
- roofce: the program, its `.desktop` file, README.

Each package declares its runtime dependencies in that distro's names
(e.g. roofce: Qt 6 base and multimedia, layer-shell-qt, ffmpeg, unzip),
so pacman, dnf or apt installs them.

## Smoke tests

Each Linux package job, after building:

1. Start a fresh container of the same distro.
2. Install the package with the distro's package manager. For ac, mtools
   and roofce, also install the launcher package from this run if
   building the launcher, otherwise from the launcher's newest release.
3. Run `<program> -v` and check it prints the expected version.

A failing build or test fails the whole run; nothing is published.

## New files

- `main`: `.github/workflows/release.yml`; `packaging/fedora/catberry.spec`;
  `packaging/debian/control`; RPM/Debian post-install udev reload.
- `ac`, `mtools`, `roof-carton-engine`: `packaging/fedora/<program>.spec`
  and `packaging/debian/control`.
- Existing PKGBUILDs, Makefiles and `release/` folders are not changed.

## After it works

- Publish the four current versions: catberry 1.2.0, ac 1.1.2,
  mtools 1.1.0, roofce 1.2.1.
- Check the Arch packages by installing them on the developer's CachyOS PC.
- The `release/` folders stay so existing README links keep working; new
  versions only go to GitHub Releases. README links move to Releases in
  project 2.
- The old `v1.0.2` release is left as it is.

## Out of scope

- The `catberry install` / `uninstall` commands (project 2).
- Rewriting ac and mtools (project 3).
- Signing packages or hosting a pacman/dnf/apt repository.
- Architectures other than x86_64.
