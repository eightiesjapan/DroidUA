package org.openua.android;
import java.io.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.zip.*;

public class GameFilesTest {
    static final class Local implements GameFiles.Source {
        final File f; Local(File f) { this.f=f; }
        public String name() { return f.getName(); }
        public boolean directory() { return f.isDirectory(); }
        public List<GameFiles.Source> children() throws IOException {
            List<GameFiles.Source> out=new ArrayList<>(); File[] list=f.listFiles();
            if(list==null) throw new IOException("Not a readable folder");
            for(File child:list) out.add(new Local(child)); return out;
        }
        public InputStream open() throws IOException { return new FileInputStream(f); }
    }
    static void put(File root,String path,String value) throws IOException {
        File f=GameFiles.child(root,path); f.getParentFile().mkdirs(); Files.write(f.toPath(),value.getBytes(StandardCharsets.UTF_8));
    }
    static String get(File root,String path) throws IOException { return new String(Files.readAllBytes(new File(root,path).toPath()),StandardCharsets.UTF_8); }
    static void check(boolean condition,String text) { if(!condition) throw new AssertionError(text); }
    interface Fails { void run() throws Exception; }
    static void reject(Fails work) throws Exception { boolean failed=false; try { work.run(); } catch(IOException e) { failed=true; } check(failed,"Unsafe operation accepted"); }
    static void malicious(File file,String... names) throws IOException {
        try(ZipOutputStream zip=new ZipOutputStream(new FileOutputStream(file))) {
            for(String name:names) { zip.putNextEntry(new ZipEntry(name)); zip.write("invalid".getBytes(StandardCharsets.UTF_8)); zip.closeEntry(); }
        }
    }
    public static void main(String[] args) throws Exception {
        File temp=Files.createTempDirectory(new File(args[0]).toPath(),"game-files-test-").toFile();
        try {
            File source=new File(temp,"download/OpenUA"), files=new File(temp,"private"), game=new File(files,"game"); files.mkdirs();
            put(source,"DATA/world.ini","world"); put(source,"DATA/OBJECTS/base","asset");
            put(source,"ENV/assign.txt","paths"); put(source,"ENV/user.def","SDU7"); put(source,"LEVELS/SINGLE/test.ldf","level");
            put(source,"LOCALE/English.lng","English"); put(source,"res/Pointer.cur","cursor");
            put(source,"fonts/LiberationSans-Regular.ttf","font"); put(source,"NUCLEUS.INI","gfx.movie_player = off\n");
            put(source,"SAVE/SDU7/user.txt","starter"); put(source,"UA_source.exe","must not copy");
            GameFiles.importGame(new Local(source.getParentFile()),files,s->{});
            check(GameFiles.missing(game).isEmpty(),"Imported game not ready");
            check(get(game,"NUCLEUS.INI").contains("gfx.movie_player = on"),"Movies not enabled");
            check(!new File(game,"UA_source.exe").exists(),"Copied Windows executable");
            put(game,"SAVE/SDU7/user.txt","progress A"); put(game,"android-settings.cfg","opacity 27");
            put(game,"android-touch-layout.cfg","custom layout A");
            put(game,"android-gamepad.cfg","gamepad A");
            GameFiles.importGame(new Local(source),files,s->{});
            check(get(game,"SAVE/SDU7/user.txt").equals("progress A"),"Import overwrote saves");
            check(get(game,"android-settings.cfg").equals("opacity 27"),"Import overwrote display settings");
            check(get(game,"android-touch-layout.cfg").equals("custom layout A"),"Import overwrote custom layout");
            check(get(game,"android-gamepad.cfg").equals("gamepad A"),"Import overwrote gamepad settings");
            File broken=new File(temp,"broken"); put(broken,"DATA/world.ini","incomplete");
            reject(()->GameFiles.importGame(new Local(broken),files,s->{}));
            check(get(game,"SAVE/SDU7/user.txt").equals("progress A"),"Failed import changed saves");
            File backup=new File(temp,"saves.zip");
            try(OutputStream out=new FileOutputStream(backup)) { GameFiles.backup(game,out); }
            try(ZipFile zip=new ZipFile(backup)) {
                check(zip.getEntry("SAVE/SDU7/user.txt")!=null,"Missing profile");
                check(zip.getEntry("android-touch-layout.cfg")!=null,"Missing custom layout");
                check(zip.getEntry("android-gamepad.cfg")!=null,"Missing gamepad settings");
                check(zip.getEntry("DATA/world.ini")==null,"Backup includes game assets");
            }
            put(game,"SAVE/SDU7/user.txt","progress B"); put(game,"SAVE/extra/user.txt","extra");
            put(game,"android-touch-layout.cfg","custom layout B");
            put(game,"android-gamepad.cfg","gamepad B");
            GameFiles.restore(backup,files);
            check(get(game,"SAVE/SDU7/user.txt").equals("progress A"),"Restore failed");
            check(get(game,"android-touch-layout.cfg").equals("custom layout A"),"Restore lost touch layout");
            check(get(game,"android-gamepad.cfg").equals("gamepad A"),"Restore lost gamepad settings");
            check(!new File(game,"SAVE/extra").exists(),"Restore left stale profile");
            GameFiles.undo(files);
            check(get(game,"SAVE/SDU7/user.txt").equals("progress B"),"Undo did not restore progress");
            check(get(game,"android-touch-layout.cfg").equals("custom layout B"),"Undo did not restore touch layout");
            check(get(game,"android-gamepad.cfg").equals("gamepad B"),"Undo did not restore gamepad settings");
            check(new File(game,"SAVE/extra/user.txt").exists(),"Undo lost another profile");
            // A crashed restore is recovered before the game can be launched.
            put(files,"restore-pending","1"); put(game,"SAVE/SDU7/user.txt","partial restore");
            GameFiles.recover(files);
            check(get(game,"SAVE/SDU7/user.txt").equals("progress A"),"Interrupted restore not recovered");
            File bad=new File(temp,"bad.zip");
            malicious(bad,"../escaped"); reject(()->GameFiles.restore(bad,files));
            malicious(bad,"DATA/world.ini"); reject(()->GameFiles.restore(bad,files));
            malicious(bad,"SAVE/SDU7/user.txt","save/sdu7/USER.txt"); reject(()->GameFiles.restore(bad,files));
            Files.write(bad.toPath(),new byte[]{1,2,3}); reject(()->GameFiles.restore(bad,files));
            check(get(game,"SAVE/SDU7/user.txt").equals("progress A"),"Invalid ZIP changed live saves");
            check(!new File(files,"escaped").exists(),"ZIP traversal escaped destination");
            reject(()->GameFiles.child(files,"SAVE/../../escape"));
            try(GameFiles.Session session=new GameFiles.Session(files)) { reject(()->new GameFiles.Session(files)); }
            // Recover the tiny window between the two game-directory renames.
            GameFiles.delete(files,new File(files,"game-previous"));
            check(game.renameTo(new File(files,"game-previous")),"Cannot set up import recovery test");
            GameFiles.recover(files); check(GameFiles.missing(game).isEmpty(),"Interrupted import not recovered");
            // Older beta backups contain no touch-layout file. Restoring one
            // should choose defaults, while Undo retains the newer custom layout.
            new File(game,"android-touch-layout.cfg").delete();
            new File(game,"android-gamepad.cfg").delete();
            File legacy=new File(temp,"legacy-saves.zip");
            try(OutputStream out=new FileOutputStream(legacy)) { GameFiles.backup(game,out); }
            put(game,"android-touch-layout.cfg","new custom layout");
            put(game,"android-gamepad.cfg","new gamepad mapping");
            GameFiles.restore(legacy,files);
            check(!new File(game,"android-touch-layout.cfg").exists(),"Old backup left an unrelated custom layout");
            check(!new File(game,"android-gamepad.cfg").exists(),"Old backup left an unrelated gamepad mapping");
            GameFiles.undo(files);
            check(get(game,"android-touch-layout.cfg").equals("new custom layout"),"Undo of old backup lost custom layout");
            check(get(game,"android-gamepad.cfg").equals("new gamepad mapping"),"Undo of old backup lost gamepad mapping");
            System.out.println("PASS: import validation/preservation, asset-free backup, restore/undo, crash recovery, invalid ZIPs and session locking");
        } finally { GameFiles.delete(temp.getParentFile(),temp); }
    }
}
