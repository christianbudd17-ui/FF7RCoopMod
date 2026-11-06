// input2/in2.h - the NEW input module ("one funnel", plan doc FF7R_Controller_Redesign_Plan.md), fork1 f1.1+.
//
// Its own files (input2/in2.cpp + in2_hid / in2_dinput / in2_xinput .cpp, shared parts in in2_internal.h): it never
// touches the old code's variables. The old code talks to it
// only through this header (glue: input/40b_input2_glue.inc).
//
// It finds every controller itself, identifies each physical controller by its device path / VID:PID (not by name)
// and reads all of them into one common layout. f1.1-f1.6 it only watched / was switched on by [Input2] keys; since
// it is always on and is the only reader of the players' controllers (glue: input/40b_input2_glue.inc).
//
// Channels it reads (all on its own background thread, never the game's):
//  - XInput slots 0-3 (through the real System32 xinput1_3, not the mod's exports)
//  - DirectInput game controllers (its own device objects, read with the ORIGINAL GetDeviceState, not the mod's filter)
//  - HID: every gamepad for identity; DualShock 4 / DualSense reports are read from its own handle
// One physical controller = one Device, merged by its device interface path (DirectInput and HID give the same
// path). XInput slots are tied to their controller by "IG_" in the path plus matching button presses.
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>

namespace in2 {

typedef void  (*LogFn)(const char* fmt, ...);
typedef DWORD (WINAPI* XGetState_t)(DWORD slot, void* xinputState);    // the real xinput1_3 XInputGetState
typedef void* (*DiOrigGetState_t)(void* dev);    // the ORIGINAL IDirectInputDevice8::GetDeviceState for this device
                                                 // (nullptr = the device table isn't patched: call the table entry)
typedef HWND  (*GameWindow_t)();                 // the game's top window (DirectInput cooperative level)

struct Host
{
    LogFn log;
    XGetState_t xGetState;
    DiOrigGetState_t diOrigGetState;
    GameWindow_t gameWindow;
};

enum Layout : uint8_t { LAYOUT_XBOX = 0, LAYOUT_PLAYSTATION = 1, LAYOUT_GENERIC = 2 };
enum Api : uint8_t { API_XINPUT = 1, API_DINPUT = 2, API_HID = 4 };

// Normalised controller state. Buttons use the XInput bits (D-pad 0x1/0x2/0x4/0x8 up/down/left/right, Start 0x10,
// Back 0x20, LS 0x40, RS 0x80, LB 0x100, RB 0x200, A 0x1000, B 0x2000, X 0x4000, Y 0x8000). PlayStation pads:
// cross = A, circle = B, square = X, triangle = Y, Create/Share = Back, Options = Start (the mod's usual mapping).
// Generic DirectInput pads (unknown layout) only fill raw / the sticks; their button meaning comes later from the
// settings app (per controller identity).
struct Pad
{
    uint16_t buttons;
    uint8_t  lt, rt;
    int16_t  lx, ly, rx, ry;    // XInput convention: up = positive Y
    uint32_t raw;               // DirectInput buttons 1-32 as bits 0-31 (generic pads; 0 otherwise)
    // the DirectInput reading as it came (only when source == API_DINPUT), so a per-controller button map
    // (input2/map.h) can be applied exactly like the old code's DirectInput controllers
    uint32_t diButtons;         // buttons 1-32 as bits 0-31 (every layout)
    int32_t  diAxis[8];         // X Y Z Rx Ry Rz Slider1 Slider2, -32768..32767
    uint32_t diPov;             // POV 0 (hundredths of a degree, 0xFFFFFFFF = centred)
};

struct Device
{
    int      id;                // stable for the session (#1, #2 ...), never reused
    bool     present;
    uint8_t  apis;              // Api bits: where this controller shows up
    uint8_t  source;            // Api bit its state is read from right now (XInput > HID > DirectInput)
    Layout   layout;
    bool     xinputFamily;      // "IG_" in its path = an XInput controller (it also has an XInput slot)
    int      xinputSlot;        // 0-3 once matched, else -1
    uint16_t vid, pid;
    char     name[64];          // product name (UTF-8; the first one seen - HID or DirectInput)
    char     diName[64];        // its DirectInput product name and instance name (UTF-8, "" = not seen through
    char     diInstName[64];    //       DirectInput) - shown in the log / FF7RCoopMod_Controllers2.txt
    char     path[200];         // device interface path, lower case (identity), may be shortened
    Pad      state;
    DWORD    stateMs;           // GetTickCount() of the last good read (0 = never)
    bool     gamepad;           // looks like a gamepad (LooksLikeGamepad) - only these are taken by Auto
};

// is this a gamepad (not a wheel / pedals / flight stick)? Xbox-family and PlayStation pads always; else by
// the DirectInput device type (0x15 gamepad yes; 0x16 driving, 0x17 flight, 0x18 first person, 0x19-0x1C no) or the
// HID usage (5 = gamepad); a "joystick" (DirectInput 0x14 / HID usage 4) only with at least 8 buttons and 2 axes
// (cheap pads often call themselves joysticks; pedals have axes but no buttons). Unknown (no DirectInput info yet) = no.
bool LooksLikeGamepad(bool xinputFamily, bool sony, int hidUsage, int diType, int diButtons, int diAxes);

bool Start(const Host& host);   // starts the background thread; false = couldn't (logged)
bool Running();

// Thread-safe copies (the background thread owns the list).
int  Count();                                   // devices seen this session (present or not)
bool Get(int index, Device* out);               // index 0..Count()-1
bool FindById(int id, Device* out);              // that device (present or not)
// (finding #32) a device's reading without its names / path - for the controller reads (many a second)
struct Reading { bool present; uint8_t source; Layout layout; bool xinputFamily; int xinputSlot; uint16_t vid, pid; Pad state; DWORD stateMs; };
bool ReadById(int id, Reading* out);
// (f1.85, D-6: FindXInputSlot / FindByName / FindByDiInstance / XInputSlotConnected and Device::diInstance removed - nobody used them)
void Describe(const Device& d, char* out, size_t n);    // "#3 DualSense 054C:0CE6 (HID+DI, PlayStation)"

#ifdef IN2_TEST_HOOKS
// (review #20) TEST ONLY - made-up controllers, so a test needs no real ones and doesn't reach inside in2.cpp
// (tools/tests/unit_seats.cpp, built with -DIN2_TEST_HOOKS). Not in the mod's DLL.
void TestStart(LogFn log);   // instead of Start: no thread, no scanning - only the test's controllers
int  TestAddController(const char* path, uint16_t vid, uint16_t pid, bool gamepad);   // present, DirectInput; returns its id
bool TestSetPresent(int id, bool present);   // plugged in / unplugged (the list is published at once)
#endif

}   // namespace in2
