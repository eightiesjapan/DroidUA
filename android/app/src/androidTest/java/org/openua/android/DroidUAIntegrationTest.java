package org.openua.android;

import android.app.*;
import android.content.*;
import android.graphics.Bitmap;
import android.os.*;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.util.zip.*;

/** No dependency downloads; runs only on a disposable emulator, never a user's saves. */
public class DroidUAIntegrationTest extends Instrumentation {
    private Activity editor;
    private void check(boolean okay,String why) { if(!okay) throw new AssertionError(why); }
    @Override public void onCreate(Bundle args) { super.onCreate(args);start(); }
    private Button button(View root,String label) {
        if(root instanceof Button && ((Button)root).getText().toString().equals(label)) return (Button)root;
        if(root instanceof ViewGroup) { ViewGroup group=(ViewGroup)root;for(int i=0;i<group.getChildCount();++i) { Button found=button(group.getChildAt(i),label);if(found!=null) return found; } }
        return null;
    }
    private void click(String text) { runOnMainSync(()-> {Button b=button(editor.getWindow().getDecorView(),text);check(b!=null,"Missing "+text);b.performClick();});waitForIdleSync(); }
    private void open() { editor=startActivitySync(new Intent(getTargetContext(),TouchLayoutActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));waitForIdleSync(); }
    private void finger(int action,float x,float y,long down) {
        MotionEvent e=MotionEvent.obtain(down,SystemClock.uptimeMillis(),action,x,y,0);e.setSource(android.view.InputDevice.SOURCE_TOUCHSCREEN);
        check(getUiAutomation().injectInputEvent(e,true),"Touch injection failed");e.recycle();
    }
    @Override public void onStart() {
        Bundle result=new Bundle();
        try {
            check(Build.HARDWARE.contains("ranchu") || Build.HARDWARE.contains("goldfish"),"Use a disposable emulator only");
            Context c=getTargetContext();File game=new File(c.getFilesDir(),"game");game.mkdirs();
            File config=new File(game,"android-touch-layout.cfg");new TouchLayoutConfig().save(config);
            File output=new File(c.getFilesDir(),"integration-results");output.mkdirs();
            open();
            Bitmap before=getUiAutomation().takeScreenshot();int w=before.getWidth(),h=before.getHeight();
            try(OutputStream out=new FileOutputStream(new File(output,"layout-default.png"))) {before.compress(Bitmap.CompressFormat.PNG,100,out);}
            // Defaults use a 400-high logical view for Large; the left pad starts at (70,310).
            float scale=h/400f;long down=SystemClock.uptimeMillis();
            finger(MotionEvent.ACTION_DOWN,70*scale,310*scale,down);
            finger(MotionEvent.ACTION_MOVE,90*scale,180*scale,down);finger(MotionEvent.ACTION_UP,90*scale,180*scale,down);
            waitForIdleSync();click("Save");
            TouchLayoutConfig custom=TouchLayoutConfig.load(config);check(custom.custom,"Drag/Save did not persist custom layout");
            check(custom.bottom[0]>.4f,"Pad did not move to requested area");
            open();Bitmap saved=getUiAutomation().takeScreenshot();
            try(OutputStream out=new FileOutputStream(new File(output,"layout-custom.png"))) {saved.compress(Bitmap.CompressFormat.PNG,100,out);}
            click("Reset");click("Save");check(!TouchLayoutConfig.load(config).custom,"Reset/Save failed");
            open();click("Reset");click("Cancel");check(!TouchLayoutConfig.load(config).custom,"Cancel changed saved layout");
            CrashReports.javaCrash(c,Thread.currentThread(),new RuntimeException("Intentional diagnostic test fixture, not a game crash"));
            String report=CrashReports.collect(c);check(report!=null,"Crash fixture not collected");
            check(CrashReports.collect(c)==null,"Duplicate automatic crash report");
            File zipFile=new File(output,"diagnostics.zip");try(OutputStream out=new FileOutputStream(zipFile)) {CrashReports.export(c,out);}
            try(ZipFile zip=new ZipFile(zipFile)) {
                check(zip.getEntry("report.txt")!=null && zip.getEntry("java-crash.txt")!=null,"Missing diagnostics");
                check(zip.getEntry("settings/android-touch-layout.cfg")!=null,"Missing layout diagnostic");
                check(zip.getEntry("SAVE")==null,"Diagnostics included saved progress");
            }
            result.putString("stream","PASS: real Android editor drag/save/reset/cancel, diagnostic ZIP and automatic collection/deduplication\n"+report+"\n");
            finish(Activity.RESULT_OK,result);
        } catch(Throwable error) { result.putString("stream",android.util.Log.getStackTraceString(error));finish(Activity.RESULT_CANCELED,result); }
    }
}
