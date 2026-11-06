#!/bin/sh
# Builds FF7RCoopMod_Settings.exe from FF7RCoopMod_Settings.pyw with PyInstaller in a Windows Python under wine
# (python-build-standalone 3.12.7 install_only + pyinstaller 6.11.1). Usage: sh tools/build_exe.sh <path to the Windows python.exe>
set -e
PY="$1"; [ -n "$PY" ] || { echo "usage: sh tools/build_exe.sh /path/to/windows/python.exe"; exit 1; }
cd "$(dirname "$0")/.."
rm -rf out/exe && mkdir -p out/exe
cp FF7RCoopMod_Settings.pyw out/exe/FF7RCoopMod_Settings.py
cp tools/exe/version_info.txt out/exe/
cd out/exe
WINEDEBUG=-all wine "$PY" -m PyInstaller --noconfirm --clean --onefile --windowed --noupx --name FF7RCoopMod_Settings --version-file version_info.txt FF7RCoopMod_Settings.py
ls -la dist/FF7RCoopMod_Settings.exe
