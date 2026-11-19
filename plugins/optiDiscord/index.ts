/*
 * vskm (catberry) - OptiDiscord
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Makes Discord lighter and snappier. Each part can be switched on or off in
 * the plugin's settings:
 *  - Lower memory use: channels you haven't opened for a while drop their
 *    stored messages (Discord loads them again when you open the channel).
 *  - Pause hidden work: while Vesktop is hidden or minimised, typing
 *    indicators are ignored and animations are paused.
 *  - Lighter media: GIFs don't autoplay, animated emoji and stickers only move
 *    when you hover them (your old choices come back when this is off).
 *  - Fewer animations: Discord's animations and transitions are turned off.
 */

import { definePluginSettings } from "@api/Settings";
import { getUserSettingLazy } from "@api/UserSettings";
import { Logger } from "@utils/Logger";
import definePlugin, { OptionType } from "@utils/types";
import { FluxDispatcher, MessageStore, SelectedChannelStore } from "@webpack/common";

const logger = new Logger("OptiDiscord");

const settings = definePluginSettings({
    lowerMemory: {
        type: OptionType.BOOLEAN,
        description: "Lower memory use: forget the messages of channels you haven't opened for 10 minutes",
        default: true,
    },
    pauseHidden: {
        type: OptionType.BOOLEAN,
        description: "Pause hidden work: while Vesktop is hidden, ignore typing indicators and pause animations",
        default: true,
    },
    lighterMedia: {
        type: OptionType.BOOLEAN,
        description: "Lighter media: no GIF autoplay, animated emoji and stickers only move on hover",
        default: true,
        onChange: on => (on ? lighterMediaOn() : lighterMediaOff()),
    },
    fewerAnimations: {
        type: OptionType.BOOLEAN,
        description: "Fewer animations: turn off Discord's animations and transitions",
        default: false,
        onChange: on => setStyle("vskm-od-fewer", on ? FEWER_ANIMATIONS : null),
    },
    previousMedia: {
        type: OptionType.STRING,
        description: "(kept by OptiDiscord: your media settings from before)",
        default: "",
        hidden: true,
    },
});

let running = false;

// ---- styles ----

const FEWER_ANIMATIONS = `*, *::before, *::after {
    animation-duration: 0.001ms !important;
    animation-iteration-count: 1 !important;
    transition-duration: 0.001ms !important;
    scroll-behavior: auto !important;
}`;

const PAUSED = `html.vskm-od-hidden *, html.vskm-od-hidden *::before, html.vskm-od-hidden *::after {
    animation-play-state: paused !important;
}`;

function setStyle(id: string, css: string | null) {
    let el = document.getElementById(id);
    if (!css) return el?.remove();
    if (!el) {
        el = document.createElement("style");
        el.id = id;
        document.head.appendChild(el);
    }
    el.textContent = css;
}

// ---- lower memory use ----

const lastOpened = new Map<string, number>();
let sweepTimer: ReturnType<typeof setInterval> | undefined;
const FORGET_AFTER = 10 * 60 * 1000;

function onChannelSelect({ channelId }: { channelId?: string; }) {
    if (channelId) lastOpened.set(channelId, Date.now());
}

function sweep() {
    if (!settings.store.lowerMemory) return;
    const Messages = (MessageStore.getMessages("0") as any)?.constructor;
    const all = Messages?._channelMessages as Record<string, unknown> | undefined;
    if (!all) return;
    const open = SelectedChannelStore.getChannelId();
    const voice = SelectedChannelStore.getVoiceChannelId?.();
    const now = Date.now();
    let forgotten = 0;
    for (const id of Object.keys(all)) {
        if (id === open || id === voice) continue;
        if (now - (lastOpened.get(id) ?? 0) < FORGET_AFTER) continue;
        Messages.clear(id);
        lastOpened.delete(id);
        forgotten++;
    }
    if (forgotten) logger.debug(`Forgot the messages of ${forgotten} channel(s)`);
}

// ---- pause hidden work ----

const hidden = () => document.hidden;

function onVisibility() {
    document.documentElement.classList.toggle("vskm-od-hidden", running && settings.store.pauseHidden && hidden());
}

// Typing indicators while hidden: nobody sees them, so don't process them.
// (Discord's dispatcher has no way to remove an interceptor, so this one
// checks whether it should still act.)
let interceptorAdded = false;
function interceptor(action: any) {
    return running && settings.store.pauseHidden && hidden() && action?.type === "TYPING_START";
}

// ---- lighter media ----

const media = {
    gifAutoPlay: getUserSettingLazy<boolean>("textAndImages", "gifAutoPlay"),
    animateEmoji: getUserSettingLazy<boolean>("textAndImages", "animateEmoji"),
    animateStickers: getUserSettingLazy<number>("textAndImages", "animateStickers"),
};

function lighterMediaOn() {
    if (!running) return;
    if (!settings.store.previousMedia) {
        settings.store.previousMedia = JSON.stringify({
            gifAutoPlay: media.gifAutoPlay?.getSetting(),
            animateEmoji: media.animateEmoji?.getSetting(),
            animateStickers: media.animateStickers?.getSetting(),
        });
    }
    media.gifAutoPlay?.updateSetting(false);
    media.animateEmoji?.updateSetting(false);
    media.animateStickers?.updateSetting(1); // 1 = on hover/interaction
}

function lighterMediaOff() {
    if (!settings.store.previousMedia) return;
    try {
        const old = JSON.parse(settings.store.previousMedia);
        if (old.gifAutoPlay != null) media.gifAutoPlay?.updateSetting(old.gifAutoPlay);
        if (old.animateEmoji != null) media.animateEmoji?.updateSetting(old.animateEmoji);
        if (old.animateStickers != null) media.animateStickers?.updateSetting(old.animateStickers);
    } catch (e) {
        logger.error("Could not restore the media settings", e);
    }
    settings.store.previousMedia = "";
}

export default definePlugin({
    name: "OptiDiscord",
    description: "OptiDiscord (vskm): lighter, snappier Discord. Less memory, no work in the background, lighter media (choose in the settings).",
    authors: [{ name: "CatDesu", id: 0n }],
    settings,

    start() {
        running = true;
        FluxDispatcher.subscribe("CHANNEL_SELECT", onChannelSelect);
        const open = SelectedChannelStore.getChannelId();
        if (open) lastOpened.set(open, Date.now());
        sweepTimer = setInterval(sweep, 3 * 60 * 1000);

        if (!interceptorAdded) {
            (FluxDispatcher as any).addInterceptor?.(interceptor);
            interceptorAdded = true;
        }
        setStyle("vskm-od-paused", PAUSED);
        document.addEventListener("visibilitychange", onVisibility);

        if (settings.store.lighterMedia) lighterMediaOn();
        if (settings.store.fewerAnimations) setStyle("vskm-od-fewer", FEWER_ANIMATIONS);
    },

    stop() {
        running = false;
        FluxDispatcher.unsubscribe("CHANNEL_SELECT", onChannelSelect);
        clearInterval(sweepTimer);
        document.removeEventListener("visibilitychange", onVisibility);
        document.documentElement.classList.remove("vskm-od-hidden");
        setStyle("vskm-od-paused", null);
        setStyle("vskm-od-fewer", null);
        lighterMediaOff();
    },
});
