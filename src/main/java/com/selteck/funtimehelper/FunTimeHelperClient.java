package com.selteck.funtimehelper;

import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.fabricmc.fabric.api.client.keybinding.v1.KeyBindingHelper;
import net.minecraft.client.option.KeyBinding;
import net.minecraft.client.util.InputUtil;
import org.lwjgl.glfw.GLFW;
import java.util.HashSet;
import java.util.Set;

public class FunTimeHelperClient implements ClientModInitializer {
    public static final String MOD_ID = "funtimehelper";
    private static KeyBinding openMenu;
    private static KeyBinding manageProfiles;
    private static final Set<Integer> heldLastTick = new HashSet<>();

    @Override public void onInitializeClient() {
        ProfileStore.load();
        openMenu = KeyBindingHelper.registerKeyBinding(new KeyBinding("key.funtimehelper.open_menu", InputUtil.Type.KEYSYM, GLFW.GLFW_KEY_P, "category.funtimehelper"));
        manageProfiles = KeyBindingHelper.registerKeyBinding(new KeyBinding("key.funtimehelper.manage_profiles", InputUtil.Type.KEYSYM, GLFW.GLFW_KEY_O, "category.funtimehelper"));
        ClientTickEvents.END_CLIENT_TICK.register(client -> {
            while (openMenu.wasPressed()) client.setScreen(new MainMenuScreen());
            while (manageProfiles.wasPressed()) client.setScreen(new ProfilesScreen(null));
            if (client.player != null && client.getWindow() != null) {
                Set<Integer> currentlyHeld = new HashSet<>();
                for (ProfileStore.SortProfile profile : ProfileStore.PROFILES) {
                    int key = profile.keyCode;
                    if (key < 0 || key == GLFW.GLFW_KEY_P || key == GLFW.GLFW_KEY_O) continue;
                    boolean down = InputUtil.isKeyPressed(client.getWindow().getHandle(), key);
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
