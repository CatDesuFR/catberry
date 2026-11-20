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
 *
 * Search results (Ctrl+F): a result that is a reply shows "Message could not
 * be loaded" instead of the message it replies to, because Discord's search
 * doesn't send it. This loads those messages (one at a time, gently), the way
 * Vencord's ValidReply does when you hover one.
 */

import { definePluginSettings } from "@api/Settings";
import { Logger } from "@utils/Logger";
import definePlugin, { OptionType } from "@utils/types";
import { findByCodeLazy } from "@webpack";
import { FluxDispatcher, RestAPI } from "@webpack/common";

const logger = new Logger("VskmMiscFixes");

const settings = definePluginSettings({
    streamAudio: {
        type: OptionType.BOOLEAN,
        description: "Fix screen share sound (Vesktop on Linux): turn on Vesktop's microphone workaround",
        default: true,
        restartNeeded: true,
    },
    searchReplies: {
        type: OptionType.BOOLEAN,
        description: "Search results (Ctrl+F): load the messages that replies answer, instead of \"Message could not be loaded\"",
        default: true,
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

// ---- replies in search results ----

const enum ReferencedState { Loaded, NotLoaded, Deleted }
let ReplyStore: any;
const createMessageRecord = findByCodeLazy(".createFromServer(", ".isBlockedForMessage", "messageReference:");
const asked = new Set<string>();
const queue: { channelId: string; messageId: string; }[] = [];
let working = false;

async function loadReplies() {
    if (working) return;
    working = true;
    try {
        while (queue.length) {
            const { channelId, messageId } = queue.shift()!;
            try {
                const res = await RestAPI.get({ url: `/channels/${channelId}/messages`, query: { limit: 1, around: messageId }, retries: 2 });
                const msg = res?.body?.[0];
                if (msg && ReplyStore) {
                    if (msg.id === messageId) {
                        ReplyStore.set(channelId, messageId, { state: ReferencedState.Loaded, message: createMessageRecord(msg) });
                        FluxDispatcher.dispatch({ type: "MESSAGE_UPDATE", message: msg });
                    } else {
                        ReplyStore.set(channelId, messageId, { state: ReferencedState.Deleted });
                    }
                }
            } catch {
                asked.delete(messageId); // try again next search
            }
            // Gently: Discord limits how fast messages may be fetched.
            await new Promise(r => setTimeout(r, 250));
        }
    } finally {
        working = false;
    }
}

function onSearchFinish(event: any) {
    if (!settings.store.searchReplies || !ReplyStore) return;
    const messages = (event?.messages ?? []).flat(2);
    for (const m of messages) {
        const ref = m?.message_reference;
        // Replies only (forwards carry their message with them).
        if (!ref?.message_id || (ref.type ?? 0) !== 0 || m.referenced_message) continue;
        if (asked.has(ref.message_id)) continue;
        asked.add(ref.message_id);
        queue.push({ channelId: ref.channel_id ?? m.channel_id, messageId: ref.message_id });
    }
    loadReplies();
}

export default definePlugin({
    name: "VskmMiscFixes",
    description: "Misc Fixes (vskm): screen share sound on Linux works, and replies in search results show what they answer.",
    authors: [{ name: "CatDesu", id: 0n }],
    settings,

    patches: [
        {
            // Discord's store of replied-to messages (as Vencord's ValidReply).
            find: "ReferencedMessageStore",
            replacement: {
                match: /_channelCaches=new Map;/,
                replace: "$&_vskmReplyStore=$self.setReplyStore(this);",
            },
        },
    ],
    setReplyStore(store: any) {
        ReplyStore = store;
    },
    // For checking that the patch above found Discord's store.
    hasReplyStore: () => ReplyStore != null,
    flux: {
        SEARCH_FINISH: onSearchFinish,
    },

    start() {
        if (settings.store.streamAudio) applyStreamAudio();
    },
    stop() {
        restoreStreamAudio();
    },
});
