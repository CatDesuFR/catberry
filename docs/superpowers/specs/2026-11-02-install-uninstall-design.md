# catberry install / uninstall / versions

Project 2 of 3. Builds on project 1 (every version of every tool is a
GitHub Release, see `2026-11-02-release-pipeline-design.md`). Project 3
(ac and mtools for Wayland in Qt 6) comes after.

## Goal

Someone who has only the `catberry` launcher can install, update, change
the version of, and completely remove any catberry tool, including the
launcher itself, with one command, on Arch-based distros, Fedora, Debian
13, Ubuntu 26.04+ and Windows 10/11.

## Commands

Both word orders work for every command:

```
catberry install <tool> [<version>|latest] [--reinstall]
catberry <tool> install [<version>|latest] [--reinstall]
catberry uninstall <tool>
catberry <tool> uninstall
catberry versions <tool>
catberry <tool> versions
```

- `<tool>` is `ac`, `mtools`, `roofce` or `catberry`.
- No version means `latest`. `latest` is the highest version (compared
  number by number: 1.10.0 > 1.9.2) among the `<tool>-v*` releases.
- `<version>` is written without the `v` (`1.2.2`); a leading `v` is
  accepted too. An unknown version: "roofce has no version 9.9. Published:
  1.2.2" (the list from `versions`).
- `catberry <tool> <anything else>` keeps running the tool as today
  (`catberry roofce gui` etc.). `install`, `uninstall` and `versions` are
  taken by the launcher and never passed to the tool.
- `roofce` on Windows: "roofce is Linux-only (Wayland)."

### `versions`

Prints the published versions, newest first, marking the installed one:

```
roofce versions:
  1.2.2   (installed)
```

The installed version is what `catberry -v` reports for that tool today
(the tool's own `-v`).

## Install

1. **Find the release.** Read the repository's releases from the GitHub
   API (`https://api.github.com/repos/CatDesuFR/catberry/releases?per_page=100`),
   keep the tags `<tool>-v<version>`, pick the requested version.
2. **Already installed?** If the installed version equals the requested
   one: "roofce 1.2.2 is already installed." and exit 0, unless
   `--reinstall`.
3. **Pick the file** (Linux) from `/etc/os-release`:

   | System | Rule | File |
   | --- | --- | --- |
   | Arch-based | `ID` or `ID_LIKE` contains `arch` | `*.pkg.tar.zst` |
   | Fedora-based | `ID` or `ID_LIKE` contains `fedora` | `*.rpm` |
   | Debian 13 | `ID=debian`, `VERSION_ID=13` | `*+debian13_amd64.deb` |
   | Ubuntu 26.04+ | `ID` or `ID_LIKE` contains `ubuntu`, and the Ubuntu version (`VERSION_ID`, or for derivatives the version of `UBUNTU_CODENAME`) ≥ 26.04 | `*+ubuntu26.04_amd64.deb` |

   Anything else, including older Debian and Ubuntu: "catberry install
   supports Arch-based distros, Fedora, Debian 13 and Ubuntu 26.04 or
   newer. This system (<PRETTY_NAME>) is not one of them." Exit 1.
   For derivatives the version check uses `UBUNTU_CODENAME`: `resolute`
   (26.04) or a later name in a short built-in list; an unknown codename
   is treated as newer when `VERSION_ID` cannot decide.
4. **Download and verify** `SHA256SUMS` and the file from
   `https://github.com/CatDesuFR/catberry/releases/download/<tag>/<name>`
   into a new temporary folder (`mkdtemp`), with `curl -fL`. The file name
   comes from `SHA256SUMS` (the line whose name matches the pattern
   above). Check the hash with `sha256sum`; on mismatch stop: "download
   damaged, nothing was installed". The temporary folder is removed
   afterwards in every case.
5. **The launcher first.** Installing `ac` or `mtools` when the
   `catberry` package is not installed through the package manager
   (`pacman -Q catberry` / `rpm -q catberry` / `dpkg -s catberry`):
   install the launcher's latest release first, same steps, and say so.
6. **Install** with the package manager, prefixed with `sudo` unless
   running as root, output shown to the user:
   - Arch: `pacman -U <file>`
   - Fedora: `dnf install <file>`; `dnf downgrade <file>` when the
     requested version is lower than the installed one.
   - Debian/Ubuntu: `apt install --allow-downgrades ./<file>`
   A failing package manager command stops with its exit code and
   "nothing more was changed".
7. **Done:** "roofce 1.2.2 installed." For roofce, also: "If the
   wallpaper is running, restart it: catberry roofce stop; catberry
   roofce start".

### Windows

- Tools: `ac`, `mtools`, `catberry`. Download `<program>-windows.zip`
  and `SHA256SUMS` with `curl.exe` (part of Windows 10/11), check the hash
  with `certutil -hashfile <file> SHA256`, extract with `tar.exe -xf`,
  copy `<program>.exe` into `%LOCALAPPDATA%\Programs\catberry` (the folder
  `install.bat` uses; created if missing, and added to the user PATH the
  same way `install.bat` does when it is not there yet).
- Updating the launcher itself: the running `catberry.exe` is renamed to
  `catberry.old.exe` first, then the new one is copied in; a leftover
  `catberry.old.exe` is deleted on the next run of any catberry command.
- Installed version: the tool's `-v`, as on Linux.

## Uninstall

1. **Stop what is running** (errors ignored):
   - roofce: `catberry-roofce stop`, then end any `catberry-roofce`
     process (the open window).
   - mtools: `catberry-mtools -configunload` (all loaded macros), then
     end any `catberry-mtools` process.
   - ac: end any `catberry-ac` process.
   - Linux: `pkill -x <program>`; Windows: `taskkill /im <program>.exe /f`.
2. **Remove every copy of the program:**
   - The package, if installed through the package manager (`sudo`
     unless root): `pacman -R`, `dnf remove`, `apt remove`.
   - `~/.local/bin/<program>` (from `make install` or the README's manual
     download), if present.
   - roofce: its login autostart entry
     `~/.config/autostart/catberry-roofce.desktop`.
   - Windows: `%LOCALAPPDATA%\Programs\catberry\<program>.exe`.
3. **Ask about personal files**, once, default No (Enter = No). It lists
   only the ones that exist, then asks "Delete these too? [y/N]":

   | Tool | Linux | Windows |
   | --- | --- | --- |
   | ac | `~/.config/catberry/ac.conf` | `%APPDATA%\catberry\ac.conf` |
   | mtools | `~/.config/catberry/mtools.conf`, `~/.config/catberry/macros/`, `~/.config/catberry/configs/` | same names under `%APPDATA%\catberry\` |
   | roofce | `~/.config/catberry/roofce.conf`, `~/.local/share/catberry/roofce/` (uploaded and downloaded wallpapers), `~/.cache/catberry/roofce/`, `~/.local/share/fonts/catberry/` | — |

   `~/.config` etc. follow `XDG_CONFIG_HOME`, `XDG_DATA_HOME`,
   `XDG_CACHE_HOME` when set. Afterwards an empty `catberry` folder in
   each of those places is removed. Not a terminal (stdin not a tty): No.
4. **Done:** "roofce uninstalled." plus, if files were kept, "Your
   settings and files were kept."
5. Uninstalling something that is not installed anywhere: "roofce is not
   installed." Exit 0, but personal files left from earlier are still
   offered for deletion.

### `uninstall catberry`

- ac and mtools cannot run without the launcher (their packages depend
  on it). If either is installed: "ac and mtools need the launcher and
  will be removed too. Continue? [y/N]". No: nothing happens.
- roofce works without the launcher. If installed: "Also remove roofce?
  [y/N]".
- Then each chosen tool is uninstalled as above (tools first, launcher
  last; one combined question about personal files covering all of them),
  and the launcher's own copies: the `catberry` package (with its udev
  rule), `~/.local/bin/catberry`, `%LOCALAPPDATA%\Programs\catberry\catberry.exe`
  (on Windows: renamed aside, since it is running, and the folder removed
  from the user PATH when it is then empty).

## Code layout (launcher, `main` branch)

`src/main.cpp` keeps dispatch and running tools. New files, each one job:

- `src/version.h/.cpp`: parse and compare versions; pick `latest`.
- `src/releases.h/.cpp`: read release tags from GitHub's JSON (only the
  `"tag_name"` fields are needed); parse `SHA256SUMS`.
- `src/system.h/.cpp`: detect the Linux system from os-release text and
  choose the file pattern; package manager commands.
- `src/fetch.h/.cpp`: run curl, hash checks, temporary folder.
- `src/manage.h/.cpp`: install / uninstall / versions steps, prompts,
  stopping tools, personal files.
- `src/windows_install.cpp` (Windows-only parts), or `#ifdef _WIN32`
  sections in the files above where small.

Tools are found and run as today. No new libraries: curl, sha256sum,
certutil, tar and the package managers are run as programs.

## Version and docs

- The launcher becomes **1.3.0** (`kVersion`, PKGBUILD) and is published
  with `ci/release-local.sh catberry`.
- `catberry -help` lists the new commands.
- READMEs (main and each tool branch): install section starts with
  `catberry install <tool>`; download links point to GitHub Releases.

## Testing

- **Unit tests** (`make test`, a small test program, no framework):
  version parsing/comparison and `latest`; os-release samples for Arch,
  CachyOS, EndeavourOS, Fedora, Nobara, Debian 13, Debian 12 (rejected),
  Ubuntu 26.04, Ubuntu 24.04 (rejected), Linux Mint on 26.04 base
  (`UBUNTU_CODENAME=resolute`); `SHA256SUMS` line selection per system;
  release tag extraction from a saved GitHub API response; argument
  parsing for both word orders.
- **Real runs in podman** on archlinux, fedora, debian:13, ubuntu:26.04,
  with the 1.3.0 launcher package built by the pipeline: `versions`,
  `install ac` (pulls in the launcher package if missing), install the
  same version again (refused), `--reinstall`, install an older version
  where one exists (downgrade), `uninstall ac` answering No and Yes to
  personal files (checked by listing the files), `uninstall catberry`
  with ac installed, and an unsupported system (`debian:12`).
- **Windows:** cross-compiled with MinGW. Its file/folder logic is
  covered by the unit tests where it is shared; running it needs a
  Windows PC and is left to the user.

## Out of scope

- A graphical installer (the user chose separate apps, no hub).
- Installing several tools in one command.
- Signature checking beyond `SHA256SUMS` from the same release.
- Distros other than the four families above.
