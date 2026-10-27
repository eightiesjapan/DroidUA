package org.openua.android;

import java.io.File;

public class OpenUAActivity extends org.libsdl.app.SDLActivity {
    @Override protected String[] getLibraries() { return new String[] { "SDL2", "SDL2_image", "gl4es", "main" }; }
    @Override protected String[] getArguments() {
        File data = new File(getFilesDir(), "game");
        data.mkdirs();
        return new String[] { "-android-data", data.getAbsolutePath() };
    }
}
