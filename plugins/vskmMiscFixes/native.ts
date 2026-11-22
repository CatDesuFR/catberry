/*
 * vskm (catberry) - lets `catberry vskm unload` stop plugins in a running
 * Vesktop, so they put back what they changed (audio share settings, Discord's
 * GIF and emoji settings) before Vesktop is closed.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The program writes the plugin names, one per line, to
 * $XDG_CONFIG_HOME/catberry/vskm-stop and waits for the file to go away.
 * This runs in Vesktop's main process whenever vskm's Vencord is loaded.
 */

import { app, BrowserWindow, net } from "electron";
import { existsSync, readFileSync, unlinkSync } from "fs";
import { mkdir, writeFile } from "fs/promises";
import { homedir } from "os";
import { join } from "path";

const configHome = process.env.XDG_CONFIG_HOME || join(homedir(), ".config");
const request = join(configHome, "catberry", "vskm-stop");
let busy = false;

async function stopPlugins() {
    if (busy || !existsSync(request)) return;
    busy = true;
    try {
        const names = readFileSync(request, "utf-8").split("\n").map(s => s.trim()).filter(Boolean);
        const code = `(() => { for (const n of ${JSON.stringify(names)}) {
            const p = Vencord.Plugins.plugins[n]; if (p?.started) Vencord.Plugins.stopPlugin(p);
        } })()`;
        for (const win of BrowserWindow.getAllWindows())
            await win.webContents.executeJavaScript(code).catch(() => { });
        // Saving the settings the plugins put back is not instant.
        await new Promise(r => setTimeout(r, 1500));
    } finally {
        try { unlinkSync(request); } catch { }
        busy = false;
    }
}

setInterval(stopPlugins, 500);

export function vskmAlive() {
    return true;
}

// ---- Misc Fixes: saving a picture at full quality ----

// Saves a Discord picture into the Downloads folder; returns where. Only
// Discord's picture server is accepted.
export async function downloadPicture(_: unknown, url: string, name: string): Promise<string> {
    const u = new URL(url);
    if (u.protocol !== "https:" || u.hostname !== "cdn.discordapp.com") throw new Error("not a Discord picture");
    const res = await net.fetch(u.toString());
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const data = Buffer.from(await res.arrayBuffer());
    const dir = app.getPath("downloads");
    await mkdir(dir, { recursive: true });
    const safe = name.replace(/[\\/:*?"<>|\x00-\x1f]/g, "_").slice(0, 120) || "picture.png";
    const dot = safe.lastIndexOf(".");
    let file = join(dir, safe);
    for (let i = 1; existsSync(file); i++) file = join(dir, `${safe.slice(0, dot)} (${i})${safe.slice(dot)}`);
    await writeFile(file, data);
    return file;
}
