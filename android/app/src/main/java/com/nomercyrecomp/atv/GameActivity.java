package com.nomercyrecomp.atv;

import org.libsdl.app.SDLActivity;

/**
 * Hosts the recompiled game through SDL2 (the same SDL the desktop port uses).
 * Each product flavor ships its own native library: libNoMercyRecompiled.so for
 * WWF No Mercy, libECWRecompiled.so for ECW Born to be Wired. Prebuilt .so files
 * go in app/src/<flavor>/jniLibs/arm64-v8a/ (see android/README.md).
 */
public class GameActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", BuildConfig.NATIVE_LIB };
    }

    @Override
    protected String getMainSharedObject() {
        return getContext().getApplicationInfo().nativeLibraryDir
                + "/lib" + BuildConfig.NATIVE_LIB + ".so";
    }

    @Override
    protected String[] getArguments() {
        return new String[] { "--game", BuildConfig.GAME_ID };
    }
}
