// input2/output.h - the new leader-output module: the game gets exactly ONE controller = the leader seat's.
//
// RE 2026-10-18 (FF7R_Mod_Reference.md, "How the game picks its controller"): the game reads gamepads in ONE of two
// modes, chosen by a global flag (RVA 0x57E9A0C):
//   XInput mode      - UE's XInput device (slots 0-3, Gamepad_* keys). The mod already puts the leader seat's
//                      controller - whatever it really is - into XInput slot 0 and hides the other slots.
//   DirectInput mode - the game's own "EndDirectInput" device (FEndDirectInput, vtable RVA 0x4C29EB0) reads only the
//                      DirectInput controller(s) it has SELECTED: the one saved in the game's settings (picked in its
//                      options menu), or - when no XInput pad is attached - the first one it enumerates. Every
//                      other controller is never opened. While this mode is on, XInput input is ignored.
// So "one funnel" = keep the game in XInput mode and feed the leader through slot 0:
//   CHANNEL_XINPUT - the game's DirectInput selection is refused (FEndDirectInput Select / SelectProduct report
//                    "failed"), and a selection made before the mod started is undone at the device's next Tick
//                    (the game's own Deselect + the mode flag back to XInput). Patched in the device's vtable only.
//   CHANNEL_NONE   - nothing changed (the game may sit in DirectInput mode, as in v19.8).
// [Input2] GameInput=XInput selects CHANNEL_XINPUT (the default since f1.4; GameInput=Game = CHANNEL_NONE, the game decides).
#pragma once
#include "in2.h"

namespace in2 {
namespace output {

enum Channel : uint8_t { CHANNEL_NONE = 0, CHANNEL_XINPUT = 1 };

// exeBase = the game exe's base address. CHANNEL_XINPUT patches the game; it checks every slot first and changes
// nothing (falls back to NONE, logged) if the game's code isn't the version this was made for.
bool Start(Channel ch, LogFn log, uintptr_t exeBase);
Channel Active();
const char* ChannelName(Channel ch);
bool GameInDirectInputMode();   // the game's flag right now (false if unknown)
// (SetLeader / Leader removed - the leader's state was stored every frame but never read; XInput slot 0 is built by
//  the mod itself, input/42)

}   // namespace output
}   // namespace in2
