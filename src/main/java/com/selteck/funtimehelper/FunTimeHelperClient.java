package com.selteck.funtimehelper;

import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.fabricmc.fabric.api.client.keybinding.v1.KeyBindingHelper;
import net.minecraft.client.option.KeyBinding;
import net.minecraft.util.Identifier;
import org.lwjgl.glfw.GLFW;
import java.util.HashSet;
import java.util.Set;

public class FunTimeHelperClient implements ClientModInitializer {
    public static final String MOD_ID = "funtimehelper";
    private static final KeyBinding.Category CATEGORY = KeyBinding.Category.create(Identifier.of(MOD_ID, "general"));
    private static KeyBinding openMenu;
    private static KeyBinding manageProfiles;
    private static final Set<Integer> heldLastTick = new HashSet<>();

    @Override public void onInitializeClient() {
        ProfileStore.load();
        openMenu = KeyBindingHelper.registerKeyBinding(new KeyBinding("key.funtimehelper.open_menu", org.lwjgl.glfw.GLFW.GLFW_KEY_P, CATEGORY));
        manageProfiles = KeyBindingHelper.registerKeyBinding(new KeyBinding("key.funtimehelper.manage_profiles", org.lwjgl.glfw.GLFW.GLFW_KEY_O, CATEGORY));
        ClientTickEvents.END_CLIENT_TICK.register(client -> {
            while (openMenu.wasPressed()) client.setScreen(new MainMenuScreen());
            while (manageProfiles.wasPressed()) client.setScreen(new ProfilesScreen(null));
            if (client.player != null && client.getWindow() != null) {
                Set<Integer> currentlyHeld = new HashSet<>();
                long window = client.getWindow().getHandle();
                for (ProfileStore.SortProfile profile : ProfileStore.PROFILES) {
                    int key = profile.keyCode;
                    if (key < 0 || key == GLFW.GLFW_KEY_P || key == GLFW.GLFW_KEY_O) continue;
                    boolean down = GLFW.glfwGetKey(window, key) == GLFW.GLFW_PRESS;
                    if (down) {
                        currentlyHeld.add(key);
                        if (!heldLastTick.contains(key) && client.currentScreen == null) InventoryLogic.apply(profile, client.player);
                    }
                }
                heldLastTick.clear();
                heldLastTick.addAll(currentlyHeld);
            }
            InventoryLogic.tick(client);
        });
    }
}
