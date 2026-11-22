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
import java.util.Map;
import java.util.LinkedHashMap;

public final class ProfileStore {
    private static final Gson GSON = new GsonBuilder().setPrettyPrinting().create();
    private static final Path FILE = FabricLoader.getInstance().getConfigDir().resolve("funtimehelper-data.json");
    public static final List<SortProfile> PROFILES = new ArrayList<>();
    public static final List<FilterEntry> FILTERS = new ArrayList<>();
    private ProfileStore() {}

    public static void load() {
        try {
            if (Files.exists(FILE)) {
                try (Reader reader = Files.newBufferedReader(FILE)) {
                    SaveData data = GSON.fromJson(reader, SaveData.class);
                    if (data != null) {
                        if (data.profiles != null) PROFILES.addAll(data.profiles);
                        if (data.filters != null) FILTERS.addAll(data.filters);
                    }
                }
            }
        } catch (Exception e) {
            System.err.println("[FunTimeHelper] Could not load config: " + e.getMessage());
        }
        for (SortProfile p : PROFILES) {
            if (p.layout == null) p.layout = new LinkedHashMap<>();
            if (p.name == null) p.name = "Profile";
        }
    }

    public static boolean isKeyUsed(int key, SortProfile except) {
        return key != -1 && PROFILES.stream().anyMatch(p -> p != except && p.keyCode == key);
    }

    public static void save() {
        try {
            Files.createDirectories(FILE.getParent());
            SaveData data = new SaveData();
            data.profiles = PROFILES;
            data.filters = FILTERS;
            try (Writer writer = Files.newBufferedWriter(FILE)) { GSON.toJson(data, writer); }
        } catch (Exception e) {
            System.err.println("[FunTimeHelper] Could not save config: " + e.getMessage());
        }
    }

    public static class SaveData {
        public List<SortProfile> profiles = new ArrayList<>();
        public List<FilterEntry> filters = new ArrayList<>();
    }

    public static class SortProfile {
        public String name;
        public int keyCode = -1;
        public Map<Integer, String> layout = new LinkedHashMap<>();
        public SortProfile(String name) { this.name = name; }
    }

    public static class FilterEntry {
        public String fingerprint;
        public String displayName;
        public boolean enabled = true;
        public FilterEntry(String fingerprint, String displayName) {
            this.fingerprint = fingerprint;
            this.displayName = displayName;
        }
    }
}
