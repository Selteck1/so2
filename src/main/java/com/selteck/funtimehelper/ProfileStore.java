package com.selteck.funtimehelper;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.reflect.TypeToken;
import net.fabricmc.loader.api.FabricLoader;
import java.io.Reader;
import java.io.Writer;
import java.lang.reflect.Type;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

public final class ProfileStore {
    private static final Gson GSON = new GsonBuilder().setPrettyPrinting().create();
    private static final Path FILE = FabricLoader.getInstance().getConfigDir().resolve("funtimehelper-profiles.json");
    public static final List<SortProfile> PROFILES = new ArrayList<>();
    private ProfileStore() {}
    public static void load() {
        try { if (Files.exists(FILE)) { Type type = new TypeToken<List<SortProfile>>(){}.getType(); try (Reader reader = Files.newBufferedReader(FILE)) { List<SortProfile> loaded = GSON.fromJson(reader, type); if (loaded != null) PROFILES.addAll(loaded); } } }
        catch (Exception e) { System.err.println("[FunTimeHelper] Could not load profiles: " + e.getMessage()); }
    }
    public static boolean isKeyUsed(int key, SortProfile except) { return key != -1 && PROFILES.stream().anyMatch(p -> p != except && p.keyCode == key); }
    public static void save() {
        try { Files.createDirectories(FILE.getParent()); try (Writer writer = Files.newBufferedWriter(FILE)) { GSON.toJson(PROFILES, writer); } }
        catch (Exception e) { System.err.println("[FunTimeHelper] Could not save profiles: " + e.getMessage()); }
    }
    public static class SortProfile {
        public String name;
        public int keyCode = -1;
        public SortProfile(String name) { this.name = name; }
    }
}
