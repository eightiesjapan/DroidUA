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
        super.onCreate(state);
        useFullDisplay();
    }
    @Override public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (focused) useFullDisplay();
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
    @Override protected String[] getLibraries() { return new String[] { "SDL2", "SDL2_image", "gl4es", "main" }; }
    @Override protected String[] getArguments() {
        File data = new File(getFilesDir(), "game");
        data.mkdirs();
        return new String[] { "-android-data", data.getAbsolutePath() };
    }
}
