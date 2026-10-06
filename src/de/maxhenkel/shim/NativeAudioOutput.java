package de.maxhenkel.shim;

public class NativeAudioOutput {
    static { NativeAudio.isAvailable(); }
    public static boolean isAvailable() { return NativeAudio.isAvailable(); }
    public static native int open(int sampleRate, int channels, int framesPerBuffer);
    public static native int start(int handle);
    public static native int stop(int handle);
    public static native int close(int handle);
    public static native int write2(int handle, byte[] buf, int offset, int length, int frameSize);
}
