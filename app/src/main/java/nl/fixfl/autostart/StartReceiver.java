package nl.fixfl.autostart;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

public class StartReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        try {
            Starter.log(intent.getAction());
        } catch (Exception e) {
            Log.e("FixFlAutostart", "log failed", e);
        }
        try {
            Starter.start();
        } catch (Exception e) {
            Log.e("FixFlAutostart", "start failed", e);
        }
    }
}
