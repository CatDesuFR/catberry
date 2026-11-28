// Tells whether any application window is open on the desktop, so the
// wallpaper can mute its sound while you are using another app.
//
// It asks the compositor to list its windows. KDE's KWin offers the
// org_kde_plasma_window_management protocol; the wlroots compositors (sway,
// Hyprland...) offer wlr-foreign-toplevel-management. Either works, and a
// compositor with neither reports "not busy" always, so the feature just does
// nothing there. The desktop is busy when at least one ordinary window is
// open and not minimized.
#pragma once

#include <QObject>
#include <cstdint>
#include <unordered_map>

struct wl_display;
struct wl_registry;
struct wl_array;
struct zwlr_foreign_toplevel_manager_v1;
struct zwlr_foreign_toplevel_handle_v1;
struct org_kde_plasma_window_management;
struct org_kde_plasma_window;

class ForegroundWatcher : public QObject {
    Q_OBJECT
public:
    explicit ForegroundWatcher(QObject *parent = nullptr);
    ~ForegroundWatcher() override;

    // True once a window-listing protocol is in use.
    bool available() const { return wlrManager_ || plasmaManager_; }
    // True while at least one application window is open (not minimized).
    bool busy() const { return busy_; }

    // Registry.
    void onGlobal(uint32_t name, const char *interface, uint32_t version);
    // wlroots backend.
    void onWlrToplevel(zwlr_foreign_toplevel_handle_v1 *handle);
    void onWlrState(zwlr_foreign_toplevel_handle_v1 *handle, wl_array *states);
    void onWlrClosed(zwlr_foreign_toplevel_handle_v1 *handle);
    // KDE backend.
    void onPlasmaWindow(const char *uuid);
    void onPlasmaState(org_kde_plasma_window *window, uint32_t flags);
    void onPlasmaUnmapped(org_kde_plasma_window *window);

signals:
    void busyChanged(bool busy);

private:
    void setWindow(void *key, bool counts);
    void dropWindow(void *key);
    void recompute();

    wl_display *display_ = nullptr;
    wl_registry *registry_ = nullptr;
    zwlr_foreign_toplevel_manager_v1 *wlrManager_ = nullptr;
    org_kde_plasma_window_management *plasmaManager_ = nullptr;
    // Each open window and whether it counts (visible, not minimized).
    std::unordered_map<void *, bool> windows_;
    bool busy_ = false;
};
