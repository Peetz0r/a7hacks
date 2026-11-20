package nl.fixfl.autostart;

import android.app.Activity;
import android.content.Intent;
import android.util.Log;

import com.github.ma1co.openmemories.tweak.NativeException;
import com.github.ma1co.openmemories.tweak.Shell;

/** Leaving the app the way the camera's own apps do. */
public class Navigation {
    static final String TAG = "FixflAutostart";

    /** MENU: close the app and show the camera's application menu (the Android home screen). */
    public static void toMenu(Activity a) {
        a.finish();
        a.startActivity(new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_HOME).setFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
    }

    // The camera hands the screen back to its shooting mode when its launcher is started in its "black"
    // state. That alias is not exported, so an app can't start it; root can. EXPERIMENTAL: this mimics the
    // intent the system itself sends (act=MAIN, flags NEW_TASK|RESET_TASK_IF_NEEDED = 0x10200000).
    // The command is written to a tiny script because the root exec call takes at most 255 characters.
    static final String WRITE_SCRIPT =
        "echo 'am start -a android.intent.action.MAIN -f 270532608 " +
        "-n com.sony.scalar.dlsys.scalaralauncher/.ScalarABlackLauncher' >/android/dev/go.sh";

    /** Half-press of the shutter: back to shooting. Call from a background thread. */
    public static void toShooting() {
        try {
            Shell.exec(WRITE_SCRIPT);
            Thread.sleep(300);                  // the root exec call is asynchronous
            Shell.execAndroid("sh /dev/go.sh");
        } catch (NativeException e) {
            Log.e(TAG, "toShooting failed: " + e);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }
}
