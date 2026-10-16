#pragma once
// world_visuals.hpp — fog + ambient + skybox (local-only, changes RenderSettings)
// Standoff 2 1.0.0 ARM64 — RVA confirmed from dump.cs

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <android/log.h>

namespace ximira::world {

// ─── RVA таблица (native _Injected из dump.cs 1.0.0) ────────────────────────
namespace rva {
    constexpr std::uintptr_t kSetFogColor            = 0x75FDE94;
    constexpr std::uintptr_t kGetFogColor            = 0x760C044;
    constexpr std::uintptr_t kSetFogDensity          = 0x76096D0;
    constexpr std::uintptr_t kSetFogMode             = 0x75EA52C;
    constexpr std::uintptr_t kSetFog                 = 0x75F0BD4;
    constexpr std::uintptr_t kSetAmbientLight        = 0x75F8A44;
    constexpr std::uintptr_t kGetAmbientLight        = 0x7607D50;
    constexpr std::uintptr_t kSetAmbientSkyColor     = 0x75E48DC;
    constexpr std::uintptr_t kSetAmbientEquatorColor = 0x76030B8;
    constexpr std::uintptr_t kSetAmbientGroundColor  = 0x760BD74;
    constexpr std::uintptr_t kSetAmbientIntensity    = 0x75F05F4;
    constexpr std::uintptr_t kSetSkybox              = 0x75F73A4;
    constexpr std::uintptr_t kSetSun                 = 0x75ED53C;
}

// ─── Unity Color (r,g,b,a float) ─────────────────────────────────────────────
struct Color { float r, g, b, a; };

// ─── состояние (управляется из UI) ─────────────────────────────────────────
struct FogState {
    bool  enabled         = false;
    bool  applyColor      = false;
    float color[4]        = {0.5f, 0.5f, 0.5f, 1.0f};
};
struct AmbientState {
    bool  enabled         = false;
    bool  applyLight      = false;
    float light[4]        = {1.0f, 1.0f, 1.0f, 1.0f};
    bool  applySky        = false;
    float sky[4]          = {0.5f, 0.7f, 1.0f, 1.0f};
    bool  applyEquator    = false;
    float equator[4]      = {0.5f, 0.5f, 0.5f, 1.0f};
    bool  applyGround     = false;
    float ground[4]       = {0.3f, 0.3f, 0.3f, 1.0f};
};

struct WorldState {
    std::uintptr_t unityBase = 0;
    FogState     fog{};
    AmbientState ambient{};
    Color        origFogColor{};
    Color        origAmbientLight{};
    Color        origAmbientSky{};
    bool         origSaved = false;
};
inline WorldState g_state;

// ─── helpers ────────────────────────────────────────────────────────────────
using SetColorFn = void (*)(Color*);
using GetColorFn = void (*)(Color*);

inline void callSetColor(std::uintptr_t rva, const float c[4]) {
    if (!g_state.unityBase) return;
    auto fn = reinterpret_cast<SetColorFn>(g_state.unityBase + rva);
    if (!fn) return;
    Color col{c[0], c[1], c[2], c[3]};
    fn(&col);
}

inline void callGetColor(std::uintptr_t rva, Color& out) {
    out = Color{0, 0, 0, 0};
    if (!g_state.unityBase) return;
    auto fn = reinterpret_cast<GetColorFn>(g_state.unityBase + rva);
    if (!fn) return;
    fn(&out);
}

// ─── save original (один раз) ───────────────────────────────────────────────
inline void saveOriginals() {
    if (g_state.origSaved || !g_state.unityBase) return;
    callGetColor(rva::kGetFogColor,      g_state.origFogColor);
    callGetColor(rva::kGetAmbientLight,  g_state.origAmbientLight);
    g_state.origSaved = true;
    __android_log_print(ANDROID_LOG_INFO, "skeet",
        "WorldVisuals: originals saved fog=(%.2f,%.2f,%.2f,%.2f) ambient=(%.2f,%.2f,%.2f,%.2f)",
        g_state.origFogColor.r, g_state.origFogColor.g, g_state.origFogColor.b, g_state.origFogColor.a,
        g_state.origAmbientLight.r, g_state.origAmbientLight.g,
        g_state.origAmbientLight.b, g_state.origAmbientLight.a);
}

// ─── apply (с readback-диагностикой) ───────────────────────────────────────
inline void apply() {
    if (!g_state.unityBase) return;
    saveOriginals();

    if (g_state.fog.enabled && g_state.fog.applyColor) {callSetColor(rva::kSetFogColor, g_state.fog.color);
        static int dbg1 = 0;
        if (dbg1++ % 60 == 0) {
            Color rb{};
            callGetColor(rva::kGetFogColor, rb);
            __android_log_print(ANDROID_LOG_INFO, "skeet",
                "Fog readback: want=(%.3f,%.3f,%.3f,%.3f) got=(%.3f,%.3f,%.3f,%.3f)",
                g_state.fog.color[0], g_state.fog.color[1],
                g_state.fog.color[2], g_state.fog.color[3],
                rb.r, rb.g, rb.b, rb.a);
        }
    } else if (!g_state.fog.enabled && g_state.origSaved) {
        callSetColor(rva::kSetFogColor, &g_state.origFogColor.r);
    }

    if (g_state.ambient.enabled && g_state.ambient.applyLight) {
        callSetColor(rva::kSetAmbientLight, g_state.ambient.light);
        static int dbg2 = 0;
        if (dbg2++ % 60 == 0) {
            Color rb{};
            callGetColor(rva::kGetAmbientLight, rb);
            __android_log_print(ANDROID_LOG_INFO, "skeet",
                "Ambient readback: want=(%.3f,%.3f,%.3f,%.3f) got=(%.3f,%.3f,%.3f,%.3f)",
                g_state.ambient.light[0], g_state.ambient.light[1],
                g_state.ambient.light[2], g_state.ambient.light[3],
                rb.r, rb.g, rb.b, rb.a);
        }
    } else if (!g_state.ambient.enabled && g_state.origSaved) {
        callSetColor(rva::kSetAmbientLight, &g_state.origAmbientLight.r);
    }

    if (g_state.ambient.enabled && g_state.ambient.applySky) {
        callSetColor(rva::kSetAmbientSkyColor, g_state.ambient.sky);
    }
    if (g_state.ambient.enabled && g_state.ambient.applyEquator) {
        callSetColor(rva::kSetAmbientEquatorColor, g_state.ambient.equator);
    }
    if (g_state.ambient.enabled && g_state.ambient.applyGround) {
        callSetColor(rva::kSetAmbientGroundColor, g_state.ambient.ground);
    }
}

// ─── тестовый dump ─────────────────────────────────────────────────────────
inline void testDump() {
    if (!g_state.unityBase) return;
    static bool dumped = false;
    if (dumped) return;
    dumped = true;

    Color fog{};
    callGetColor(rva::kGetFogColor, fog);
    __android_log_print(ANDROID_LOG_INFO, "skeet",
        "WorldVisuals TEST: base=%p fog=(%.3f,%.3f,%.3f,%.3f)",
        reinterpret_cast<void*>(g_state.unityBase), fog.r, fog.g, fog.b, fog.a);

    Color amb{};
    callGetColor(rva::kGetAmbientLight, amb);
    __android_log_print(ANDROID_LOG_INFO, "skeet",
        "WorldVisuals TEST: ambient=(%.3f,%.3f,%.3f,%.3f)", amb.r, amb.g, amb.b, amb.a);
}

} // namespace ximira::world