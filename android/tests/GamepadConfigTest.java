package org.openua.android;
import java.io.*;
import java.nio.charset.StandardCharsets;
public class GamepadConfigTest {
    static void check(boolean value,String why) {if(!value) throw new AssertionError(why);}
    public static void main(String[] args) throws Exception {
        GamepadConfig c=new GamepadConfig();check(c.actions.length==10,"Select/Start must not be editable");
        c.actions[0]=14;c.actions[8]=9;c.actions[9]=11;
        File file=new File(args[0],"gamepad-test.cfg");try {
            c.save(file);GamepadConfig restored=GamepadConfig.load(file);check(restored.actions[0]==14 && restored.actions[8]==9 && restored.actions[9]==11,"Remapping lost");
            check(restored.actions[5]==7 && restored.actions[7]==8,"Remapping changed shooting");
            for(String bad:new String[]{"","droidua-gamepad 2","droidua-gamepad 1 10 1","droidua-gamepad 1 0 100"}) {
                boolean rejected=false;try {GamepadConfig.read(new ByteArrayInputStream(bad.getBytes(StandardCharsets.UTF_8)));} catch(IOException e) {rejected=true;}
                check(rejected,"Bad mapping accepted");
            }
            System.out.println("PASS: gamepad persistence, trigger independence, fixed button count and malformed input");
        } finally {file.delete();}
    }
}
