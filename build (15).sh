#pragma once

#include <atomic>
#include <cstdint>

namespace xim::features::world_mod {

struct SkyboxCfg {
    bool  enabled    = false;
    int   mode       = 1;         // 0=solid, 1=gradient, 2=cubemap, 3=custom
    float top[4]     = {0.10f, 0.20f, 0.40f, 1.0f};
    float horiz[4]   = {0.30f, 0.40f, 0.60f, 1.0f};
    float bot[4]     = {0.02f, 0.05f, 0.10f, 1.0f};
    float rotation   = 0.0f;
    float brightness = 1.0f;
};

struct FogCfg {
    bool  enabled  = false;
    int   mode     = 1;           // 0=linear, 1=exp, 2=exp2
    float density  = 0.2f;
    float start    = 20.0f;
    float end      = 200.0f;
    float color[4] = {0.5f, 0.5f, 0.5f, 1.0f};
};

struct AmbientCfg {
    bool  enabled   = false;
    float color[4]  = {1.0f, 1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
};

struct CullingCfg {
    bool no_shadows    = false;
    bool no_grass      = false;
    bool no_particles  = false;
    bool no_smoke      = false;
    bool no_flash      = false;
    bool no_hands      = false;
    bool fullbright    = false;
    bool wireframe     = false;
};

struct WorldCfg {
    SkyboxCfg  skybox;
    FogCfg     fog;
    AmbientCfg ambient;
    CullingCfg culling;
};

WorldCfg& config();

// резолв RenderSettings. Возвращает true если класс и статические поля найдены.
bool init();

// применяет настройки в память игры (fog / ambient / shadowDistance / skybox-tint).
// пишет только когда значение изменилось — в стеди-стейте ноль записей.
void update();

} // namespace xim::features::world_mod