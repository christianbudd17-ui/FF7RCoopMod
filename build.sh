#!/bin/sh
# Builds xinput1_3.dll with mingw-w64, POSIX-threads variant (apt install g++-mingw-w64-x86-64-posix). That is the
# compiler every installed build used; the plain x86_64-w64-mingw32-g++ can be the win32 variant (different DLL).
# Run from this folder: dllmain.cpp includes the module folders (core/, input/, seats/ ...); the new input
# modules (input2/*.cpp, f1.1+) are their own translation units. Each file is compiled to a fixed object name with
# -frandom-seed, so the same source always gives the same DLL (apart from the PE timestamp).
set -e
CXX="x86_64-w64-mingw32-g++-posix -O2 -Wall"
mkdir -p obj
for f in dllmain input2/in2 input2/in2_hid input2/in2_dinput input2/in2_xinput input2/seats input2/output input2/map; do
  o=obj/$(echo $f | tr / _).o
  $CXX -frandom-seed=$f -c $f.cpp -o $o
done
x86_64-w64-mingw32-g++-posix -shared -static -static-libgcc -static-libstdc++ -o xinput1_3.dll \
  obj/dllmain.o obj/input2_in2.o obj/input2_in2_hid.o obj/input2_in2_dinput.o obj/input2_in2_xinput.o obj/input2_seats.o obj/input2_output.o obj/input2_map.o exports.def -ldinput8 -ldxguid -lgdi32
