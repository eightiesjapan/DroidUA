package org.openua.android;

import android.app.*;
import android.content.*;
import android.content.pm.ActivityInfo;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.text.method.LinkMovementMethod;
import android.text.util.Linkify;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.text.SimpleDateFormat;
import java.util.*;
import java.util.concurrent.Executors;

/** Runs separately from SDL, so a clean engine exit can return to this screen. */
public class LauncherActivity extends Activity {
    private static final int IMPORT=10, BACKUP=11, RESTORE=12, DIAGNOSTICS=13;
    private static final int BACKGROUND=Color.rgb(7,18,24), CYAN=Color.rgb(109,220,230), TEXT=Color.rgb(225,239,241);
    private final java.util.concurrent.ExecutorService worker=Executors.newSingleThreadExecutor();
    private final List<Button> actions=new ArrayList<>();
    private TextView status,description;
    private Button play,backup,undo;
    private ProgressBar progress;
    private boolean busy=false;
    private File files,game;
    private int dp(int value) { return Math.round(value*getResources().getDisplayMetrics().density); }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        files=getFilesDir(); game=new File(files,"game");
        getWindow().setStatusBarColor(BACKGROUND); getWindow().setNavigationBarColor(BACKGROUND);
        ScrollView scroll=new ScrollView(this); scroll.setFillViewport(true); scroll.setBackgroundColor(BACKGROUND);
        LinearLayout content=new LinearLayout(this); content.setOrientation(LinearLayout.VERTICAL); content.setPadding(dp(24),dp(20),dp(24),dp(24));
        scroll.addView(content); setContentView(scroll);
        LinearLayout heading=new LinearLayout(this); heading.setGravity(Gravity.CENTER_VERTICAL);
        ImageView icon=new ImageView(this); icon.setImageResource(R.drawable.droidua_icon); icon.setContentDescription("DroidUA host station emblem");
        heading.addView(icon,new LinearLayout.LayoutParams(dp(76),dp(76)));
        LinearLayout names=new LinearLayout(this); names.setOrientation(LinearLayout.VERTICAL); names.setPadding(dp(14),0,0,0);
        names.addView(label("DroidUA",30,CYAN)); names.addView(label("Urban Assault for Android · "+BuildConfig.VERSION_NAME,15,TEXT)); heading.addView(names);
        content.addView(heading);
        description=label("",16,TEXT); description.setPadding(0,dp(14),0,dp(12)); content.addView(description);
        play=button("Play",()->startActivity(new Intent(this,OpenUAActivity.class))); content.addView(play);
        LinearLayout row=new LinearLayout(this);
        row.addView(button("Import game files",this::chooseImport),weighted());
        row.addView(button("Get game files",()->openWeb("https://metropolisdawn.de/filebase/file/10-openua-standalone-package-full-game/")),weighted());
        content.addView(row);
        LinearLayout saves=new LinearLayout(this);
        backup=button("Back up saves",this::chooseBackup); saves.addView(backup,weighted());
        saves.addView(button("Restore saves",this::chooseRestore),weighted()); content.addView(saves);
        undo=button("Undo last restore",()->confirm("Undo last restore?","This returns your progress and settings to their state before the last restore.",
            ()->job("Restoring previous saves…",()->GameFiles.undo(files),"Previous saves restored.")));
        content.addView(undo);
        content.addView(button("About & controls",this::about));
        content.addView(button("Export diagnostics",()->launchPicker(new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
            .setType("application/zip").putExtra(Intent.EXTRA_TITLE,"DroidUA-report-"+new SimpleDateFormat("yyyy-MM-dd-HHmmss",Locale.ROOT).format(new Date())+".zip"),DIAGNOSTICS)));
        progress=new ProgressBar(this); progress.setVisibility(View.GONE); content.addView(progress);
        status=label("",15,CYAN); status.setPadding(0,dp(12),0,0); content.addView(status);
        refresh();
        busy=true; progress.setVisibility(View.VISIBLE); refresh();
        worker.execute(()-> {
            try(GameFiles.Session session=new GameFiles.Session(files)) { GameFiles.recover(files); }
            catch(IOException error) { if(new File(files,"restore-pending").exists()) runOnUiThread(()->message(error.getMessage())); }
            runOnUiThread(()-> { busy=false; progress.setVisibility(View.GONE); refresh(); });
        });
    }
    @Override public void onResume() {
        super.onResume(); if(description!=null) refresh();
        worker.execute(()-> { String report=CrashReports.collect(this); if(report!=null) runOnUiThread(()->status.setText(report)); });
    }
    private LinearLayout.LayoutParams weighted() { return new LinearLayout.LayoutParams(0,ViewGroup.LayoutParams.WRAP_CONTENT,1); }
    private TextView label(String text,int size,int color) { TextView v=new TextView(this); v.setText(text); v.setTextSize(size); v.setTextColor(color); return v; }
    private Button button(String text,Runnable action) {
        Button b=new Button(this); b.setText(text); b.setAllCaps(false); b.setTextSize(17); b.setTextColor(TEXT);
        b.setMinHeight(dp(54)); b.setBackgroundTintList(android.content.res.ColorStateList.valueOf(Color.rgb(25,49,59)));
        b.setOnClickListener(v->action.run()); actions.add(b); return b;
    }
    private void refresh() {
        boolean ready=GameFiles.missing(game).isEmpty() && !new File(files,"restore-pending").exists();
        description.setText(ready?"Your game files are ready. Exit the game to return here for backups or updates."
            :"Welcome! Download and extract the OpenUA standalone package, then import its folder once. Your original folder stays untouched.");
        for(Button b:actions) b.setEnabled(!busy);
        play.setEnabled(!busy && ready); backup.setEnabled(!busy && ready);
        undo.setEnabled(!busy && ready);
        undo.setVisibility(GameFiles.hasUndo(files)?View.VISIBLE:View.GONE);
        if(!ready) play.setText("Import game files to play"); else play.setText("Play");
    }
    private void message(String text) { new AlertDialog.Builder(this).setTitle("DroidUA").setMessage(text).setPositiveButton("OK",null).show(); }
    private void confirm(String title,String text,Runnable action) { new AlertDialog.Builder(this).setTitle(title).setMessage(text).setNegativeButton("Cancel",null).setPositiveButton("Continue",(d,w)->action.run()).show(); }
    private boolean available() {
        try(GameFiles.Session session=new GameFiles.Session(files)) { return true; }
        catch(IOException error) { message(error.getMessage()); return false; }
    }
    private void launchPicker(Intent intent,int request) {
        try { startActivityForResult(intent,request); }
        catch(ActivityNotFoundException error) { message("No system file picker is available on this device."); }
    }
    private void chooseImport() {
        if(!available()) return;
        Runnable pick=()->launchPicker(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION),IMPORT);
        if(game.isDirectory() && GameFiles.missing(game).isEmpty()) confirm("Replace game files?","Your existing saves and Android settings will be preserved. Choose the extracted OpenUA package folder containing DATA, ENV and LEVELS.",pick);
        else pick.run();
    }
    private void chooseBackup() {
        if(!available()) return;
        String date=new SimpleDateFormat("yyyy-MM-dd-HHmmss",Locale.ROOT).format(new Date());
        launchPicker(new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("application/zip")
            .putExtra(Intent.EXTRA_TITLE,"DroidUA-saves-"+date+".zip"),BACKUP);
    }
    private void chooseRestore() {
        if(!available()) return;
        if(!GameFiles.missing(game).isEmpty()) { message("Import the game files first, then restore your save backup."); return; }
        launchPicker(new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
            .putExtra(Intent.EXTRA_MIME_TYPES,new String[]{"application/zip","application/octet-stream","application/x-zip-compressed"}),RESTORE);
    }
    private interface Work { void run() throws Exception; }
    private void job(String title,Work work,String success) {
        if(busy) return;
        busy=true; status.setText(title); progress.setVisibility(View.VISIBLE); refresh();
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LOCKED);
        worker.execute(()-> {
            String result=success; boolean okay=true;
            try(GameFiles.Session session=new GameFiles.Session(files)) { GameFiles.recover(files); work.run(); }
            catch(Exception error) { okay=false; result=error.getMessage()==null?"The operation could not be completed.":error.getMessage(); }
            final String done=result; final boolean completed=okay;
            runOnUiThread(()-> { busy=false; progress.setVisibility(View.GONE); setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED);
                status.setText(done); refresh(); if(!completed) message(done); });
        });
    }
    @Override protected void onActivityResult(int request,int result,Intent intent) {
        super.onActivityResult(request,result,intent);
        if(result!=RESULT_OK || intent==null || intent.getData()==null) return;
        Uri uri=intent.getData();
        if(request==DIAGNOSTICS) {
            job("Exporting diagnostics…",()-> {
                try(OutputStream out=getContentResolver().openOutputStream(uri,"wt")) {
                    if(out==null) throw new IOException("Cannot write to that location."); CrashReports.export(this,out);
                }
            },"Diagnostics ZIP saved. You can share this file when reporting a problem.");
        } else if(request==IMPORT) {
            job("Checking game folder…",()-> {
                String id=DocumentsContract.getTreeDocumentId(uri);
                Uri root=DocumentsContract.buildDocumentUriUsingTree(uri,id);
                GameFiles.Source source=new DocumentSource(uri,root,"selected folder",true);
                GameFiles.importGame(source,files,text->runOnUiThread(()->status.setText(text)));
            },"Game files imported. Your saves and settings are ready.");
        } else if(request==BACKUP) {
            job("Creating save backup…",()-> {
                File temp=new File(files,"backup-export.zip");
                try {
                    try(OutputStream out=new FileOutputStream(temp)) { GameFiles.backup(game,out); }
                    try(InputStream in=new FileInputStream(temp); OutputStream out=getContentResolver().openOutputStream(uri,"wt")) {
                        if(out==null) throw new IOException("Cannot write to that location."); GameFiles.copy(in,out,512L*1024*1024);
                    }
                } finally { GameFiles.delete(files,temp); }
            },"Backup saved. Keep this ZIP somewhere safe, or copy it to your other device.");
        } else if(request==RESTORE) {
            confirm("Restore this backup?","This replaces your saved progress and settings. You can undo the last restore from this screen. Game assets stay unchanged.",
                ()->job("Checking save backup…",()-> {
                    File temp=new File(files,"restore-input.zip");
                    try {
                        try(InputStream in=getContentResolver().openInputStream(uri)) {
                            if(in==null) throw new IOException("Cannot open the selected backup."); GameFiles.copyBackupInput(in,temp);
                        }
                        GameFiles.restore(temp,files);
                    } finally { GameFiles.delete(files,temp); }
                },"Saved progress and settings restored."));
        }
    }
    private final class DocumentSource implements GameFiles.Source {
        final Uri tree,document; final String name; final boolean dir;
        DocumentSource(Uri tree,Uri document,String name,boolean dir) { this.tree=tree; this.document=document; this.name=name; this.dir=dir; }
        public String name() { return name; }
        public boolean directory() { return dir; }
        public InputStream open() throws IOException { InputStream in=getContentResolver().openInputStream(document); if(in==null) throw new IOException("Cannot read "+name); return in; }
        public List<GameFiles.Source> children() throws IOException {
            List<GameFiles.Source> list=new ArrayList<>();
            Uri children=DocumentsContract.buildChildDocumentsUriUsingTree(tree,DocumentsContract.getDocumentId(document));
            try(Cursor c=getContentResolver().query(children,new String[]{DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,DocumentsContract.Document.COLUMN_MIME_TYPE},null,null,null)) {
                if(c==null) throw new IOException("Cannot read the selected folder.");
                while(c.moveToNext()) {
                    String display=c.getString(1);
                    if(display==null) throw new IOException("A file has no name.");
                    list.add(new DocumentSource(tree,DocumentsContract.buildDocumentUriUsingTree(tree,c.getString(0)),display,
                        DocumentsContract.Document.MIME_TYPE_DIR.equals(c.getString(2))));
                }
            }
            return list;
        }
    }
    private void openWeb(String url) { try { startActivity(new Intent(Intent.ACTION_VIEW,Uri.parse(url))); } catch(ActivityNotFoundException e) { message(url); } }
    private void about() {
        String credits="DroidUA "+BuildConfig.VERSION_NAME+"\nUrban Assault for Android\n\n"+
            "Android adaptation built with OpenAI GPT Sol and Astra; coordinated and play-tested by eightiesjapan.\n\n"+
            "Based on OpenUA / UA:Source by Marisa-Chan and contributors. Thanks to the Metropolis Dawn / Stoudson Corporation community for keeping Urban Assault alive.\n\n"+
            "Original Urban Assault: TerraTools and Microsoft. Original game content remains the property of its respective owners. This is an unofficial community project.\n\n"+
            "Engine source: GPLv2. Original game data is supplied separately.\nhttps://github.com/Marisa-Chan/UA_source\nhttps://metropolisdawn.de\n\n"+
            "Gamepad: left stick moves / controls the cursor; right stick looks or pans an open map. R1 switches VIEW / SELECT. L1 clicks and drags; double-click a map vehicle to enter it. L2/R2 fire weapons.\n"+
            "A: pause · B: brake / auto-level / VTOL · X: current squad leader · Y: become squad leader · Select: map · Start: back / game menu.\n\n"+
            "Touch: Move / Cursor and Look / Pan pads; VIEW / SELECT switches modes. Fire / Alt shoot. Tap the original HUD buttons directly; double-tap a map vehicle to enter it. Touch controls and opacity can be changed in game Options.\n\n"+
            "Touch: Brake stops / auto-levels / VTOL; Leader jumps to the squad leader; Lead makes you squad leader. Options > Touch screen opens the draggable layout editor. Reset restores the default layout; Save applies changes. Layouts adapt to screen edges and avoid overlapping buttons.\n\n"+
            "Backups include campaign profiles, saved progress, settings and your touch layout, not the game assets. Copy the ZIP to another device, import its game files, then choose Restore saves. Export before uninstalling: uninstalling removes private saves and the local undo copy.\n\n"+
            "Updates: install a newer DroidUA APK over this app using the same signing key. Android offers Update and preserves imported files, saves and settings. Do not uninstall first. Exported backup ZIPs remain wherever you saved them.\n\n"+
            "Problems: reopen DroidUA after a crash. A report is saved under Download/DroidUA on Android 10 or newer where supported. Export diagnostics also works before importing game files. Reports contain app logs, crash traces and display settings, without your saved progress; nothing is uploaded automatically.";
        showText("About & controls",credits,true);
    }
    private void showText(String title,String text,boolean licencesButton) {
        ScrollView scroll=new ScrollView(this); TextView body=label(text,16,TEXT); body.setPadding(dp(20),dp(16),dp(20),dp(16));
        Linkify.addLinks(body,Linkify.WEB_URLS); body.setLinkTextColor(CYAN); body.setMovementMethod(LinkMovementMethod.getInstance()); scroll.addView(body);
        AlertDialog.Builder dialog=new AlertDialog.Builder(this).setTitle(title).setView(scroll).setPositiveButton("Close",null);
        if(licencesButton) dialog.setNeutralButton("Licences",(d,w)->licences()); dialog.show();
    }
    private void licences() {
        try {
            StringBuilder text=new StringBuilder();
            for(String name:getAssets().list("licences")) {
                text.append("\n=== ").append(name).append(" ===\n\n");
                try(InputStream in=getAssets().open("licences/"+name); ByteArrayOutputStream out=new ByteArrayOutputStream()) {
                    GameFiles.copy(in,out,2*1024*1024); text.append(new String(out.toByteArray(),java.nio.charset.StandardCharsets.UTF_8));
                }
            }
            showText("Third-party licences",text.toString(),false);
        } catch(IOException error) { message("Licence texts could not be opened."); }
    }
    @Override public void onBackPressed() { if(busy) message("Please wait until the file operation finishes."); else super.onBackPressed(); }
}
