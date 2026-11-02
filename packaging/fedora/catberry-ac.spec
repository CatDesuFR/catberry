# Fedora package of catberry-ac. Built by .github/workflows/release.yml on main:
#   rpmbuild -bb --build-in-place --define "pkgver X" --define "pkgrel N" packaging/fedora/catberry-ac.spec
Name:           catberry-ac
Version:        %{pkgver}
Release:        %{pkgrel}%{?dist}
Summary:        catberry autoclicker: clicks or presses a key at a set rate
License:        LicenseRef-custom
URL:            https://github.com/CatDesuFR/catberry/tree/ac
BuildRequires:  gcc-c++ make libX11-devel libXtst-devel
Requires:       catberry

%description
catberry autoclicker: clicks or presses a key at a set rate. Run it with: catberry ac gui

%build

%install
packaging/stage.sh %{buildroot}

%post
echo ":: catberry ac installed. Start it with: catberry ac gui"
echo ":: For the hotkey to work in every window, run once:"
echo "::     sudo usermod -aG input \$USER"
echo ":: then log out and back in."

%files
/usr/bin/catberry-ac
%doc /usr/share/doc/catberry-ac/README.md
