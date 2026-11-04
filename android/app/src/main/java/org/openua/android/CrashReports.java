package org.openua.android;

import android.app.*;
import android.content.*;
import android.net.Uri;
import android.os.*;
import android.provider.MediaStore;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.*;
import java.util.zip.*;

/** Bounded, local diagnostics. No account, network upload or save-game contents. */
public final class CrashReports {
    private static final int LIMIT=1024*1024;
    private static File directory(Context c) { File f=new File(c.getFilesDir(),"diagnostics"); f.mkdirs(); return f; }
    private static void write(File f,String text) throws IOException {
        try(Writer w=new OutputStreamWriter(new FileOutputStream(f),StandardCharsets.UTF_8)) { w.write(text); }
    }
    static void javaCrash(Context c,Thread thread,Throwable error) throws IOException {
        File dir=directory(c);
        try(PrintWriter w=new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir,"java-crash.txt")),StandardCharsets.UTF_8))) {
            w.println("DroidUA "+BuildConfig.VERSION_NAME+" / "+new Date()); w.println("Thread: "+thread.getName()); error.printStackTrace(w);
        }
    }
    static void beginGame(Context c) {
        File dir=directory(c);
        try {
            write(new File(dir,"session.txt"),"Started: "+new Date()+"\nVersion: "+BuildConfig.VERSION_NAME+"\nPID: "+android.os.Process.myPid()+"\n");
        } catch(IOException error) { android.util.Log.w("DroidUA","Session diagnostics unavailable",error); }
    }
    private static String device(Context c) {
        ActivityManager am=(ActivityManager)c.getSystemService(Context.ACTIVITY_SERVICE);
        ActivityManager.MemoryInfo mem=new ActivityManager.MemoryInfo(); am.getMemoryInfo(mem);
        return "DroidUA diagnostics\nVersion: "+BuildConfig.VERSION_NAME+" ("+BuildConfig.VERSION_CODE+")\nGenerated: "+new Date()+
            "\nDevice: "+Build.MANUFACTURER+" "+Build.MODEL+"\nAndroid: "+Build.VERSION.RELEASE+" / API "+Build.VERSION.SDK_INT+
            "\nABIs: "+Arrays.toString(Build.SUPPORTED_ABIS)+"\nDisplay: "+c.getResources().getDisplayMetrics()+
            "\nMemory total/available: "+mem.totalMem+" / "+mem.availMem+"\nFree app storage: "+c.getFilesDir().getUsableSpace()+
            "\n\nIncludes app logs, crash traces and display/touch settings; excludes game assets and saved progress.\n";
    }
    private static void entry(ZipOutputStream zip,String name,File f) throws IOException {
        if(!f.isFile() || f.length()>LIMIT) return;
        zip.putNextEntry(new ZipEntry(name));
        try(InputStream in=new FileInputStream(f)) { GameFiles.copy(in,zip,LIMIT); }
        zip.closeEntry();
    }
    static void export(Context c,OutputStream output) throws IOException {
        try(ZipOutputStream zip=new ZipOutputStream(output)) {
            zip.putNextEntry(new ZipEntry("report.txt")); zip.write(device(c).getBytes(StandardCharsets.UTF_8)); zip.closeEntry();
            File dir=directory(c);
            for(String name:new String[]{"session.txt","java-crash.txt","native-crash.txt","system-exit.txt","system-trace.bin",
                "game-log.txt","game-log.txt.1","game-log.txt.2","game-log.txt.3"}) entry(zip,name,new File(dir,name));
            for(String name:new String[]{"android-settings.cfg","android-touch-layout.cfg","android-gamepad.cfg"}) entry(zip,"settings/"+name,new File(c.getFilesDir(),"game/"+name));
        }
    }
    /** Called on a worker when returning to the launcher; native crash handling runs here, not in the signal handler. */
    static String collect(Context c) {
        File dir=directory(c); long latest=0;
        for(String name:new String[]{"java-crash.txt","native-crash.txt"}) latest=Math.max(latest,new File(dir,name).lastModified());
        try {
            if(Build.VERSION.SDK_INT>=30) {
                ActivityManager am=(ActivityManager)c.getSystemService(Context.ACTIVITY_SERVICE);
                for(ApplicationExitInfo exit:am.getHistoricalProcessExitReasons(c.getPackageName(),0,12)) {
                    int reason=exit.getReason();
                    if(reason!=ApplicationExitInfo.REASON_CRASH && reason!=ApplicationExitInfo.REASON_CRASH_NATIVE && reason!=ApplicationExitInfo.REASON_ANR) continue;
                    if(exit.getTimestamp()<c.getSharedPreferences("reports",0).getLong("last",0)) continue;
                    latest=Math.max(latest,exit.getTimestamp());
                    write(new File(dir,"system-exit.txt"),"Time: "+new Date(exit.getTimestamp())+"\nProcess: "+exit.getProcessName()+
                        "\nReason: "+reason+"\nStatus: "+exit.getStatus()+"\nDescription: "+exit.getDescription()+
                        "\nTrace format is supplied by Android (native tombstone protobuf or ANR text). Check the timestamp: this may describe a previous app build.\n");
                    new File(dir,"system-trace.bin").delete();
                    try(InputStream trace=exit.getTraceInputStream()) {
                        if(trace!=null) try(OutputStream out=new FileOutputStream(new File(dir,"system-trace.bin"))) {
                            byte[] bytes=new byte[8192]; int count,total=0;
                            while(total<LIMIT && (count=trace.read(bytes,0,Math.min(bytes.length,LIMIT-total)))>0) { out.write(bytes,0,count); total+=count; }
                        }
                    } catch(IOException unavailable) { android.util.Log.w("DroidUA","Android crash trace unavailable",unavailable); }
                    break; // Android returns newest first.
                }
            }
            if(latest<=c.getSharedPreferences("reports",0).getLong("last",0)) return null;
            String location="Crash details are ready. Use Export diagnostics to save a report ZIP.";
            if(Build.VERSION.SDK_INT>=29) {
                String date=new SimpleDateFormat("yyyy-MM-dd-HHmmss",Locale.ROOT).format(new Date());
                ContentValues values=new ContentValues(); values.put(MediaStore.Downloads.DISPLAY_NAME,"DroidUA-report-"+date+".zip");
                values.put(MediaStore.Downloads.MIME_TYPE,"application/zip"); values.put(MediaStore.Downloads.RELATIVE_PATH,"Download/DroidUA");
                values.put(MediaStore.Downloads.IS_PENDING,1);
                Uri uri=c.getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI,values);
                if(uri!=null) {
                    try {
                        try(OutputStream out=c.getContentResolver().openOutputStream(uri)) { if(out==null) throw new IOException("Cannot create report"); export(c,out); }
                        values.clear();values.put(MediaStore.Downloads.IS_PENDING,0); c.getContentResolver().update(uri,values,null,null);
                        location="Crash report saved in internal storage: Download/DroidUA/"+"DroidUA-report-"+date+".zip";
                    } catch(Exception error) { c.getContentResolver().delete(uri,null,null); throw error; }
                }
            }
            // This runs on a worker. Persist before returning so even a quick
            // process exit cannot generate the same report at the next launch.
            c.getSharedPreferences("reports",0).edit().putLong("last",latest).commit(); return location;
        } catch(Exception error) {
            android.util.Log.w("DroidUA","Automatic crash export unavailable",error);
            return "Crash details are stored in the app. Use Export diagnostics to choose a report location.";
        }
    }
}
