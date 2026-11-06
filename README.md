FINAL FANTASY VII REMAKE INTERGRADE - LOCAL CO-OP MOD - SOURCE CODE (1.0 beta)
==============================================================================
Created by Christian Budd. MIT License - see LICENSE.txt. A fan project, not affiliated with or endorsed by Square
Enix; no game files or assets are included. The code works on the running Steam version of the game (memory
addresses, hooks); it does not change the game's files.

WHAT'S HERE
- dllmain.cpp + the module folders (core, input, input2, seats, passes, menus, enemies, marker, camera, pip_hud,
  settings): the mod, built as xinput1_3.dll. dllmain.cpp #includes the .inc modules in a fixed order - it is ONE
  translation unit (each file may use what an earlier one defines). input2/*.cpp are separate units (controllers).
  README_FORK1.txt has a map of every module file.
- FF7RCoopMod_Settings.pyw: the settings app (Python 3 + tkinter). tools/build_exe.sh makes the .exe (PyInstaller).
- tools/make_presets.py: writes the ready-made difficulty profiles (release/FF7RCoopMod_Profiles).
- tools/test.sh + tools/tests: unit tests (no game needed). tools/check_log.py: checks a FF7RCoopMod_Coop.log.
- CHANGELOG_FORK1.txt: every build and why (newest first). README_RELEASE_DRAFT.txt: the player README.

BUILD (Linux / WSL, mingw-w64 POSIX-threads variant)
  sudo apt install g++-mingw-w64-x86-64-posix
  sh build.sh                 -> xinput1_3.dll
  sh tools/test.sh            -> unit tests (also needs wine)
Copy xinput1_3.dll next to ff7remake_.exe (End\Binaries\Win64) to try it.
