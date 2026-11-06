// input2/map.h - per-controller button maps. A DirectInput controller whose layout isn't known (e.g. the
// msi GC30) is turned into the common XInput layout with a button map ([Pad.VID:PID] sections, below - the old
// [PlayerN] DirectInputA..RS / LeftX..RightY keys are no longer read). ApplyMap gives exactly the old code's result
// (VPadGet) for the same DirectInput reading - checked by tools/tests/unit_map.cpp.
#pragma once
#include "in2.h"

namespace in2 {

struct ButtonMap
{
    int  btn[12];   // A B X Y LB RB LT RT Back Start LS RS: DirectInput button number 1-32, 0 = none
    int  axis[4];   // LX LY RX RY: 0-7 = X Y Z Rx Ry Rz Slider1 Slider2
    bool trigZ;     // LT / RT share the Z axis (Xbox-type pads through DirectInput)
};

struct XPad { uint16_t buttons; uint8_t lt, rt; int16_t lx, ly, rx, ry; };   // XINPUT_GAMEPAD layout

// a button map per controller MODEL - section [Pad.VID:PID] of the ini (e.g. [Pad.0D22:0C34]), written by the
// settings app or by hand. Keys: A B X Y LB RB LT RT Back Start LS RS = DirectInput button numbers (1 = first, 0 = none),
// LeftX LeftY RightX RightY = X Y Z Rx Ry Rz Slider1 Slider2, Name = just a note. (TriggersOnZ no longer read -
// trigZ comes only from the built-in Xbox-type default in input/40b.) Missing keys: the PlayStation numbering (Cross 2, Circle 3, Square 1, Triangle 4,
// L1 5, R1 6, L2 7, R2 8, Share 9, Options 10, L3 11, R3 12; sticks X/Y + Z/Rz).
// Returns false (out untouched) when the ini has no such section.
bool MapFromIni(const wchar_t* iniPath, uint16_t vid, uint16_t pid, ButtonMap* out);

// raw = a Pad read through DirectInput (diButtons / diAxis / diPov filled)
void ApplyMap(const Pad& raw, const ButtonMap& m, XPad* out);
// a Pad from XInput / HID (already in the common layout) -> XPad
void FromPad(const Pad& p, XPad* out);

}   // namespace in2
