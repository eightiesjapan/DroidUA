package org.openua.android;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

/** Numeric action IDs match AndroidControls::Action. Start/Select cannot be remapped. */
public final class GamepadConfig {
    public static final String[] BUTTONS={"A","B","X","Y","L1","L2","R1","R2","L3 (left stick click)","R3 (right stick click)"};
    public static final String[] ACTIONS={"Unused","Vehicle creation: open / close","Brake / auto-level / VTOL","Jump to current squad leader","Become squad leader",
        "Mouse left-click / drag","Switch VIEW / SELECT","Fire weapon","Fire alternate weapon",
        "Map: zoom in (+)","Map: zoom out (-)","Cycle units / host gun turrets (F6)","Cycle squad leaders (F7)",
        "Squad aggression level 1","Squad aggression level 5","Open / close map","Open game menu"};
    public final int[] actions={1,2,3,4,5,7,6,8,0,0};
    public static GamepadConfig load(File file) {
        try(InputStream in=new FileInputStream(file)) {return read(in);} catch(IOException|IllegalArgumentException e) {return new GamepadConfig();}
    }
    public static GamepadConfig read(InputStream in) throws IOException {
        ByteArrayOutputStream bytes=new ByteArrayOutputStream();GameFiles.copy(in,bytes,16384);
        try(Scanner s=new Scanner(new String(bytes.toByteArray(),StandardCharsets.UTF_8))) {
            if(!s.next().equals("droidua-gamepad") || s.nextInt()!=1) throw new IOException("Unsupported gamepad settings.");
            GamepadConfig config=new GamepadConfig();
            for(int i=0;i<BUTTONS.length;++i) {
                if(s.nextInt()!=i) throw new IOException("Invalid gamepad button index.");
                int action=s.nextInt();if(action<0 || action>=ACTIONS.length) throw new IOException("Invalid gamepad action.");config.actions[i]=action;
            }
            if(s.hasNext()) throw new IOException("Unexpected gamepad settings.");return config;
        } catch(NoSuchElementException e) {throw new IOException("Incomplete gamepad settings.",e);}
    }
    public void save(File file) throws IOException {
        File temp=new File(file.getParentFile(),file.getName()+".tmp");
        try(Writer out=new OutputStreamWriter(new FileOutputStream(temp),StandardCharsets.UTF_8)) {
            out.write("droidua-gamepad 1\n");for(int i=0;i<BUTTONS.length;++i) out.write(i+" "+actions[i]+"\n");
        }
        if(!temp.renameTo(file)) {temp.delete();throw new IOException("Could not save gamepad settings.");}
    }
}
