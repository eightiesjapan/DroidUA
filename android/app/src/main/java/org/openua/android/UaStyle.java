package org.openua.android;

import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.LayerDrawable;
import android.graphics.drawable.StateListDrawable;
import android.widget.Button;
import java.io.File;

/** Button look matching the original Urban Assault menu tiles: steel-blue bevel,
 *  light steel border, cyan glow line along the bottom and the game font with a
 *  hard black drop shadow. */
final class UaStyle {
    private static Typeface font;
    private static boolean fontTried;
    private UaStyle() {}

    static Typeface font(Context c) {
        if(!fontTried) {
            fontTried=true;
            // The game's "Dungeon" UI font maps to Xolonium (shipped in the imported game folder).
            for(String name:new String[]{"Xolonium-Bold.otf","xolonium-bold.otf","XOLONIUM-BOLD.OTF"}) {
                File f=new File(c.getFilesDir(),"game/fonts/"+name);
                if(f.isFile()) { try { font=Typeface.createFromFile(f); break; } catch(RuntimeException ignored) {} }
            }
            if(font==null) font=Typeface.create(Typeface.SANS_SERIF,Typeface.BOLD);
        }
        return font;
    }

    private static Drawable face(float d,int top,int bottom,int border,int glow) {
        GradientDrawable body=new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM,new int[]{top,bottom});
        body.setStroke(Math.max(1,Math.round(d)),border);
        GradientDrawable shine=new GradientDrawable(); shine.setColor(0x30ffffff);
        GradientDrawable line=new GradientDrawable(); line.setColor(glow);
        GradientDrawable rim=new GradientDrawable(); rim.setColor(Color.BLACK);
        LayerDrawable l=new LayerDrawable(new Drawable[]{rim,body,shine,line});
        int px=Math.max(1,Math.round(d));
        l.setLayerInset(0,0,0,0,0);
        l.setLayerInset(1,px,px,px,px);
        l.setLayerInset(2,2*px,2*px,2*px,0); l.setLayerHeight(2,px);
        l.setLayerGravity(2,android.view.Gravity.TOP|android.view.Gravity.FILL_HORIZONTAL);
        l.setLayerInset(3,2*px,0,2*px,2*px); l.setLayerHeight(3,2*px);
        l.setLayerGravity(3,android.view.Gravity.BOTTOM|android.view.Gravity.FILL_HORIZONTAL);
        return l;
    }

    static void button(Button b) {
        float d=b.getResources().getDisplayMetrics().density;
        StateListDrawable s=new StateListDrawable();
        s.addState(new int[]{-android.R.attr.state_enabled},face(d,0xff2a3a3f,0xff182226,0xff4a5b5f,0xff24474d));
        s.addState(new int[]{android.R.attr.state_pressed},face(d,0xff26404a,0xff17292f,0xff78999f,0xff6ddce6));
        s.addState(new int[]{android.R.attr.state_focused},face(d,0xff5d8792,0xff2b4750,0xffb6e6ec,0xff6ddce6));
        s.addState(new int[]{},face(d,0xff4e7078,0xff22363d,0xff78999f,0xff3c96a5));
        b.setBackgroundTintList(null);
        b.setBackground(s);
        b.setStateListAnimator(null);
        b.setTypeface(font(b.getContext()));
        b.setShadowLayer(0.01f,1.5f*d,1.5f*d,Color.BLACK);
        b.setLetterSpacing(0.03f);
    }
}
