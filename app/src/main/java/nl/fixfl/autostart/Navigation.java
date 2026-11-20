package nl.fixfl.autostart;

import android.app.Activity;
import android.content.Intent;

/** Leaving the app the way the camera's own apps do. */
public class Navigation {
    /** MENU: close the app and show the camera's application menu (the Android home screen). */
    public static void toMenu(Activity a) {
        a.finish();
        a.startActivity(new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_HOME).setFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
    }
}
