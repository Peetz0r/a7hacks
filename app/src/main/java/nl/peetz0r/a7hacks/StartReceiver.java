package nl.peetz0r.a7hacks;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

public class StartReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        // Hand over to a service: it can retry for a while, a receiver cannot.
        Intent i = new Intent(context, StartService.class);
        i.putExtra("event", intent.getAction());
        context.startService(i);
    }
}
