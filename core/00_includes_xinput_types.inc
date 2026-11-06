#include <windows.h>
#include <tlhelp32.h>   // (finding #34) the game's threads, paused while a hook is written (core/28)
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <cstdio>
#include <share.h>   // _SH_DENYNO (the log is opened shared)
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <cmath>

// We deliberately do NOT #include <Xinput.h> here. That header declares these same
// function names as "import this from a DLL" (dllimport), which collides with us
// declaring them as "this DLL exports them" (dllexport) below - same name, different
// linkage, which the compiler rejects. Instead we define the handful of small,
// stable, publicly documented structs ourselves, under our own copy of the names.
typedef struct _XINPUT_GAMEPAD
{
    WORD  wButtons;
    BYTE  bLeftTrigger;
    BYTE  bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
} XINPUT_GAMEPAD;

typedef struct _XINPUT_STATE
{
    DWORD          dwPacketNumber;
    XINPUT_GAMEPAD Gamepad;
} XINPUT_STATE;

typedef struct _XINPUT_VIBRATION
{
    WORD wLeftMotorSpeed;
    WORD wRightMotorSpeed;
} XINPUT_VIBRATION;

typedef struct _XINPUT_CAPABILITIES
{
    BYTE             Type;
    BYTE             SubType;
    WORD             Flags;
    XINPUT_GAMEPAD   Gamepad;
    XINPUT_VIBRATION Vibration;
} XINPUT_CAPABILITIES;

typedef struct _XINPUT_BATTERY_INFORMATION
{
    BYTE BatteryType;
    BYTE BatteryLevel;
} XINPUT_BATTERY_INFORMATION;

typedef struct _XINPUT_KEYSTROKE
{
    WORD  VirtualKey;
    WCHAR Unicode;
    BYTE  Flags;
    BYTE  UserIndex;
    BYTE  HidCode;
} XINPUT_KEYSTROKE, *PXINPUT_KEYSTROKE;

