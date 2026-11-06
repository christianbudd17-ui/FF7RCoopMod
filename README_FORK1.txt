FF7R co-op mod - fork1 (started 2026-10-18 from v19.8)
Work branch for the structured rebuild: split dllmain.cpp into modules, strip superseded code, then a new
controller system from scratch (plan: Claude project doc FF7R_Controller_Redesign_Plan.md; how the mod works:
FF7R_Mod_Reference.md).
Nothing from here is installed into the game until the author says so; the stable build lives in _ModDev\stable.
Fork builds get their own version numbers (fork1 f1.0, f1.1, ...). History: CHANGELOG_FORK1.txt.

BUILD: sh build.sh (mingw-w64 posix) in this folder. xinput1_3_f1_N.dll = the fN builds (NOT installed).

HOW THE SOURCE IS LAID OUT (since f1.0)
dllmain.cpp is only the entry point: the #define COOP_MOD_VERSION and one #include per
module file, in a fixed order. It is still ONE translation unit (unity build): each file may use anything defined
in a file included before it, and the order in dllmain.cpp must not change without care. The files were cut
verbatim from v19.8 (tools/split_v19_8.py). New code goes into the module it belongs to.

Folders: core = start-up, state, log, main tick, hook helpers | input = XInput, DirectInput, PlayStation HID |
seats = who plays whom, player 3 swap, hot swap, leader switches | passes = the extra players' movement and
command passes | menus = command menus, palettes, own command systems | enemies = targeting, balance, attention,
player rules | marker = reticles, status circles, off-screen arrows | camera = co-op camera + camera hook |
pip_hud = split-screen windows, HUD copy, enemy names | settings = FF7RCoopMod.ini.

  file                                           lines  what
  core\00_includes_xinput_types.inc               61  includes, our own copy of the XInput structs
  input\01_slots_real_xinput.inc                      seat read declarations (PadGetSeat / PadGetMine), real xinput1_3.dll, battery cache
  core\02_state_globals_log.inc                  135  CO-OP PASS 2 RE notes, core RVAs, per-player state, WithP2/WithP3, CoopLog
  settings\03_settings_file.inc                 1005  FF7RCoopMod.ini: Cfg struct, parsing, defaults, LoadConfig/EnsureConfig
  seats\04_party_identity.inc                    105  who is player 2/3 (party manager, character types, P2Char)
  passes\05_movement_join_keys.inc               218  pad polling, join/leave + connection, movement-pass key/axis hooks
  core\06_processevent_names.inc                  70  ProcessEvent hook, NameOf, Readable, Rva
  passes\07_action_pass_state.inc                 72  ACTION PASS 2 RVAs, key map, shortcut-palette state
  menus\08_battle_state_p1_menu.inc              115  battle state, hotkeys allowed, player 1's keys / controller, player 1's menu kept off P2 / P3
  menus\08b_command_keys.inc                     155  the WidgetKey hook: the keys each command system sees (player 1's, players 2 / 3's pad, their own menu)
  menus\08c_palette_bcm_before.inc               119  L2 / R2 teammate pick, the shortcut palette, BcmBefore (restructure 3b split menus/08 by job)
  menus\09_presenter_refresh.inc                 30  command HUD presenter refresh hook (own command systems dirty), WidgetSetVis (f1.15: OwnPalettes removed)
  menus\10_second_command_system.inc             211  own command systems (BCM2), Command_C key forwarding, B2MainTurn
  menus\11_simultaneous_menus_ui.inc              25  GpKeyF, UI dispatcher pass-through, Menu2Keys -> CtxKeys (f1.38: the OwnCommandMenu=1 experiment removed)
  passes\12_command_passes.inc                   208  BcmAfter2 / BcmAfter (extra players' command passes), menu slowdown, Hook_BcmUpdate
  seats\13_leader_switch_guard.inc               203  SetPartyLeader / SwitchToCharacter redirects, RequestGuard, field keys, UnitState
  input\14_playstation_hid.inc                    98  the game's own DS4 / DualSense HID reader hook
  passes\15_battle_hooks_ai_off.inc              120  InstallBattleAndSwitchHooks, AI decisions off for human-played characters
  enemies\16_targeting_state.inc                  58  P2 TARGETING RE notes + state
  seats\17_player3_swap.inc                       90  player 3 state swap (RawSwapP3 / LoadP3)
  enemies\18_targeting.inc                       215  enemy list, soft target / R3 lock / flick switching
  marker\19_canvas_marker.inc                    518  HUD canvas calls, game-style reticle, DrawP2Marker
  marker\20_status_offscreen_hud.inc             245  status circles, off-screen arrows, DrawCoopHud
  enemies\21_balance.inc                         137  enemy HP / stagger / focus balance
  enemies\22_attention.inc                       280  enemies share attention (SetTargetByType wrap)
  enemies\23_player_rules_atb.inc                134  human players get leader rules (ATB, stagger, hate patches)
  seats\24_hotswap_pause_switch.inc              188  seats + hot swap (TradeLead), pause check, KO hand-off, D-pad switch
  passes\25_player_frame.inc                     166  per-player frame input, movement pass, AI guard copy, rumble
  core\26_help_tips_reload.inc                   228  controls overlay, tips, settings reload
  core\27_main_tick.inc                           86  Hook_Tick - the per-frame driver
  core\28_inline_hook_weak_refs.inc               75  MakeInlineHook, weak object references
  camera\29_coop_camera.inc                      212  shared co-op camera framing, wall check
  pip_hud\30_pip_window.inc                      569  split-screen windows (SceneCapture2D)
  pip_hud\31_hud_copy.inc                        992  HUD copy walker (game widgets redrawn over the windows)
  menus\32_menus_into_window.inc                 179  menu target cursor / Command_C moved into windows
  menus\34_focus_1_contexts.inc                 133  the game's focus manager, widget owners (OWNER_*), per-player focus contexts (restructure 3d)
  menus\34_focus_2_hooks_log.inc                130  the focus hooks: each push / pop / focus change routed to its player's context + logged (FOCUS lines)
  menus\34_focus_3_pad_keys.inc                 173  players 2 / 3's D-pad / A / B as UI keys in their own focus context (CtxKeys)
  menus\34b_shared_items.inc                     96  f1.21: queued item commands count as used (ReserveQueuedItems)
  menus\35_bcm2_into_window.inc                   46  own Command_C into the player's window
  pip_hud\36_enemy_names.inc                     125  enemy names learnt from EnemyStatus_C
  pip_hud\37_pip_draw.inc                        125  PipDraw / PipDrawUnderHud
  camera\38_camera_hook.inc                      339  UpdateCamera hook: listener, menu camera hold, split aim, framing
  input\39_dinput_hide.inc                       203  DirectInput: GetDeviceState/Data patch - every controller neutral for the game
  input\40b_input2_glue.inc                     363  f1.1: glue to the new input modules (input2\)
  core\41_install_hooks.inc                       61  InstallCoopHooks
  input\42_xinput_exports.inc                    175  the exported XInput functions
  core\43_crash_dllmain.inc                       57  crash reporter, DllMain

NEW MODULES (f1.1+), each its own .cpp file (compiled separately, see build.sh) - they only talk to the old
code through their header and the glue file input\40b_input2_glue.inc:
  input2\in2.h / in2.cpp         devices: finds every controller (XInput, DirectInput, PlayStation HID) by identity
                                 (VID:PID + device path), reads them into one layout, ties XInput slots to them.
                                 in2.cpp = the device list, the background thread, the interface; one file per channel
                                 (f1.204): in2_hid.cpp (PlayStation / HID), in2_dinput.cpp (DirectInput), in2_xinput.cpp
                                 (XInput slots); in2_internal.h = what those four share (not for the rest of the mod)
  input2\seats.h / seats.cpp     seats: which controller each seat has, by identity (survives unplug / re-plug)
  input2\map.h / map.cpp         button maps: a DirectInput controller -> the common layout (same result as the old code)
  input2\output.h / output.cpp   leader output: keeps the game in XInput mode (its DirectInput controller pick refused),
                                 so XInput slot 0 = the leader is its only controller. f1.4: on by default
                                 ([Input2] GameInput=Game turns it off - then the leader only reaches the game in XInput mode)
Since f1.7 they are always on and read every player's controller (the old reader - VPad, the DualSense taken from
the game's own reads - is gone). Which controller each seat has: [Input2] Seat1/Seat2/Seat3 = Keyboard | None | Auto |
VID:PID | VID:PID#n | path:<device path> (blank = Auto; f1.14: the old [PlayerN] ControllerSlot / DirectInputController /
Device keys are no longer read); each seat keeps its controller by identity (unplug / re-plug, hot swap). Button maps for
generic pads: [Pad.VID:PID] sections. [Input2] GameInput=XInput (default) keeps the game in XInput mode. [Input2] Mode / Slot0 / Pads / Seats (f1.1-f1.6)
are no longer needed. f1.9: no controller numbers inside the mod any more - every read asks for a seat's controller
(seat 1 = the leader = the game's XInput slot 0). Controller list by identity: FF7RCoopMod_Controllers2.txt next to the ini.
f1.11: [Input2] SeatN picks by controller MODEL (VID:PID) and wins over the old [PlayerN] keys; Auto = the first free
gamepad of any kind; [Pad.VID:PID] = button map per controller model; JoinAtStart = 1 / 0 / Auto.
Tests (no game needed): sh tools/test.sh builds and runs all of them (stops at the first failure; "sh tools/test.sh
  settings OLDDIR" also compares the settings dumps with an earlier run). One by one: tools\tests\unit_in2.cpp, unit_seats.cpp, smoke_in2.cpp, unit_output.cpp, unit_map.cpp, e.g.
  x86_64-w64-mingw32-g++-posix -O1 -static -o unit.exe tools/tests/unit_in2.cpp input2/in2*.cpp -ldinput8 -ldxguid
  (unit_seats.cpp: add -DIN2_TEST_HOOKS and input2/seats.cpp - it makes up controllers through in2.h's test hooks;
  smoke_in2.cpp needs input2/*.cpp on the line and -lhid -lsetupapi)
  f1.85: unit_settings.cpp (built the same way, no libraries) loads an ini through LoadConfig and prints every setting;
  sh tools/tests/run_settings.sh <exe> <outdir> runs it on tools/tests/settings_cases/*.ini - diff two builds' outdirs.
Plan: once a new module is tested and switched on for real, the old code it replaces is removed (removal
checklist in the Claude project).
