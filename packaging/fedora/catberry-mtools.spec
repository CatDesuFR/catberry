# Fedora package of catberry-mtools. Built by .github/workflows/release.yml on main:
#   rpmbuild -bb --build-in-place --define "pkgver X" --define "pkgrel N" packaging/fedora/catberry-mtools.spec
Name:           catberry-mtools
Version:        %{pkgver}
Release:        %{pkgrel}%{?dist}
Summary:        catberry macro recorder and background hotkey macros
License:        LicenseRef-custom
URL:            https://github.com/CatDesuFR/catberry/tree/mtools
BuildRequires:  gcc-c++ make libX11-devel libXtst-devel
Requires:       catberry

%description
catberry macro recorder and background hotkey macros. Run it with: catberry mtools gui

%build

%install
packaging/stage.sh %{buildroot}

%post
echo ":: catberry mtools installed. Start it with: catberry mtools gui"
echo ":: Recording, hotkeys and -configload read the keyboard directly; run once:"
echo "::     sudo usermod -aG input \$USER"
echo ":: then log out and back in."

%files
/usr/bin/catberry-mtools
%doc /usr/share/doc/catberry-mtools/README.md
