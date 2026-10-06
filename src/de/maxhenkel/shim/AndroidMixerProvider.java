package de.maxhenkel.shim;

import javax.sound.sampled.*;
import javax.sound.sampled.spi.MixerProvider;

public class AndroidMixerProvider extends MixerProvider {

    private static final AndroidMixer INSTANCE = new AndroidMixer();

    @Override
    public Mixer.Info[] getMixerInfo() {
        return new Mixer.Info[]{ INSTANCE.getMixerInfo() };
    }

    @Override
    public Mixer getMixer(Mixer.Info info) {
        if (info == null || info.equals(INSTANCE.getMixerInfo()))
            return INSTANCE;
        throw new IllegalArgumentException("Unknown mixer: " + info);
    }
}
