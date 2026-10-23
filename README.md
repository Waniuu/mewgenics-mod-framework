# Mewgenics Mod Framework - loader

Source of `version.dll`, the mod loader of the [Mewgenics Mod Framework](https://www.nexusmods.com/mewgenics/mods/183) (game build 1.1.21239, Steam and repack).

It is a `version.dll` proxy: the game loads it at start, it forwards the real `version.dll` calls to the system, hooks a few game functions, loads the mods from the `Mods` folder and shows the F10 mod menu.

The SDK used to write mods is not in this repository; it is downloaded from the [Nexus page](https://www.nexusmods.com/mewgenics/mods/183).

## Download
Get the compiled `version.dll` from the [latest release](../../releases/latest). The SHA-256 is in the release notes.

## Install
1. Copy `version.dll` next to `Mewgenics.exe`.
2. Create a `Mods` folder there and drop mod DLLs in it.
3. Start the game and press **F10**.

## Build
Needs Visual Studio 2022 (MSVC, x64).

```powershell
.\core\build.ps1      # produces core\build\version.dll
```

- `core/core.c`, `core/menu.c`: loader, hooks, event dispatch, mod menu.
- `core/minhook/`: [MinHook](https://github.com/TsudaKageyu/minhook) (BSD-2-Clause, see its `LICENSE.txt`).
- `agent/agent.c`: in-process helper compiled into the loader.
