# Android TV apps

This folder builds two Android TV apps that install side by side, each with its own
home-screen banner:

| App | Package | Native library |
|---|---|---|
| **WWF No Mercy** | `com.nomercyrecomp.nomercy` | `libNoMercyRecompiled.so` |
| **ECW Born to be Wired** | `com.nomercyrecomp.ecw` | `libECWRecompiled.so` |

They are the `nomercy` and `ecw` product flavors of one Gradle project. Each flavor has its
own name, banner (`app/src/<flavor>/res/drawable-*/banner.png`) and icon. The banners are
the box art with the N64 logo, "Only For" strip, ESRB rating and publisher mark taken out;
the full-size sources are in `art/`.

## Build

Needs JDK 17 and the Android SDK (platform 34). Put `sdk.dir=...` in `local.properties`, then:

```
./gradlew assembleNomercyRelease assembleEcwRelease
adb install app/build/outputs/apk/nomercy/release/app-nomercy-release.apk
adb install app/build/outputs/apk/ecw/release/app-ecw-release.apk
```

Release APKs are signed with the debug key so they can be sideloaded; use your own
keystore before sharing them.

## The game engine

`android/native/build.sh <nomercy|ecw>` cross-compiles the engine (RT64 on Vulkan,
N64ModernRuntime, RecompFrontend, SDL 2.30.12) for arm64 Android with the NDK and copies
`libSDL2.so`, the game library and `libc++_shared.so` into `app/src/<flavor>/jniLibs/`.
It first applies `android/native/patches/` to the runtime submodules (Android window
and Vulkan surface, cross-compiling host tools, Bionic fixes).

- `--engine-only` builds everything except the ROM-generated game code. CI runs this on
  every push.
- Without it, the game code must already be generated from your ROM (`RecompiledFuncs/`
  and `rsp/`, see BUILDING.md). CI does this when the repository has the
  `NOMERCY_ROM_REPO` and `NOMERCY_CI_PAT` secrets, the same ones the Windows build uses.

On Android the engine runs from the app's internal storage in portable mode: its UI
files are unpacked there on first launch, and saves, config and mods live there too.
The ROM is never in the APK. On first start the game asks for it like on PC, using the
system file picker. On TVs without one, copy the ROM into
`Android/data/<package>/files/` (shown on screen) and choose again.

Requires Android 9 or newer and a Vulkan 1.1 GPU.

## Status

- **WWF No Mercy**: the engine compiles for Android. The full game build needs the ROM
  secrets in CI and hasn't been tested on a TV yet.
- **ECW Born to be Wired**: still needs its own recompile. ECW is a relinked hack of No
  Mercy, so it can't be a patch mod (see the main README). It needs its own N64Recomp
  config and symbols, built from the ECW ROM into `libECWRecompiled.so`. Until then, its
  app shows a missing-library message when opened.
