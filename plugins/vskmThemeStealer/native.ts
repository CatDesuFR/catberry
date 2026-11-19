/*
 * vskm (catberry) - Theme Stealer, the part that runs in Vesktop's main process
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import { THEMES_DIR } from "@main/utils/constants";
import { IpcMainInvokeEvent } from "electron";
import { mkdir, writeFile } from "fs/promises";
import { join } from "path";

// Saves a theme into Vencord's themes folder. Returns the file name used.
export async function saveTheme(_: IpcMainInvokeEvent, name: string, css: string): Promise<string> {
    const safe = name.replace(/[\\/:*?"<>|\x00-\x1f]/g, "_").replace(/^\.+/, "").trim().slice(0, 80) || "Stolen theme";
    const file = `${safe}.css`;
    await mkdir(THEMES_DIR, { recursive: true });
    await writeFile(join(THEMES_DIR, file), css, "utf-8");
    return file;
}
