# Fedora package of catberry-vskm. Built by ci/build.sh on main:
#   rpmbuild -bb --build-in-place --define "pkgver X" --define "pkgrel N" packaging/fedora/catberry-vskm.spec
Name:           catberry-vskm
Version:        %{pkgver}
Release:        %{pkgrel}%{?dist}
Summary:        Vencord plugins for Vesktop
License:        GPL-3.0-or-later
URL:            https://github.com/CatDesuFR/catberry/tree/vskm
BuildRequires:  gcc-c++ make
Requires:       catberry

%description
Vencord plugins for Vesktop, switched on with: catberry vskm load NAME

%build

%install
packaging/stage.sh %{buildroot}

%post
echo ":: catberry vskm installed. List the plugins with: catberry vskm -list"

%files
/usr/bin/catberry-vskm
/usr/share/catberry-vskm
%doc /usr/share/doc/catberry-vskm/README.md
