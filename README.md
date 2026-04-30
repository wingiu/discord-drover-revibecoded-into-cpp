# Discord Drover C++ Port

This is a C++/clang-cl port of Discord Drover's `version.dll` proxy shim and a small console installer. ( 100% pure crystal vibecode ).
Instead of sending  0, 1 before voice initialization, this sends whole fake.bin.

Original is here:  ---> https://github.com/hdrover/discord-drover <---

## Install
Make sure your folder with drover looks like the following:
```
Mode                 LastWriteTime         Length Name
----                 -------------         ------ ----
-a----        xx.xx.xxxx     xx:xx         166400 drover.exe
-a----        xx.xx.xxxx     xx:xx             40 drover.ini
-a----        xx.xx.xxxx     xx:xx           1200 fake.bin
-a----        xx.xx.xxxx     xx:xx         182784 version.dll
```
Close Discord first.

HTTP proxy:

```powershell
.\drover.exe install http 127.0.0.1:1080
```

SOCKS5 proxy:

```powershell
.\drover.exe install socks5 127.0.0.1:2080
```

Direct mode:

```powershell
.\drover.exe install direct
```

The installer writes `drover.ini` and copies `version.dll` , `fake.bin` into Discord `app-*` folders next to `Discord.exe`.
It intentionally does not install the DLL next to `Update.exe`.

## Uninstall

```powershell
.\drover.exe uninstall --remove-ini
```

## Notes

- `fake.bin`, if placed next to `drover.exe`, is copied during install and cached by the DLL at runtime.
- Build artifacts are ignored by `.gitignore`.



## Requirements

- Windows x64
- LLVM/clang-cl
- CMake
- Ninja
- Visual Studio Build Tools or Visual Studio Community
- MinHook prebuilt or built for x64 `/MD`

The build script uses `clang-cl` for compilation and MSVC `link.exe` for linking, because many prebuilt MinHook `.lib` files are LTCG libraries that `lld-link` cannot read.

## Build

Default MinHook location:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build-clang.ps1
```

Custom MinHook folder:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build-clang.ps1 -MinHookRoot "D:\libs\MinHook"
```

Custom MinHook library:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build-clang.ps1 `
  -MinHookRoot "D:\libs\MinHook" `
  -MinHookLib "D:\libs\MinHook\lib\libMinHook-x64-v141-md.lib"
```

Manual CMake example:

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_CXX_COMPILER=clang-cl `
  -DMINHOOK_ROOT="D:\libs\MinHook" `
  -DMINHOOK_LIB="D:\libs\MinHook\lib\libMinHook-x64-v100-md.lib" `
  -DCMAKE_BUILD_TYPE=Release

cmake --build build --config Release
```

Output files:

- `build-clang-msvc\version.dll`
- `build-clang-msvc\drover.exe`

