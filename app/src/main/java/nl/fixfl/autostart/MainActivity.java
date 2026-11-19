package nl.fixfl.autostart;

import android.app.Activity;
import android.os.Bundle;
import android.widget.TextView;

public class MainActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        String msg;
        try {
            Starter.start();
            msg = "FixFl start requested.\n\nIt will also start by itself at boot and after wake-up.\n\nExpected files on the SD card:\nHAX/fixfl.sh, HAX/fixfl";
        } catch (Throwable t) {
            msg = "Start failed: " + t;
        }
        TextView tv = new TextView(this);
        tv.setTextSize(18);
        tv.setPadding(24, 24, 24, 24);
        tv.setText(msg);
        setContentView(tv);
    }
}
