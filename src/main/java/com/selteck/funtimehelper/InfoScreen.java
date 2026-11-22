package com.selteck.funtimehelper;

import net.minecraft.client.gui.DrawContext;
import net.minecraft.client.gui.screen.Screen;
import net.minecraft.client.gui.widget.ButtonWidget;
import net.minecraft.text.Text;

public class InfoScreen extends Screen {
    private final String heading, message;
    public InfoScreen(String heading,String message) { super(Text.literal(heading)); this.heading=heading; this.message=message; }
    @Override protected void init() { addDrawableChild(ButtonWidget.builder(Text.literal("Back"), b -> close()).dimensions(width/2-60,height/2+20,120,20).build()); }
    @Override public void render(DrawContext c,int mx,int my,float d) { renderBackground(c,mx,my,d); c.drawCenteredTextWithShadow(textRenderer,Text.literal(heading),width/2,height/2-25,0xFFFFFF); c.drawCenteredTextWithShadow(textRenderer,Text.literal(message),width/2,height/2,0xBBBBBB); super.render(c,mx,my,d); }
}
