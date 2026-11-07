package com.nomercyrecomp.atv;

import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.res.AssetManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.util.Log;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;

/**
 * Hosts the recompiled game through SDL2 (the same SDL the desktop port uses).
 * Each product flavor ships its own native library: libNoMercyRecompiled.so for
 * WWF No Mercy, libECWRecompiled.so for ECW Born to be Wired.
 *
 * Before the engine starts, the game's UI files (bundled as APK assets under
 * "game/") are unpacked into internal storage, which the engine uses as its
 * working folder. The engine's file dialogs call {@link #pickFiles}.
 */
public class GameActivity extends SDLActivity {
    private static final String TAG = "GameActivity";
    private static final int REQUEST_PICK = 4064;

    private CountDownLatch pickLatch;
    private String[] pickResult;
    private String pickExtensions = "";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        unpackGameAssets();
        super.onCreate(savedInstanceState);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "c++_shared", "SDL2", BuildConfig.NATIVE_LIB };
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

    // ---- Game assets -------------------------------------------------------

    /** Copies APK assets/game/** into getFilesDir() once per app version. */
    private void unpackGameAssets() {
        File root = getFilesDir();
        File stamp = new File(root, ".assets-" + BuildConfig.VERSION_CODE);
        if (stamp.exists()) return;
        try {
            copyAssetDir(getAssets(), "game", root);
            stamp.createNewFile();
        } catch (IOException e) {
            Log.e(TAG, "Unpacking game assets failed", e);
        }
    }

    private static void copyAssetDir(AssetManager am, String path, File dest) throws IOException {
        String[] children = am.list(path);
        if (children == null || children.length == 0) {
            // A file (asset dirs always have children).
            dest.getParentFile().mkdirs();
            try (InputStream in = am.open(path); OutputStream out = new FileOutputStream(dest)) {
                copy(in, out);
            }
            return;
        }
        dest.mkdirs();
        for (String child : children) {
            copyAssetDir(am, path + "/" + child, new File(dest, child));
        }
    }

    private static void copy(InputStream in, OutputStream out) throws IOException {
        byte[] buf = new byte[1 << 16];
        int n;
        while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
    }

    // ---- File picker (called from native code, see nfd_android.cpp) --------

    /**
     * Shows the system file picker and blocks the calling (native) thread until
     * the user chooses. Picked files are copied into app storage; returns their
     * paths, or null when cancelled. Must not be called on the UI thread.
     */
    public static String[] pickFiles(String extensions, boolean multiple) {
        final GameActivity activity = (GameActivity) mSingleton;
        if (activity == null) return null;
        return activity.pickFilesBlocking(extensions == null ? "" : extensions, multiple);
    }

    private String[] pickFilesBlocking(String extensions, boolean multiple) {
        pickLatch = new CountDownLatch(1);
        pickResult = null;
        pickExtensions = extensions;
        runOnUiThread(() -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, multiple);
            try {
                startActivityForResult(intent, REQUEST_PICK);
            } catch (ActivityNotFoundException e) {
                // Many Android TV devices ship without a document picker:
                // fall back to files the user copied into the app's folder.
                pickResult = filesInDropFolder(extensions);
                if (pickResult == null) showDropFolderHelp();
                pickLatch.countDown();
            }
        });
        try {
            pickLatch.await();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
        return pickResult;
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != REQUEST_PICK) {
            super.onActivityResult(requestCode, resultCode, data);
            return;
        }
        final List<Uri> uris = new ArrayList<>();
        if (resultCode == RESULT_OK && data != null) {
            if (data.getClipData() != null) {
                for (int i = 0; i < data.getClipData().getItemCount(); i++) {
                    uris.add(data.getClipData().getItemAt(i).getUri());
                }
            } else if (data.getData() != null) {
                uris.add(data.getData());
            }
        }
        // Copy off the UI thread: ROMs are tens of megabytes.
        new Thread(() -> {
            List<String> paths = new ArrayList<>();
            File dir = new File(getFilesDir(), "picked");
            dir.mkdirs();
            for (Uri uri : uris) {
                File dest = new File(dir, displayName(uri));
                try (InputStream in = getContentResolver().openInputStream(uri);
                     OutputStream out = new FileOutputStream(dest)) {
                    copy(in, out);
                    paths.add(dest.getAbsolutePath());
                } catch (IOException | NullPointerException e) {
                    Log.e(TAG, "Copying picked file failed: " + uri, e);
                }
            }
            pickResult = paths.isEmpty() ? null : paths.toArray(new String[0]);
            pickLatch.countDown();
        }).start();
    }

    private String displayName(Uri uri) {
        String name = null;
        try (Cursor c = getContentResolver().query(uri, new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (c != null && c.moveToFirst()) name = c.getString(0);
        } catch (RuntimeException ignored) {
        }
        if (name == null || name.isEmpty()) name = "picked_file";
        return name.replace('/', '_');
    }

    /** The folder users can copy files into over USB/adb when there is no picker. */
    private File dropFolder() {
        File dir = getExternalFilesDir(null);
        return dir != null ? dir : getFilesDir();
    }

    private String[] filesInDropFolder(String extensions) {
        File[] files = dropFolder().listFiles();
        if (files == null) return null;
        List<String> out = new ArrayList<>();
        for (File f : files) {
            if (f.isFile() && matches(f.getName(), extensions)) out.add(f.getAbsolutePath());
        }
        return out.isEmpty() ? null : out.toArray(new String[0]);
    }

    private static boolean matches(String name, String extensions) {
        if (extensions.isEmpty()) return true;
        String lower = name.toLowerCase();
        for (String ext : extensions.split("[,;]")) {
            ext = ext.trim().toLowerCase();
            if (!ext.isEmpty() && lower.endsWith("." + ext)) return true;
        }
        return false;
    }

    private void showDropFolderHelp() {
        String msg = "This TV has no file browser. Copy the file into:\n\n"
                + dropFolder().getAbsolutePath()
                + "\n\n(for example with a USB drive file manager or adb push), then try again.";
        try {
            new AlertDialog.Builder(this).setTitle("Choose a file").setMessage(msg)
                    .setPositiveButton(android.R.string.ok, null).show();
        } catch (RuntimeException e) {
            Toast.makeText(this, msg, Toast.LENGTH_LONG).show();
        }
    }
}
