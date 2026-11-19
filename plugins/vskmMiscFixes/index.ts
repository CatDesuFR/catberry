/*
 * vskm (catberry) - Misc Fixes
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Screen share sound on Linux: Vesktop sends a share's sound through a
 * virtual microphone (venmic, on PipeWire). When Discord picks your real
 * microphone instead, viewers hear nothing from the share; Vesktop's
 * "microphone workaround" fixes that. This turns it on, and makes sure
 * nothing the share should send is filtered out. Your old audio settings
 * come back when the plugin is switched off.
 */

import { definePluginSettings } from "@api/Settings";
import { Logger } from "@utils/Logger";
import definePlugin, { OptionType } from "@utils/types";

const logger = new Logger("VskmMiscFixes");

const settings = definePluginSettings({
    streamAudio: {
        type: OptionType.BOOLEAN,
        description: "Fix screen share sound (Vesktop on Linux): turn on Vesktop's microphone workaround",
        default: true,
        restartNeeded: true,
    },
});

// Vesktop's own settings, from its renderer bridge.
const vesktop = () => (window as any).VesktopNative?.settings as
    | { get(): any; set(settings: any, path?: string): Promise<void> }
    | undefined;

const STREAM_AUDIO = {
    workaround: true,      // the virtual microphone, not your real one
    ignoreDevices: false,
    ignoreVirtual: false,
    onlySpeakers: false,
    onlyDefaultSpeakers: false,
};

function applyStreamAudio() {
    const native = vesktop();
    if (!native) return logger.warn("Not running in Vesktop: nothing to fix for screen share sound");
    const all = native.get() ?? {};
    const audio = all.audio ?? {};
    // Remember the settings as they were, once.
    if (!all.vskmPreviousAudio) all.vskmPreviousAudio = JSON.stringify(audio);
    all.audio = { ...audio, ...STREAM_AUDIO };
    native.set(all, "audio").catch(e => logger.error("Could not save Vesktop's audio settings", e));
    logger.info("Screen share sound: microphone workaround on");
}

function restoreStreamAudio() {
    const native = vesktop();
    if (!native) return;
    const all = native.get() ?? {};
    if (!all.vskmPreviousAudio) return;
    try {
        all.audio = JSON.parse(all.vskmPreviousAudio);
    } catch {
        all.audio = {};
    }
    delete all.vskmPreviousAudio;
    native.set(all, "audio").catch(e => logger.error("Could not restore Vesktop's audio settings", e));
}

export default definePlugin({
    name: "VskmMiscFixes",
    description: "Misc Fixes (vskm): screen share sound on Linux works.",
    authors: [{ name: "CatDesu", id: 0n }],
    settings,

    start() {
        if (settings.store.streamAudio) applyStreamAudio();
    },
    stop() {
        restoreStreamAudio();
    },
});
