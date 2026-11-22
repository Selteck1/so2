package com.selteck.funtimehelper;

import net.minecraft.client.gui.DrawContext;
import net.minecraft.client.gui.screen.Screen;
import net.minecraft.client.gui.widget.ButtonWidget;
import net.minecraft.text.Text;
import net.minecraft.item.ItemStack;

public class FilterScreen extends Screen {
    private final Screen parent;
    public FilterScreen(Screen parent) { super(Text.literal("Smart Item Filter")); this.parent = parent; }

    @Override protected void init() {
        int x = width / 2 - 150;
        int y = 42;
        addDrawableChild(ButtonWidget.builder(Text.literal("Add item from main hand"), b -> {
            if (client.player == null) return;
            ItemStack stack = client.player.getMainHandStack();
            if (stack.isEmpty()) return;
            String fp = InventoryLogic.fingerprint(stack);
            boolean exists = ProfileStore.FILTERS.stream().anyMatch(e -> e.fingerprint.equals(fp));
            if (!exists) {
                ProfileStore.FILTERS.add(new ProfileStore.FilterEntry(fp, stack.getName().getString()));
                ProfileStore.save();
            }
            clearAndInit();
        }).dimensions(x, y, 300, 22).build());

        int listY = y + 32;
        int visible = Math.max(0, Math.min(ProfileStore.FILTERS.size(), (height - listY - 65) / 25));
        for (int i = 0; i < visible; i++) {
            final int index = i;
            ProfileStore.FilterEntry entry = ProfileStore.FILTERS.get(i);
            addDrawableChild(ButtonWidget.builder(Text.literal((entry.enabled ? "[ON] " : "[OFF] ") + entry.displayName), b -> {
                entry.enabled = !entry.enabled; ProfileStore.save(); clearAndInit();
            }).dimensions(x, listY + i * 25, 220, 20).build());
            addDrawableChild(ButtonWidget.builder(Text.literal("Remove"), b -> {
                ProfileStore.FILTERS.remove(index); ProfileStore.save(); clearAndInit();
            }).dimensions(x + 226, listY + i * 25, 74, 20).build());
        }
        addDrawableChild(ButtonWidget.builder(Text.literal("Back"), b -> client.setScreen(parent)).dimensions(width / 2 - 70, height - 30, 140, 20).build());
    }

    @Override public void render(DrawContext c, int mx, int my, float d) {
        renderBackground(c, mx, my, d);
        c.drawCenteredTextWithShadow(textRenderer, title, width / 2, 18, 0xFFFFFF);
        c.drawCenteredTextWithShadow(textRenderer, Text.literal("Select the item in your main hand before adding it."), width / 2, 30, 0xAAAAAA);
        super.render(c, mx, my, d);
    }
}
