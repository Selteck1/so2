package com.selteck.funtimehelper;

import net.minecraft.client.MinecraftClient;
import net.minecraft.client.network.ClientPlayerEntity;
import net.minecraft.item.ItemStack;
import net.minecraft.registry.Registries;
import net.minecraft.component.DataComponentTypes;
import net.minecraft.component.type.LoreComponent;
import net.minecraft.text.Text;
import net.minecraft.screen.slot.SlotActionType;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Queue;
import java.util.Set;

public final class InventoryLogic {
    private static final Queue<Integer> CLICK_QUEUE = new ArrayDeque<>();
    private static int clickDelay = 0;
    private static final Map<Integer, String> previous = new HashMap<>();
    private static final Set<Integer> pendingDrops = new HashSet<>();
    private InventoryLogic() {}

    public static String fingerprint(ItemStack stack) {
        if (stack == null || stack.isEmpty()) return "";
        StringBuilder out = new StringBuilder(Registries.ITEM.getId(stack.getItem()).toString());
        Text customName = stack.get(DataComponentTypes.CUSTOM_NAME);
        if (customName != null) out.append("|name:").append(customName.getString());
        LoreComponent lore = stack.get(DataComponentTypes.LORE);
        if (lore != null) {
            for (Text line : lore.lines()) out.append("|lore:").append(line.getString());
        }
        Object modelData = stack.get(DataComponentTypes.CUSTOM_MODEL_DATA);
        if (modelData != null) out.append("|model:").append(modelData);
        return out.toString();
    }

    public static void capture(ProfileStore.SortProfile profile, ClientPlayerEntity player) {
        profile.layout.clear();
        for (int slot = 5; slot <= 45; slot++) {
            ItemStack stack = player.playerScreenHandler.getSlot(slot).getStack();
            profile.layout.put(slot, fingerprint(stack));
        }
        ProfileStore.save();
    }

    public static void apply(ProfileStore.SortProfile profile, ClientPlayerEntity player) {
        if (profile.layout == null || profile.layout.isEmpty() || !CLICK_QUEUE.isEmpty()) return;
        List<String> current = new ArrayList<>();
        List<String> desired = new ArrayList<>();
        for (int slot = 5; slot <= 45; slot++) {
            current.add(fingerprint(player.playerScreenHandler.getSlot(slot).getStack()));
            desired.add(profile.layout.getOrDefault(slot, ""));
        }
        List<Integer> swaps = new ArrayList<>();
        for (int i = 0; i < desired.size(); i++) {
            if (current.get(i).equals(desired.get(i))) continue;
            int found = -1;
            for (int j = i + 1; j < current.size(); j++) {
                if (current.get(j).equals(desired.get(i))) { found = j; break; }
            }
            if (found >= 0) {
                String temp = current.get(i);
                current.set(i, current.get(found));
                current.set(found, temp);
                swaps.add(i + 5);
                swaps.add(found + 5);
            }
        }
        for (int n = 0; n < swaps.size(); n += 2) {
            int a = swaps.get(n), b = swaps.get(n + 1);
            CLICK_QUEUE.add(a);
            CLICK_QUEUE.add(b);
            CLICK_QUEUE.add(a);
            CLICK_QUEUE.add(-1);
        }
    }

    public static void tick(MinecraftClient client) {
        ClientPlayerEntity player = client.player;
        if (player == null) return;

        if (!CLICK_QUEUE.isEmpty()) {
            if (clickDelay > 0) clickDelay--;
            else {
                int slot = CLICK_QUEUE.poll();
                if (slot >= 0) {
                    client.interactionManager.clickSlot(player.playerScreenHandler.syncId, slot, 0, SlotActionType.PICKUP, player);
                }
                clickDelay = 2;
            }
        }

        if (client.currentScreen == null && CLICK_QUEUE.isEmpty()) {
            Set<String> enabled = new HashSet<>();
            for (ProfileStore.FilterEntry entry : ProfileStore.FILTERS) {
                if (entry.enabled) enabled.add(entry.fingerprint);
            }
            for (int slot = 9; slot <= 44; slot++) {
                ItemStack stack = player.playerScreenHandler.getSlot(slot).getStack();
                String fp = fingerprint(stack);
                String now = fp + "#" + (stack.isEmpty() ? 0 : stack.getCount());
                String old = previous.getOrDefault(slot, "");
                boolean sameStackGrew = old.startsWith(fp + "#") && count(old) < stack.getCount();
                boolean gained = !fp.isEmpty() && (old.isEmpty() || !old.startsWith(fp + "#") || sameStackGrew);
                if (gained && enabled.contains(fp) && !pendingDrops.contains(slot)) {
                    client.interactionManager.clickSlot(player.playerScreenHandler.syncId, slot, 1, SlotActionType.THROW, player);
                    pendingDrops.add(slot);
                }
                if (fp.isEmpty() || !enabled.contains(fp)) pendingDrops.remove(slot);
                previous.put(slot, now);
            }
        } else {
            snapshot(player);
        }
    }

    private static int count(String value) {
        int at = value.lastIndexOf('#');
        try { return Integer.parseInt(value.substring(at + 1)); } catch (Exception ignored) { return 0; }
    }

    private static void snapshot(ClientPlayerEntity player) {
        for (int slot = 9; slot <= 44; slot++) {
            ItemStack stack = player.playerScreenHandler.getSlot(slot).getStack();
            previous.put(slot, fingerprint(stack) + "#" + (stack.isEmpty() ? 0 : stack.getCount()));
            if (stack.isEmpty()) pendingDrops.remove(slot);
        }
    }
}
