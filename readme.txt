To build on Windows use MSYS with MINGW/GCC and MESON installed 
pacman -Syu
pacman --noconfirm -S base-devel mingw-w64-x86_64-toolchain mingw-w64-x86_64-meson mingw-w64-x86_64-gegl

Open C:\msys64\ucrt64.exe shell:

cd ./photonegative
meson setup builddir
ninja -C builddir

mkdir [USER]/AppData/Local/gegl-0.4/plug-ins/photonegative
cp builddir/photonegative.dll [USER]/AppData/Local/gegl-0.4/plug-ins/photonegative