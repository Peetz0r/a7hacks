package com.github.ma1co.openmemories.tweak;

import android.os.Process;

// Same JNI binding as OpenMemories-Tweak: runs a command as root, outside the Android chroot.
public class Shell {
    static {
        System.loadLibrary("tweak");
    }

    private static native void nativeExec(String command) throws NativeException;

    public static void exec(String command) throws NativeException {
        nativeExec(command);
    }

    // As in OpenMemories-Tweak: runs the command as root inside the Android chroot, with this app's
    // environment (needed by tools like "am"). The root exec call accepts at most 255 characters in
    // total, so the command itself has to be short (put longer logic in a small script).
    public static void execAndroid(String command) throws NativeException {
        int pid = Process.myPid();
        exec("ps e --no-heading " + pid + " | (" +
                 "read _ _ _ _ _ e;" +
                 "export $e;" +
                 "echo $ANDROID_PROPERTY_WORKSPACE | (" +
                     "IFS=, read f _;" +
                     "eval \"exec $f< /proc/" + pid + "/fd/$f\";" +
                     "/bin/busybox chroot /android " + command +
                 ")" +
             ")");
    }
}
