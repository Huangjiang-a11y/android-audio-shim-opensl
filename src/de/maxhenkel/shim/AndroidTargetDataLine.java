package de.maxhenkel.shim;
import javax.sound.sampled.*;
import java.util.concurrent.atomic.AtomicBoolean;

public class AndroidTargetDataLine implements TargetDataLine {
private AudioFormat format;
private int bufferSizeBytes;
private final AtomicBoolean open = new AtomicBoolean(false);
private final AtomicBoolean active = new AtomicBoolean(false);
private long framesRead = 0;
private int handle;

public AndroidTargetDataLine(AudioFormat format) {
this.format = format;
}

@Override
public void open(AudioFormat format, int bufferSize) throws LineUnavailableException {
if (open.get()) return;
if (!NativeAudio.isAvailable())
throw new LineUnavailableException("libaudioshim.so not loaded: " + NativeAudio.getLoadError());
this.format = format;
int rate = (int) format.getSampleRate();
int channels = format.getChannels();
bufferSizeBytes = (bufferSize > 0) ? bufferSize : rate * 2 * channels / 5;
int framesPerBuffer = bufferSizeBytes / (channels * 2);

// Modified: pass channels to open
this.handle = NativeAudio.open(rate, channels, framesPerBuffer);
if (this.handle < 0)
throw new LineUnavailableException("OpenSL ES record open failed: " + handle);
open.set(true);
framesRead = 0;
}

@Override public void open(AudioFormat fmt) throws LineUnavailableException { open(fmt, 0); }
@Override public void open() throws LineUnavailableException { open(format, 0); }

@Override
public void start() {
if (!open.get() || active.get()) return;
int r = NativeAudio.start(handle);
if (r != 0) throw new RuntimeException("OpenSL ES record start failed: " + r);
active.set(true);
}

@Override
public void stop() {
if (!open.get() || !active.get()) return;
NativeAudio.stop(handle);
active.set(false);
}

@Override
public void close() {
if (!open.get()) return;
if (active.get()) stop();
NativeAudio.close(handle);
open.set(false);
}

@Override
public int read(byte[] b, int off, int len) {
if (!open.get() || !active.get()) return 0;
int n = NativeAudio.read(handle, b, off, len);
if (n > 0) framesRead += n / format.getFrameSize();
return n < 0 ? 0 : n;
}

@Override public boolean isOpen() { return open.get(); }
@Override public boolean isActive() { return active.get(); }
@Override public boolean isRunning() { return active.get(); }
@Override public void drain() {}
@Override public void flush() {}
@Override public int available() {
if (!open.get()) return 0;
int n = NativeAudio.available(handle);
return n < 0 ? 0 : n;
}
@Override public int getBufferSize() { return bufferSizeBytes; }
@Override public AudioFormat getFormat() { return format; }
@Override public int getFramePosition() { return (int) framesRead; }
@Override public long getLongFramePosition() { return framesRead; }
@Override public long getMicrosecondPosition() {
return (long)(framesRead * 1_000_000.0 / format.getSampleRate());
}
@Override public float getLevel() {
if (!open.get()) return AudioSystem.NOT_SPECIFIED;
return NativeAudio.getLevel(handle);
}
@Override public Line.Info getLineInfo() {
return new DataLine.Info(TargetDataLine.class, format);
}
@Override public Control[] getControls() { return new Control[0]; }
@Override public boolean isControlSupported(Control.Type t) { return false; }
@Override public Control getControl(Control.Type t) { throw new IllegalArgumentException(); }
@Override public void addLineListener(LineListener l) {}
@Override public void removeLineListener(LineListener l) {}
}
