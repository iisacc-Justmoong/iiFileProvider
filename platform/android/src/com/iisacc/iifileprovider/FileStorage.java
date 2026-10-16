package com.iisacc.iifileprovider;

import android.content.ContentResolver;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;

/** OS bridge for granted Android documents; domain permission checks stay with callers. */
public final class FileStorage {
    private FileStorage() {}
    private static final class Runtime {
        static final boolean available;
        static {
            boolean loaded = false;
            try { System.loadLibrary("iiFileProviderNative"); loaded = true; }
            catch (UnsatisfiedLinkError error) { Log.e("iiFileProvider", "Native presentation runtime is missing", error); }
            available = loaded;
        }
    }
    public static boolean isControlFile(String name) {
        if (name == null) return true;
        String folded = name.toLowerCase(java.util.Locale.ROOT);
        return folded.equals(".iisacc-file-provider") || folded.equals(".iisacc-temporary.lock")
            || folded.startsWith(".iifile-stage-") || folded.endsWith(".iisacc-preview")
            || folded.endsWith(".iisacc.json") || folded.endsWith(".iisacc-lock");
    }
    private static native void nativePresentLater(String path);
    private static void completed(File path) { if (Runtime.available) nativePresentLater(path.getAbsolutePath()); }
    public static OutputStream openOutput(String path) throws IOException {
        return new FileOutputStream(path) {
            private boolean finished;
            @Override public void close() throws IOException {
                if (finished) return;
                super.close(); finished = true; completed(new File(path));
            }
        };
    }
    public static OutputStream openOutput(ContentResolver resolver, Uri uri, String mode) throws IOException {
        OutputStream stream = resolver.openOutputStream(uri, mode);
        if (stream == null) throw new IOException("The document provider did not open an output stream");
        return stream;
    }
    public static ParcelFileDescriptor openDescriptor(File path, int mode) throws IOException {
        if ((mode & ParcelFileDescriptor.MODE_WRITE_ONLY) == 0) return ParcelFileDescriptor.open(path, mode);
        return ParcelFileDescriptor.open(path, mode, new Handler(Looper.getMainLooper()), error -> {
            if (error == null) completed(path);
        });
    }
    public static ParcelFileDescriptor openDescriptor(File path, int mode, Handler handler,
            ParcelFileDescriptor.OnCloseListener listener) throws IOException {
        return ParcelFileDescriptor.open(path, mode, handler, error -> {
            if (error == null && (mode & ParcelFileDescriptor.MODE_WRITE_ONLY) != 0) completed(path);
            if (listener != null) listener.onClose(error);
        });
    }
    public static boolean mkdir(File path) { return path.mkdir(); }
    public static boolean mkdirs(File path) { return path.mkdirs(); }
    public static boolean createNewFile(File path) throws IOException { boolean created = path.createNewFile(); if (created) completed(path); return created; }
    public static final class AtomicFile extends android.util.AtomicFile {
        private final File destination;
        public AtomicFile(File path) { super(path); destination = path; }
        @Override public FileOutputStream startWrite() throws IOException { return super.startWrite(); }
        @Override public void finishWrite(FileOutputStream stream) { super.finishWrite(stream); completed(destination); }
        @Override public void failWrite(FileOutputStream stream) { super.failWrite(stream); }
    }
}
