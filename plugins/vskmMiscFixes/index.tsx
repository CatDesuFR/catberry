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
 * Vencord's ValidReply does when you hover one. Each result is also shown in
 * Discord's compact layout (time, name and text on one line when it fits).
 *
 * Pictures: avatars and banners in profiles open full size when clicked
 * (Vencord's ViewIcons, switched on with this plugin), and right-clicking an
 * avatar, banner or server icon in the image viewer offers "Download (full
 * quality)": the picture as stored by Discord, at its original size and
 * format, saved to your Downloads folder.
 */

import { definePluginSettings } from "@api/Settings";
import { Logger } from "@utils/Logger";
import { NavContextMenuPatchCallback } from "@api/ContextMenu";
import definePlugin, { OptionType, PluginNative } from "@utils/types";
import { findByCodeLazy } from "@webpack";
import { FluxDispatcher, Menu, React, RestAPI, showToast, UserStore } from "@webpack/common";

const Native = VencordNative.pluginHelpers.VskmMiscFixes as PluginNative<typeof import("./native")>;

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
    compactSearch: {
        type: OptionType.BOOLEAN,
        description: "Search results (Ctrl+F): each result on one line when it fits (Discord's compact layout)",
        default: true,
        restartNeeded: true,
    },
    pictureDownload: {
        type: OptionType.BOOLEAN,
        description: "Right-click an avatar, banner or server icon in the image viewer: Download (full quality)",
        default: true,
    },
});

// ---- pictures at full quality ----

// Discord's picture addresses: /avatars/USER/HASH, /banners/USER/HASH,
// /icons/SERVER/HASH, /guilds/SERVER/users/USER/(avatars|banners)/HASH.
const PICTURES: [RegExp, string][] = [
    [/^\/avatars\/(\d+)\/(\w+)\.\w+$/, "avatar"],
    [/^\/banners\/(\d+)\/(\w+)\.\w+$/, "banner"],
    [/^\/icons\/(\d+)\/(\w+)\.\w+$/, "server-icon"],
    [/^\/guilds\/\d+\/users\/(\d+)\/avatars\/(\w+)\.\w+$/, "server-avatar"],
    [/^\/guilds\/\d+\/users\/(\d+)\/banners\/(\w+)\.\w+$/, "server-banner"],
];

// The picture as Discord stores it: its original format (GIF when animated)
// at its original size (asking for 4096 gives the original, never bigger).
function fullQuality(src: string): { url: string; name: string; } | null {
    let u: URL;
    try { u = new URL(src, location.href); } catch { return null; }
    if (!/^(cdn|media)\.discordapp\.(com|net)$/.test(u.hostname)) return null;
    for (const [re, kind] of PICTURES) {
        const m = re.exec(u.pathname);
        if (!m) continue;
        const [, ownerId, hash] = m;
        const ext = hash.startsWith("a_") ? "gif" : "png";
        const owner = (kind !== "server-icon" && UserStore.getUser(ownerId)?.username) || ownerId;
        const url = `https://cdn.discordapp.com${u.pathname.replace(/\.\w+$/, "." + ext)}?size=4096`;
        return { url, name: `${owner}-${kind}-${hash}.${ext}` };
    }
    return null;
}

const imageMenu: NavContextMenuPatchCallback = (children, props) => {
    if (!settings.store.pictureDownload || !props?.src) return;
    const picture = fullQuality(props.src);
    if (!picture) return;
    children.push(
        <Menu.MenuItem
            id="vskm-download-full"
            label="Download (full quality)"
            action={async () => {
                try {
                    const path = await Native.downloadPicture(picture.url, picture.name);
                    showToast({ text: `Saved: ${path}`, variant: "success" });
                } catch (e: any) {
                    logger.error("Download failed", e);
                    showToast({ text: "Could not download the picture", variant: "critical" });
                }
            }}
        />
    );
};

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
    // SEARCH_MESSAGES_SUCCESS: data is a list of results, each with messages
    // (lists of messages); older Discord sent SEARCH_FINISH with messages.
    const lists = [event?.messages ?? [], ...(Array.isArray(event?.data) ? event.data.map((d: any) => d?.messages ?? []) : [])];
    const messages = lists.flat(3);
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
    description: "Misc Fixes (vskm): screen share sound on Linux, search results on one line with the replies they answer, full-size avatars and banners with Download (full quality).",
    authors: [{ name: "CatDesu", id: 0n }],
    settings,
    // Avatars and banners in profiles open full size when clicked.
    dependencies: ["ViewIcons"],
    contextMenus: { "image-context": imageMenu },
    fullQuality,

    patches: [
        {
            // Search results: Discord's compact layout, one line per result.
            find: "subscribeToComponentDispatch:!1,trackAnnouncementViews:!0",
            replacement: {
                match: /animateAvatar:!1,subscribeToComponentDispatch:!1,trackAnnouncementViews:!0/,
                replace: "$&,compact:$self.compactSearch()",
            },
        },
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
    compactSearch: () => settings.store.compactSearch,
    // For checking that the patch above found Discord's store.
    hasReplyStore: () => ReplyStore != null,
    flux: {
        SEARCH_FINISH: onSearchFinish,
        SEARCH_MESSAGES_SUCCESS: onSearchFinish,
    },

    start() {
        if (settings.store.streamAudio) applyStreamAudio();
    },
    stop() {
        restoreStreamAudio();
    },
});
