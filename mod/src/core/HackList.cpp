// Every hack in the menu, with its Pointercrate status.
// Statuses come from Pointercrate's "Allowed Mods/Programs" sheet,
// Eclipse (v1.9.4) tab, last updated 10/02/26. Hacks that are not on that
// sheet are marked NotAllowed, because the sheet says to assume that.
#include <features/Stats.hpp>
#include "Hacks.hpp"
#include <imgui.h>

using namespace geode::prelude;

namespace cmm::global { void applyFps(); }
namespace cmm { void registerShortcuts(); void registerPerformance(); void registerBatch1(); void registerBatch2(); void registerBatch3(); void registerBatch4(); void registerBatch5(); }
namespace cmm::labels { void registerLabels(); }

namespace cmm {
    void registerHacks() {
        using enum Tab;
        constexpr auto OK = PC::Allowed;
        constexpr auto SOMETIMES = PC::Sometimes;
        constexpr auto NO = PC::NotAllowed;

        // ---------------- Global ----------------
        add({"speedhack", "Speedhack", "Speeds the whole game up or down.", Global, NO,
            false,
            [] {
                optFloat("Speed", "speedhack.value", 1.f, 0.1f, 4.f, "%.2fx");
                optBool("Change audio speed too", "speedhack.audio", true);
            },
            [] { return val<float>("speedhack.value", 1.f) != 1.f; }});
        add({"audiospeed", "Audio Speed", "Changes the speed (pitch) of all game audio.", Global, OK,
            false, [] { optFloat("Speed", "audiospeed.value", 1.f, 0.1f, 4.f, "%.2fx"); }});
        add({"fpsbypass", "FPS Bypass", "Caps the frame rate at the value you pick. Set it to your monitor's refresh rate or higher, or it will LOWER your FPS.", Global, OK,
            false,
            [] { if (optFloat("FPS", "fpsbypass.value", 1000.f, 30.f, 2000.f, "%.0f")) global::applyFps(); },
            nullptr,
            [](bool on) {
                if (on) global::applyFps();
                else GameManager::get()->updateCustomFPS();
            }});
        add({"safemode", "Safe Mode", "Never saves progress or completions.", Global, OK});
        add({"autosafemode", "Auto Safe Mode", "Turns on Safe Mode by itself when a hack Pointercrate doesn't allow is on.", Global, OK});
        add({"lockcursor", "Lock Cursor", "Keeps the mouse inside the game window while playing.", Global, OK});
        add({"showtaps", "Show Taps", "Shows a circle where you click while playing.", Global, OK,
            false, [] { optFloat("Size", "showtaps.size", 12.f, 4.f, 40.f, "%.0f"); }});
        add({"transitionspeed", "Transition Speed", "Changes how long the fade between menus takes.", Global, OK,
            false, [] { optFloat("Multiplier", "transitionspeed.value", 0.f, 0.f, 2.f, "%.2fx"); }});

        // ---------------- Player ----------------
        add({"noclip", "Noclip", "You can't die. The cheat indicator only turns red once noclip actually saves you.", Player, NO,
            false,
            [] {
                optBool("Player 1", "noclip.p1", true);
                optBool("Player 2", "noclip.p2", true);
                optBool("Red flash when hit", "noclip.tint", true);
                optFloat("Flash opacity", "noclip.tintopacity", 0.3f, 0.05f, 1.f);
                {
                    static char hex[8] = "";
                    static bool loaded = false;
                    auto& color = val<std::string>("noclip.color", "FF0000");
                    if (!loaded) { std::snprintf(hex, sizeof(hex), "%s", color.c_str()); loaded = true; }
                    if (ImGui::InputText("Flash colour (hex)", hex, sizeof(hex), ImGuiInputTextFlags_CharsHexadecimal)) save<std::string>("noclip.color", hex);
                }
                optInt("Die after N deaths (0 = off)", "noclip.maxdeaths", 0, 0, 1000);
                optFloat("Die below accuracy % (0 = off)", "noclip.minacc", 0.f, 0.f, 100.f, "%.1f%%");
            },
            // Only counts as a cheat once it has actually saved you this attempt.
            [] { return stats::noclipDeaths > 0 || stats::noclipHitThisFrame; }});
        add({"nodeatheffect", "No Death Effect", "Hides the death explosion.", Player, OK});
        add({"norespawnflash", "No Respawn Flash", "Hides the flash when you respawn.", Player, OK});
        add({"mutelevelaudioondeath", "Mute Level Audio on Death", "Pauses the song when you die.", Player, OK});
        add({"notrail", "No Trail", "Hides the player trail.", Player, OK});
        add({"alwaystrail", "Always Show Trail", "Always shows the player trail. Not allowed on levels that hide the player on purpose.", Player, SOMETIMES});
        add({"solidwavetrail", "Solid Wave Trail", "Makes the wave trail solid instead of see-through.", Player, OK});
        add({"norobotfire", "No Robot Fire", "Hides the robot's boost fire.", Player, OK});
        add({"novehicleparticles", "No Vehicle Particles", "Hides ground particles under the player.", Player, OK});
        add({"hideplayer", "Hide Player", "Makes the player invisible.", Player, NO});
        add({"jumphack", "Jump Hack", "Lets you jump in the air.", Player, NO});
        add({"respawndelay", "Respawn Delay", "Changes how long you wait before respawning.", Player, OK,
            false, [] { optFloat("Seconds", "respawndelay.value", 0.5f, 0.f, 2.f, "%.2fs"); }});
        add({"nospiderdash", "No Spider Dash", "Hides the spider teleport trail.", Player, OK});
        add({"autokill", "Auto Kill", "Kills you when you reach a percentage.", Player, OK,
            false, [] { optFloat("At %", "autokill.percent", 50.f, 0.f, 100.f, "%.0f%%"); }});
        add({"framestepper", "Frame Stepper", "Freezes the level. Press F (or the Shortcuts button) to run one tick.", Player, NO});
        add({"autoclicker", "AutoClicker", "Clicks for you on a timer.", Player, NO,
            false,
            [] {
                optInt("Hold (ticks)", "autoclicker.hold", 2, 1, 120);
                optInt("Release (ticks)", "autoclicker.release", 2, 1, 120);
            }});

        // ---------------- Level ----------------
        add({"smartstartpos", "Smart StartPos", "Sets each start position to the gamemode, speed, size, gravity, mirror and dual the player really has there, from the portals before it.", Level, OK});
        add({"startposswitcher", "StartPos Switcher", "Use Q and E (or the on-screen arrows) in a level to switch between start positions.", Level, OK,
            false, [] { optBool("On-screen arrows", "startposswitcher.arrows", true); }});
        add({"accuratepercentage", "Accurate Percentage", "Shows decimals on the percentage.", Level, OK,
            false, [] { optInt("Decimals", "accuratepercentage.decimals", 2, 0, 6); }});
        add({"showhitboxes", "Show Hitboxes", "Draws hitboxes all the time.", Level, NO,
            false, [] { optBool("Hide player icon", "showhitboxes.hideplayer", false); }});
        add({"hitboxesondeath", "Hitboxes On Death", "Draws hitboxes only after you die.", Level, OK});
        add({"hitboxtrail", "Hitbox Trail", "Draws your hitbox for every tick while hitboxes are showing (Show Hitboxes, or after death with Hitboxes on Death). Green = you pressed, blue = you let go.", Level, NO,
            false, [] { optInt("Length (ticks)", "hitboxtrail.length", 240, 10, 2000); }});
        add({"autopracticemode", "Auto Practice Mode", "Starts every level in practice mode.", Level, OK,
            false, [] { optBool("Demons only", "autopracticemode.demons", false); }});
        add({"noparticles", "No Particles", "Hides particles in levels.", Level, OK});
        add({"noglow", "No Object Glow", "Removes object glow (re-enter the level to apply).", Level, OK});
        add({"noendshake", "No End Shake", "Removes the camera shake when you beat a level.", Level, OK});
        add({"autoldm", "Auto LDM", "Turns on low detail mode for every level that has it.", Level, OK});
        add({"confirmrestart", "Confirm Restart", "Asks before restarting from the pause menu.", Level, OK});
        add({"confirmexit", "Confirm Exit", "Asks before leaving the level from the pause menu.", Level, OK});
        add({"bestcolor", "Best Color", "Colours the % while you're past your best on this level.", Level, OK,
            false,
            [] {
                static char const* presets[] = {"Golden", "Silver", "Purple", "Custom hex"};
                auto& preset = val<int>("bestcolor.preset", 0);
                if (ImGui::Combo("Colour", &preset, presets, 4)) save<int>("bestcolor.preset", preset);
                if (preset == 3) {
                    static char buf[8] = "";
                    if (!buf[0]) std::snprintf(buf, sizeof(buf), "%s", val<std::string>("bestcolor.hex", "FF4FA3").c_str());
                    if (ImGui::InputText("Hex", buf, sizeof(buf), ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase)) {
                        save<std::string>("bestcolor.hex", buf);
                    }
                }
            }});
        add({"confirmpractice", "Confirm Practice Mode", "Asks before entering or leaving practice mode.", Level, OK});
        add({"hidecompletevfx", "Hide Level Complete VFX", "Hides the effects when you beat a level.", Level, OK});
        add({"forceplatformer", "Force Platformer", "Plays any level in platformer mode.", Level, NO});
        add({"nomirror", "No Mirror", "Ignores mirror portals.", Level, NO});
        add({"instantmirror", "Instant Mirror", "Mirror portals flip instantly with no transition.", Level, NO});
        add({"noshaders", "No Shaders", "Turns off shader effects in levels.", Level, OK});
        add({"noportallightning", "No Portal Lightning", "Hides the lightning from portals.", Level, OK});
        add({"hidepause", "Hide Pause Menu", "Hides the pause menu. Pointercrate counts pausing with it as a death.", Level, NO});

        // ---------------- Bypass ----------------
        add({"unlockicons", "Unlock Icons", "Unlocks every icon and color (only while on).", Bypass, OK});
        add({"practicemusic", "Practice Music Sync", "Plays the level song in practice mode.", Bypass, OK});
        add({"copybypass", "Level Copy Bypass", "Lets you copy any online level.", Bypass, OK});
        add({"charlimit", "Character Limit Bypass", "Removes the length limit on text boxes.", Bypass, OK});
        add({"charfilter", "Character Filter Bypass", "Lets you type any character in text boxes.", Bypass, OK});

        // ---------------- Creator ----------------
        add({"freescroll", "Free Scroll", "Scroll anywhere in the editor.", Creator, OK});
        add({"leveledit", "Level Edit", "Adds the edit button to the pause menu of any level. Pointercrate only allows it in some cases.", Creator, SOMETIMES});
        add({"verifybypass", "Verify Bypass", "Lets you upload a level without verifying it.", Creator, OK});
        add({"nocmark", "No (C) Mark", "Removes the copy mark when uploading a copied level.", Creator, OK});

        // ---------------- Labels ----------------
        labels::registerLabels();

        // ---------------- Click Before Frames ----------------
        registerShortcuts();
        registerPerformance();
        registerBatch1();
        registerBatch2();
        registerBatch3();
        registerBatch4();
        registerBatch5();

        // ---------------- Replay Bot ----------------
        add({"bot.record", "Record", "Records your inputs.", Bot, NO});
        add({"bot.play", "Playback", "Plays a recorded macro. Your own clicks are ignored while it plays.", Bot, NO});

        add({"prediction", "Frame Window Analyser", "Works out each replay click's frame window (how many ticks in a row it could land on and survive). Click Before Frames then aims for the earliest of them.", CBF, NO,
            false, [] { optInt("Search range (ticks each way)", "prediction.range", 16, 2, 64); }});
        add({"framecounter", "Frame Window Counter", "A ring and the window at every click of the replay, in NaN GD's colours, with a tally per colour.", CBF, NO,
            false, [] {
                {
                    // How far each click is moved, each way. Older setting: Balanced = 32, Max = 64.
                    static char const* kAcc[] = {"32 ticks each way", "64 ticks each way", "128 ticks each way", "256 ticks each way",
                                                 "512 ticks each way", "1024 ticks each way", "2048 ticks each way", "4096 ticks each way"};
                    int r = val<int>("framecounter.range", 0);
                    if (r <= 0) r = val<int>("framecounter.accuracy", 1) == 0 ? 32 : 64;
                    int idx = 0;
                    while (idx < 7 && (32 << idx) < r) idx++;
                    if (ImGui::Combo("Accuracy", &idx, kAcc, 8)) save<int>("framecounter.range", 32 << idx);
                    if (idx >= 3) ImGui::TextDisabled("Big ranges take longer to measure each click.");
                }
                optBool("Measure the bot's macro first (loading screen)", "framecounter.preload", true);
                optBool("Hide windows over 10 (like NaN GD)", "framecounter.hide10", true);
                optBool("Ding when you pass a click", "framecounter.sound", true);
                optFloat("Ding volume", "framecounter.volume", 0.6f, 0.f, 1.f, "%.2f");
            }});
        add({"cbf", "Click Before Frames", "Follows a replay: a click up to N ticks (the stage, F8) before one of the replay's clicks is delayed to land exactly on it.", CBF, NO});
    }
}
