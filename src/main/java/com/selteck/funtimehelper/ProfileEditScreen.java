package com.selteck.funtimehelper;

import net.minecraft.client.gui.DrawContext;
import net.minecraft.client.gui.screen.Screen;
import net.minecraft.client.gui.widget.ButtonWidget;
import net.minecraft.client.gui.widget.TextFieldWidget;
import net.minecraft.text.Text;
import org.lwjgl.glfw.GLFW;

public class ProfileEditScreen extends Screen {
    private final Screen parent;
    private final ProfileStore.SortProfile profile;
    private boolean waitingForKey = false;
    private TextFieldWidget nameField;

    public ProfileEditScreen(Screen parent, ProfileStore.SortProfile profile) {
        super(Text.literal("Edit profile"));
        this.parent = parent;
        this.profile = profile;
    }

    @Override protected void init() {
        int x = width / 2 - 110;
        int y = height / 2 - 42;
        nameField = new TextFieldWidget(textRenderer, x, y, 220, 20, Text.literal("Profile name"));
        nameField.setMaxLength(32);
        nameField.setText(profile.name == null ? "Profile" : profile.name);
        addDrawableChild(nameField);
        addDrawableChild(ButtonWidget.builder(Text.literal("Bind: " + ProfilesScreen.keyName(profile.keyCode)), b -> {
            waitingForKey = true;
        }).dimensions(x, y + 30, 220, 22).build());
        addDrawableChild(ButtonWidget.builder(Text.literal("Save"), b -> {
            String value = nameField.getText().trim();
            profile.name = value.isEmpty() ? "Profile" : value;
            ProfileStore.save();
            client.setScreen(parent);
        }).dimensions(x, y + 62, 105, 22).build());
        addDrawableChild(ButtonWidget.builder(Text.literal("Cancel"), b -> client.setScreen(parent)).dimensions(x + 115, y + 62, 105, 22).build());
    }

    @Override public boolean keyPressed(int keyCode, int scanCode, int modifiers) {
        if (waitingForKey) {
            if (keyCode == GLFW.GLFW_KEY_ESCAPE) {
                waitingForKey = false;
                return true;
            }
            if (keyCode == GLFW.GLFW_KEY_P || keyCode == GLFW.GLFW_KEY_O || ProfileStore.isKeyUsed(keyCode, profile)) {
                waitingForKey = false;
                clearAndInit();
                return true;
            }
            profile.keyCode = keyCode;
            waitingForKey = false;
            clearAndInit();
            return true;
        }
        return super.keyPressed(keyCode, scanCode, modifiers);
    }

    @Override public void render(DrawContext c, int mx, int my, float d) {
        renderBackground(c, mx, my, d);
        c.drawCenteredTextWithShadow(textRenderer,
                waitingForKey ? Text.literal("Press an unused key (ESC to cancel)") : title,
                width / 2, height / 2 - 72, 0xFFFFFF);
        super.render(c, mx, my, d);
    }
}
