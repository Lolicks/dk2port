package com.jme3.system;
import java.io.InputStream;
import java.io.OutputStream;
public final class AppSettings {
    public AppSettings(boolean loadDefaults) {}
    public void load(InputStream in) {}
    public void save(OutputStream out) {}
    public String getString(String key) { return null; }
    public void putString(String key, String value) {}
}
