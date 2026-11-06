// FF7RCoopMod_InputHook - FINAL FANTASY VII REMAKE INTERGRADE (PC) local co-op mod (players 2 and 3)
//
// This DLL stands in for xinput1_3.dll: Windows loads it from the game's folder before System32, so every
// XInput call the game makes comes through here first; the real xinput1_3.dll is loaded from System32 and
// everything is forwarded to it. From that entry point the mod hooks the game's input, battle command,
// camera and HUD code (addresses are offsets from the exe's base) to let players 2 and 3 control party
// members with their own controllers, in their own split-screen windows.
// Settings: FF7RCoopMod.ini next to the game exe (written with defaults if missing). Log: FF7RCoopMod_Coop.log.
// Version history: CHANGELOG.txt (kept out of the source since v18.4). Build: mingw-w64 posix, see the handoff.

// fork1 (f1.0, 2026-10-18): this file is now only the build entry point. The code lives in the module files
// below, cut verbatim from v19.8 and included in their original order (one translation unit, as before - each
// file may use anything defined in an earlier one). Module map: README_FORK1.txt.

#define COOP_MOD_VERSION "1.0 beta (f1.337)"   // shown in the log and read by the settings app (release name + the fork1 build f1.x)

#include "core/00_includes_xinput_types.inc"   // 61 lines: includes, our own copy of the XInput structs
#include "input/01_slots_real_xinput.inc"   // seat read declarations (PadGetSeat / PadGetMine), loading the real xinput1_3.dll, battery cache
#include "core/02_state_globals_log.inc"   // 135 lines: CO-OP PASS 2 RE notes, core RVAs, per-player state, WithP2/WithP3, CoopLog
#include "settings/03_settings_file.inc"   // FF7RCoopMod.ini: Cfg struct, parsing, LoadConfig (one reader per part, f1.192) / EnsureConfig; the default ini is settings/03a
#include "seats/04_party_identity.inc"   // 105 lines: who is player 2/3 (party manager, character types, P2Char)
#include "passes/05_movement_join_keys.inc"   // 218 lines: pad polling, join/leave + connection, movement-pass key/axis hooks
#include "core/06_processevent_names.inc"   // 70 lines: ProcessEvent hook, NameOf, Readable, Rva
#include "passes/07_action_pass_state.inc"   // 72 lines: ACTION PASS 2 RVAs, key map, shortcut-palette state
#include "menus/08_battle_state_p1_menu.inc"   // battle state, hotkeys allowed, player 1's keys / controller / menu kept off P2 / P3
#include "menus/08b_command_keys.inc"   // the WidgetKey hook: the keys each command system sees (player 1's, players 2 / 3's pad, their own menu)
#include "menus/08c_palette_bcm_before.inc"   // L2 / R2 teammate pick, the shortcut palette, BcmBefore (each player's pad for the battle update)
#include "menus/09_presenter_refresh.inc"   // 30 lines: command HUD presenter refresh hook (marks the own command systems dirty), WidgetSetVis (OwnPalettes removed)
#include "menus/10_second_command_system.inc"   // own command systems (BCM2), Command_C key forwarding
#include "menus/11_simultaneous_menus_ui.inc"   // GpKeyF, UI dispatcher pass-through, Menu2Keys (the OwnCommandMenu=1 experiment removed)
#include "passes/12_command_passes.inc"   // 208 lines: BcmAfter2 / BcmAfter (extra players' command passes), menu slowdown, Hook_BcmUpdate
#include "seats/13_leader_switch_guard.inc"   // 203 lines: SetPartyLeader / SwitchToCharacter redirects, RequestGuard, field keys, UnitState
#include "seats/13b_gate_research_log.inc"   // f1.246 (log only): GATELOG - what runs after the game's script asks for a person's character as the leader
#include "input/14_playstation_hid.inc"   // 98 lines: the game's own DS4 / DualSense HID reader hook
#include "passes/15_battle_hooks_ai_off.inc"   // 120 lines: InstallBattleAndSwitchHooks, AI decisions off for human-played characters
#include "enemies/16_targeting_state.inc"   // 58 lines: P2 TARGETING RE notes + state
#include "seats/17_player3_swap.inc"   // 90 lines: player 3 state swap (RawSwapP3 / LoadP3)
#include "enemies/18_targeting.inc"   // 215 lines: enemy list, soft target / R3 lock / flick switching
#include "marker/19_canvas_marker.inc"   // 518 lines: HUD canvas calls, game-style reticle, DrawP2Marker
#include "marker/20a_smooth_sprites.inc"   // smooth shapes (render-target sprites) for the badges and off-screen pointers
#include "marker/20_status_offscreen_hud.inc"   // 245 lines: status circles, off-screen arrows, DrawCoopHud
#include "enemies/21_balance.inc"   // enemy HP / stagger / focus / damage / attack rate balance; enemy settings per party size (PartyBalanceTick)
#include "enemies/22_attention.inc"   // who enemies go after - StayOnHitChance / TurnOnHeavyDamage as hate-score edits
#include "enemies/22b_target_log.inc"   // (back from f1.91): who enemies go after + how much each player moves (log only, [General] TargetLog)
#include "enemies/22c_target_set_log.inc"   // f1.212 TARGETSET: which game code writes each enemy's target (log only - the bosses-only-target-player-1 research)
#include "enemies/22d_boss_target.inc"   // f1.213 / f1.214: bosses get a mod-kept hate table run with the enemy settings (their script always targets the leader)
#include "enemies/22e_boss_range_log.inc"   // f1.217 (log only): BOSSRANGE / BOSSHIT - a boss's distance to its target, its contacts
#include "enemies/22f_boss_phase_log.inc"   // f1.219 (log only): BOSSHP / BOSSSET / BOSSIDLE - a boss's HP every second, every target write, a quiet script
#include "enemies/22g_boss_action_log.inc"   // f1.226 (log only): BOSSACT - each boss move: its target, distances, contacts; a table per battle
#include "enemies/23_player_rules_atb.inc"   // 134 lines: human players get leader rules (ATB, stagger, hate patches)
#include "seats/24_hotswap_pause_switch.inc"   // 188 lines: seats + hot swap (TradeLead), pause check, KO hand-off, D-pad switch
#include "seats/24b_quiet_lead.inc"   // f1.248 QUIETLEAD: a battle script that needs player 2's / 3's character as the leader gets it for a moment - nobody changes characters
#include "passes/25_player_frame.inc"   // 166 lines: per-player frame input, movement pass, AI guard copy, rumble
#include "core/26_help_tips_reload.inc"   // 228 lines: controls overlay, tips, settings reload
#include "core/27a_talk_diag.inc"   // TEMPORARY: TALKDIAG - which game state marks a talking scene
#include "core/27_main_tick.inc"   // 86 lines: Hook_Tick - the per-frame driver
#include "core/28_inline_hook_weak_refs.inc"   // 75 lines: MakeInlineHook, weak object references
#include "camera/29_coop_camera.inc"   // 212 lines: shared co-op camera framing, wall check
#include "pip_hud/30_pip_window.inc"   // 569 lines: split-screen windows (SceneCapture2D)
#include "pip_hud/30b_dynamic_quality.inc"   // [PictureInPicture] DynamicQuality - the windows get plainer when the frame rate drops
#include "pip_hud/30c_monitors.inc"   // multi-monitor step 1 - MONITORS research lines + the layout maths (not wired yet)
#include "pip_hud/31_hud_copy.inc"   // 992 lines: HUD copy walker (game widgets redrawn over the windows)
#include "pip_hud/31b_monitor_switch.inc"   // f1.290 multiple monitors (experimental): one monitor / all monitors switching, the layout
#include "menus/32_menus_into_window.inc"   // 179 lines: menu target cursor / Command_C moved into windows
#include "menus/34_focus_1_contexts.inc"   // two command menus at once: the game's focus manager, widget owners, per-player focus contexts
#include "menus/34_focus_2_hooks_log.inc"   // the focus hooks: each call routed into the right player's context + logged (FOCUS lines)
#include "menus/34_focus_3_pad_keys.inc"   // players 2 / 3's D-pad / A / B as UI keys in their own focus context (CtxKeys)
#include "menus/34b_shared_items.inc"   // queued item commands count as used (ReserveQueuedItems)
#include "menus/39_menu_dim.inc"   // [General] MenuDarken - the command menus' screen darkening on / off
#include "menus/35_bcm2_into_window.inc"   // 46 lines: own Command_C into the player's window
#include "menus/35b_target_scan.inc"   // players 2/3's marker on the target they pick (the game's GetMenuTarget, f1.45), player 1's reticle kept out of the windows
#include "pip_hud/36_enemy_names.inc"   // 125 lines: enemy names learnt from EnemyStatus_C
#include "pip_hud/36b_party_panels.inc"   // [PictureInPicture] PartyPanel = Shared / Individual (+ PartyPanelRows All / Own)
#include "pip_hud/36c_message_log.inc"   // the game's battle messages (defeated / Gil / EXP) moved like the menus
#include "pip_hud/36e_popup_research.inc"   // RESEARCH (log only) - the game's own damage-number popups
#include "pip_hud/36f_enemy_status_research.inc"   // RESEARCH (log only) - the game's enemy health / stagger bars
#include "pip_hud/36g_game_reticle.inc"   // players 2/3's reticle = the game's own, recolored
#include "pip_hud/36h_canvas_perspective.inc"   // f1.307 the game's tilted menus lean toward the middle of each player's own part
#include "pip_hud/37_pip_draw.inc"   // 125 lines: PipDraw / PipDrawUnderHud
#include "camera/38_camera_hook.inc"   // 339 lines: UpdateCamera hook: listener, menu camera hold, split aim, framing
#include "camera/38b_grabcam_log.inc"   // f1.241 (log only): GRABCAM - the game's grab / battle camera shots, PlayBattleCameraSequence
#include "input/39_dinput_hide.inc"   // 203 lines: DirectInput: GetDeviceState/Data patch - every controller neutral for the game
#include "input/40b_input2_glue.inc"   // glue to the new input modules in input2/ (separate .cpp files): In2SeatRead = every controller read, seats
#include "input/40c_button_icons.inc"   // players 2/3 see their own controller's button icons (text rebuild switches the fonts' icon style per player)
#include "core/41_install_hooks.inc"   // 61 lines: InstallCoopHooks
#include "input/42_xinput_exports.inc"   // 175 lines: the exported XInput functions
#include "core/43_crash_dllmain.inc"   // 57 lines: crash reporter, DllMain
