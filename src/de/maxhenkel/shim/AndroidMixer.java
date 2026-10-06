package de.maxhenkel.shim;

import javax.sound.sampled.*;

public class AndroidMixer implements Mixer {

    private static final Mixer.Info MIXER_INFO = new Mixer.Info(
            "Android AudioRecord",
            "Android",
            "Microphone/Speaker via AAudio",
            "1.0") {};

    private static final AudioFormat[] FORMATS = {
        new AudioFormat(AudioFormat.Encoding.PCM_SIGNED, 48000, 16, 1, 2, 48000, false),
        new AudioFormat(AudioFormat.Encoding.PCM_SIGNED, 44100, 16, 1, 2, 44100, false),
        new AudioFormat(AudioFormat.Encoding.PCM_SIGNED, 16000, 16, 1, 2, 16000, false),
    };

    @Override
    public Mixer.Info getMixerInfo() { return MIXER_INFO; }

    @Override
    public boolean isLineSupported(Line.Info info) {
        if (!(info instanceof DataLine.Info)) return false;
        Class<?> cls = ((DataLine.Info) info).getLineClass();
        return TargetDataLine.class.isAssignableFrom(cls)
            || SourceDataLine.class.isAssignableFrom(cls);
    }

    @Override
    public Line getLine(Line.Info info) throws LineUnavailableException {
        if (!isLineSupported(info))
            throw new LineUnavailableException("Unsupported: " + info);

        AudioFormat fmt = FORMATS[0];
        if (info instanceof DataLine.Info) {
            AudioFormat[] fmts = ((DataLine.Info) info).getFormats();
            if (fmts != null && fmts.length > 0 && fmts[0].getSampleRate() != AudioSystem.NOT_SPECIFIED)
                fmt = fmts[0];
        }

        Class<?> cls = ((DataLine.Info) info).getLineClass();
        if (SourceDataLine.class.isAssignableFrom(cls))
            return new AndroidSourceDataLine(fmt);
        return new AndroidTargetDataLine(fmt);
    }

    @Override
    public Line.Info[] getTargetLineInfo() {
        Line.Info[] r = new Line.Info[FORMATS.length];
        for (int i = 0; i < FORMATS.length; i++)
            r[i] = new DataLine.Info(TargetDataLine.class, FORMATS[i]);
        return r;
    }

    @Override
    public Line.Info[] getSourceLineInfo() {
        Line.Info[] r = new Line.Info[FORMATS.length];
        for (int i = 0; i < FORMATS.length; i++)
            r[i] = new DataLine.Info(SourceDataLine.class, FORMATS[i]);
        return r;
    }

    @Override public Line.Info[] getTargetLineInfo(Line.Info info) { return getTargetLineInfo(); }
    @Override public Line.Info[] getSourceLineInfo(Line.Info info) { return getSourceLineInfo(); }
    @Override public int     getMaxLines(Line.Info info) { return 8; }
    @Override public Line[]  getSourceLines()            { return new Line[0]; }
    @Override public Line[]  getTargetLines()            { return new Line[0]; }
    @Override public void    synchronize(Line[] l, boolean b) {}
    @Override public void    unsynchronize(Line[] l) {}
    @Override public boolean isSynchronizationSupported(Line[] l, boolean b) { return false; }
    @Override public void    open() throws LineUnavailableException {}
    @Override public void    close() {}
    @Override public boolean isOpen() { return true; }
    @Override public Control[] getControls() { return new Control[0]; }
    @Override public boolean   isControlSupported(Control.Type t) { return false; }
    @Override public Control   getControl(Control.Type t) { throw new IllegalArgumentException(); }
    @Override public Line.Info getLineInfo() { return new Line.Info(Mixer.class); }
    @Override public void addLineListener(LineListener l) {}
    @Override public void removeLineListener(LineListener l) {}
}
