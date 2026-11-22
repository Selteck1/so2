package com.selteck.funtimehelper;

import net.minecraft.client.gui.DrawContext;
import net.minecraft.client.gui.screen.Screen;
import net.minecraft.client.gui.widget.ButtonWidget;
import net.minecraft.text.Text;
import org.lwjgl.glfw.GLFW;

public class ProfilesScreen extends Screen {
    private final Screen parent;
    public ProfilesScreen(Screen parent) { super(Text.literal("Saved Sort Profiles")); this.parent = parent; }
    @Override protected void init() {
        int x = width/2 - 150, y = 42;
        for (int i=0; i<ProfileStore.PROFILES.size(); i++) {
            final int index=i; ProfileStore.SortProfile p=ProfileStore.PROFILES.get(i);
            addDrawableChild(ButtonWidget.builder(Text.literal(p.name + "  [" + keyName(p.keyCode) + "]"), b -> client.setScreen(new ProfileEditScreen(this, p))).dimensions(x, y+i*26, 190, 20).build());
            addDrawableChild(ButtonWidget.builder(Text.literal("Delete"), b -> { ProfileStore.PROFILES.remove(index); ProfileStore.save(); clearAndInit(); }).dimensions(x+196, y+i*26, 104, 20).build());
        }
        int bottom = Math.min(y + ProfileStore.PROFILES.size()*26, height-60);
        addDrawableChild(ButtonWidget.builder(Text.literal("+ Create profile"), b -> { ProfileStore.SortProfile p=new ProfileStore.SortProfile("Profile " + (ProfileStore.PROFILES.size()+1)); ProfileStore.PROFILES.add(p); ProfileStore.save(); client.setScreen(new ProfileEditScreen(this,p)); }).dimensions(x, bottom, 145, 20).build());
        addDrawableChild(ButtonWidget.builder(Text.literal("Back"), b -> client.setScreen(parent)).dimensions(x+155, bottom, 145, 20).build());
    }
    @Override public void render(DrawContext c,int mx,int my,float d) { c.fill(0, 0, width, height, 0xC0101010); c.drawCenteredTextWithShadow(textRenderer,title,width/2,18,0xFFFFFF); super.render(c,mx,my,d); }
    static String keyName(int code) { return code == -1 ? "UNBOUND" : GLFW.glfwGetKeyName(code,0) == null ? "KEY " + code : GLFW.glfwGetKeyName(code,0).toUpperCase(); }
}
