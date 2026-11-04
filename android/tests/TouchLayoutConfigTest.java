package org.openua.android;
import java.io.*;
import java.nio.charset.StandardCharsets;

public class TouchLayoutConfigTest {
    static void check(boolean value,String why) { if(!value) throw new AssertionError(why); }
    static void fits(TouchLayoutConfig config,float w,float h) {
        TouchLayoutConfig.Control[] controls=config.layout(w,h);
        check(controls.length==8,"Missing touch action");
        for(int i=0;i<controls.length;++i) {
            TouchLayoutConfig.Control c=controls[i];
            check(c.x-c.r>=0 && c.x+c.r<=w && c.y-c.r>=0 && c.y+c.r<=h-40,"Control outside safe area");
            for(int j=0;j<i;++j) {
                float x=c.x-controls[j].x,y=c.y-controls[j].y,r=c.r+controls[j].r;
                check(x*x+y*y>r*r,"Overlapping touch targets");
            }
        }
    }
    public static void main(String[] args) throws Exception {
        TouchLayoutConfig wide=TouchLayoutConfig.from(new TouchLayoutConfig().layout(898,400),898,400);
        // Deliberate edge/overlap placements must be repaired after a phone-to-handheld restore.
        wide.position(5,897,399,898,400);wide.position(6,897,399,898,400);wide.position(7,897,399,898,400);
        File file=new File(args[0],"touch-layout-test.cfg");
        try {
            wide.save(file);TouchLayoutConfig restored=TouchLayoutConfig.load(file);check(restored.custom,"Custom layout lost");
            for(float[] size:new float[][]{{533,400},{640,480},{898,400},{1077,480}}) { fits(restored,size[0],size[1]);fits(new TouchLayoutConfig(),size[0],size[1]); }
            check(Math.abs(restored.edge[5]-wide.edge[5])<.00001,"Round trip changed position");
            // Android's rename replaces files atomically; Windows File.renameTo
            // does not replace an existing target, so use a fresh host fixture.
            check(file.delete(),"Could not replace host fixture");
            new TouchLayoutConfig().save(file);check(!TouchLayoutConfig.load(file).custom,"Reset did not persist");
            for(String bad:new String[]{"","droidua-touch-layout 2 custom 0","droidua-touch-layout 1 custom 1 0 0 NaN 0","droidua-touch-layout 1 custom 0 garbage"}) {
                boolean rejected=false;try { TouchLayoutConfig.read(new ByteArrayInputStream(bad.getBytes(StandardCharsets.UTF_8))); } catch(IOException e) { rejected=true; }
                check(rejected,"Invalid layout accepted");
            }
            System.out.println("PASS: layout save/reset, cross-aspect restore, collision repair, malformed layouts");
        } finally { file.delete(); }
    }
}
