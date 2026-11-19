package com.github.ma1co.openmemories.tweak;

// Must live in this package: libtweak.so (taken from OpenMemories-Tweak 0.11) looks it up by name.
public class NativeException extends Exception {
    public NativeException(String message) {
        super(message);
    }
}
