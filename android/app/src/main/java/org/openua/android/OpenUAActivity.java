package org.openua.android;

import java.io.File;

public class OpenUAActivity extends org.libsdl.app.SDLActivity {
    private volatile int[] contentInsets = new int[] {0,0,0,0};
    public int[] getContentInsets() { return contentInsets.clone(); }
    @Override protected void onCreate(android.os.Bundle state) {
        super.onCreate(state);
        getWindow().getDecorView().setOnApplyWindowInsetsListener((view, insets) -> {
            int left=0, top=0, right=0, bottom=0;
            if (android.os.Build.VERSION.SDK_INT >= 28 && insets.getDisplayCutout() != null) {
                android.view.DisplayCutout cutout=insets.getDisplayCutout();
                left=cutout.getSafeInsetLeft(); top=cutout.getSafeInsetTop();
                right=cutout.getSafeInsetRight(); bottom=cutout.getSafeInsetBottom();
            }
            contentInsets=new int[] {Math.max(left,insets.getSystemWindowInsetLeft()),
                Math.max(top,insets.getSystemWindowInsetTop()),Math.max(right,insets.getSystemWindowInsetRight()),
                Math.max(bottom,insets.getSystemWindowInsetBottom())};
            return insets;
        });
        getWindow().getDecorView().requestApplyInsets();
    }
    @Override protected void onDestroy() {
        boolean finished=isFinishing();
        super.onDestroy(); // SDL joins the native thread and releases its resources first.
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
