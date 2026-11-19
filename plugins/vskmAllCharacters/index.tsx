/*
 * vskm (catberry) - All Characters In Channel/Category Names
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Discord makes text channel names lowercase, turns spaces into dashes and
 * drops most symbols. This writes the name with look-alike characters that
 * Discord leaves alone, so it reads as you typed it: capitals become
 * mathematical sans-serif capitals (no lowercase form, so they stay), spaces
 * a blank character (choose which in the settings), symbols similar-looking
 * ones. Right-click a channel or category: "Rename (all characters)...".
 */

import { definePluginSettings } from "@api/Settings";
import { NavContextMenuPatchCallback } from "@api/ContextMenu";
import { Logger } from "@utils/Logger";
import { ModalCloseButton, ModalContent, ModalFooter, ModalHeader, ModalProps, ModalRoot, openModal } from "@utils/modal";
import definePlugin, { OptionType } from "@utils/types";
import { Button, TextButton } from "@components/Button";
import { Heading } from "@components/Heading";
import { Paragraph } from "@components/Paragraph";
import { Menu, PermissionStore, React, RestAPI, showToast, TextInput } from "@webpack/common";

const logger = new Logger("VskmAllCharacters");

const SPACES: Record<string, string> = {
    thin: " ",        // thin space
    figure: " ",      // figure space
    braille: "⠀",     // braille blank
    hangul: "ㅤ",      // hangul filler
};

const settings = definePluginSettings({
    space: {
        type: OptionType.SELECT,
        description: "Character used for spaces (if Discord still turns spaces into dashes, try another)",
        options: [
            { label: "Thin space", value: "thin", default: true },
            { label: "Figure space", value: "figure" },
            { label: "Braille blank", value: "braille" },
            { label: "Hangul filler", value: "hangul" },
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
    ";": ";",  // ;
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
    const space = SPACES[settings.store.space] ?? SPACES.thin;
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
    for (const v of Object.values(SPACES)) back[v] = " ";
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

function RenameModal({ modalProps, channel }: { modalProps: ModalProps; channel: any; }) {
    const [name, setName] = React.useState(decode(channel.name ?? ""));
    const done = () => {
        if (!name.trim()) return;
        rename(channel, name.trim());
        modalProps.onClose();
    };
    return (
        <ModalRoot {...modalProps}>
            <ModalHeader>
                <Heading tag="h2" style={{ flexGrow: 1, margin: 0 }}>Rename (all characters)</Heading>
                <ModalCloseButton onClick={modalProps.onClose} />
            </ModalHeader>
            <ModalContent style={{ padding: "16px 0" }}>
                <Paragraph style={{ marginBottom: 8 }}>Name: capitals, spaces and symbols are kept</Paragraph>
                <TextInput value={name} onChange={setName} autoFocus maxLength={100}
                    onKeyDown={(e: React.KeyboardEvent) => e.key === "Enter" && done()} />
                <Paragraph style={{ marginTop: 8, opacity: 0.7 }}>Shown as: {encode(name)}</Paragraph>
            </ModalContent>
            <ModalFooter>
                <Button onClick={done} disabled={!name.trim()}>Rename</Button>
                <TextButton variant="secondary" onClick={modalProps.onClose}>Cancel</TextButton>
            </ModalFooter>
        </ModalRoot>
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
});
