# Fedora package of the launcher. Built by .github/workflows/release.yml:
#   rpmbuild -bb --build-in-place --define "pkgver 1.2.0" --define "pkgrel 1" packaging/fedora/catberry.spec
Name:           catberry
Version:        %{pkgver}
Release:        %{pkgrel}%{?dist}
Summary:        catberry launcher: runs the catberry autoclicker, macro tools and wallpaper manager
License:        LicenseRef-custom
URL:            https://github.com/CatDesuFR/catberry
BuildRequires:  gcc-c++ make
Suggests:       catberry-ac catberry-mtools catberry-roofce

%description
The catberry command. It runs the catberry tools (catberry ac, catberry
mtools, catberry roofce), which are installed separately.

%build

%install
packaging/stage.sh %{buildroot}

%post
udevadm control --reload-rules >/dev/null 2>&1 || :
udevadm trigger --name-match=uinput >/dev/null 2>&1 || :

%postun
udevadm control --reload-rules >/dev/null 2>&1 || :

%files
/usr/bin/catberry
/usr/lib/udev/rules.d/70-catberry-uinput.rules
%doc /usr/share/doc/catberry/README.md
