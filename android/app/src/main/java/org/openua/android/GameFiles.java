package org.openua.android;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.channels.*;
import java.text.SimpleDateFormat;
import java.util.*;
import java.util.zip.*;

/** File operations shared by the Android launcher and host-side recovery tests. */
public final class GameFiles {
    private GameFiles() {}
    public interface Source {
        String name();
        boolean directory();
        List<Source> children() throws IOException;
        InputStream open() throws IOException;
    }
    public interface Progress { void update(String message); }
    private static final String[] PROGRESS={"SAVE","Ghorsave","Taersave","ENV/user.def","android-settings.cfg","android-touch-layout.cfg"};
    private static final String META="droidua-backup.properties";
    private static final long BACKUP_LIMIT=512L*1024*1024, IMPORT_LIMIT=4L*1024*1024*1024;
    private static final Set<String> IMPORT_DIRS=new HashSet<>(Arrays.asList(
        "data","env","levels","locale","fonts","res","music","save",
        "dataxp","levelsxp","localexp","ghor_env","taer_env","ghorsave","taersave"));

    public static final class Session implements Closeable {
        private final RandomAccessFile file;
        private final FileLock lock;
        public Session(File files) throws IOException {
            file=new RandomAccessFile(new File(files,"engine.lock"),"rw");
            FileLock acquired;
            try { acquired=file.getChannel().tryLock(); }
            catch(OverlappingFileLockException e) { acquired=null; }
            if(acquired==null) { file.close(); throw new IOException("Please exit the game before importing or managing saves."); }
            lock=acquired;
        }
        public void close() throws IOException { try { lock.release(); } finally { file.close(); } }
    }
    static File child(File base,String path) throws IOException {
        if(path.isEmpty() || path.startsWith("/") || path.indexOf('\\')>=0 || path.indexOf(':')>=0 || path.indexOf('\0')>=0)
            throw new IOException("Invalid file path.");
        for(String part:path.split("/",-1)) if(part.isEmpty() || part.equals(".") || part.equals(".."))
            throw new IOException("Invalid file path.");
        File result=new File(base,path);
        if(!result.getCanonicalPath().startsWith(base.getCanonicalPath()+File.separator)) throw new IOException("File is outside its destination.");
        return result;
    }
    private static File ci(File base,String name) {
        File[] list=base.listFiles();
        if(list!=null) for(File f:list) if(f.getName().equalsIgnoreCase(name)) return f;
        return new File(base,name);
    }
    private static File progressFile(File base,String path) {
        File f=base;
        for(String part:path.split("/")) f=ci(f,part);
        return f;
    }
    private static void mkdir(File dir) throws IOException {
        if(!dir.isDirectory() && !dir.mkdirs()) throw new IOException("Cannot create a folder. Check free storage.");
    }
    static void delete(File base,File path) throws IOException {
        if(!path.exists()) return;
        if(!path.getCanonicalPath().startsWith(base.getCanonicalPath()+File.separator)) throw new IOException("Invalid cleanup path.");
        if(path.isDirectory()) { File[] children=path.listFiles(); if(children==null) throw new IOException("Cannot read folder."); for(File f:children) delete(base,f); }
        if(!path.delete()) throw new IOException("Cannot replace a file. Check storage.");
    }
    static long copy(InputStream input,OutputStream output,long limit) throws IOException {
        byte[] buffer=new byte[65536]; long total=0; int n;
        while((n=input.read(buffer))!=-1) { total+=n; if(total>limit) throw new IOException("The selected files are too large."); output.write(buffer,0,n); }
        return total;
    }
    private static void clone(File from,File to) throws IOException {
        if(from.isDirectory()) {
            mkdir(to); File[] list=from.listFiles(); if(list==null) throw new IOException("Cannot read folder.");
            for(File f:list) clone(f,child(to,f.getName()));
        } else {
            mkdir(to.getParentFile());
            try(InputStream in=new FileInputStream(from); OutputStream out=new FileOutputStream(to)) { copy(in,out,IMPORT_LIMIT); }
        }
    }
    private static boolean has(List<Source> list,String name) { for(Source s:list) if(s.name().equalsIgnoreCase(name)) return true; return false; }
    private static Source packageRoot(Source root,int depth) throws IOException {
        List<Source> list=root.children();
        if(has(list,"DATA") && has(list,"ENV") && has(list,"LEVELS")) return root;
        if(depth<2) for(Source s:list) if(s.directory()) {
            Source found=packageRoot(s,depth+1); if(found!=null) return found;
        }
        return null;
    }
    private static final class Budget { long bytes=0; int files=0; final Set<String> paths=new HashSet<>(); }
    private static void importNode(Source source,File stage,String relative,Budget budget,Progress report,int depth) throws IOException {
        if(depth>24 || ++budget.files>50000) throw new IOException("Too many nested files in the selected folder.");
        File target=child(stage,relative);
        if(!budget.paths.add(relative.toLowerCase(Locale.ROOT))) throw new IOException("Duplicate file names in the game folder.");
        if(source.directory()) {
            mkdir(target);
            for(Source s:source.children()) importNode(s,stage,relative+"/"+s.name(),budget,report,depth+1);
        } else {
            if(stage.getUsableSpace()<16L*1024*1024) throw new IOException("Not enough free storage to import the game.");
            mkdir(target.getParentFile());
            try(InputStream in=source.open(); OutputStream out=new FileOutputStream(target)) {
                budget.bytes+=copy(in,out,Math.min(256L*1024*1024,IMPORT_LIMIT-budget.bytes));
            }
            if(budget.files%50==0) report.update("Importing game files… "+(budget.bytes/1024/1024)+" MB");
        }
    }
    public static String missing(File game) {
        String[] required={"DATA/world.ini","DATA/OBJECTS","ENV/assign.txt","ENV/user.def","LEVELS/SINGLE",
            "LOCALE/English.lng","res/Pointer.cur","fonts/LiberationSans-Regular.ttf","NUCLEUS.INI"};
        List<String> absent=new ArrayList<>();
        for(String path:required) if(!progressFile(game,path).exists()) absent.add(path);
        return String.join(", ",absent);
    }
    public static void recover(File files) throws IOException {
        File game=new File(files,"game"), old=new File(files,"game-previous");
        if(!game.exists() && old.exists() && !old.renameTo(game)) throw new IOException("Cannot recover the previous game installation.");
        if(new File(files,"restore-pending").exists()) {
            File undo=new File(files,"restore-undo");
            if(!undo.isDirectory()) throw new IOException("Save recovery is incomplete. Keep your backup and contact support.");
            installProgress(undo,game);
            if(!new File(files,"restore-pending").delete()) throw new IOException("Cannot finish save recovery.");
        }
    }
    public static void importGame(Source source,File files,Progress report) throws IOException {
        recover(files);
        Source root=packageRoot(source,0);
        if(root==null) throw new IOException("Choose the extracted OpenUA package folder containing DATA, ENV and LEVELS.");
        File stage=new File(files,"import-stage"), game=new File(files,"game"), old=new File(files,"game-previous");
        delete(files,stage); mkdir(stage);
        try {
            Budget budget=new Budget();
            for(Source s:root.children()) {
                String name=s.name().toLowerCase(Locale.ROOT);
                if(s.directory() && IMPORT_DIRS.contains(name)) importNode(s,stage,s.name(),budget,report,0);
                else if(!s.directory() && name.equals("nucleus.ini")) importNode(s,stage,"NUCLEUS.INI",budget,report,0);
            }
            String absent=missing(stage);
            if(!absent.isEmpty()) throw new IOException("The folder is missing: "+absent+". Please use the complete extracted OpenUA standalone package.");
            for(String path:PROGRESS) {
                File previous=progressFile(game,path);
                if(previous.exists()) { File target=progressFile(stage,path); delete(stage,target); clone(previous,child(stage,path)); }
            }
            File config=progressFile(stage,"NUCLEUS.INI");
            String text=new String(read(config,1024*1024),StandardCharsets.ISO_8859_1);
            text=text.replaceAll("(?im)^\\s*gfx\\.movie_player\\s*=.*$","gfx.movie_player = on");
            if(!text.toLowerCase(Locale.ROOT).contains("gfx.movie_player")) text+="\ngfx.movie_player = on\n";
            write(config,text.getBytes(StandardCharsets.ISO_8859_1));
            if(!new File(stage,"android-settings.cfg").exists()) write(new File(stage,"android-settings.cfg"),
                "layout 0\nlarge 1\nfps 60\ntouch 0\nshowFPS 0\ntouchOpacity 75\n".getBytes(StandardCharsets.UTF_8));
            report.update("Finishing import…");
            delete(files,old);
            if(game.exists() && !game.renameTo(old)) throw new IOException("Cannot keep the current installation safe. Import cancelled.");
            if(!stage.renameTo(game)) { if(old.exists()) old.renameTo(game); throw new IOException("Cannot finish import. The previous installation was retained."); }
            // Keep the previous installation until the next import. A failed copy
            // never touches live data, and a process death between renames recovers.
        } finally { delete(files,stage); }
    }
    private static byte[] read(File file,long limit) throws IOException {
        try(InputStream in=new FileInputStream(file); ByteArrayOutputStream out=new ByteArrayOutputStream()) { copy(in,out,limit); return out.toByteArray(); }
    }
    private static void write(File file,byte[] bytes) throws IOException { mkdir(file.getParentFile()); try(OutputStream out=new FileOutputStream(file)) { out.write(bytes); } }
    private static void zipNode(File source,String path,ZipOutputStream zip,Budget budget) throws IOException {
        if(++budget.files>20000) throw new IOException("Too many files in this save backup.");
        zip.putNextEntry(new ZipEntry(path+(source.isDirectory()?"/":"")));
        if(source.isDirectory()) {
            zip.closeEntry(); File[] list=source.listFiles(); if(list==null) throw new IOException("Cannot read saved games.");
            for(File f:list) zipNode(f,path+"/"+f.getName(),zip,budget);
        } else { try(InputStream in=new FileInputStream(source)) { budget.bytes+=copy(in,zip,BACKUP_LIMIT-budget.bytes); } zip.closeEntry(); }
    }
    public static void backup(File game,OutputStream output) throws IOException {
        try(ZipOutputStream zip=new ZipOutputStream(output)) {
            zip.putNextEntry(new ZipEntry(META));
            Properties metadata=new Properties(); metadata.setProperty("format","droidua-saves"); metadata.setProperty("version","1");
            metadata.setProperty("created",new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ssZ",Locale.ROOT).format(new Date()));
            metadata.store(zip,"DroidUA saved progress and settings. No game assets."); zip.closeEntry();
            Budget budget=new Budget();
            for(String path:PROGRESS) { File source=progressFile(game,path); if(source.exists()) zipNode(source,path,zip,budget); }
        }
    }
    private static boolean allowed(String name) {
        for(String path:PROGRESS) if(name.equals(path) || ((path.equals("SAVE") || path.endsWith("save")) && name.startsWith(path+"/"))) return true;
        return false;
    }
    static void extractBackup(File archive,File stage) throws IOException {
        mkdir(stage); Budget budget=new Budget(); boolean metadata=false;
        try(ZipFile zip=new ZipFile(archive)) {
            Enumeration<? extends ZipEntry> entries=zip.entries();
            while(entries.hasMoreElements()) {
                ZipEntry entry=entries.nextElement(); String name=entry.getName();
                if(entry.isDirectory() && name.endsWith("/")) name=name.substring(0,name.length()-1);
                if(++budget.files>20000 || !budget.paths.add(name.toLowerCase(Locale.ROOT))) throw new IOException("Invalid or duplicate backup entries.");
                File target=child(stage,name);
                if(name.equals(META) && !entry.isDirectory()) {
                    Properties props=new Properties();
                    try(InputStream in=zip.getInputStream(entry); ByteArrayOutputStream bytes=new ByteArrayOutputStream()) { copy(in,bytes,16384); props.load(new ByteArrayInputStream(bytes.toByteArray())); }
                    if(!"droidua-saves".equals(props.getProperty("format")) || !"1".equals(props.getProperty("version"))) throw new IOException("This is not a supported DroidUA backup.");
                    metadata=true; continue;
                }
                if(!allowed(name)) throw new IOException("The backup contains unexpected files. Choose a DroidUA save backup ZIP.");
                if(entry.isDirectory()) {
                    if(name.equals("ENV/user.def") || name.endsWith(".cfg")) throw new IOException("A settings file is a folder in this backup.");
                    mkdir(target);
                }
                else {
                    mkdir(target.getParentFile());
                    try(CheckedInputStream in=new CheckedInputStream(zip.getInputStream(entry),new CRC32()); OutputStream out=new FileOutputStream(target)) {
                        budget.bytes+=copy(in,out,BACKUP_LIMIT-budget.bytes);
                        if(entry.getCrc()!=in.getChecksum().getValue()) throw new IOException("The backup is damaged (checksum mismatch).");
                    }
                }
            }
        }
        if(!metadata || !new File(stage,"SAVE").isDirectory()) throw new IOException("The selected ZIP is not a complete DroidUA save backup.");
    }
    private static void snapshot(File game,File target) throws IOException {
        mkdir(target);
        for(String path:PROGRESS) { File f=progressFile(game,path); if(f.exists()) clone(f,child(target,path)); }
    }
    private static void installProgress(File source,File game) throws IOException {
        for(String path:PROGRESS) delete(game,progressFile(game,path));
        snapshot(source,game);
    }
    public static void restore(File archive,File files) throws IOException {
        recover(files);
        File game=new File(files,"game"), stage=new File(files,"restore-stage"), undo=new File(files,"restore-undo"), pending=new File(files,"restore-pending");
        delete(files,stage);
        try {
            extractBackup(archive,stage); // Validate everything before modifying live saves.
            delete(files,undo); snapshot(game,undo);
            write(pending,new byte[]{1});
            try { installProgress(stage,game); }
            catch(IOException failure) { recover(files); throw failure; }
            if(!pending.delete()) throw new IOException("Restore needs recovery at the next launch.");
        } finally { delete(files,stage); }
    }
    public static boolean hasUndo(File files) { return new File(files,"restore-undo").isDirectory(); }
    public static void undo(File files) throws IOException {
        File undo=new File(files,"restore-undo");
        if(!undo.isDirectory()) throw new IOException("No previous restore to undo.");
        File archive=new File(files,"undo-input.zip");
        try(OutputStream out=new FileOutputStream(archive)) { backup(undo,out); }
        restore(archive,files); // Retains a recovery snapshot of the replaced state.
        delete(files,archive);
    }
    public static void copyBackupInput(InputStream in,File target) throws IOException {
        try(OutputStream out=new FileOutputStream(target)) { copy(in,out,BACKUP_LIMIT); }
    }
}
