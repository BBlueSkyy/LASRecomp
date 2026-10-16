package com.bblueskyy.lasrecomp;

import android.os.Bundle;
import android.view.KeyEvent;
import android.view.ViewGroup;
import android.view.WindowManager;
import java.io.File;
import org.libsdl.app.SDLActivity;

/** SDL owns surface, controller discovery, audio and Android pause/resume. */
public final class GameActivity extends SDLActivity {
    private TouchControls controls;
    private static native void configureGameModule(String path);
    private static native void configureDriver(String mode, String directory, String library,
            String nativeLibraryDir, String temporaryDirectory);

    @Override protected String[] getLibraries() {
        return new String[]{"las_platform", "SDL3", "lasrecomp"};
    }
    @Override protected String[] getArguments() {
        // A capture from an earlier launch is not evidence that this boot rendered.
        File oldFrame = new File(getFilesDir(), "first_guest_frame.png");
        if (oldFrame.exists() && !oldFrame.delete())
            throw new IllegalStateException("Cannot clear the previous private guest-frame capture");
        File module = new File(getFilesDir(), "module");
        configureGameModule(GameModule.isPresent(module)
                ? new File(module, "lasgame.so").getAbsolutePath() : "");
        // Dormant until native driver-provider integration is explicitly enabled by the launcher.
        if (getIntent().getBooleanExtra("configure_driver", false)) {
            String mode = getIntent().getStringExtra(DriverImporter.MODE);
            String directory = getIntent().getStringExtra(DriverImporter.DIRECTORY);
            String library = getIntent().getStringExtra(DriverImporter.LIBRARY);
            configureDriver(mode == null ? "system" : mode, directory == null ? "" : directory,
                    library == null ? "" : library, getApplicationInfo().nativeLibraryDir,
                    getCacheDir().getAbsolutePath());
        }
        return new String[]{new File(getFilesDir(), "extracted").getAbsolutePath(), "--play"};
    }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        // SDL displays its own useful error dialog if loading the native libraries failed.
        if (mLayout == null) return;
        controls = new TouchControls(this);
        mLayout.addView(controls, new ViewGroup.LayoutParams(-1, -1));
    }

    @Override protected void onPause() {
        if (controls != null) controls.releaseAll();
        super.onPause();
    }

    @Override public void onBackPressed() { finish(); }

    @Override protected void onDestroy() {
        super.onDestroy();
        // SDL and guest threads cannot safely be re-entered in the same process.
        // This activity is isolated in :game; the importer/launcher stays alive.
        if (isFinishing()) android.os.Process.killProcess(android.os.Process.myPid());
    }

    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        // Android Back returns to the launcher; the separate runtime process is torn down by SDL.
        if (event.getKeyCode() == KeyEvent.KEYCODE_BACK) {
            if (event.getAction() == KeyEvent.ACTION_UP) finish();
            return true;
        }
        return super.dispatchKeyEvent(event);
    }
}
