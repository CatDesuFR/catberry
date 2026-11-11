# Fedora package of catberry-cbmm. Built by ci/build.sh on main:
#   rpmbuild -bb --build-in-place --define "pkgver X" --define "pkgrel N" packaging/fedora/catberry-cbmm.spec
Name:           catberry-cbmm
Version:        %{pkgver}
Release:        %{pkgrel}%{?dist}
Summary:        Catberry's Mod Menu for Geometry Dash (Geode)
License:        LicenseRef-custom
URL:            https://github.com/CatDesuFR/catberry/tree/cbmm
BuildRequires:  gcc-c++ make
Requires:       catberry

%description
Catberry's Mod Menu for Geometry Dash (Geode). Put it into Geometry Dash with: catberry cbmm load

%build

%install
packaging/stage.sh %{buildroot}

%post
echo ":: catberry cbmm installed. Put the mod into Geometry Dash with: catberry cbmm load"

%files
/usr/bin/catberry-cbmm
%doc /usr/share/doc/catberry-cbmm/README.md
