/*
 * vskm (catberry) - Theme Stealer
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copies a look into a Vencord theme you name and keep:
 *  - Everything Discord looks like right now: Discord's colour theme (dark,
 *    light or a Nitro gradient theme, with the parts of Discord that only
 *    show with a gradient theme), every Vencord/Vesktop theme you have on
 *    (local files and online links), your QuickCSS, and Discord's App Icon.
 *  - A server's look: its icon's colour and its role colours, turned into
 *    backgrounds and accents, and the server's icon.
 * The theme is saved in Vencord's themes folder and switched on. While a
 * theme with an icon is on, the icon shows on the home button (the theme's
 * own CSS), the taskbar/window and the tray.
 */

import { ApplicationCommandInputType, ApplicationCommandOptionType } from "@api/Commands";
import { NavContextMenuPatchCallback } from "@api/ContextMenu";
import { Settings, SettingsStore } from "@api/Settings";
import { Logger } from "@utils/Logger";
import definePlugin, { PluginNative } from "@utils/types";
import { Paragraph } from "@components/Paragraph";
import { RenderModalProps } from "@vencord/discord-types";
import { findByCodeLazy, findStoreLazy } from "@webpack";
import { GuildRoleStore, Menu, Modal, openModal, React, showToast, TextInput } from "@webpack/common";

const Native = VencordNative.pluginHelpers.VskmThemeStealer as PluginNative<typeof import("./native")>;
const logger = new Logger("VskmThemeStealer");

// ---- reading a look ----

// Discord's colour variables right now (values that are colours or gradients).
function discordColours(): string {
    const cs = getComputedStyle(document.documentElement);
    const vars: string[] = [];
    for (let i = 0; i < cs.length; i++) {
        const prop = cs[i];
        if (!prop.startsWith("--")) continue;
        const value = cs.getPropertyValue(prop).trim();
        if (!value || !/(#[0-9a-f]{3,8}\b|rgba?\(|hsla?\(|color-mix\(|gradient\()/i.test(value)) continue;
        vars.push(`    ${prop}: ${value};`);
    }
    // A Nitro gradient theme paints the app's background too.
    const app = document.querySelector<HTMLElement>("[class*='bg_'], [class*='app_'], #app-mount");
    const background = app ? getComputedStyle(app).backgroundImage : "none";
    let css = `:root, .theme-dark, .theme-light, .theme-darker, .theme-midnight, .visual-refresh {\n${vars.join("\n")}\n}\n`;
    if (background && background !== "none" && background.includes("gradient")) {
        css += `\n#app-mount, [class*='bg_'] {\n    background: ${background} !important;\n}\n`;
    }
    return css;
}

// Parts of Discord that only look different while a Nitro gradient theme is
// on (html.custom-theme-background): copied so they always apply.
function gradientThemeRules(): string {
    if (!document.documentElement.classList.contains("custom-theme-background")) return "";
    const ungate = (selector: string) => selector
        .replace(/(^|[\s(,>+~])\.custom-theme-background/g, "$1:root")
        .replace(/\.custom-theme-background/g, "");
    const out: string[] = [];
    const walk = (rules: CSSRuleList) => {
        for (const rule of Array.from(rules)) {
            if (rule instanceof CSSStyleRule) {
                if (rule.selectorText.includes("custom-theme-background"))
                    out.push(`${ungate(rule.selectorText)} { ${rule.style.cssText} }`);
            } else if ("cssRules" in rule) {
                walk((rule as CSSGroupingRule).cssRules);
            }
        }
    };
    for (const sheet of Array.from(document.styleSheets)) {
        // Discord's own stylesheets only (not the themes, collected below).
        if (!sheet.href?.startsWith(location.origin + "/assets/")) continue;
        try {
            walk(sheet.cssRules);
        } catch { }
    }
    return out.join("\n");
}

// A theme's text, made safe to put inside another theme: its @name header
// becomes a plain comment, and its @imports are handed back to go first.
function inline(css: string, imports: string[]): string {
    return css
        .replace(/^(\s*\*\s*)@(?=\w)/gm, "$1")
        .replace(/^\s*@import[^;]*;\s*$/gm, line => {
            imports.push(line.trim());
            return "";
        });
}

interface Look {
    css: string;
    parts: string[]; // what it includes, for the name popup
}

// Everything Discord looks like right now.
async function discordLook(): Promise<Look> {
    const imports: string[] = [];
    const sections: string[] = [];
    const parts: string[] = ["Discord's colours"];
    sections.push("/* Discord's colours */\n" + discordColours());
    const gradient = gradientThemeRules();
    if (gradient) {
        sections.push("/* Discord's gradient theme */\n" + gradient);
        parts.push("the gradient theme");
    }

    const dark = document.documentElement.classList.contains("theme-light") ? "light" : "dark";
    let themes = 0;
    for (const file of Settings.enabledThemes ?? []) {
        const data = await VencordNative.themes.getThemeData(file).catch(() => null);
        if (!data) continue;
        sections.push(`/* Theme: ${file.replace(/\*\//g, "")} */\n` + inline(data, imports));
        themes++;
    }
    for (const raw of Settings.themeLinks ?? []) {
        const m = /^@(light|dark) (.*)/.exec(raw);
        if (m && m[1] !== dark) continue;
        const link = (m ? m[2] : raw).trim();
        if (!link) continue;
        const text = await fetch(link).then(r => r.ok ? r.text() : null).catch(() => null);
        if (text) sections.push(`/* Theme: ${link.replace(/\*\//g, "")} */\n` + inline(text, imports));
        else imports.push(`@import url("${link}");`);
        themes++;
    }
    if (themes) parts.push(themes === 1 ? "1 Vencord theme" : `${themes} Vencord themes`);

    if (Settings.useQuickCss) {
        const quick = await VencordNative.quickCss.get().catch(() => "");
        if (quick.trim()) {
            sections.push("/* QuickCSS */\n" + inline(quick, imports));
            parts.push("your QuickCSS");
        }
    }
    const head = [...new Set(imports)].join("\n");
    return { css: (head ? head + "\n\n" : "") + sections.join("\n\n") + "\n", parts };
}

// ---- icons ----

const AppIconStore = findStoreLazy("AppIconPersistedStoreState");
const appIcons = findByCodeLazy("BRAND_DARK", "iconSource:");     // () => every regular icon
const appIconById = findByCodeLazy("HEXAGON", "iconSource:");     // (id) => that icon (also limited ones)

// The picture of the App Icon chosen in Discord's Appearance settings.
function discordAppIconUrl(): string | null {
    try {
        const id = AppIconStore.getCurrentDesktopIcon?.() ?? AppIconStore.getState?.()?.client?.desktop;
        let icon: any = null;
        try { icon = (appIcons() ?? []).find((i: any) => i?.id === id); } catch { }
        if (!icon?.iconSource) try { icon = appIconById(id); } catch { }
        return icon?.iconSource ? new URL(icon.iconSource, location.origin).href : null;
    } catch (e) {
        logger.error("Could not find Discord's app icon", e);
        return null;
    }
}

interface Icon {
    png: string;    // base64
    unread: string; // base64, with the unread dot for the tray
}

// An image as a 256px PNG, and the same with a red dot (the tray's unread
// picture).
function loadIcon(url: string): Promise<Icon | null> {
    return new Promise(resolve => {
        const img = new Image();
        img.crossOrigin = "anonymous";
        img.onload = () => {
            try {
                const canvas = document.createElement("canvas");
                canvas.width = canvas.height = 256;
                const ctx = canvas.getContext("2d")!;
                ctx.drawImage(img, 0, 0, 256, 256);
                const png = canvas.toDataURL("image/png").split(",")[1];
                ctx.beginPath();
                ctx.arc(196, 196, 52, 0, Math.PI * 2);
                ctx.fillStyle = "#f23f43";
                ctx.fill();
                const unread = canvas.toDataURL("image/png").split(",")[1];
                resolve({ png, unread });
            } catch {
                resolve(null);
            }
        };
        img.onerror = () => resolve(null);
        img.src = url;
    });
}

// The theme's own part of the icon: the home button at the top of the
// server list.
const homeButtonCss = (png: string) => `
/* Icon: the home button */
[data-list-item-id="guildsnav___home"] [class*="childWrapper"] {
    background: url("data:image/png;base64,${png}") center / cover no-repeat !important;
}
[data-list-item-id="guildsnav___home"] [class*="childWrapper"] > * {
    opacity: 0 !important;
}
`;

// The average colour of an image (a server icon), as [r, g, b].
function averageColour(url: string): Promise<[number, number, number] | null> {
    return new Promise(resolve => {
        const img = new Image();
        img.crossOrigin = "anonymous";
        img.onload = () => {
            try {
                const canvas = document.createElement("canvas");
                canvas.width = canvas.height = 32;
                const ctx = canvas.getContext("2d")!;
                ctx.drawImage(img, 0, 0, 32, 32);
                const data = ctx.getImageData(0, 0, 32, 32).data;
                let r = 0, g = 0, b = 0, n = 0;
                for (let i = 0; i < data.length; i += 4) {
                    if (data[i + 3] < 128) continue;
                    r += data[i]; g += data[i + 1]; b += data[i + 2]; n++;
                }
                resolve(n ? [r / n, g / n, b / n].map(Math.round) as [number, number, number] : null);
            } catch {
                resolve(null);
            }
        };
        img.onerror = () => resolve(null);
        img.src = url;
    });
}

function toHsl([r, g, b]: [number, number, number]): [number, number, number] {
    r /= 255; g /= 255; b /= 255;
    const max = Math.max(r, g, b), min = Math.min(r, g, b);
    const l = (max + min) / 2;
    if (max === min) return [0, 0, l * 100];
    const d = max - min;
    const s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
    const h = max === r ? (g - b) / d + (g < b ? 6 : 0) : max === g ? (b - r) / d + 2 : (r - g) / d + 4;
    return [h * 60, s * 100, l * 100];
}

const hsl = (h: number, s: number, l: number) => `hsl(${h.toFixed(1)} ${s.toFixed(1)}% ${l.toFixed(1)}%)`;

async function serverThemeCss(guild: any): Promise<string> {
    const icon = guild.icon ? `https://cdn.discordapp.com/icons/${guild.id}/${guild.icon}.png?size=64` : null;
    const base: [number, number, number] = (icon ? await averageColour(icon) : null) ?? [88, 101, 242];
    const [h, s] = toHsl(base);
    const sat = Math.min(s, 45);
    // Role colours (highest first) for accents.
    const roles = Object.values((GuildRoleStore as any).getRolesSnapshot?.(guild.id) ?? (GuildRoleStore as any).getAllGuildRoles?.(guild.id) ?? {}) as any[];
    const colours = roles.filter(r => r.color).sort((a, b) => b.position - a.position).map(r => `#${r.color.toString(16).padStart(6, "0")}`);
    const accent = colours[0] ?? hsl(h, Math.max(s, 60), 60);
    const accent2 = colours[1] ?? accent;
    return `:root, .theme-dark, .theme-darker, .theme-midnight, .visual-refresh {
    --background-primary: ${hsl(h, sat, 12)};
    --background-secondary: ${hsl(h, sat, 9)};
    --background-secondary-alt: ${hsl(h, sat, 8)};
    --background-tertiary: ${hsl(h, sat, 6)};
    --background-floating: ${hsl(h, sat, 7)};
    --background-base-lowest: ${hsl(h, sat, 5)};
    --background-base-lower: ${hsl(h, sat, 8)};
    --background-base-low: ${hsl(h, sat, 10)};
    --background-surface-high: ${hsl(h, sat, 14)};
    --background-modifier-hover: ${hsl(h, sat, 18)};
    --channeltextarea-background: ${hsl(h, sat, 15)};
    --brand-500: ${accent};
    --brand-560: ${accent};
    --brand-experiment: ${accent};
    --text-link: ${accent2};
    --interactive-active: ${hsl(h, 20, 95)};
    --header-primary: ${hsl(h, 20, 95)};
}
`;
}

// ---- naming and saving ----

function header(name: string, description: string) {
    const clean = (t: string) => t.replace(/\*\//g, "* /").replace(/\n/g, " ");
    return `/**\n * @name ${clean(name)}\n * @description ${clean(description)}\n * @author Theme Stealer (vskm)\n * @version 1.0.0\n */\n\n`;
}

async function save(name: string, description: string, body: string, icon: Icon | null) {
    try {
        const css = header(name, description) + body + (icon ? homeButtonCss(icon.png) : "");
        const file = await Native.saveTheme(name, css, icon?.png, icon?.unread);
        const themes = Settings.enabledThemes ?? [];
        if (!themes.includes(file)) Settings.enabledThemes = [...themes, file];
        else syncIcon(true);
        showToast({ text: `Theme saved and switched on: ${file}`, variant: "success" });
    } catch (e) {
        logger.error("Could not save the theme", e);
        showToast({ text: "Could not save the theme", variant: "critical" });
    }
}

// The icon of the last switched-on theme that has one goes on the taskbar,
// window and tray; with none, Vesktop's own.
let shownIcon: string | null | undefined;
async function syncIcon(force = false) {
    let file: string | null = null;
    for (const f of Settings.enabledThemes ?? [])
        if (await Native.hasIcon(f)) file = f;
    if (!force && file === shownIcon) return;
    shownIcon = file;
    await Native.applyIcon(file).catch(e => logger.error("Could not set the icon", e));
}
const onThemesChanged = () => syncIcon();

function NameModal({ modalProps, initial, title, parts, onSave }: { modalProps: RenderModalProps; initial: string; title: string; parts: string[]; onSave(name: string): void; }) {
    const [name, setName] = React.useState(initial);
    const done = () => {
        if (!name.trim()) return;
        onSave(name.trim());
        modalProps.onClose();
    };
    const list = parts.length > 1 ? parts.slice(0, -1).join(", ") + " and " + parts[parts.length - 1] : parts[0];
    return (
        <Modal
            {...modalProps}
            title={title}
            actions={[
                { text: "Cancel", variant: "secondary", onClick: () => modalProps.onClose() },
                { text: "Save theme", variant: "primary", onClick: done },
            ]}
        >
            <div>
                <Paragraph style={{ marginBottom: 12 }}>Copies {list}.</Paragraph>
                <Paragraph style={{ marginBottom: 8 }}>Theme name</Paragraph>
                <TextInput value={name} onChange={setName} autoFocus onKeyDown={(e: React.KeyboardEvent) => e.key === "Enter" && done()} />
            </div>
        </Modal>
    );
}

function askName(title: string, initial: string, parts: string[], onSave: (name: string) => void) {
    openModal(props => <NameModal modalProps={props} title={title} initial={initial} parts={parts} onSave={onSave} />);
}

export async function stealDiscordTheme() {
    const look = await discordLook();
    const iconUrl = discordAppIconUrl();
    const icon = iconUrl ? await loadIcon(iconUrl) : null;
    if (icon) look.parts.push("Discord's App Icon");
    askName("Save Discord's theme", "My Discord theme", look.parts,
        name => save(name, "Discord's look, copied by Theme Stealer", look.css, icon));
}

async function stealServer(guild: any) {
    const css = await serverThemeCss(guild);
    const parts = ["the server's colours", "its role colours"];
    const icon = guild.icon ? await loadIcon(`https://cdn.discordapp.com/icons/${guild.id}/${guild.icon}.png?size=512`) : null;
    if (icon) parts.push("its icon");
    askName(`Save ${guild.name}'s look`, `${guild.name} theme`, parts,
        name => save(name, `The look of the server ${guild.name}, copied by Theme Stealer`, css, icon));
}

const guildMenu: NavContextMenuPatchCallback = (children, { guild }) => {
    if (!guild) return;
    children.push(
        <Menu.MenuGroup>
            <Menu.MenuItem id="vskm-steal-server" label="Steal server look..." action={() => stealServer(guild)} />
            <Menu.MenuItem id="vskm-steal-discord" label="Save Discord's theme..." action={stealDiscordTheme} />
        </Menu.MenuGroup>
    );
};

export default definePlugin({
    name: "VskmThemeStealer",
    description: "Theme Stealer (vskm): save everything Discord looks like (colours, gradient, your themes, QuickCSS, App Icon) or a server's look and icon as a theme you name (/stealtheme, or right-click a server).",
    authors: [{ name: "CatDesu", id: 0n }],
    contextMenus: { "guild-context": guildMenu },
    discordLook,
    serverThemeCss,
    discordAppIconUrl,
    loadIcon,
    syncIcon,

    start() {
        SettingsStore.addChangeListener("enabledThemes", onThemesChanged);
        syncIcon(true);
    },
    stop() {
        SettingsStore.removeChangeListener("enabledThemes", onThemesChanged);
        shownIcon = undefined;
        Native.applyIcon(null);
    },
    commands: [
        {
            name: "stealtheme",
            description: "Save how Discord looks right now (colours, themes, QuickCSS, App Icon) as a theme you name",
            inputType: ApplicationCommandInputType.BUILT_IN,
            options: [{ name: "server", description: "This server's look instead", type: ApplicationCommandOptionType.BOOLEAN, required: false }],
            execute: (opts, ctx) => {
                const server = opts.find(o => o.name === "server")?.value;
                if (server && ctx.guild) stealServer(ctx.guild);
                else stealDiscordTheme();
            },
        },
    ],
});
