package com.selteck.funtimehelper;

import net.minecraft.client.gui.DrawContext;
import net.minecraft.client.gui.screen.Screen;
import net.minecraft.client.gui.widget.ButtonWidget;
import net.minecraft.text.Text;

public class OrganizerScreen extends Screen {
    private final Screen parent;
    private int selected = 0;
    public OrganizerScreen(Screen parent) { super(Text.literal("Inventory Organizer")); this.parent = parent; }

    @Override protected void init() {
        int x = width / 2 - 145;
        int y = 45;
        if (ProfileStore.PROFILES.isEmpty()) {
            addDrawableChild(ButtonWidget.builder(Text.literal("Create first profile"), b -> {
                ProfileStore.SortProfile p = new ProfileStore.SortProfile("Profile 1");
                ProfileStore.PROFILES.add(p); ProfileStore.save();
                client.setScreen(new ProfilesScreen(this));
            }).dimensions(x, y, 290, 22).build());
        } else {
            selected = Math.max(0, Math.min(selected, ProfileStore.PROFILES.size() - 1));
            for (int i = 0; i < ProfileStore.PROFILES.size(); i++) {
                final int index = i;
                ProfileStore.SortProfile p = ProfileStore.PROFILES.get(i);
                addDrawableChild(ButtonWidget.builder(Text.literal((i == selected ? "> " : "") + p.name + "  [" + ProfilesScreen.keyName(p.keyCode) + "]"), b -> {
                    selected = index; clearAndInit();
                }).dimensions(x, y + i * 25, 290, 20).build());
            }
            int actionsY = Math.min(y + ProfileStore.PROFILES.size() * 25 + 10, height - 85);
            addDrawableChild(ButtonWidget.builder(Text.literal("Capture current inventory layout"), b -> {
                InventoryLogic.capture(ProfileStore.PROFILES.get(selected), client.player);
                client.setScreen(new InfoScreen("Layout saved", "Your current inventory arrangement was saved to this profile."));
            }).dimensions(x, actionsY, 290, 22).build());
            addDrawableChild(ButtonWidget.builder(Text.literal("Manage profiles / keys (O)"), b -> client.setScreen(new ProfilesScreen(this))).dimensions(x, actionsY + 28, 290, 22).build());
        }
        addDrawableChild(ButtonWidget.builder(Text.literal("Back"), b -> client.setScreen(parent)).dimensions(width / 2 - 70, height - 32, 140, 20).build());
    }

    @Override public void render(DrawContext c, int mx, int my, float d) {
        renderBackground(c, mx, my, d);
        c.drawCenteredTextWithShadow(textRenderer, title, width / 2, 18, 0xFFFFFF);
        c.drawCenteredTextWithShadow(textRenderer, Text.literal("Arrange your inventory first, then capture it."), width / 2, 32, 0xAAAAAA);
        super.render(c, mx, my, d);
    }
}
