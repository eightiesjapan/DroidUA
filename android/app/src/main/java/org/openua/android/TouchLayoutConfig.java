package org.openua.android;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

/** Same edge-relative format and fitting rules as android_touch_layout.h. */
public final class TouchLayoutConfig {
    public static final int COUNT=8;
    public boolean custom=false;
    public final int[] right=new int[COUNT];
    public final float[] edge=new float[COUNT],bottom=new float[COUNT];
    public static final class Control {
        public float x,y,r; public final String label;
        Control(float x,float y,float r,String label) { this.x=x;this.y=y;this.r=r;this.label=label; }
    }
    public static TouchLayoutConfig load(File file) {
        try(InputStream in=new FileInputStream(file)) { return read(in); }
        catch(IOException|IllegalArgumentException error) { return new TouchLayoutConfig(); }
    }
    public static TouchLayoutConfig read(InputStream input) throws IOException {
        ByteArrayOutputStream bytes=new ByteArrayOutputStream(); GameFiles.copy(input,bytes,16384);
        try(Scanner s=new Scanner(new String(bytes.toByteArray(),StandardCharsets.UTF_8))) {
            s.useLocale(Locale.ROOT); TouchLayoutConfig config=new TouchLayoutConfig();
            if(!s.next().equals("droidua-touch-layout") || s.nextInt()!=1 || !s.next().equals("custom")) throw new IOException("Unsupported touch layout.");
            int enabled=s.nextInt(); if(enabled!=0 && enabled!=1) throw new IOException("Invalid layout mode.");
            config.custom=enabled==1;
            if(config.custom) for(int i=0;i<COUNT;++i) {
                if(s.nextInt()!=i) throw new IOException("Invalid touch button index.");
                config.right[i]=s.nextInt(); config.edge[i]=s.nextFloat(); config.bottom[i]=s.nextFloat();
                if((config.right[i]!=0 && config.right[i]!=1) || !Float.isFinite(config.edge[i]) || !Float.isFinite(config.bottom[i]) ||
                    config.edge[i]<0 || config.edge[i]>4 || config.bottom[i]<0 || config.bottom[i]>1) throw new IOException("Invalid touch position.");
            }
            if(s.hasNext()) throw new IOException("Unexpected layout data.");
            return config;
        } catch(NoSuchElementException error) { throw new IOException("Incomplete touch layout.",error); }
    }
    public void save(File file) throws IOException {
        File temp=new File(file.getParentFile(),file.getName()+".tmp");
        try(Writer out=new OutputStreamWriter(new FileOutputStream(temp),StandardCharsets.UTF_8)) {
            out.write("droidua-touch-layout 1\ncustom "+(custom?1:0)+"\n");
            if(custom) for(int i=0;i<COUNT;++i) out.write(String.format(Locale.ROOT,"%d %d %.6f %.6f\n",i,right[i],edge[i],bottom[i]));
        }
        if(!temp.renameTo(file)) { temp.delete(); throw new IOException("Could not save the layout."); }
    }
    public static TouchLayoutConfig from(Control[] controls,float w,float h) {
        TouchLayoutConfig config=new TouchLayoutConfig(); config.custom=true;
        for(int i=0;i<COUNT;++i) config.position(i,controls[i].x,controls[i].y,w,h);
        return config;
    }
    public void position(int i,float x,float y,float w,float h) {
        right[i]=x>w/2?1:0; edge[i]=Math.max(0,Math.min(4,(right[i]==1?w-x:x)/h));
        bottom[i]=Math.max(0,Math.min(1,(h-y)/h));
    }
    public Control[] layout(float w,float h) {
        Control[] c={new Control(70,h-90,48,"Move"),new Control(w-70,h-90,48,"Look"),
            new Control(55,h-180,25,"Fire"),new Control(120,h-155,25,"Alt"),
            new Control(w-160,h-65,25,"Mode"),new Control(184,h-85,25,"Brake"),
            new Control(w-175,h-170,25,"Leader"),new Control(w-105,h-190,25,"Lead")};
        for(int i=0;i<COUNT;++i) {
            if(custom) { c[i].x=right[i]==1?w-edge[i]*h:edge[i]*h; c[i].y=h-bottom[i]*h; }
            c[i].x=Math.max(c[i].r+8,Math.min(w-c[i].r-8,c[i].x));
            c[i].y=Math.max(c[i].r+44,Math.min(h-c[i].r-40,c[i].y));
            if(!free(c,i,c[i].x,c[i].y)) {
                float best=Float.MAX_VALUE,bx=c[i].x,by=c[i].y;
                for(float y=c[i].r+44;y<=h-c[i].r-40;y+=8) for(float x=c[i].r+8;x<=w-c[i].r-8;x+=8) {
                    float dx=x-c[i].x,dy=y-c[i].y,cost=dx*dx+dy*dy;
                    if(cost<best && free(c,i,x,y)) { best=cost;bx=x;by=y; }
                }
                c[i].x=bx;c[i].y=by;
            }
        }
        return c;
    }
    private static boolean free(Control[] c,int i,float x,float y) {
        for(int j=0;j<i;++j) { float dx=x-c[j].x,dy=y-c[j].y,gap=c[i].r+c[j].r+6; if(dx*dx+dy*dy<gap*gap) return false; }
        return true;
    }
}
