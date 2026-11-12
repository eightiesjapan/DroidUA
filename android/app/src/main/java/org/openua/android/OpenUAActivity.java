package org.openua.android;

import java.io.File;

public class OpenUAActivity extends org.libsdl.app.SDLActivity {
    private GameFiles.Session engineSession;
    // Native rendering uses the full surface; subtracting system insets again
    // creates black strips on phones. Immersive mode hides navigation bars.
    public int[] getContentInsets() { return new int[] {0,0,0,0}; }
    private void useFullDisplay() {
        if (android.os.Build.VERSION.SDK_INT >= 28) {
            android.view.WindowManager.LayoutParams attrs=getWindow().getAttributes();
            attrs.layoutInDisplayCutoutMode=android.os.Build.VERSION.SDK_INT >= 30
                ? android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
                : android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(attrs);
        }
        getWindow().getDecorView().setSystemUiVisibility(
            android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE |
            android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
            android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
            android.view.View.SYSTEM_UI_FLAG_FULLSCREEN |
            android.view.View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
            android.view.View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }
    @Override protected void onCreate(android.os.Bundle state) {
        try { engineSession=new GameFiles.Session(getFilesDir()); }
        catch(java.io.IOException error) {
            super.onCreate(state);
            android.widget.Toast.makeText(this,error.getMessage(),android.widget.Toast.LENGTH_LONG).show();
            finish(); return;
        }
        CrashReports.beginGame(this);
        super.onCreate(state);
        useFullDisplay();
    }
    @Override public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (focused) { useFullDisplay(); hintFrameRate(); }
    }
    /** Tell high-refresh displays (e.g. 120 Hz phones) that the game renders at a
     *  fixed rate, so the compositor can pick a matching mode for even pacing. */
    private void hintFrameRate() {
        if (android.os.Build.VERSION.SDK_INT < 30 || mSurface == null) return;
        int fps=60;
        try(java.util.Scanner s=new java.util.Scanner(new File(getFilesDir(),"game/android-settings.cfg"))) {
            while(s.hasNext()) { String key=s.next(); if(!s.hasNextInt()) break; int v=s.nextInt(); if(key.equals("fps")) fps=v; }
        } catch(java.io.IOException|RuntimeException ignored) {}
        if (fps < 20 || fps > 240) fps=60;
        try {
            android.view.Surface surface=mSurface.getHolder().getSurface();
            if (surface != null && surface.isValid())
                surface.setFrameRate(fps, android.view.Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE);
        } catch(RuntimeException ignored) {}
    }
    @Override protected void onDestroy() {
        boolean finished=isFinishing();
        super.onDestroy(); // SDL joins the native thread and releases its resources first.
        if(engineSession!=null) try { engineSession.close(); } catch(java.io.IOException ignored) {}
        // The legacy engine/GL wrapper own process-wide state. A completed Exit
        // must start with fresh native globals next time, rather than reuse them.
        // Home/background pause never finishes the activity and keeps the game alive.
        if (finished) android.os.Process.killProcess(android.os.Process.myPid());
    }
    public void showTouchLayoutEditor() {
        runOnUiThread(()->startActivity(new android.content.Intent(this,TouchLayoutActivity.class)));
    }
    /** Options > Exit to Launcher: close the engine (releasing the save lock) and
     *  show the launcher for backups, restore or a new import. */
    public void openLauncher() {
        runOnUiThread(()-> {
            startActivity(new android.content.Intent(this,LauncherActivity.class).putExtra(LauncherActivity.EXTRA_SHOW,true));
            finish();
        });
    }
    public void showGamepadSettings() {
        runOnUiThread(()->startActivity(new android.content.Intent(this,GamepadSettingsActivity.class)));
    }
    @Override protected String[] getLibraries() { return new String[] { "SDL2", "SDL2_image", "gl4es", "main" }; }
    @Override protected String[] getArguments() {
        File data = new File(getFilesDir(), "game");
        data.mkdirs();
        return new String[] { "-android-data", data.getAbsolutePath() };
    }
}
