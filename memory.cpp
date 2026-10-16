#pragma once
// post_fx.hpp — Unity PostProcessing v2 control (local-only)
#include "game/offsets.hpp"
#include "features/visuals/esp_runtime.hpp"
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <android/log.h>
#include <vector>

namespace ximira::postfx {

namespace ed = lemming::esp::detail;

namespace rva {
    constexpr std::uintptr_t kFindObjectsOfTypeAll  = 0x67E81A0;
    constexpr std::uintptr_t kClassGetType          = 0x6327B48;
}

namespace off {
    constexpr std::size_t kVolumeSharedProfile = 0x20;
    constexpr std::size_t kProfileSettings     = 0x18;
    constexpr std::size_t kListItems           = 0x10;
    constexpr std::size_t kListSize            = 0x18;
    constexpr std::size_t kOverrideState       = 0x10;
    constexpr std::size_t kValueFloat          = 0x14;
    constexpr std::size_t kValueColor          = 0x14;
}

struct PostFxState {
    bool   enabled = false;
    bool   bloomEnabled = false;
    float  bloomIntensity = 1.0f;
    bool   bloomApplyThreshold = false;
    float  bloomThreshold = 0.9f;
    bool   mbEnabled = false;
    float  mbShutterAngle = 90.0f;
    bool   dofEnabled = false;
    float  dofFocusDistance = 10.0f;
    float  dofAperture = 5.6f;
    float  dofFocalLength = 50.0f;
    bool   vigEnabled = false;
    float  vigIntensity = 0.45f;
    float  vigSmoothness = 0.2f;
    bool   caEnabled = false;
    float  caIntensity = 0.1f;
    bool   cgEnabled = false;
    float  cgSaturation = 0.0f;
    float  cgContrast = 0.0f;
    float  cgBrightness = 0.0f;
    float  cgHueShift = 0.0f;
};
inline PostFxState g_state;

inline std::uintptr_t g_unityBase = 0;
inline void*          g_ppv = nullptr;
inline void*          g_profile = nullptr;
inline bool           g_ready = false;
inline uint64_t       g_frameCounter = 0;

template<typename T>
inline bool rd(std::uintptr_t addr, T& out) {
    return ed::readValue(reinterpret_cast<void*>(addr), out);
}
inline void* rdObj(std::uintptr_t addr) {
    return ed::readObject(reinterpret_cast<void*>(addr));
}
inline const char* className(void* obj) {
    if (!obj) return nullptr;
    void* klass = ed::readObject(obj);
    if (!klass) return nullptr;
    auto get = ed::g_runtime.api.classGetName;
    return get ? get(klass) : nullptr;
}
inline bool nameIs(void* obj, const char* n) {
    const char* c = className(obj);
    return c && n && std::strcmp(c, n) == 0;
}

inline void* findClassFromUnityAssembly(const char* ns, const char* name) {
    auto& api = ed::g_runtime.api;
    if (!api.domainAssemblyOpen || !api.assemblyGetImage || !api.classFromName) return nullptr;
    if (!ed::g_runtime.domain) return nullptr;
    const char* assemblies[] = {
        "Unity.Postprocessing.Runtime.dll",
        "Unity.Postprocessing.Runtime",
        "UnityEngine.Rendering.PostProcessing.dll",
        "UnityEngine.Rendering.PostProcessing",
        "Assembly-CSharp.dll",
        "Assembly-CSharp",
        "UnityEngine.dll",
        "UnityEngine"
    };
    for (const char* asmName : assemblies) {
        void* asm_ = api.domainAssemblyOpen(ed::g_runtime.domain, asmName);
        if (!asm_) continue;
        void* img = api.assemblyGetImage(asm_);
        if (!img) continue;
        if (void* k = api.classFromName(img, ns, name)) return k;
    }
    return nullptr;
}

inline void* findPPV() {
    if (g_ppv && nameIs(g_ppv, "PostProcessVolume")) return g_ppv;
    void* klass = findClassFromUnityAssembly("UnityEngine.Rendering.PostProcessing", "PostProcessVolume");
    if (!klass) klass = findClassFromUnityAssembly("", "PostProcessVolume");
    if (!klass) {
        static int logged = 0;
        if (logged++ < 3)
            __android_log_print(ANDROID_LOG_WARN, "skeet", "PostFX: PPV class not found");
        return nullptr;
    }
    using ClassGetTypeFn = void* (*)(void*);
    auto classGetType = reinterpret_cast<ClassGetTypeFn>(g_unityBase + rva::kClassGetType);
    void* typeObj = classGetType ? classGetType(klass) : nullptr;
    if (!typeObj) return nullptr;
    using FindAllFn = void* (*)(void*);
    auto findAll = reinterpret_cast<FindAllFn>(g_unityBase + rva::kFindObjectsOfTypeAll);
    if (!findAll) return nullptr;
    void* arr = findAll(typeObj);
    if (!arr) return nullptr;
    std::uintptr_t arrBase = reinterpret_cast<std::uintptr_t>(arr);
    std::uintptr_t length = 0;
    if (!rd(arrBase + 0x18, length)) return nullptr;
    if (length == 0 || length > 64) return nullptr;
    __android_log_print(ANDROID_LOG_INFO, "skeet",
        "PostFX: FindObjectsOfTypeAll len=%llu", (unsigned long long)length);
    for (std::uintptr_t i = 0; i < length && i < 8; ++i) {
        void* obj = rdObj(arrBase + 0x20 + i * 8);
        if (!obj) continue;
        if (nameIs(obj, "PostProcessVolume")) {
            g_ppv = obj;
            std::uintptr_t base = reinterpret_cast<std::uintptr_t>(obj);
            g_profile = rdObj(base + off::kVolumeSharedProfile);
            __android_log_print(ANDROID_LOG_INFO, "skeet",
                "PostFX: PPV=%p profile=%p class=%s",
                obj, g_profile, className(g_profile) ? className(g_profile) : "?");
            if (g_profile) { g_ready = true; return obj; }
        }
    }
    return nullptr;
}

inline void* findEffect(const char* effectClassName) {
    if (!g_profile) return nullptr;
    std::uintptr_t profBase = reinterpret_cast<std::uintptr_t>(g_profile);
    void* list = rdObj(profBase + off::kProfileSettings);
    if (!list) return nullptr;
    std::uintptr_t listBase = reinterpret_cast<std::uintptr_t>(list);
    std::int32_t size = 0;
    if (!rd(listBase + off::kListSize, size) || size <= 0 || size > 64) return nullptr;
    void* items = rdObj(listBase + off::kListItems);
    if (!items) return nullptr;
    std::uintptr_t itemsBase = reinterpret_cast<std::uintptr_t>(items);
    for (std::int32_t i = 0; i < size; ++i) {
        void* effect = rdObj(itemsBase + 0x20 + i * 8);
        if (!effect) continue;
        if (nameIs(effect, effectClassName)) return effect;
    }
    return nullptr;
}

inline bool setFloatParam(void* owner, std::size_t fieldOffset, float value) {
    if (!owner) return false;
    std::uintptr_t base = reinterpret_cast<std::uintptr_t>(owner);
    void* param = rdObj(base + fieldOffset);
    if (!param) return false;
    std::uintptr_t pBase = reinterpret_cast<std::uintptr_t>(param);
    uint8_t ovr = 1;
    ed::writeValue(reinterpret_cast<void*>(pBase + off::kOverrideState), ovr, 1);
    ed::writeValue(reinterpret_cast<void*>(pBase + off::kValueFloat), value, sizeof(float));
    return true;
}

inline void apply() {
    g_frameCounter++;
    if (!g_unityBase) return;
    if (!g_ready) {
        if (g_frameCounter % 60 == 0) findPPV();
        if (!g_ready) return;
    }
    if (!nameIs(g_ppv, "PostProcessVolume")) {
        g_ready = false; g_ppv = nullptr; g_profile = nullptr;
        return;
    }
    if (!g_profile) {
        std::uintptr_t base = reinterpret_cast<std::uintptr_t>(g_ppv);
        g_profile = rdObj(base + off::kVolumeSharedProfile);
        if (!g_profile) return;
    }
    if (!g_state.enabled) return;
    if (void* bloom = findEffect("Bloom")) {
        if (g_state.bloomEnabled) {
            setFloatParam(bloom, 0x30, g_state.bloomIntensity);
            if (g_state.bloomApplyThreshold)
                setFloatParam(bloom, 0x38, g_state.bloomThreshold);
        }
    }
    if (void* mb = findEffect("MotionBlur")) {
        if (g_state.mbEnabled) setFloatParam(mb, 0x30, g_state.mbShutterAngle);
    }
    if (void* dof = findEffect("DepthOfField")) {
        if (g_state.dofEnabled) {
            setFloatParam(dof, 0x30, g_state.dofFocusDistance);
            setFloatParam(dof, 0x38, g_state.dofAperture);
            setFloatParam(dof, 0x40, g_state.dofFocalLength);
        }
    }
    if (void* vig = findEffect("Vignette")) {
        if (g_state.vigEnabled) {
            setFloatParam(vig, 0x48, g_state.vigIntensity);
            setFloatParam(vig, 0x50, g_state.vigSmoothness);
        }
    }
    if (void* ca = findEffect("ChromaticAberration")) {
        if (g_state.caEnabled) setFloatParam(ca, 0x38, g_state.caIntensity);
    }
    if (void* cg = findEffect("ColorGrading")) {
        if (g_state.cgEnabled) {
            setFloatParam(cg, 0xA8, g_state.cgSaturation);
            setFloatParam(cg, 0xC0, g_state.cgContrast);
            setFloatParam(cg, 0xB0, g_state.cgBrightness);
            setFloatParam(cg, 0xA0, g_state.cgHueShift);
        }
    }
}

inline void setBase(std::uintptr_t base) { g_unityBase = base; }
inline bool ready() { return g_ready; }

inline void testDump() {
    if (!g_unityBase) return;
    static bool dumped = false;
    if (dumped) return;
    if (!findPPV()) return;
    dumped = true;
    __android_log_print(ANDROID_LOG_INFO, "skeet", "PostFX TEST: PPV=%p profile=%p", g_ppv, g_profile);
}

} // namespace ximira::postfx
