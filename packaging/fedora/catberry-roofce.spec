# Fedora package of catberry-roofce. Built by .github/workflows/release.yml on main:
#   rpmbuild -bb --build-in-place --define "pkgver X" --define "pkgrel N" packaging/fedora/catberry-roofce.spec
Name:           catberry-roofce
Version:        %{pkgver}
Release:        %{pkgrel}%{?dist}
Summary:        Roof Carton Engine: live and still wallpapers for Wayland desktops (catberry roofce)
License:        LicenseRef-custom
URL:            https://github.com/CatDesuFR/catberry/tree/roof-carton-engine
BuildRequires:  cmake gcc-c++ qt6-qtbase-devel qt6-qtmultimedia-devel layer-shell-qt-devel
# ffmpeg is ffmpeg-free on stock Fedora, or ffmpeg from RPM Fusion: either has /usr/bin/ffmpeg.
Requires:       qt6-qtmultimedia /usr/bin/ffmpeg unzip
Suggests:       catberry

%description
Live and still wallpapers for Wayland desktops, with a clock and a music
player on top. Open it from the app menu (Roof Carton Engine).

%build

%install
packaging/stage.sh %{buildroot}

%files
/usr/bin/catberry-roofce
/usr/share/applications/catberry-roofce.desktop
%doc /usr/share/doc/catberry-roofce/README.md
