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

## Status: the game engine is not in these APKs yet

The apps start the game through SDL2 (`GameActivity`, SDL 2.30.3 Java glue in
`org.libsdl.app`), which loads `libSDL2.so` and the flavor's game library from
`app/src/<flavor>/jniLibs/arm64-v8a/`. Those libraries don't exist yet, so today the
apps install and show their banners, then report a missing library on launch.

Still to do:

1. **Android/arm64 build of the port.** The top-level `CMakeLists.txt` and the runtime
   stack (N64ModernRuntime, RecompFrontend, RT64) are Windows-only right now. RT64 has to
   run on Vulkan through SDL's Android window, and the game has to read the ROM and write
   saves through Android storage.
2. **ECW Born to be Wired recompile.** ECW is a relinked hack of No Mercy, so it can't be a
   patch mod (see the main README). It needs its own N64Recomp run and symbols, built
   into `libECWRecompiled.so`.

The game reads `--game nomercy` or `--game ecw` from its arguments.
