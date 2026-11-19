package com.github.ma1co.openmemories.tweak;

// Same JNI binding as OpenMemories-Tweak: runs a command as root, outside the Android chroot.
public class Shell {
    static {
        System.loadLibrary("tweak");
    }

    private static native void nativeExec(String command) throws NativeException;

    public static void exec(String command) throws NativeException {
        nativeExec(command);
    }
}
