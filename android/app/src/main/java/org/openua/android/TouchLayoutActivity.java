package org.openua.android;
import android.app.*;
import android.os.Bundle;
import android.view.*;
import android.widget.*;
import android.graphics.*;
import java.io.*;

public class TouchLayoutActivity extends Activity {
    private TouchLayoutConfig draft,saved;
    private Editor editor;
    private File config;
    private boolean changed=false;
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        config=new File(getFilesDir(),"game/android-touch-layout.cfg"); saved=TouchLayoutConfig.load(config); draft=saved;
        if(android.os.Build.VERSION.SDK_INT>=28) {
            WindowManager.LayoutParams a=getWindow().getAttributes();
            a.layoutInDisplayCutoutMode=android.os.Build.VERSION.SDK_INT>=30?WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS:WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(a);
        }
        getWindow().getDecorView().setSystemUiVisibility(5894|View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN|View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        FrameLayout frame=new FrameLayout(this); editor=new Editor(); frame.addView(editor);
        LinearLayout tools=new LinearLayout(this); tools.setBackgroundColor(0xee071218);
        add(tools,"Buttons",()-> {
            boolean[] active=draft.enabled.clone();
            new AlertDialog.Builder(this).setTitle("Show touch buttons")
                .setMultiChoiceItems(TouchLayoutConfig.NAMES,active,(d,which,on)->active[which]=on)
                .setNegativeButton("Cancel",null).setPositiveButton("Done",(d,w)-> {
                    System.arraycopy(active,0,draft.enabled,0,active.length);changed=true;editor.invalidate();
                }).show();
        });
        add(tools,"Reset",()-> { draft=new TouchLayoutConfig(); changed=true; editor.invalidate(); });
        add(tools,"Cancel",this::finish);
        add(tools,"Save",()-> {
            try {
                if(draft.custom) draft=TouchLayoutConfig.from(editor.controls(),editor.width(),editor.height());
                draft.save(config); Toast.makeText(this,"Touch layout saved",Toast.LENGTH_SHORT).show(); finish();
            } catch(IOException error) { new AlertDialog.Builder(this).setMessage(error.getMessage()).setPositiveButton("OK",null).show(); }
        });
        frame.addView(tools,new FrameLayout.LayoutParams(-1,Math.round(48*getResources().getDisplayMetrics().density),Gravity.TOP));
        setContentView(frame);
        Toast.makeText(this,"Drag buttons to move them. Grey buttons are hidden in game; use Buttons to show or hide them.",Toast.LENGTH_LONG).show();
    }
    private void add(LinearLayout bar,String label,Runnable action) {
        Button b=new Button(this); b.setText(label); b.setAllCaps(false); b.setTextColor(0xffb9f4f7); b.setTextSize(15); b.setOnClickListener(v->action.run());
        UaStyle.button(b);
        bar.addView(b,new LinearLayout.LayoutParams(0,-1,1));
    }
    @Override public void onBackPressed() {
        if(changed) new AlertDialog.Builder(this).setMessage("Discard layout changes?").setNegativeButton("Keep editing",null).setPositiveButton("Discard",(d,w)->finish()).show();
        else finish();
    }
    private final class Editor extends View {
        private final Paint paint=new Paint(3);
        private int selected=-1;
        private float offsetX,offsetY;
        private final boolean large,original;
        Editor() {
            super(TouchLayoutActivity.this);
            boolean l=true,o=false;
            try(java.util.Scanner s=new java.util.Scanner(new File(getFilesDir(),"game/android-settings.cfg"))) {
                while(s.hasNext()) { String key=s.next(); if(!s.hasNextInt()) break; int value=s.nextInt(); if(key.equals("large")) l=value==1; if(key.equals("layout")) o=value==1; }
            } catch(IOException ignored) {}
            large=l;original=o; setContentDescription("Drag touch controls to reposition them");
        }
        float height() { return large?400:480; }
        RectF viewport() {
            float w=getWidth(),h=getHeight();
            if(original && w*3>h*4) { float target=h*4/3; return new RectF((w-target)/2,0,(w+target)/2,h); }
            return new RectF(0,0,w,h);
        }
        float width() { RectF r=viewport(); return Math.round(r.width()*height()/Math.max(1,r.height())); }
        TouchLayoutConfig.Control[] controls() { return draft.layout(width(),height()); }
        @Override protected void onDraw(Canvas canvas) {
            canvas.drawColor(0xff071218); RectF vp=viewport(); float scale=vp.height()/height();
            canvas.save();canvas.translate(vp.left,vp.top);canvas.scale(scale,scale);
            paint.setStyle(Paint.Style.FILL);paint.setColor(0xff10252d);canvas.drawRect(0,0,width(),height(),paint);
            paint.setColor(0xff1d3b43); paint.setStrokeWidth(.5f);
            for(int i=0;i<width();i+=40) canvas.drawLine(i,0,i,height()-40,paint);
            for(int i=0;i<height()-40;i+=40) canvas.drawLine(0,i,width(),i,paint);
            paint.setStyle(Paint.Style.FILL);paint.setColor(0xff071218);canvas.drawRect(0,height()-40,width(),height(),paint);
            paint.setColor(0xff72929a);paint.setTextSize(12);paint.setTextAlign(Paint.Align.CENTER);
            canvas.drawText("Game buttons stay clear",width()/2,height()-15,paint);
            TouchLayoutConfig.Control[] list=controls();
            for(int i=0;i<list.length;++i) {
                TouchLayoutConfig.Control c=list[i];
                // Same look as the in-game overlay: dark rim, steel edge, bevel gradient, cyan glow.
                paint.setShader(null);paint.setStyle(Paint.Style.FILL);
                paint.setColor(0xff080e12);canvas.drawCircle(c.x,c.y,c.r,paint);
                paint.setColor(c.enabled?0xff78999f:0xff4a5b5f);canvas.drawCircle(c.x,c.y,c.r-1,paint);
                boolean held=i==selected;
                int top=!c.enabled?0xff2a3a3f:(held?0xff6996a2:0xff4e7078), bottom=!c.enabled?0xff182226:(held?0xff2b4a52:0xff1c2a30);
                paint.setShader(new android.graphics.LinearGradient(0,c.y-c.r,0,c.y+c.r,top,bottom,android.graphics.Shader.TileMode.CLAMP));
                canvas.drawCircle(c.x,c.y,c.r-2,paint);paint.setShader(null);
                if(i<2) { paint.setColor(0xff0a161c);canvas.drawCircle(c.x,c.y,c.r*.55f,paint); }
                paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(1.4f);paint.setColor(c.enabled?0xff3c96a5:0xff24474d);
                canvas.drawArc(new RectF(c.x-c.r+2,c.y-c.r+2,c.x+c.r-2,c.y+c.r-2),30,120,false,paint);
                paint.setStyle(Paint.Style.FILL);paint.setTypeface(UaStyle.font(getContext()));String label=c.label.toUpperCase(java.util.Locale.ROOT);paint.setTextSize(13);float length=paint.measureText(label);
                if(length>c.r*1.65f) paint.setTextSize(13*c.r*1.65f/length);
                paint.setColor(0xff000000);canvas.drawText(label,c.x+1,c.y+1-(paint.ascent()+paint.descent())/2,paint);
                paint.setColor(c.enabled?0xffdcf0f0:0xff6c7a80);
                canvas.drawText(label,c.x,c.y-(paint.ascent()+paint.descent())/2,paint);
            }
            canvas.restore();
        }
        @Override public boolean onTouchEvent(MotionEvent e) {
            RectF vp=viewport();float scale=height()/Math.max(1,vp.height()),x=(e.getX()-vp.left)*scale,y=(e.getY()-vp.top)*scale;
            if(e.getActionMasked()==MotionEvent.ACTION_DOWN) {
                TouchLayoutConfig.Control[] list=controls();
                for(int i=0;i<list.length;++i) { float dx=x-list[i].x,dy=y-list[i].y; if(dx*dx+dy*dy<=list[i].r*list[i].r) {
                    selected=i;offsetX=dx;offsetY=dy;draft=TouchLayoutConfig.from(list,width(),height());changed=true;invalidate();return true;
                }}
                return true;
            }
            if(selected>=0 && e.getActionMasked()==MotionEvent.ACTION_MOVE) { draft.position(selected,x-offsetX,y-offsetY,width(),height());invalidate(); }
            if(e.getActionMasked()==MotionEvent.ACTION_UP || e.getActionMasked()==MotionEvent.ACTION_CANCEL) { selected=-1;invalidate(); }
            return true;
        }
    }
}
