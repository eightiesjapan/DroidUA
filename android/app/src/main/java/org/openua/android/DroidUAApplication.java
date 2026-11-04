package org.openua.android;

/** Installs before activity creation, including native-library loading failures. */
public class DroidUAApplication extends android.app.Application {
    @Override public void onCreate() {
        super.onCreate();
        final Thread.UncaughtExceptionHandler original=Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((thread,error)-> {
            try { CrashReports.javaCrash(this,thread,error); } catch(Throwable ignored) {}
            if(original!=null) original.uncaughtException(thread,error);
            else android.os.Process.killProcess(android.os.Process.myPid());
        });
    }
}
