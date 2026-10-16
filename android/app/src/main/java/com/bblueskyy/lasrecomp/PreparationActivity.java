package com.bblueskyy.lasrecomp;

import android.app.Activity;
import android.content.Intent;
import android.content.res.AssetManager;
import android.os.Binder;
import android.os.Bundle;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;
import android.widget.TextView;
import java.io.File;
import java.io.InputStream;
import java.io.IOException;

/** Dedicated compiler process. No platform, SDL, or game runtime library is loaded here. */
public final class PreparationActivity extends Activity {
    public interface Progress { void onProgress(String message); }
    public static native String buildIdentity();
    public static native String prepare(String gameDir, String assetsRoot, String nativeLibDir, Progress progress);
    private static final String CALLBACK = "prepare_callback";
    private static final int READY = IBinder.FIRST_CALL_TRANSACTION;
    private TextView status;
    private IBinder exitToken;

    /** Invoke only when the complete native embedded preparation backend is enabled. */
    public static void start(Activity launcher, Intent runtimeIntent) {
        Intent runtime = new Intent(runtimeIntent);
        Binder callback = new Binder() {
            @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
                    throws RemoteException {
                if (code != READY) return super.onTransact(code, data, reply, flags);
                IBinder token = data.readStrongBinder();
                if (token == null) throw new RemoteException("Missing preparation exit token");
                token.linkToDeath(() -> launcher.runOnUiThread(() -> launcher.startActivity(runtime)), 0);
                // The game starts only from binderDied, after all compiler mappings are gone.
                Parcel ack = Parcel.obtain();
                try { token.transact(READY, ack, null, IBinder.FLAG_ONEWAY); }
                finally { ack.recycle(); }
                if (reply != null) reply.writeNoException();
                return true;
            }
        };
        Bundle extras = new Bundle();
        extras.putBinder(CALLBACK, callback);
        Intent intent = new Intent(launcher, PreparationActivity.class);
        intent.putExtras(extras);
        launcher.startActivity(intent);
    }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        status = new TextView(this);
        status.setPadding(24, 32, 24, 32);
        status.setTextIsSelectable(true);
        status.setText("Starting embedded preparation…");
        setContentView(status);
        new Thread(this::runPreparation, "las-prepare").start();
    }

    private void progress(String message) { runOnUiThread(() -> status.setText(message)); }

    private void runPreparation() {
        try {
            System.loadLibrary("las_setup");
            AssetManager assets = getAssets();
            File embedded = EmbeddedAssets.install(new EmbeddedAssets.Source() {
                public String[] list(String path) throws IOException { return assets.list(path); }
                public InputStream open(String path) throws IOException { return assets.open(path); }
            }, getFilesDir(), buildIdentity(), this::progress);
            String error = prepare(new File(getFilesDir(), "extracted").getAbsolutePath(),
                    embedded.getAbsolutePath(), getApplicationInfo().nativeLibraryDir, this::progress);
            if (error != null) { progress("Preparation failed: " + error); return; }
            progress("Preparation complete. Releasing compiler process…");
            IBinder callback = getIntent().getExtras() == null ? null : getIntent().getExtras().getBinder(CALLBACK);
            if (callback == null) throw new IOException("Launcher callback is unavailable; return and retry");
            exitToken = new Binder() {
                @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
                        throws RemoteException {
                    if (code != READY) return super.onTransact(code, data, reply, flags);
                    runOnUiThread(() -> { finish(); android.os.Process.killProcess(android.os.Process.myPid()); });
                    return true;
                }
            };
            Parcel data = Parcel.obtain(), reply = Parcel.obtain();
            try {
                data.writeStrongBinder(exitToken);
                if (!callback.transact(READY, data, reply, 0)) throw new IOException("Launcher rejected result");
                reply.readException();
            } finally { data.recycle(); reply.recycle(); }
        } catch (Exception | LinkageError error) {
            progress("Preparation failed: " + error);
        }
    }

    @Override public void onBackPressed() { finish(); }
    @Override protected void onDestroy() {
        super.onDestroy();
        if (isFinishing()) android.os.Process.killProcess(android.os.Process.myPid());
    }
}
