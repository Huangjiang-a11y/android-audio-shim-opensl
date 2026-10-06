package de.maxhenkel.shim;
import java.io.*;
public class NativeAudio {
private static boolean loaded = false;
private static String loadError = null;
static {
String[] candidates = {
"/data/data/git.artdeell.mojo/files/libaudioshim.so",
"/data/data/git.artdeell.mojo/runtimes/Internal/lib/aarch64/server/libaudioshim.so",
System.getProperty("user.dir") + "/libaudioshim.so",
System.getProperty("java.io.tmpdir") + "/libaudioshim.so",
};
Throwable lastErr = null;

// Read the bundled native library once
byte[] data = null;
try {
InputStream is = NativeAudio.class.getResourceAsStream("/natives/libaudioshim.so");
if (is != null) {
ByteArrayOutputStream bos = new ByteArrayOutputStream();
byte[] buf = new byte[8192];
int n;
while ((n = is.read(buf)) != -1) bos.write(buf, 0, n);
is.close();
data = bos.toByteArray();
}
} catch (Throwable t) { lastErr = t; }

for (String path : candidates) {
try {
File f = new File(path);
boolean writable = true;
if (data != null) {
try {
File parent = f.getParentFile();
if (parent != null) parent.mkdirs();
FileOutputStream fos = new FileOutputStream(f);
fos.write(data);
fos.close();
f.setExecutable(true, false);
} catch (Throwable t) {
writable = false;
}
}
if (!f.exists()) { lastErr = new IOException("not found: " + path); continue; }
if (!writable && data != null && f.length() != data.length) {
lastErr = new IOException("stale cached .so at " + path + " (cannot overwrite)");
continue;
}
System.load(f.getAbsolutePath());
loaded = true;
break;
} catch (Throwable t) { lastErr = t; }
}
if (!loaded && lastErr != null) loadError = lastErr.toString();
}
public static boolean isAvailable() { return loaded; }
public static String getLoadError() { return loadError; }

// Modified: channels param + handle-based API (input now uses OpenSL ES too)
public static native int open(int sampleRate, int channels, int framesPerBuffer);
public static native int start(int handle);
public static native int stop(int handle);
public static native int close(int handle);
public static native int read(int handle, byte[] buf, int offset, int length);
// Real number of bytes currently readable without blocking
public static native int available(int handle);
}
