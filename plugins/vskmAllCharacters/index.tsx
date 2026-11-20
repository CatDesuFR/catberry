/*
 * vskm (catberry) - All Characters In Channel/Category Names
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Discord makes text channel names lowercase, turns spaces into dashes and
 * drops most symbols. This writes the name with look-alike characters that
 * Discord leaves alone, so it reads as you typed it: capitals become
 * mathematical sans-serif capitals (no lowercase form, so they stay), symbols
 * similar-looking ones. Spaces: Discord's servers turn every kind of blank
 * space into "-" or remove it, so a space becomes a middle dot (or another
 * mark, chosen in the settings). Works in the Edit Channel name box, and
 * right-click a channel: "Rename (all characters)...".
 */

import { definePluginSettings } from "@api/Settings";
import { NavContextMenuPatchCallback } from "@api/ContextMenu";
import { Logger } from "@utils/Logger";
import definePlugin, { OptionType } from "@utils/types";
import { Paragraph } from "@components/Paragraph";
import { RenderModalProps } from "@vencord/discord-types";
import { Menu, Modal, openModal, PermissionStore, React, RestAPI, showToast, TextInput } from "@webpack/common";

const logger = new Logger("VskmAllCharacters");

// Discord's servers keep these (checked); no blank character survives.
const SPACES: Record<string, string> = {
    dot: "\u00B7",        // · middle dot
    underscore: "_",
    bar: "\u2502",        // │ thin bar
    dash: "\u2010",       // ‐ dash
};

const settings = definePluginSettings({
    space: {
        type: OptionType.SELECT,
        description: "What a space becomes (Discord doesn't allow blank spaces in text channel names)",
        options: [
            { label: "· Middle dot", value: "dot", default: true },
            { label: "_ Underscore", value: "underscore" },
            { label: "│ Thin bar", value: "bar" },
            { label: "‐ Dash", value: "dash" },
        ],
    },
    allChannels: {
        type: OptionType.BOOLEAN,
        description: "Also convert names of voice channels and categories (they already allow capitals and spaces)",
        default: false,
    },
});

// Capitals: Mathematical Sans-Serif Capital A (U+1D5A0) onwards.
const capital = (c: string) => String.fromCodePoint(0x1D5A0 + c.charCodeAt(0) - 65);

const SYMBOLS: Record<string, string> = {
    "!": "ǃ",  // ǃ
    "?": "？",  // ？
    ".": "․",  // ․
    ",": "‚",  // ‚
    ":": "꞉",  // ꞉
    ";": "\uFF1B", // ； (the Greek look-alike is turned back into ";" and dropped)
    "'": "ʼ",  // ʼ
    "\"": "ʺ", // ʺ
    "&": "＆",  // ＆
    "(": "（",  // （
    ")": "）",  // ）
    "/": "∕",  // ∕
    "+": "＋",  // ＋
    "=": "＝",  // ＝
    "@": "＠",  // ＠
    "#": "＃",  // ＃
    "*": "∗",  // ∗
    "<": "‹",  // ‹
    ">": "›",  // ›
    "|": "∣",  // ∣
    "~": "∼",  // ∼
};

export function encode(name: string): string {
    const space = SPACES[settings.store.space] ?? SPACES.dot;
    let out = "";
    for (const c of name) {
        if (c >= "A" && c <= "Z") out += capital(c);
        else if (c === " ") out += space;
        else out += SYMBOLS[c] ?? c;
    }
    return out;
}

// The other way, so the box shows the name as you'd type it.
export function decode(name: string): string {
    const back: Record<string, string> = {};
    for (const [k, v] of Object.entries(SYMBOLS)) back[v] = k;
    for (const v of [SPACES.dot, SPACES.bar, SPACES.dash]) back[v] = " ";
    let out = "";
    for (const c of name) {
        const cp = c.codePointAt(0)!;
        if (cp >= 0x1D5A0 && cp <= 0x1D5B9) out += String.fromCharCode(65 + cp - 0x1D5A0);
        else out += back[c] ?? c;
    }
    return out;
}

// Text-like channels are the ones Discord changes names of.
const TEXT_LIKE = new Set([0, 5, 15, 16]); // text, announcement, forum, media

async function rename(channel: any, name: string) {
    const convert = TEXT_LIKE.has(channel.type) || settings.store.allChannels;
    const body = { name: convert ? encode(name) : name };
    try {
        await RestAPI.patch({ url: `/channels/${channel.id}`, body });
        showToast({ text: "Renamed", variant: "success" });
    } catch (e: any) {
        logger.error("Rename failed", e);
        showToast({ text: `Could not rename: ${e?.body?.message ?? e?.message ?? "error"}`, variant: "critical" });
    }
}

function RenameModal({ modalProps, channel }: { modalProps: RenderModalProps; channel: any; }) {
    const [name, setName] = React.useState(decode(channel.name ?? ""));
    const done = () => {
        if (!name.trim()) return;
        rename(channel, name.trim());
        modalProps.onClose();
    };
    return (
        <Modal
            {...modalProps}
            title="Rename (all characters)"
            actions={[
                { text: "Cancel", variant: "secondary", onClick: () => modalProps.onClose() },
                { text: "Rename", variant: "primary", onClick: done },
            ]}
        >
            <div>
                <Paragraph style={{ marginBottom: 8 }}>Name: capitals, spaces and symbols are kept</Paragraph>
                <TextInput value={name} onChange={setName} autoFocus maxLength={100}
                    onKeyDown={(e: React.KeyboardEvent) => e.key === "Enter" && done()} />
                <Paragraph style={{ marginTop: 8, opacity: 0.7 }}>Shown as: {encode(name)}</Paragraph>
            </div>
        </Modal>
    );
}

const MANAGE_CHANNELS = 1n << 4n;

const channelMenu: NavContextMenuPatchCallback = (children, { channel }) => {
    if (!channel?.guild_id || !PermissionStore.can(MANAGE_CHANNELS, channel)) return;
    children.push(
        <Menu.MenuItem id="vskm-aca-rename" label="Rename (all characters)..."
            action={() => openModal(props => <RenameModal modalProps={props} channel={channel} />)} />
    );
};

export default definePlugin({
    name: "VskmAllCharacters",
    description: "All Characters In Channel/Category Names (vskm): capitals, spaces and symbols in channel names. Right-click a channel: Rename (all characters).",
    authors: [{ name: "CatDesu", id: 0n }],
    settings,
    contextMenus: { "channel-context": channelMenu },
    encode,
    decode,

    start() {
        // Older versions offered blank spaces, which Discord removes.
        if (!(settings.store.space in SPACES)) settings.store.space = "dot";
    },

    patches: [
        {
            // The name box in a channel's settings (Edit Channel > Overview):
            // Discord makes the name lowercase with dashes as you type.
            find: "handleChangeName=",
            replacement: {
                match: /(handleChangeName=(\i)=>\{let\{channel:(\i)\}=this\.props;if\(null==\3\)return;)\2=(\(0,\i\.\i\))\(\2,\3\.type\);/,
                replace: "$1$2=$self.channelName($2,$3,$4);",
            },
        },
    ],
    // What the name box keeps of what you typed.
    channelName(typed: string, channel: any, discord: (name: string, type: number) => string) {
        if (!TEXT_LIKE.has(channel.type) && !settings.store.allChannels) return discord(typed, channel.type);
        return encode(typed);
    },
});
