/*
 * vskm (catberry) - Theme Stealer, the part that runs in Vesktop's main process:
 * saves stolen themes (and their icons) and puts a theme's icon on the
 * taskbar/window and the tray.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import { THEMES_DIR } from "@main/utils/constants";
import { execFile } from "child_process";
import { app, BrowserWindow, IpcMainInvokeEvent, nativeImage, Tray } from "electron";
import { existsSync, readFileSync } from "fs";
import { copyFile, mkdir, readFile, rename, rm, writeFile } from "fs/promises";
import { homedir } from "os";
import { join } from "path";

// ---- saving ----

const safeName = (name: string) =>
    name.replace(/[\\/:*?"<>|\x00-\x1f]/g, "_").replace(/^\.+/, "").trim().slice(0, 80) || "Stolen theme";

// Next to a theme "X.css": its icon "X.vskm-icon.png" and the tray's
// unread variant "X.vskm-unread.png".
const iconOf = (themeFile: string) => join(THEMES_DIR, themeFile.replace(/\.css$/, "") + ".vskm-icon.png");
const unreadOf = (themeFile: string) => join(THEMES_DIR, themeFile.replace(/\.css$/, "") + ".vskm-unread.png");

export async function saveTheme(_: IpcMainInvokeEvent, name: string, css: string, icon?: string, unread?: string): Promise<string> {
    const file = `${safeName(name)}.css`;
    await mkdir(THEMES_DIR, { recursive: true });
    await writeFile(join(THEMES_DIR, file), css, "utf-8");
    await rm(iconOf(file), { force: true });
    await rm(unreadOf(file), { force: true });
    if (icon) await writeFile(iconOf(file), Buffer.from(icon, "base64"));
    if (unread) await writeFile(unreadOf(file), Buffer.from(unread, "base64"));
    return file;
}

export function hasIcon(_: IpcMainInvokeEvent, themeFile: string) {
    return existsSync(iconOf(themeFile));
}

// ---- the tray ----

// Electron has no list of trays; Vesktop's is caught when it sets it up or
// changes its picture.
let tray: Tray | undefined;
for (const method of ["setToolTip", "setImage"] as const) {
    const original = Tray.prototype[method] as (...args: any[]) => void;
    Tray.prototype[method] = function (this: Tray, ...args: any[]) {
        tray = this;
        return original.apply(this, args);
    } as any;
}

// Vesktop's own "custom tray icon" files, which it also uses when the unread
// dot comes and goes.
const dataDir = process.env.VENCORD_USER_DATA_DIR || app.getPath("userData");
const assets = join(dataDir, "userAssets");
const marker = join(assets, ".vskm-icon");
const vesktopStatic = (file: string) => join(app.getAppPath(), "static", file);

async function setTrayAssets(icon: string | null, unread: string | null) {
    const ours = existsSync(marker);
    if (icon && unread) {
        await mkdir(assets, { recursive: true });
        if (!ours) {
            // Keep a tray picture you chose yourself, to put back later.
            for (const f of ["tray", "trayUnread"])
                if (existsSync(join(assets, f))) await rename(join(assets, f), join(assets, f + ".vskm-backup"));
            await writeFile(marker, "Theme Stealer (vskm) set the tray pictures\n");
        }
        await copyFile(icon, join(assets, "tray"));
        await copyFile(unread, join(assets, "trayUnread"));
        tray?.setImage(join(assets, "tray"));
    } else if (ours) {
        for (const f of ["tray", "trayUnread"]) {
            await rm(join(assets, f), { force: true });
            if (existsSync(join(assets, f + ".vskm-backup"))) await rename(join(assets, f + ".vskm-backup"), join(assets, f));
        }
        await rm(marker, { force: true });
        const back = existsSync(join(assets, "tray")) ? join(assets, "tray") : vesktopStatic("tray/tray.png");
        if (existsSync(back)) tray?.setImage(back);
    }
}

// ---- the taskbar ----

// On Wayland the taskbar shows the icon of Vesktop's .desktop file, so a copy
// of it with the theme's icon goes in your own applications folder.
const dataHome = process.env.XDG_DATA_HOME || join(homedir(), ".local", "share");
const userDesktop = join(dataHome, "applications", "vesktop.desktop");
const OURS = "X-Vskm-Icon=true";

function systemDesktopFile(): string | null {
    const dirs = (process.env.XDG_DATA_DIRS || "/usr/local/share:/usr/share").split(":");
    for (const d of dirs) {
        const f = join(d, "applications", "vesktop.desktop");
        if (existsSync(f)) return f;
    }
    return null;
}

function refreshMenus() {
    // KDE reads .desktop files from its cache; others notice by themselves.
    for (const tool of ["kbuildsycoca6", "kbuildsycoca5"])
        execFile(tool, [], () => { });
    execFile("update-desktop-database", [join(dataHome, "applications")], () => { });
}

async function setTaskbarIcon(icon: string | null) {
    const exists = existsSync(userDesktop);
    const ours = exists && readFileSync(userDesktop, "utf-8").includes(OURS);
    if (icon) {
        if (exists && !ours) return; // your own Vesktop launcher: left alone
        const source = systemDesktopFile();
        if (!source) return;
        const text = (await readFile(source, "utf-8"))
            .replace(/^Icon=.*$/m, `Icon=${icon}`)
            .replace(/^\[Desktop Entry\]$/m, `[Desktop Entry]\n${OURS}`);
        await mkdir(join(dataHome, "applications"), { recursive: true });
        await writeFile(userDesktop, text, "utf-8");
        refreshMenus();
    } else if (ours) {
        await rm(userDesktop, { force: true });
        refreshMenus();
    }
}

// ---- together ----

// Puts the icon of a theme on the taskbar, window and tray; null puts
// Vesktop's own back.
export async function applyIcon(_: IpcMainInvokeEvent, themeFile: string | null) {
    const icon = themeFile && existsSync(iconOf(themeFile)) ? iconOf(themeFile) : null;
    const unread = themeFile && existsSync(unreadOf(themeFile)) ? unreadOf(themeFile) : icon;
    await setTrayAssets(icon, unread).catch(e => console.error("[vskm] tray icon", e));
    await setTaskbarIcon(icon).catch(e => console.error("[vskm] taskbar icon", e));
    const windowIcon = icon ?? vesktopStatic("tray.png");
    if (existsSync(windowIcon)) {
        const image = nativeImage.createFromPath(windowIcon);
        for (const win of BrowserWindow.getAllWindows()) win.setIcon(image);
    }
}
