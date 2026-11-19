/*
 * vskm (catberry) - Theme Stealer
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copies a look into a Vencord theme you name and keep:
 *  - Discord's colour theme as it is right now (dark/light, or a Nitro
 *    gradient theme): every colour variable Discord sets, plus the gradient.
 *  - A server's look: its icon's colour and its role colours, turned into
 *    backgrounds and accents.
 * The theme is saved in Vencord's themes folder and switched on.
 */

import { ApplicationCommandInputType, ApplicationCommandOptionType } from "@api/Commands";
import { NavContextMenuPatchCallback } from "@api/ContextMenu";
import { Settings } from "@api/Settings";
import { Logger } from "@utils/Logger";
import { ModalCloseButton, ModalContent, ModalFooter, ModalHeader, ModalProps, ModalRoot, openModal } from "@utils/modal";
import definePlugin, { PluginNative } from "@utils/types";
import { Button, TextButton } from "@components/Button";
import { Heading } from "@components/Heading";
import { Paragraph } from "@components/Paragraph";
import { GuildRoleStore, Menu, React, showToast, TextInput } from "@webpack/common";

const Native = VencordNative.pluginHelpers.VskmThemeStealer as PluginNative<typeof import("./native")>;
const logger = new Logger("VskmThemeStealer");

// ---- reading a look ----

// Discord's colour variables right now (values that are colours or gradients).
function discordThemeCss(): string {
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
    const base = (icon && await averageColour(icon)) ?? [88, 101, 242];
    const [h, s] = toHsl(base);
    const sat = Math.min(s, 45);
    // Role colours (highest first) for accents.
    const roles = Object.values(GuildRoleStore.getRolesSnapshot?.(guild.id) ?? GuildRoleStore.getAllGuildRoles?.(guild.id) ?? {}) as any[];
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

async function save(name: string, description: string, body: string) {
    try {
        const file = await Native.saveTheme(name, header(name, description) + body);
        const themes = Settings.enabledThemes ?? [];
        if (!themes.includes(file)) Settings.enabledThemes = [...themes, file];
        showToast({ text: `Theme saved and switched on: ${file}`, variant: "success" });
    } catch (e) {
        logger.error("Could not save the theme", e);
        showToast({ text: "Could not save the theme", variant: "critical" });
    }
}

function NameModal({ modalProps, initial, title, onSave }: { modalProps: ModalProps; initial: string; title: string; onSave(name: string): void; }) {
    const [name, setName] = React.useState(initial);
    const done = () => {
        if (!name.trim()) return;
        onSave(name.trim());
        modalProps.onClose();
    };
    return (
        <ModalRoot {...modalProps}>
            <ModalHeader>
                <Heading tag="h2" style={{ flexGrow: 1, margin: 0 }}>{title}</Heading>
                <ModalCloseButton onClick={modalProps.onClose} />
            </ModalHeader>
            <ModalContent style={{ padding: "16px 0" }}>
                <Paragraph style={{ marginBottom: 8 }}>Theme name</Paragraph>
                <TextInput value={name} onChange={setName} autoFocus onKeyDown={(e: React.KeyboardEvent) => e.key === "Enter" && done()} />
            </ModalContent>
            <ModalFooter>
                <Button onClick={done} disabled={!name.trim()}>Save theme</Button>
                <TextButton variant="secondary" onClick={modalProps.onClose}>Cancel</TextButton>
            </ModalFooter>
        </ModalRoot>
    );
}

function askName(title: string, initial: string, onSave: (name: string) => void) {
    openModal(props => <NameModal modalProps={props} title={title} initial={initial} onSave={onSave} />);
}

export function stealDiscordTheme() {
    const css = discordThemeCss();
    askName("Save Discord's theme", "My Discord theme", name => save(name, "Discord's colour theme, copied by Theme Stealer", css));
}

async function stealServer(guild: any) {
    const css = await serverThemeCss(guild);
    askName(`Save ${guild.name}'s look`, `${guild.name} theme`, name => save(name, `The look of the server ${guild.name}, copied by Theme Stealer`, css));
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
    description: "Theme Stealer (vskm): save Discord's colour theme or a server's look as a theme you name (/stealtheme, or right-click a server).",
    authors: [{ name: "CatDesu", id: 0n }],
    contextMenus: { "guild-context": guildMenu },
    discordThemeCss,
    serverThemeCss,
    commands: [
        {
            name: "stealtheme",
            description: "Save Discord's current colour theme as a theme you name",
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
