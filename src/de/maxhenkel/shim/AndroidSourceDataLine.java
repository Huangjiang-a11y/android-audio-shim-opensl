package de.maxhenkel.shim;

import javax.sound.sampled.*;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicBoolean;

public class AndroidSourceDataLine implements SourceDataLine {

    private static final ConcurrentHashMap<Long, Integer> pool = new ConcurrentHashMap<>();

    private static long key(int rate, int ch) { return ((long)rate << 8) | ch; }

    private static synchronized int acquireHandle(int rate, int channels) {
        long k = key(rate, channels);
        Integer h = pool.get(k);
        if (h != null) return h;
        int handle = NativeAudioOutput.open(rate, channels, 960);
        if (handle < 0) return handle;
        NativeAudioOutput.start(handle);
        pool.put(k, handle);
        return handle;
    }

    private AudioFormat format;
    private int handle = -1;
    private final AtomicBoolean open   = new AtomicBoolean(false);
    private final AtomicBoolean active = new AtomicBoolean(false);
    private long framesWritten = 0;

    private final FloatControl gainControl = new FloatControl(
            FloatControl.Type.MASTER_GAIN, -80f, 6f, 0.1f, 0, 0f, "dB") {};

    public AndroidSourceDataLine(AudioFormat format) { this.format = format; }

    @Override
    public void open(AudioFormat format, int bufferSize) throws LineUnavailableException {
        if (open.get()) return;
        if (!NativeAudioOutput.isAvailable())
            throw new LineUnavailableException("libaudioshim.so not loaded: " + NativeAudio.getLoadError());
        this.format = format;
        handle = acquireHandle((int) format.getSampleRate(), format.getChannels());
        if (handle < 0) throw new LineUnavailableException("OpenSL output open failed: " + handle);
        open.set(true); framesWritten = 0;
    }

    @Override public void open(AudioFormat fmt) throws LineUnavailableException { open(fmt, 0); }
    @Override public void open()               throws LineUnavailableException { open(format, 0); }
    @Override public void start() { if (open.get()) active.set(true); }
    @Override public void stop()  { active.set(false); }
    @Override public void close() { active.set(false); open.set(false); handle = -1; }

    @Override
    public int write(byte[] b, int off, int len) {
        if (!open.get() || handle < 0 || len == 0) return len;
        int n = NativeAudioOutput.write2(handle, b, off, len, format.getFrameSize());
        if (n < 0) return 0;
        framesWritten += n / format.getFrameSize();
        return len;
    }

    @Override public boolean isOpen()    { return open.get(); }
    @Override public boolean isActive()  { return active.get(); }
    @Override public boolean isRunning() { return active.get(); }
    @Override public void    drain()     {}
    @Override public void    flush()     {}
    @Override public int     available() { return 3840; }
    @Override public int     getBufferSize() { return 3840; }
    @Override public AudioFormat getFormat() { return format; }
    @Override public int  getFramePosition()       { return (int) framesWritten; }
    @Override public long getLongFramePosition()   { return framesWritten; }
    @Override public long getMicrosecondPosition() {
        return (long)(framesWritten * 1_000_000.0 / format.getSampleRate());
    }
    @Override public float getLevel() { return AudioSystem.NOT_SPECIFIED; }
    @Override public Line.Info getLineInfo() { return new DataLine.Info(SourceDataLine.class, format); }
    @Override public Control[] getControls() { return new Control[]{ gainControl }; }
    @Override public boolean isControlSupported(Control.Type t) {
        return FloatControl.Type.MASTER_GAIN.equals(t);
    }
    @Override public Control getControl(Control.Type t) {
        if (FloatControl.Type.MASTER_GAIN.equals(t)) return gainControl;
        throw new IllegalArgumentException("Unsupported: " + t);
    }
    @Override public void addLineListener(LineListener l)    {}
    @Override public void removeLineListener(LineListener l) {}
}
