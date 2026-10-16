#include "features/world_mod/world_mod.h"
#include "core/il2cpp.h"
#include "core/memory.h"
#include "util/log.h"

#include <cstdio>
#include <cstring>
#include <dlfcn.h>

namespace xim::features::world_mod {

using namespace xim::mem;

// ---------- config ----------
static WorldCfg g_cfg;
WorldCfg& config() { return g_cfg; }

// ---------- il2cpp low-level (dlsym, чтобы не тянуть чужой api) ----------
using t_domain_get     = void* (*)();
using t_asm_open       = void* (*)(void*, const char*);
using t_asm_image      = void* (*)(void*);
using t_class_from_name= void* (*)(void*, const char*, const char*);
using t_class_get_field= void* (*)(void*, const char*);
using t_field_offset   = std::size_t (*)(void*);

static t_domain_get      p_domain_get     = nullptr;
static t_asm_open        p_asm_open       = nullptr;
static t_asm_image       p_asm_image      = nullptr;
static t_class_from_name p_class_from_name= nullptr;
static t_class_get_field p_class_get_field= nullptr;
static t_field_offset    p_field_offset   = nullptr;

static bool resolve_il2cpp() {
    if (p_domain_get) return true;
    p_domain_get      = reinterpret_cast<t_domain_get>     (dlsym(RTLD_DEFAULT, "il2cpp_domain_get"));
    p_asm_open        = reinterpret_cast<t_asm_open>       (dlsym(RTLD_DEFAULT, "il2cpp_domain_assembly_open"));
    p_asm_image       = reinterpret_cast<t_asm_image>      (dlsym(RTLD_DEFAULT, "il2cpp_assembly_get_image"));
    p_class_from_name = reinterpret_cast<t_class_from_name>(dlsym(RTLD_DEFAULT, "il2cpp_class_from_name"));
    p_class_get_field = reinterpret_cast<t_class_get_field>(dlsym(RTLD_DEFAULT, "il2cpp_class_get_field_from_name"));
    p_field_offset    = reinterpret_cast<t_field_offset>   (dlsym(RTLD_DEFAULT, "il2cpp_field_get_offset"));

    return p_domain_get && p_asm_open && p_asm_image
        && p_class_from_name && p_class_get_field && p_field_offset;
}

// ---------- RenderSettings ----------
// UnityEngine.RenderSettings — статический класс. Все поля статические.
// Доступ к static_fields идёт через Il2CppClass+0xB8 (il2cpp 2020+ ARM64).
static constexpr std::size_t kStaticFields = 0xB8;

static void*     g_klass        = nullptr;
static uintptr_t g_static_fields= 0;

// оффсеты полей внутри static_fields (заполняются в init через il2cpp_field_get_offset)
static std::size_t off_fog          = 0;
static std::size_t off_fog_color    = 0;
static std::size_t off_fog_mode     = 0;
static std::size_t off_fog_density  = 0;
static std::size_t off_fog_start    = 0;   // нет прямого поля у Unity; для linear-режима
static std::size_t off_fog_end      = 0;   // (не используется, оставлено для ясности)
static std::size_t off_ambient      = 0;
static std::size_t off_ambient_int  = 0;
static std::size_t off_shadow_dist  = 0;

// ---------- resolve ----------
static bool resolve_render_settings() {
    if (g_static_fields) return true;

    if (!resolve_il2cpp()) {
        XERR("world_mod: il2cpp fns missing");
        return false;
    }

    void* domain = p_domain_get();
    if (!domain) return false;

    const char* candidates[] = {
        "UnityEngine.CoreModule.dll",
        "UnityEngine.CoreModule",
        "UnityEngine.dll",
        "UnityEngine",
    };

    void* img = nullptr;
    for (const char* a : candidates) {
        void* asm_ = p_asm_open(domain, a);
        if (!asm_) continue;
        img = p_asm_image(asm_);
        if (img) break;
    }
    if (!img) { XERR("world_mod: no UnityEngine image"); return false; }

    g_klass = p_class_from_name(img, "UnityEngine", "RenderSettings");
    if (!g_klass) { XERR("world_mod: no RenderSettings class"); return false; }

    // static_fields
    uintptr_t sf = 0;
    if (!read(reinterpret_cast<uintptr_t>(g_klass) + kStaticFields, sf) || !sf) {
        XERR("world_mod: no static_fields");
        return false;
    }
    g_static_fields = sf;

    auto field_off = [&](const char* name) -> std::size_t {
        void* f = p_class_get_field(g_klass, name);
        return f ? p_field_offset(f) : 0;
    };

    off_fog         = field_off("fog");
    off_fog_color   = field_off("fogColor");
    off_fog_mode    = field_off("fogMode");
    off_fog_density = field_off("fogDensity");
    off_ambient     = field_off("ambientLight");
    off_ambient_int = field_off("ambientIntensity");
    off_shadow_dist = field_off("shadowDistance");

    XLOG("world_mod: RenderSettings ok fog=%zu fogColor=%zu fogMode=%zu fogDensity=%zu ambient=%zu ambientInt=%zu shadowDist=%zu",
         off_fog, off_fog_color, off_fog_mode, off_fog_density,
         off_ambient, off_ambient_int, off_shadow_dist);
    return off_fog || off_ambient;
}

// ---------- cached last applied ----------
struct LastApplied {
    bool  fog_on       = false;
    bool  fog_first    = false;
    float fog_density  = -1.0f;
    int   fog_mode     = -1;
    float fog_color[4] = {0,0,0,0};

    bool  amb_on       = false;
    float amb_int      = -1.0f;
    float amb_color[4] = {0,0,0,0};

    bool  shadows_off  = false;
    bool  skybox_on    = false;
};
static LastApplied g_last;

// ---------- helpers ----------
static bool floats_differ(const float a[4], const float b[4]) {
    for (int i = 0; i < 4; ++i)
        if (a[i] != b[i]) return true;
    return false;
}

// ---------- fog ----------
static void apply_fog() {
    if (!off_fog) return;
    auto& f = g_cfg.fog;

    // enable/disable
    if (f.enabled != g_last.fog_first) {
        uint8_t on = f.enabled ? 1 : 0;
        write_at(g_static_fields, off_fog, on);
        g_last.fog_first = f.enabled;
    }
    if (!f.enabled) return;

    // mode (Unity: 1=Linear, 2=Exp, 3=Exp2). наш UI: 0/1/2
    int unity_mode = f.mode + 1;
    if (off_fog_mode && unity_mode != g_last.fog_mode) {
        write_at(g_static_fields, off_fog_mode, unity_mode);
        g_last.fog_mode = unity_mode;
    }

    // density
    if (off_fog_density && f.density != g_last.fog_density) {
        write_at(g_static_fields, off_fog_density, f.density);
        g_last.fog_density = f.density;
    }

    // color
    if (off_fog_color && floats_differ(f.color, g_last.fog_color)) {
        write_at(g_static_fields, off_fog_color, f.color);
        memcpy(g_last.fog_color, f.color, sizeof(g_last.fog_color));
    }
}

// ---------- ambient ----------
static void apply_ambient() {
    auto& a = g_cfg.ambient;

    if (off_ambient_int) {
        float want = a.enabled ? a.intensity : 1.0f;
        if (want != g_last.amb_int) {
            write_at(g_static_fields, off_ambient_int, want);
            g_last.amb_int = want;
        }
    }

    if (off_ambient) {
        if (a.enabled && floats_differ(a.color, g_last.amb_color)) {
            write_at(g_static_fields, off_ambient, a.color);
            memcpy(g_last.amb_color, a.color, sizeof(g_last.amb_color));
        } else if (!a.enabled && g_last.amb_on) {
            // восстановить дефолт
            const float def[4] = {0.21f, 0.21f, 0.21f, 1.0f};
            write_at(g_static_fields, off_ambient, def);
            memcpy(g_last.amb_color, def, sizeof(def));
        }
    }
    g_last.amb_on = a.enabled;
}

// ---------- shadows ----------
static void apply_shadows() {
    if (!off_shadow_dist) return;
    const bool off = g_cfg.culling.no_shadows;

    if (off != g_last.shadows_off) {
        float dist = off ? 0.0f : 150.0f;
        write_at(g_static_fields, off_shadow_dist, dist);
        g_last.shadows_off = off;
    }
}

// ---------- skybox tint через ambient ----------
// полноценная подмена cubemap требует создавать Texture каждый раз —
// здесь делаем визуальный аналог через глобальный tint ambient.
static void apply_skybox_tint() {
    auto& s = g_cfg.skybox;
    if (!s.enabled) { g_last.skybox_on = false; return; }
    if (!off_ambient) return;

    if (!g_last.skybox_on || floats_differ(s.horiz, g_last.amb_color)) {
        float t = s.brightness;
        float col[4] = {
            s.horiz[0] * t,
            s.horiz[1] * t,
            s.horiz[2] * t,
            1.0f
        };
        write_at(g_static_fields, off_ambient, col);
        memcpy(g_last.amb_color, col, sizeof(col));
        g_last.skybox_on = true;
    }
}

// ---------- culling ----------
// no_particles / no_smoke / no_flash / no_grass / no_hands / fullbright / wireframe —
// реализуются через обход UnityEngine.Object.FindObjectsOfType и отключение
// конкретных Renderer/ParticleSystem. Это делается отдельным worker'ом раз в 500мс,
// чтобы не жрать CPU в кадре. В текущей версии флаги сохраняются в конфиг и
// отдаются в UI, но реальный обход будет добавлен отдельным блоком после того,
// как подтвердим что игры корректно отдают FindObjectsOfType через il2cpp.

// ---------- public ----------
bool init() {
    return resolve_render_settings();
}

void update() {
    if (!g_static_fields) {
        if (!resolve_render_settings()) return;
    }

    apply_fog();
    apply_ambient();
    apply_shadows();
    apply_skybox_tint();
}

} // namespace xim::features::world_mod