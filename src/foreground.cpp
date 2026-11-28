#include "foreground.h"

#include <QGuiApplication>
#include <QtGui/qguiapplication_platform.h>

#include <wayland-client.h>

#include "plasma-window-management-client-protocol.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

namespace {

// =====================================================================
// wlroots: wlr-foreign-toplevel-management
// =====================================================================
void wlrToplevel(void *data, zwlr_foreign_toplevel_manager_v1 *, zwlr_foreign_toplevel_handle_v1 *handle) {
    static_cast<ForegroundWatcher *>(data)->onWlrToplevel(handle);
}
void wlrFinished(void *, zwlr_foreign_toplevel_manager_v1 *) {}
const zwlr_foreign_toplevel_manager_v1_listener kWlrManager = {wlrToplevel, wlrFinished};

void wlrTitle(void *, zwlr_foreign_toplevel_handle_v1 *, const char *) {}
void wlrAppId(void *, zwlr_foreign_toplevel_handle_v1 *, const char *) {}
void wlrOutputEnter(void *, zwlr_foreign_toplevel_handle_v1 *, wl_output *) {}
void wlrOutputLeave(void *, zwlr_foreign_toplevel_handle_v1 *, wl_output *) {}
void wlrState(void *data, zwlr_foreign_toplevel_handle_v1 *handle, wl_array *states) {
    static_cast<ForegroundWatcher *>(data)->onWlrState(handle, states);
}
void wlrDone(void *, zwlr_foreign_toplevel_handle_v1 *) {}
void wlrClosed(void *data, zwlr_foreign_toplevel_handle_v1 *handle) {
    static_cast<ForegroundWatcher *>(data)->onWlrClosed(handle);
}
void wlrParent(void *, zwlr_foreign_toplevel_handle_v1 *, zwlr_foreign_toplevel_handle_v1 *) {}
const zwlr_foreign_toplevel_handle_v1_listener kWlrHandle = {
    wlrTitle, wlrAppId, wlrOutputEnter, wlrOutputLeave, wlrState, wlrDone, wlrClosed, wlrParent,
};

// =====================================================================
// KDE: org_kde_plasma_window_management
// =====================================================================
void pwmShowDesktop(void *, org_kde_plasma_window_management *, uint32_t) {}
void pwmWindow(void *, org_kde_plasma_window_management *, uint32_t) {}
void pwmStackingOrder(void *, org_kde_plasma_window_management *, wl_array *) {}
void pwmStackingUuid(void *, org_kde_plasma_window_management *, const char *) {}
void pwmWindowUuid(void *data, org_kde_plasma_window_management *, uint32_t, const char *uuid) {
    static_cast<ForegroundWatcher *>(data)->onPlasmaWindow(uuid);
}
void pwmStackingOrder2(void *, org_kde_plasma_window_management *) {}
const org_kde_plasma_window_management_listener kPlasmaManager = {
    pwmShowDesktop, pwmWindow, pwmStackingOrder, pwmStackingUuid, pwmWindowUuid, pwmStackingOrder2,
};

void pwTitle(void *, org_kde_plasma_window *, const char *) {}
void pwAppId(void *, org_kde_plasma_window *, const char *) {}
void pwState(void *data, org_kde_plasma_window *window, uint32_t flags) {
    static_cast<ForegroundWatcher *>(data)->onPlasmaState(window, flags);
}
void pwVirtualDesktop(void *, org_kde_plasma_window *, int32_t) {}
void pwThemedIcon(void *, org_kde_plasma_window *, const char *) {}
void pwUnmapped(void *data, org_kde_plasma_window *window) {
    static_cast<ForegroundWatcher *>(data)->onPlasmaUnmapped(window);
}
void pwInitialState(void *, org_kde_plasma_window *) {}
void pwParent(void *, org_kde_plasma_window *, org_kde_plasma_window *) {}
void pwGeometry(void *, org_kde_plasma_window *, int32_t, int32_t, uint32_t, uint32_t) {}
void pwIcon(void *, org_kde_plasma_window *) {}
void pwPid(void *, org_kde_plasma_window *, uint32_t) {}
void pwVdEntered(void *, org_kde_plasma_window *, const char *) {}
void pwVdLeft(void *, org_kde_plasma_window *, const char *) {}
void pwAppMenu(void *, org_kde_plasma_window *, const char *, const char *) {}
void pwActivityEntered(void *, org_kde_plasma_window *, const char *) {}
void pwActivityLeft(void *, org_kde_plasma_window *, const char *) {}
void pwResourceName(void *, org_kde_plasma_window *, const char *) {}
void pwClientGeometry(void *, org_kde_plasma_window *, int32_t, int32_t, uint32_t, uint32_t) {}
void pwMapped(void *, org_kde_plasma_window *) {}
void pwResourceClass(void *, org_kde_plasma_window *, const char *) {}
const org_kde_plasma_window_listener kPlasmaWindow = {
    pwTitle,       pwAppId,          pwState,     pwVirtualDesktop, pwThemedIcon,    pwUnmapped,      pwInitialState,
    pwParent,      pwGeometry,       pwIcon,      pwPid,            pwVdEntered,     pwVdLeft,        pwAppMenu,
    pwActivityEntered, pwActivityLeft, pwResourceName, pwClientGeometry, pwMapped,    pwResourceClass,
};

// --- registry ---
void registryGlobal(void *data, wl_registry *, uint32_t name, const char *interface, uint32_t version) {
    static_cast<ForegroundWatcher *>(data)->onGlobal(name, interface, version);
}
void registryRemove(void *, wl_registry *, uint32_t) {}
const wl_registry_listener kRegistry = {registryGlobal, registryRemove};

} // namespace

ForegroundWatcher::ForegroundWatcher(QObject *parent) : QObject(parent) {
    if (auto *wl = qApp->nativeInterface<QNativeInterface::QWaylandApplication>()) display_ = wl->display();
    if (!display_) return;
    // Our own registry (it re-announces every global), on Qt's default event
    // queue, so Qt's loop delivers these events for us - no polling.
    registry_ = wl_display_get_registry(display_);
    wl_registry_add_listener(registry_, &kRegistry, this);
}

ForegroundWatcher::~ForegroundWatcher() {
    for (auto &[key, _] : windows_) {
        if (plasmaManager_)
            org_kde_plasma_window_destroy(static_cast<org_kde_plasma_window *>(key));
        else if (wlrManager_)
            zwlr_foreign_toplevel_handle_v1_destroy(static_cast<zwlr_foreign_toplevel_handle_v1 *>(key));
    }
    if (plasmaManager_) org_kde_plasma_window_management_destroy(plasmaManager_);
    if (wlrManager_) zwlr_foreign_toplevel_manager_v1_destroy(wlrManager_);
    if (registry_) wl_registry_destroy(registry_);
}

void ForegroundWatcher::onGlobal(uint32_t name, const char *interface, uint32_t version) {
    // KDE's protocol is preferred; where it exists, don't also use wlr.
    if (qstrcmp(interface, org_kde_plasma_window_management_interface.name) == 0 && !plasmaManager_) {
        uint32_t use = qMin<uint32_t>(version, 16); // window_with_uuid + get_window_by_uuid
        plasmaManager_ = static_cast<org_kde_plasma_window_management *>(
            wl_registry_bind(registry_, name, &org_kde_plasma_window_management_interface, use));
        org_kde_plasma_window_management_add_listener(plasmaManager_, &kPlasmaManager, this);
        return;
    }
    if (qstrcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0 && !wlrManager_ && !plasmaManager_) {
        uint32_t use = qMin<uint32_t>(version, 3);
        wlrManager_ = static_cast<zwlr_foreign_toplevel_manager_v1 *>(
            wl_registry_bind(registry_, name, &zwlr_foreign_toplevel_manager_v1_interface, use));
        zwlr_foreign_toplevel_manager_v1_add_listener(wlrManager_, &kWlrManager, this);
    }
}

// --- wlroots backend ---
void ForegroundWatcher::onWlrToplevel(zwlr_foreign_toplevel_handle_v1 *handle) {
    windows_.emplace(handle, true); // visible until a state says otherwise
    zwlr_foreign_toplevel_handle_v1_add_listener(handle, &kWlrHandle, this);
    recompute();
}

void ForegroundWatcher::onWlrState(zwlr_foreign_toplevel_handle_v1 *handle, wl_array *states) {
    bool minimized = false;
    for (auto *s = static_cast<uint32_t *>(states->data);
         reinterpret_cast<char *>(s) < static_cast<char *>(states->data) + states->size; s++)
        if (*s == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED) minimized = true;
    setWindow(handle, !minimized);
}

void ForegroundWatcher::onWlrClosed(zwlr_foreign_toplevel_handle_v1 *handle) {
    dropWindow(handle);
    zwlr_foreign_toplevel_handle_v1_destroy(handle);
}

// --- KDE backend ---
void ForegroundWatcher::onPlasmaWindow(const char *uuid) {
    if (!plasmaManager_ || !uuid) return;
    auto *window = org_kde_plasma_window_management_get_window_by_uuid(plasmaManager_, uuid);
    if (!window) return;
    windows_.emplace(window, false); // unknown until its first state
    org_kde_plasma_window_add_listener(window, &kPlasmaWindow, this);
    recompute();
}

void ForegroundWatcher::onPlasmaState(org_kde_plasma_window *window, uint32_t flags) {
    // A window counts when it is a normal taskbar window and not minimized.
    bool counts = !(flags & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MINIMIZED) &&
                  !(flags & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_SKIPTASKBAR);
    setWindow(window, counts);
}

void ForegroundWatcher::onPlasmaUnmapped(org_kde_plasma_window *window) {
    dropWindow(window);
    org_kde_plasma_window_destroy(window);
}

// --- shared ---
void ForegroundWatcher::setWindow(void *key, bool counts) {
    auto it = windows_.find(key);
    if (it == windows_.end() || it->second == counts) {
        if (it != windows_.end()) it->second = counts;
        return;
    }
    it->second = counts;
    recompute();
}

void ForegroundWatcher::dropWindow(void *key) {
    if (windows_.erase(key)) recompute();
}

void ForegroundWatcher::recompute() {
    bool busy = false;
    for (auto &[key, counts] : windows_)
        if (counts) {
            busy = true;
            break;
        }
    if (busy != busy_) {
        busy_ = busy;
        emit busyChanged(busy_);
    }
}
