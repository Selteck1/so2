package com.selteck.funtimehelper;

import net.minecraft.client.gui.DrawContext;
import net.minecraft.client.gui.screen.Screen;
import net.minecraft.client.gui.widget.ButtonWidget;
import net.minecraft.text.Text;

public class MainMenuScreen extends Screen {
    public MainMenuScreen() { super(Text.literal("FunTimeHelper")); }
    @Override protected void init() {
        int w = 240, x = (width - w) / 2, y = height / 2 - 35;
        addDrawableChild(ButtonWidget.builder(Text.literal("Inventory Organizer"), b -> client.setScreen(new OrganizerScreen(this))).dimensions(x, y, w, 22).build());
        addDrawableChild(ButtonWidget.builder(Text.literal("Smart Item Filter"), b -> client.setScreen(new FilterScreen(this))).dimensions(x, y + 28, w, 22).build());
        addDrawableChild(ButtonWidget.builder(Text.literal("Manage Profiles (O)"), b -> client.setScreen(new ProfilesScreen(this))).dimensions(x, y + 56, w, 22).build());
        addDrawableChild(ButtonWidget.builder(Text.literal("Close"), b -> close()).dimensions(x, y + 84, w, 22).build());
    }
    @Override public void render(DrawContext context, int mouseX, int mouseY, float delta) {
        context.fill(0, 0, width, height, 0xC0101010);
        context.drawCenteredTextWithShadow(textRenderer, title, width / 2, height / 2 - 82, 0xFFFFFF);
        super.render(context, mouseX, mouseY, delta);
    }
}
