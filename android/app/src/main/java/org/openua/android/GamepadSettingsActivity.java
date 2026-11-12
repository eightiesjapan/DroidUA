package org.openua.android;
import android.app.*;
import android.os.Bundle;
import android.view.*;
import android.widget.*;
import java.io.*;

public class GamepadSettingsActivity extends Activity {
    private GamepadConfig draft;
    private File file;
    private final Button[] rows=new Button[GamepadConfig.BUTTONS.length];
    private boolean changed=false;
    private int dp(int n) {return Math.round(n*getResources().getDisplayMetrics().density);}
    private Button button(String text,Runnable action) {
        Button b=new Button(this);b.setText(text);b.setAllCaps(false);b.setTextColor(0xffdff5f7);b.setTextSize(17);b.setMinHeight(dp(54));
        UaStyle.button(b);b.setOnClickListener(v->action.run());return b;
    }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);file=new File(getFilesDir(),"game/android-gamepad.cfg");draft=GamepadConfig.load(file);
        getWindow().setStatusBarColor(0xff071218);getWindow().setNavigationBarColor(0xff071218);
        LinearLayout root=new LinearLayout(this);root.setOrientation(LinearLayout.VERTICAL);root.setBackgroundColor(0xff071218);root.setPadding(dp(16),dp(8),dp(16),dp(8));
        TextView heading=new TextView(this);heading.setText("Gamepad Settings");heading.setTextColor(0xff6ddce6);heading.setTextSize(24);root.addView(heading);
        TextView help=new TextView(this);help.setText("Choose a button, then its action. Select: map • Start: menu (fixed). Sticks and D-pad keep their current behavior. Menu navigation stays A/L1: click, B: back.");
        help.setTextColor(0xffdff5f7);help.setTextSize(15);root.addView(help);
        ScrollView scroll=new ScrollView(this);LinearLayout list=new LinearLayout(this);list.setOrientation(LinearLayout.VERTICAL);scroll.addView(list);
        for(int i=0;i<rows.length;++i) {final int id=i;rows[i]=button("",()->choose(id));list.addView(rows[i]);}
        root.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));
        LinearLayout tools=new LinearLayout(this);
        tools.addView(button("Reset",()-> {draft=new GamepadConfig();changed=true;refresh();}),new LinearLayout.LayoutParams(0,-2,1));
        tools.addView(button("Cancel",this::finish),new LinearLayout.LayoutParams(0,-2,1));
        tools.addView(button("Save",()-> {
            try {draft.save(file);Toast.makeText(this,"Gamepad settings saved",Toast.LENGTH_SHORT).show();finish();}
            catch(IOException error) {new AlertDialog.Builder(this).setMessage(error.getMessage()).setPositiveButton("OK",null).show();}
        }),new LinearLayout.LayoutParams(0,-2,1));
        root.addView(tools);setContentView(root);refresh();
    }
    private void refresh() {for(int i=0;i<rows.length;++i) rows[i].setText(GamepadConfig.BUTTONS[i]+" · "+GamepadConfig.ACTIONS[draft.actions[i]]);}
    private void choose(int id) {
        new AlertDialog.Builder(this).setTitle(GamepadConfig.BUTTONS[id]).setSingleChoiceItems(GamepadConfig.ACTIONS,draft.actions[id],(d,action)-> {
            draft.actions[id]=action;changed=true;refresh();d.dismiss();
        }).setNegativeButton("Cancel",null).show();
    }
    @Override public void onBackPressed() {
        if(changed) new AlertDialog.Builder(this).setMessage("Discard gamepad changes?").setNegativeButton("Keep editing",null).setPositiveButton("Discard",(d,w)->finish()).show();
        else finish();
    }
}
