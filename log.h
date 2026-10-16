#pragma once

#include "game/offsets.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <cerrno>
#include <fcntl.h>
#include <mutex>
#include <pthread.h>
#include <string>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <vector>

#include <android/log.h>

namespace lemming::esp {

inline constexpr const char* kLogTag = "LemmingRMT";

struct ScreenBox {
    float left;
    float top;
    float right;
    float bottom;
    bool enemy;
    bool statsValid;
    bool weaponValid;
    std::uint8_t team;
    std::uint8_t weaponId;
    std::uint8_t magazineAmmo;
    std::uint16_t reserveAmmo;
    std::uint16_t hp;
    std::uint16_t maxHp;
    std::uint16_t armor;
    std::uint16_t maxArmor;
};

struct Vec3 {
    float x;
    float y;
    float z;
};

struct Vec4 {
    float x;
    float y;
    float z;
    float w;
};

struct Matrix4x4 {
    // Unity layout: m00,m10,m20,m30,m01,... (column-major storage).
    float value[16];
};

struct CameraMatrixCache {
    Matrix4x4 worldToCamera;
    Matrix4x4 projection;
};

static_assert(sizeof(Matrix4x4) == 0x40, "Unity Matrix4x4 size mismatch");
static_assert(sizeof(CameraMatrixCache) == 0x80, "Camera matrix cache size mismatch");

struct Quaternion {
    float x;
    float y;
    float z;
    float w;
};

enum class Status : int {
    WaitingForUnity = 0,
    UnsupportedArchitecture,
    ApiUnavailable,
    TypeInfoUnavailable,
    IcallUnavailable,
    ScanningPlayers,
    WaitingForMatch,
    Active
};

inline std::atomic<int> g_status{static_cast<int>(Status::WaitingForUnity)};
inline std::atomic<int> g_playerCount{0};
inline std::atomic<std::uint64_t> g_initAttempts{0};
inline std::atomic<std::uint64_t> g_scanCycles{0};
inline std::atomic<std::uint64_t> g_scannedBytes{0};
inline std::atomic<int> g_candidateCount{0};
inline std::atomic<int> g_netObjectCount{0};
inline std::atomic<std::uint64_t> g_registryReadFailures{0};
inline std::atomic<bool> g_playerUpdateHookReady{false};
inline std::atomic<std::uint64_t> g_playerUpdateHookCalls{0};
inline std::atomic<int> g_hookedPlayerCount{0};
inline std::atomic<int> g_projectedCount{0};
inline std::atomic<int> g_localTeam{0};
inline std::atomic<int> g_statsReadCount{0};
inline std::atomic<int> g_weaponReadCount{0};
inline std::atomic<bool> g_cameraSnapshotReady{false};
inline std::atomic<std::uint64_t> g_cameraSnapshotCaptures{0};
inline std::atomic<std::uint64_t> g_cameraSnapshotFailures{0};
inline std::atomic<pid_t> g_cameraSnapshotTid{0};

inline const char* statusTextFor(Status value) {
    // The bundled ImGui font is ASCII-only. Keep runtime labels ASCII so the
    // menu never renders Cyrillic as question marks.
    switch (value) {
        case Status::WaitingForUnity: return "Waiting for libunity";
        case Status::UnsupportedArchitecture: return "ESP supports ARM64 only";
        case Status::ApiUnavailable: return "IL2CPP API unavailable";
        case Status::TypeInfoUnavailable: return "NetObject TypeInfo not ready";
        case Status::IcallUnavailable: return "Unity native API unavailable";
        case Status::ScanningPlayers: return "Scanning ClientWorld / NetObjects";
        case Status::WaitingForMatch: return "Players found / waiting for visible enemies";
        case Status::Active: return "Enemy ESP active";
    }
    return "ESP state unknown";
}

inline const char* statusText() {
    return statusTextFor(static_cast<Status>(g_status.load(std::memory_order_relaxed)));
}

inline void setStatus(Status value, const char* detail = nullptr) {
    const int next = static_cast<int>(value);
    const int previous = g_status.exchange(next, std::memory_order_relaxed);
    if (previous != next) {
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "ESP status: %s%s%s",
                            statusTextFor(value), detail ? " | " : "", detail ? detail : "");
    }
}

inline int playerCount() {
    return g_playerCount.load(std::memory_order_relaxed);
}

inline int projectedCount() {
    return g_projectedCount.load(std::memory_order_relaxed);
}

inline int netObjectCount() {
    return g_netObjectCount.load(std::memory_order_relaxed);
}

inline int playerObjectCount() {
    return g_candidateCount.load(std::memory_order_relaxed);
}

inline int hookedPlayerCount() {
    return g_hookedPlayerCount.load(std::memory_order_relaxed);
}

inline bool playerUpdateHookReady() {
    return g_playerUpdateHookReady.load(std::memory_order_relaxed);
}

namespace detail {

constexpr std::size_t kPtr = sizeof(void*);

struct MapRange {
    std::uintptr_t begin;
    std::uintptr_t end;
    bool readable;
    bool writable;
    bool scanEligible;
};

inline std::vector<MapRange> g_maps;
inline std::chrono::steady_clock::time_point g_lastMapRefresh{};
inline std::uint64_t g_mapGeneration = 0;

inline void refreshMaps(bool force = false) {
    const auto now = std::chrono::steady_clock::now();
    if (!force && !g_maps.empty() && now - g_lastMapRefresh < std::chrono::seconds(10)) return;

    std::vector<MapRange> next;
    if (FILE* file = std::fopen("/proc/self/maps", "r")) {
        char line[768];
        while (std::fgets(line, sizeof(line), file)) {
            unsigned long long begin = 0;
            unsigned long long end = 0;
            char perms[5]{};
            if (std::sscanf(line, "%llx-%llx %4s", &begin, &end, perms) != 3 || begin >= end) continue;
            const bool readable = perms[0] == 'r';
            const bool writable = perms[1] == 'w';
            const bool isPrivate = perms[3] == 'p';
            const bool fileBacked = std::strchr(line, '/') != nullptr;
            const bool stack = std::strstr(line, "[stack") != nullptr;
            const bool device = std::strstr(line, "/dev/") != nullptr;
            const auto size = static_cast<std::uint64_t>(end - begin);
            const bool eligible = readable && writable && isPrivate && !fileBacked && !stack && !device &&
                                  size >= 0x1000 && size <= (512ull * 1024ull * 1024ull);
            next.push_back({static_cast<std::uintptr_t>(begin), static_cast<std::uintptr_t>(end),
                            readable, writable, eligible});
        }
        std::fclose(file);
    }
    if (!next.empty()) {
        bool changed = next.size() != g_maps.size();
        if (!changed) {
            for (std::size_t i = 0; i < next.size(); ++i) {
                const MapRange& a = next[i];
                const MapRange& b = g_maps[i];
                if (a.begin != b.begin || a.end != b.end || a.readable != b.readable ||
                    a.writable != b.writable || a.scanEligible != b.scanEligible) {
                    changed = true;
                    break;
                }
            }
        }
        if (changed) {
            g_maps.swap(next);
            ++g_mapGeneration;
        }
    }
    g_lastMapRefresh = now;
}

inline bool plausibleAddress(const void* pointer, std::size_t bytes = 1) {
    if (!pointer || bytes == 0) return false;
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    return address >= 0x10000 && address + bytes >= address;
}

inline bool readable(const void* pointer, std::size_t bytes = 1) {
    if (!plausibleAddress(pointer, bytes)) return false;
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    for (const auto& range : g_maps) {
        if (range.readable && address >= range.begin && address + bytes <= range.end) return true;
    }
    return false;
}

inline bool writable(const void* pointer, std::size_t bytes = 1) {
    if (!plausibleAddress(pointer, bytes)) return false;
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    for (const auto& range : g_maps) {
        if (range.writable && address >= range.begin && address + bytes <= range.end) return true;
    }
    return false;
}

// Direct loads can fault when Unity unmaps a range between /proc/self/maps refreshes.
// Read through the kernel so stale snapshot entries return EFAULT instead of SIGSEGV.
inline std::atomic<int> g_memoryReadBackend{0};  // 0=unknown, 1=process_vm_readv, 2=/proc/self/mem, -1=unavailable
inline std::atomic<int> g_selfMemFd{-1};
inline std::mutex g_memoryReaderMutex;

inline const char* memoryReadBackendName() {
    switch (g_memoryReadBackend) {
        case 1: return "process_vm_readv";
        case 2: return "proc_self_mem";
        case -1: return "unavailable";
        default: return "unknown";
    }
}

inline ssize_t safeReadMemory(void* destination, const void* source, std::size_t bytes) {
    if (!destination || !plausibleAddress(source, bytes)) return -1;

#if defined(__NR_process_vm_readv)
    if (g_memoryReadBackend == 0 || g_memoryReadBackend == 1) {
        iovec local{destination, bytes};
        iovec remote{const_cast<void*>(source), bytes};
        errno = 0;
        const auto result = static_cast<ssize_t>(
            ::syscall(__NR_process_vm_readv, ::getpid(), &local, 1, &remote, 1, 0));
        if (result >= 0) {
            if (g_memoryReadBackend == 0) {
                g_memoryReadBackend = 1;
                __android_log_print(ANDROID_LOG_INFO, kLogTag,
                                    "ESP safe reader ready: backend=%s", memoryReadBackendName());
            }
            return result;
        }
        if (errno != ENOSYS && errno != EPERM && errno != EACCES) return result;
        g_memoryReadBackend = 2;
    }
#else
    if (g_memoryReadBackend == 0) g_memoryReadBackend = 2;
#endif

#if defined(__LP64__)
    if (g_memoryReadBackend == 2) {
        std::lock_guard<std::mutex> lock(g_memoryReaderMutex);
        if (g_selfMemFd < 0) {
            g_selfMemFd = ::open("/proc/self/mem", O_RDONLY | O_CLOEXEC);
            if (g_selfMemFd >= 0) {
                __android_log_print(ANDROID_LOG_INFO, kLogTag,
                                    "ESP safe reader ready: backend=%s", memoryReadBackendName());
            }
        }
        if (g_selfMemFd >= 0) {
            return ::pread(g_selfMemFd, destination, bytes,
                           static_cast<off_t>(reinterpret_cast<std::uintptr_t>(source)));
        }
    }
#endif

    g_memoryReadBackend = -1;
    return -1;
}

template <typename T>
inline bool readValue(const void* address, T& value) {
    T copy{};
    if (safeReadMemory(&copy, address, sizeof(T)) != static_cast<ssize_t>(sizeof(T))) return false;
    value = copy;
    return true;
}

inline void* readObject(const void* address) {
    void* value = nullptr;
    if (!readValue(address, value) || !plausibleAddress(value, kPtr)) return nullptr;
    return value;
}

struct Api {
    using DomainGet = void* (*)();
    using DomainAssemblyOpen = void* (*)(void*, const char*);
    using AssemblyGetImage = void* (*)(void*);
    using ClassFromName = void* (*)(void*, const char*, const char*);
    using ClassGetFields = void* (*)(void*, void**);
    using ClassGetName = const char* (*)(void*);
    using ClassGetParent = void* (*)(void*);
    using FieldGetOffset = std::size_t (*)(void*);
    using FieldGetType = void* (*)(void*);
    using TypeGetName = char* (*)(void*);
    using Free = void (*)(void*);
    using ResolveIcall = void* (*)(const char*);
    using ThreadAttach = void* (*)(void*);

    DomainGet domainGet{};
    DomainAssemblyOpen domainAssemblyOpen{};
    AssemblyGetImage assemblyGetImage{};
    ClassFromName classFromName{};
    ClassGetFields classGetFields{};
    ClassGetName classGetName{};
    ClassGetParent classGetParent{};
    FieldGetOffset fieldGetOffset{};
    FieldGetType fieldGetType{};
    TypeGetName typeGetName{};
    Free freeFn{};
    ResolveIcall resolveIcall{};
    ThreadAttach threadAttach{};
};

struct Runtime {
    std::uintptr_t unityBase{};
    bool ready{};
    Api api{};
    void* domain{};
    void* clientWorldClass{};
    void* registryClass{};
    void* storeClass{};
    void* netObjectClass{};
    void* playerClass{};
    void* transformsClass{};
    void* modelClass{};
    void* statsClass{};
    void* weaponPartClass{};
    void* weaponInstanceClass{};

    using CameraMainFn = std::uintptr_t (*)();
    using CameraMatrixFn = void (*)(std::uintptr_t, Matrix4x4*);
    using WorldToScreenFn = void (*)(std::uintptr_t, const Vec3*, int, Vec3*);
    using TransformPositionFn = void (*)(std::uintptr_t, Vec3*);
    using TransformPositionRotationFn = void (*)(std::uintptr_t, Vec3*, Quaternion*);
    using ComponentTransformFn = std::uintptr_t (*)(std::uintptr_t);
    using PlayerPositionFn = Vec3 (*)(void*, const void*);

    CameraMainFn cameraMain{};  // native Camera*, never a GC handle
    CameraMatrixFn worldToCameraMatrix{};
    CameraMatrixFn projectionMatrix{};
    WorldToScreenFn worldToScreen{};  // resolved for diagnostics only; never called from ESP.
    TransformPositionFn transformPosition{};
    TransformPositionRotationFn transformPositionRotation{};
    ComponentTransformFn componentTransform{};
    PlayerPositionFn playerPosition{};
};

inline Runtime g_runtime;
inline std::chrono::steady_clock::time_point g_lastRetry{};
inline std::chrono::steady_clock::time_point g_lastFrame{};
inline std::chrono::steady_clock::time_point g_lastFrameLog{};

template <typename T>
inline T resolveSymbol(void* unity, const char* name) {
    if (unity) {
        if (void* value = dlsym(unity, name)) return reinterpret_cast<T>(value);
    }
    if (void* value = dlsym(RTLD_DEFAULT, name)) return reinterpret_cast<T>(value);
    return nullptr;
}

template <typename T>
inline T resolveApi(void* unity, const char* name, std::uintptr_t base, std::uintptr_t rva) {
    if (T symbol = resolveSymbol<T>(unity, name)) return symbol;
#if defined(__aarch64__)
    if (base && rva >= offsets::elf::kTextBegin && rva < offsets::elf::kTextEnd) {
        return reinterpret_cast<T>(base + rva);
    }
#else
    (void)base;
    (void)rva;
#endif
    return nullptr;
}

template <typename T>
inline T resolveIcallAny(std::initializer_list<const char*> names) {
    if (!g_runtime.api.resolveIcall) return nullptr;
    for (const char* name : names) {
        if (void* result = g_runtime.api.resolveIcall(name)) return reinterpret_cast<T>(result);
    }
    return nullptr;
}

inline void* objectClass(void* object) {
    return object ? readObject(object) : nullptr;
}

inline const char* className(void* klass) {
    if (!klass || !g_runtime.api.classGetName || !readable(klass, 0x20)) return nullptr;
    return g_runtime.api.classGetName(klass);
}

inline bool nameEquals(const char* value, const char* expected) {
    return value && expected && std::strcmp(value, expected) == 0;
}

inline bool nameContains(const char* value, const char* token) {
    return value && token && std::strstr(value, token) != nullptr;
}

inline void* findClass(const std::vector<const char*>& assemblies, const char* namespaze, const char* name) {
    for (const char* assemblyName : assemblies) {
        void* assembly = g_runtime.api.domainAssemblyOpen(g_runtime.domain, assemblyName);
        if (!assembly) continue;
        void* image = g_runtime.api.assemblyGetImage(assembly);
        if (!image) continue;
        if (void* klass = g_runtime.api.classFromName(image, namespaze, name)) return klass;
    }
    return nullptr;
}

inline void* classFromSlot(std::uintptr_t rva, const char* expectedName) {
#if !defined(__aarch64__)
    (void)rva;
    (void)expectedName;
    return nullptr;
#else
    if (!rva || !g_runtime.unityBase) return nullptr;
    void* klass = readObject(reinterpret_cast<void*>(g_runtime.unityBase + rva));
    if (!klass || !readObject(klass)) return nullptr;
    const char* name = className(klass);
    if (expectedName && !nameEquals(name, expectedName)) return nullptr;
    return klass;
#endif
}

inline bool readPosition(std::uintptr_t transform, Vec3& position) {
    position = {};
    if (!transform) return false;
    if (g_runtime.transformPosition) {
        g_runtime.transformPosition(transform, &position);
    } else if (g_runtime.transformPositionRotation) {
        Quaternion rotation{};
        g_runtime.transformPositionRotation(transform, &position, &rotation);
    } else {
        return false;
    }
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z) &&
           std::fabs(position.x) < 100000.0f && std::fabs(position.y) < 100000.0f &&
           std::fabs(position.z) < 100000.0f;
}

inline bool initialize(std::uintptr_t unityBase) {
    ++g_initAttempts;
    refreshMaps(true);
    g_runtime = Runtime{};
    g_runtime.unityBase = unityBase;

#if !defined(__aarch64__)
    setStatus(Status::UnsupportedArchitecture);
    return false;
#else
    if (!unityBase) {
        setStatus(Status::WaitingForUnity);
        return false;
    }

    void* unity = dlopen("libunity.so", RTLD_NOW | RTLD_LOCAL);
    auto& api = g_runtime.api;
    using namespace offsets::il2cpp_api;
    api.domainGet = resolveApi<Api::DomainGet>(unity, "il2cpp_domain_get", unityBase, kDomainGet);
    api.domainAssemblyOpen = resolveApi<Api::DomainAssemblyOpen>(unity, "il2cpp_domain_assembly_open", unityBase, kDomainAssemblyOpen);
    api.assemblyGetImage = resolveApi<Api::AssemblyGetImage>(unity, "il2cpp_assembly_get_image", unityBase, kAssemblyGetImage);
    api.classFromName = resolveApi<Api::ClassFromName>(unity, "il2cpp_class_from_name", unityBase, kClassFromName);
    api.classGetFields = resolveApi<Api::ClassGetFields>(unity, "il2cpp_class_get_fields", unityBase, kClassGetFields);
    api.classGetName = resolveApi<Api::ClassGetName>(unity, "il2cpp_class_get_name", unityBase, kClassGetName);
    api.classGetParent = resolveApi<Api::ClassGetParent>(unity, "il2cpp_class_get_parent", unityBase, kClassGetParent);
    api.fieldGetOffset = resolveApi<Api::FieldGetOffset>(unity, "il2cpp_field_get_offset", unityBase, kFieldGetOffset);
    api.fieldGetType = resolveApi<Api::FieldGetType>(unity, "il2cpp_field_get_type", unityBase, kFieldGetType);
    api.typeGetName = resolveApi<Api::TypeGetName>(unity, "il2cpp_type_get_name", unityBase, kTypeGetName);
    api.freeFn = resolveApi<Api::Free>(unity, "il2cpp_free", unityBase, kFree);
    // The stripped 1.0.0 binary's candidate at kResolveIcall enters a type-name
    // formatter for these strings and crashes in strlen. Keep it disabled and
    // use the independently disassembled native camera/transform entry points.
    api.resolveIcall = nullptr;
    api.threadAttach = resolveApi<Api::ThreadAttach>(unity, "il2cpp_thread_attach", unityBase, kThreadAttach);

    if (!api.domainGet || !api.domainAssemblyOpen || !api.assemblyGetImage || !api.classFromName ||
        !api.classGetFields || !api.classGetName || !api.classGetParent || !api.fieldGetOffset ||
        !api.fieldGetType || !api.typeGetName || !api.threadAttach) {
        setStatus(Status::ApiUnavailable, "required functions missing");
        return false;
    }

    g_runtime.domain = api.domainGet();
    if (!g_runtime.domain) {
        setStatus(Status::ApiUnavailable, "domain is null");
        return false;
    }
    void* attachedThread = api.threadAttach(g_runtime.domain);
    if (!attachedThread) {
        setStatus(Status::ApiUnavailable, "thread attach returned null");
        return false;
    }
    __android_log_print(ANDROID_LOG_INFO, kLogTag,
                        "ESP IL2CPP thread attached: domain=%p thread=%p attachRva=0x%lx",
                        g_runtime.domain, attachedThread,
                        static_cast<unsigned long>(offsets::il2cpp_api::kThreadAttach));

    g_runtime.clientWorldClass = classFromSlot(offsets::typeinfo::kClientWorld, "njh");
    g_runtime.registryClass = classFromSlot(offsets::typeinfo::kRegistry, "gat");
    g_runtime.storeClass = classFromSlot(offsets::typeinfo::kStore, "gzx");
    g_runtime.netObjectClass = classFromSlot(offsets::typeinfo::kNetObject, "mfc");
    g_runtime.playerClass = classFromSlot(offsets::typeinfo::kPlayer, "ake");
    g_runtime.transformsClass = classFromSlot(offsets::typeinfo::kTransforms, "dwc");
    g_runtime.modelClass = classFromSlot(offsets::typeinfo::kModel, "jgi");
    g_runtime.statsClass = classFromSlot(offsets::typeinfo::kStats, "ylw");
    g_runtime.weaponPartClass = classFromSlot(offsets::typeinfo::kWeaponPart, "nrk");
    g_runtime.weaponInstanceClass = classFromSlot(offsets::typeinfo::kWeaponInstance, "qjf");

    const std::vector<const char*> images = {
        "Axlebolt.NetCode.Framework.dll", "Axlebolt.NetCode.Framework",
        "Axlebolt.Standoff.Game.dll", "Axlebolt.Standoff.Game",
        "Axlebolt.Standoff.Gameplay.dll", "Axlebolt.Standoff.Gameplay",
        "Axlebolt.Standoff.Gameplay.Player.dll", "Axlebolt.Standoff.Gameplay.Player",
        "Axlebolt.Standoff.Gameplay.Features.dll", "Axlebolt.Standoff.Gameplay.Features",
        "Assembly-CSharp.dll", "Assembly-CSharp"
    };
    const auto fallbackClass = [&](void*& target, const char* name) {
        if (!target) target = findClass(images, "", name);
    };
    fallbackClass(g_runtime.clientWorldClass, "njh");
    fallbackClass(g_runtime.registryClass, "gat");
    fallbackClass(g_runtime.storeClass, "gzx");
    fallbackClass(g_runtime.netObjectClass, "mfc");
    fallbackClass(g_runtime.playerClass, "ake");
    fallbackClass(g_runtime.transformsClass, "dwc");
    fallbackClass(g_runtime.modelClass, "jgi");
    fallbackClass(g_runtime.statsClass, "ylw");
    fallbackClass(g_runtime.weaponPartClass, "nrk");
    fallbackClass(g_runtime.weaponInstanceClass, "qjf");

    if (!g_runtime.registryClass || !g_runtime.storeClass || !g_runtime.playerClass) {
        setStatus(Status::TypeInfoUnavailable, "gat/gzx/ake class not initialized");
        return false;
    }

    g_runtime.cameraMain = reinterpret_cast<Runtime::CameraMainFn>(
        unityBase + offsets::unity::kNativeCameraGetMain);
    // Copy the game's already-updated caches. Do not trigger native Transform
    // recalculation through either matrix getter, even on UnityMain.
    g_runtime.worldToCameraMatrix = nullptr;
    g_runtime.projectionMatrix = nullptr;
    g_runtime.worldToScreen = reinterpret_cast<Runtime::WorldToScreenFn>(
        unityBase + offsets::unity::kCameraWorldToScreen);
    g_runtime.componentTransform = reinterpret_cast<Runtime::ComponentTransformFn>(
        unityBase + offsets::unity::kComponentGetTransform);
    g_runtime.transformPositionRotation = reinterpret_cast<Runtime::TransformPositionRotationFn>(
        unityBase + offsets::unity::kTransformGetPositionAndRotation);
    g_runtime.playerPosition = reinterpret_cast<Runtime::PlayerPositionFn>(
        unityBase + offsets::managed_rva::kPlayerGetPosition);
    __android_log_print(ANDROID_LOG_INFO, kLogTag,
                        "ESP direct helpers ready: nativeCameraMain=0x%lx viewMatrix(disabled)=0x%lx projectionMatrix(disabled)=0x%lx worldToScreen(disabled)=0x%lx componentTransform=0x%lx transformPosRot=0x%lx playerPosition=0x%lx",
                        static_cast<unsigned long>(offsets::unity::kNativeCameraGetMain),
                        static_cast<unsigned long>(offsets::unity::kCameraWorldToCameraMatrix),
                        static_cast<unsigned long>(offsets::unity::kCameraProjectionMatrix),
                        static_cast<unsigned long>(offsets::unity::kCameraWorldToScreen),
                        static_cast<unsigned long>(offsets::unity::kComponentGetTransform),
                        static_cast<unsigned long>(offsets::unity::kTransformGetPositionAndRotation),
                        static_cast<unsigned long>(offsets::managed_rva::kPlayerGetPosition));

    if (!g_runtime.cameraMain ||
        !g_runtime.componentTransform || !g_runtime.playerPosition ||
        (!g_runtime.transformPosition && !g_runtime.transformPositionRotation)) {
        setStatus(Status::IcallUnavailable, "camera/transform helper missing");
        return false;
    }

    g_runtime.ready = true;
    setStatus(Status::ScanningPlayers);
    __android_log_print(ANDROID_LOG_INFO, kLogTag,
                        "ESP resolver ready: profile=%s base=%p clientWorld=%p registry=%p store=%p netObject=%p player=%p transforms=%p model=%p stats=%p weaponPart=%p weapon=%p",
                        offsets::kProfileName, reinterpret_cast<void*>(unityBase),
                        g_runtime.clientWorldClass, g_runtime.registryClass, g_runtime.storeClass,
                        g_runtime.netObjectClass, g_runtime.playerClass, g_runtime.transformsClass,
                        g_runtime.modelClass, g_runtime.statsClass, g_runtime.weaponPartClass,
                        g_runtime.weaponInstanceClass);
    return true;
#endif
}

inline bool classMatches(void* object, void* expected) {
    if (!object || !expected) return false;
    void* klass = objectClass(object);
    for (std::size_t depth = 0; klass && depth < 12; ++depth) {
        if (klass == expected) return true;
        klass = g_runtime.api.classGetParent(klass);
    }
    return false;
}

inline std::string fieldTypeName(void* field) {
    if (!field) return {};
    void* type = g_runtime.api.fieldGetType(field);
    if (!type) return {};
    char* raw = g_runtime.api.typeGetName(type);
    if (!raw) return {};
    std::string result(raw);
    if (g_runtime.api.freeFn) g_runtime.api.freeFn(raw);
    return result;
}

inline bool followReferenceType(const std::string& typeName) {
    static constexpr std::array<const char*, 16> tokens = {
        "dwc", "jgi", "jsx", "pdd", "ake", "Player", "Character", "Binding",
        "View", "Visual", "Controller", "Presenter", "Transform", "Model", "Rig", "Avatar"
    };
    for (const char* token : tokens) {
        if (typeName.find(token) != std::string::npos) return true;
    }
    return false;
}

inline bool unityComponent(void* object) {
    void* klass = objectClass(object);
    for (std::size_t depth = 0; klass && depth < 16; ++depth) {
        const char* name = className(klass);
        if (nameEquals(name, "Component") || nameEquals(name, "Behaviour") ||
            nameEquals(name, "MonoBehaviour") || nameEquals(name, "Transform")) return true;
        klass = g_runtime.api.classGetParent(klass);
    }
    return false;
}

inline std::uintptr_t transformFromManaged(void* object, int depth,
                                           std::array<void*, 12>& visited,
                                           std::size_t& visitedCount) {
    if (!object || depth > 4 || visitedCount >= visited.size()) return 0;
    if (std::find(visited.begin(), visited.begin() + static_cast<std::ptrdiff_t>(visitedCount), object) !=
        visited.begin() + static_cast<std::ptrdiff_t>(visitedCount)) return 0;
    visited[visitedCount++] = object;

    void* klass = objectClass(object);
    const char* ownName = className(klass);
    if (nameEquals(ownName, "Transform")) {
        void* native = readObject(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(object) + 2 * kPtr));
        return reinterpret_cast<std::uintptr_t>(native);
    }
    if (unityComponent(object)) {
        void* native = readObject(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(object) + 2 * kPtr));
        if (native) {
            if (std::uintptr_t transform = g_runtime.componentTransform(reinterpret_cast<std::uintptr_t>(native))) {
                return transform;
            }
        }
    }

    for (void* cursor = klass; cursor && depth <= 4; cursor = g_runtime.api.classGetParent(cursor)) {
        void* iterator = nullptr;
        for (std::size_t fieldIndex = 0; fieldIndex < 192; ++fieldIndex) {
            void* field = g_runtime.api.classGetFields(cursor, &iterator);
            if (!field) break;
            const std::size_t offset = g_runtime.api.fieldGetOffset(field);
            if (offset < 2 * kPtr || offset > 0x900) continue;
            const std::string typeName = fieldTypeName(field);
            if (typeName.empty()) continue;
            void* child = readObject(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(object) + offset));
            if (!child || !objectClass(child)) continue;

            if (typeName.find("UnityEngine.Transform") != std::string::npos ||
                nameEquals(className(objectClass(child)), "Transform")) {
                void* native = readObject(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(child) + 2 * kPtr));
                if (native) return reinterpret_cast<std::uintptr_t>(native);
            }

            const bool knownPart = objectClass(child) == g_runtime.transformsClass ||
                                   objectClass(child) == g_runtime.modelClass;
            if (depth < 4 && (knownPart || followReferenceType(typeName))) {
                if (std::uintptr_t nested = transformFromManaged(child, depth + 1, visited, visitedCount)) {
                    return nested;
                }
            }
        }
    }
    return 0;
}

inline std::uintptr_t transformFromManaged(void* object) {
    std::array<void*, 12> visited{};
    std::size_t count = 0;
    return transformFromManaged(object, 0, visited, count);
}

inline std::size_t g_scanRangeIndex = 0;
inline std::uintptr_t g_scanAddress = 0;
inline std::uint64_t g_scanMapGeneration = 0;
inline std::vector<MapRange> g_scanMaps;
inline std::vector<std::uintptr_t> g_scanBuffer;
inline std::vector<void*> g_playerObjects;
inline void* g_clientWorldObject = nullptr;
inline void* g_registryObject = nullptr;
inline void* g_storeObject = nullptr;
inline std::chrono::steady_clock::time_point g_scanPauseUntil{};
inline std::chrono::steady_clock::time_point g_lastScanProgressLog{};
inline std::chrono::steady_clock::time_point g_lastNetObjectLog{};
inline std::uint64_t g_scanCycleBytes = 0;
inline std::uint64_t g_scanReadFailures = 0;

// Hook the current ake update callback through its IL2CPP class vtable. Unity
// 6000.3 does not use the generated header's old +0x138 vtable layout at runtime.
// Matching ARM64 code in ake::xdzx() dispatches from Il2CppClass+0x1A0. We scan
// only bounded VirtualInvokeData entries for the exact ake::opmn pointer and
// validate the adjacent MethodInfo before changing a writable slot.
using PlayerUpdateHookFn = void (*)(void*, const void*);
inline std::atomic<PlayerUpdateHookFn> g_originalPlayerUpdate{nullptr};
inline std::mutex g_hookPlayerMutex;
inline std::vector<void*> g_hookPlayerObjects;
inline void* g_hookLocalPlayer = nullptr;
inline std::uintptr_t g_playerUpdateVtableSlot = 0;
inline std::size_t g_playerUpdatePatchedSlots = 0;
inline std::chrono::steady_clock::time_point g_lastPlayerHookInstallAttempt{};
inline std::chrono::steady_clock::time_point g_lastPlayerHookLog{};
inline std::chrono::steady_clock::time_point g_lastPlayerHookMissLog{};
inline constexpr std::size_t kLegacyVtableCountOffset = 0x126;
inline constexpr std::size_t kRuntimeVtableOffset = 0x1A0;
inline constexpr std::size_t kRuntimeVtableSearchEntries = 256;
inline constexpr std::size_t kVirtualInvokeDataSize = 2 * kPtr;

// UnityMain obtains a typed native Camera* and safely copies its matrix caches.
// eglSwapBuffers refreshes matrix values by safe reads. No matrix/W2S/Transform getters
// are called for the camera on any thread.
inline std::mutex g_cameraCaptureMutex;
inline std::mutex g_cameraSnapshotMutex;
inline CameraMatrixCache g_cameraSnapshot{};
inline std::uintptr_t g_cameraSnapshotObject = 0;
inline std::uintptr_t g_cameraSnapshotNative = 0;
inline std::uintptr_t g_cameraSnapshotVtable = 0;
inline std::uintptr_t g_cameraSnapshotGameObject = 0;
inline std::atomic<std::uint64_t> g_presentCameraReads{0};
inline std::atomic<std::uint64_t> g_presentCameraRejects{0};
inline std::atomic<int> g_mainCameraAgeMs{0};
inline std::chrono::steady_clock::time_point g_cameraSnapshotTime{};
inline std::chrono::steady_clock::time_point g_lastCameraCaptureAttempt{};
inline std::chrono::steady_clock::time_point g_lastCameraCaptureLog{};
inline std::atomic<bool> g_nonUnityMainHookThreadLogged{false};

inline bool cameraMatrixFinite(const Matrix4x4& matrix) {
    float magnitude = 0.0f;
    for (float value : matrix.value) {
        if (!std::isfinite(value) || std::fabs(value) > 1000000.0f) return false;
        magnitude += std::fabs(value);
    }
    return magnitude > 0.01f;
}

inline bool isUnityMainCallback() {
    char name[16]{};
    (void)prctl(PR_GET_NAME, name, 0, 0, 0);
    return std::strncmp(name, "UnityMain", 9) == 0;
}

inline void captureCameraSnapshotFromPlayerCallback() {
    if (!isUnityMainCallback()) {
        if (!g_nonUnityMainHookThreadLogged.exchange(true, std::memory_order_relaxed)) {
            __android_log_print(ANDROID_LOG_WARN, kLogTag,
                                "ESP camera capture blocked on non-game thread: tid=%d",
                                static_cast<int>(syscall(SYS_gettid)));
        }
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    std::unique_lock<std::mutex> captureLock(g_cameraCaptureMutex, std::try_to_lock);
    if (!captureLock.owns_lock() ||
        now - g_lastCameraCaptureAttempt < std::chrono::milliseconds(16)) return;
    g_lastCameraCaptureAttempt = now;
    std::uintptr_t nativeCamera = 0;
    auto fail = [&](const char* reason) {
        const auto failures = g_cameraSnapshotFailures.fetch_add(1, std::memory_order_relaxed) + 1;
        if (failures == 1 || now - g_lastCameraCaptureLog >= std::chrono::seconds(5)) {
            g_lastCameraCaptureLog = now;
            __android_log_print(ANDROID_LOG_WARN, kLogTag,
                "ESP camera cache rejected: reason=%s native=%p failures=%llu tid=%d",
                reason, reinterpret_cast<void*>(nativeCamera),
                static_cast<unsigned long long>(failures), static_cast<int>(syscall(SYS_gettid)));
        }
    };
    if (!g_runtime.cameraMain) { fail("missing-native-main"); return; }
    // 0x54C1338 returns Camera*. 0x53653C4 returns a GC handle, so reading
    // handle+0x10 was the precise source of the 2026-09-30 UnityMain crash.
    nativeCamera = g_runtime.cameraMain();
    if (!nativeCamera) { fail("no-main-camera"); return; }
    std::uintptr_t vtable = 0;
    std::uintptr_t gameObject = 0;
    std::uintptr_t activeMethod = 0;
    if (!readValue(reinterpret_cast<void*>(nativeCamera), vtable) ||
        vtable < g_runtime.unityBase + offsets::elf::kDataRelRoBegin ||
        vtable >= g_runtime.unityBase + offsets::elf::kDataEnd ||
        !readValue(reinterpret_cast<void*>(vtable + 0xD0), activeMethod) ||
        activeMethod < g_runtime.unityBase + offsets::elf::kTextBegin ||
        activeMethod >= g_runtime.unityBase + offsets::elf::kTextEnd ||
        !readValue(reinterpret_cast<void*>(nativeCamera + 0x20), gameObject) ||
        !plausibleAddress(reinterpret_cast<void*>(gameObject), kPtr)) {
        fail("native-camera-header"); return;
    }
    CameraMatrixCache candidate{};
    if (!readValue(reinterpret_cast<void*>(nativeCamera +
            offsets::managed::kNativeCameraWorldToCameraMatrix), candidate)) {
        fail("matrix-copy"); return;
    }
    std::uintptr_t currentVtable = 0;
    std::uintptr_t currentGameObject = 0;
    if (!readValue(reinterpret_cast<void*>(nativeCamera), currentVtable) ||
        !readValue(reinterpret_cast<void*>(nativeCamera + 0x20), currentGameObject) ||
        currentVtable != vtable || currentGameObject != gameObject) {
        fail("camera-changed-during-copy"); return;
    }
    const auto& view = candidate.worldToCamera.value;
    if (!cameraMatrixFinite(candidate.worldToCamera) ||
        !cameraMatrixFinite(candidate.projection) ||
        std::fabs(view[3]) > 0.001f || std::fabs(view[7]) > 0.001f ||
        std::fabs(view[11]) > 0.001f || std::fabs(view[15] - 1.0f) > 0.01f ||
        std::fabs(candidate.projection.value[0]) < 0.001f ||
        std::fabs(candidate.projection.value[5]) < 0.001f) {
        fail("invalid-or-uninitialized-matrices"); return;
    }
    const std::uint64_t captures =
        g_cameraSnapshotCaptures.fetch_add(1, std::memory_order_relaxed) + 1;
    g_cameraSnapshotTid.store(static_cast<pid_t>(syscall(SYS_gettid)), std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> snapshotLock(g_cameraSnapshotMutex);
        g_cameraSnapshot = candidate;
        g_cameraSnapshotObject = 0;  // no managed Camera reference is retained
        g_cameraSnapshotNative = nativeCamera;
        g_cameraSnapshotVtable = vtable;
        g_cameraSnapshotGameObject = gameObject;
        g_cameraSnapshotTime = now;
        g_cameraSnapshotReady.store(true, std::memory_order_release);
    }
    if (captures == 1 || now - g_lastCameraCaptureLog >= std::chrono::seconds(5)) {
        g_lastCameraCaptureLog = now;
        __android_log_print(ANDROID_LOG_INFO, kLogTag,
            "ESP UnityMain camera snapshot: source=native-cache tid=%d captures=%llu native=%p gameObject=%p getters=0 viewDiag=(%.3f,%.3f,%.3f,%.3f) projDiag=(%.3f,%.3f,%.3f,%.3f)",
            static_cast<int>(syscall(SYS_gettid)), static_cast<unsigned long long>(captures),
            reinterpret_cast<void*>(nativeCamera), reinterpret_cast<void*>(gameObject),
            view[0], view[5], view[10], view[15],
            candidate.projection.value[0], candidate.projection.value[5],
            candidate.projection.value[10], candidate.projection.value[15]);
    }
}

// Re-copy matrix VALUES at presentation time; never execute Unity methods here.
// The main callback leases a typed camera identity. Checking both header fields
// before/after the two identical copies rejects freed/reused objects and tears.
inline bool readPresentCameraMatrices(std::uintptr_t nativeCamera,
                                      std::uintptr_t expectedVtable,
                                      std::uintptr_t expectedGameObject,
                                      CameraMatrixCache& output) {
    if (!nativeCamera || !expectedGameObject ||
        expectedVtable < g_runtime.unityBase + offsets::elf::kDataRelRoBegin ||
        expectedVtable >= g_runtime.unityBase + offsets::elf::kDataEnd) return false;
    const auto headerMatches = [&]() {
        std::uintptr_t vtable = 0;
        std::uintptr_t gameObject = 0;
        return readValue(reinterpret_cast<void*>(nativeCamera), vtable) &&
               readValue(reinterpret_cast<void*>(nativeCamera + 0x20), gameObject) &&
               vtable == expectedVtable && gameObject == expectedGameObject;
    };
    const auto address = reinterpret_cast<void*>(nativeCamera +
        offsets::managed::kNativeCameraWorldToCameraMatrix);
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!headerMatches()) return false;
        CameraMatrixCache candidate{};
        CameraMatrixCache verification{};
        if (!readValue(address, candidate) || !readValue(address, verification)) return false;
        if (!headerMatches()) return false;
        if (std::memcmp(&candidate, &verification, sizeof(candidate)) != 0) continue;
        const auto& view = candidate.worldToCamera.value;
        if (!cameraMatrixFinite(candidate.worldToCamera) ||
            !cameraMatrixFinite(candidate.projection) ||
            std::fabs(view[3]) > 0.001f || std::fabs(view[7]) > 0.001f ||
            std::fabs(view[11]) > 0.001f || std::fabs(view[15] - 1.0f) > 0.01f ||
            std::fabs(candidate.projection.value[0]) < 0.001f ||
            std::fabs(candidate.projection.value[5]) < 0.001f) return false;
        output = candidate;
        return true;
    }
    return false;
}

inline bool readCameraForPresent(CameraMatrixCache& output,
                                 std::uintptr_t& nativeCamera) {
    std::uintptr_t vtable = 0;
    std::uintptr_t gameObject = 0;
    std::chrono::steady_clock::time_point mainCaptureTime{};
    {
        std::lock_guard<std::mutex> lock(g_cameraSnapshotMutex);
        if (!g_cameraSnapshotReady.load(std::memory_order_acquire)) return false;
        nativeCamera = g_cameraSnapshotNative;
        vtable = g_cameraSnapshotVtable;
        gameObject = g_cameraSnapshotGameObject;
        mainCaptureTime = g_cameraSnapshotTime;
    }
    const auto age = std::chrono::steady_clock::now() - mainCaptureTime;
    const auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(age).count();
    g_mainCameraAgeMs.store(static_cast<int>(std::min<std::int64_t>(ageMs, 10000)),
                           std::memory_order_relaxed);
    if (age >= std::chrono::milliseconds(250) ||
        !readPresentCameraMatrices(nativeCamera, vtable, gameObject, output)) {
        g_presentCameraRejects.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    g_presentCameraReads.fetch_add(1, std::memory_order_relaxed);
    return true;
}

struct PlayerPoseCache {
    void* player{};
    void* world{};
    Vec3 position{};
    std::chrono::steady_clock::time_point captured{};
};
inline std::mutex g_playerPoseMutex;
inline std::array<PlayerPoseCache, 128> g_playerPoses{};
inline std::atomic<std::uint64_t> g_playerPoseCaptures{0};

// Only a live game's own callback may invoke ake::xdzx(). The render thread
// consumes copied positions and never follows Unity's native Transform chain.
inline void capturePlayerPoseFromCallback(void* player) {
    if (!player || !g_runtime.playerPosition || !isUnityMainCallback()) return;
    if (objectClass(player) != g_runtime.playerClass) return;
    const auto base = reinterpret_cast<std::uintptr_t>(player);
    void* world = readObject(reinterpret_cast<void*>(base + offsets::field::mfc::kWorld));
    void* parts = readObject(reinterpret_cast<void*>(base + offsets::field::mfc::kPartsContainer));
    if (!world || !parts || !g_runtime.transformsClass) return;
    void* list = readObject(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(parts) +
                                                    offsets::field::part_container::kPartList));
    if (!list) return;
    const auto listBase = reinterpret_cast<std::uintptr_t>(list);
    std::int32_t count = 0;
    if (!readValue(reinterpret_cast<void*>(listBase + offsets::field::managed_list::kSize), count) ||
        count <= 0 || count > 64) return;
    void* array = readObject(reinterpret_cast<void*>(listBase + offsets::field::managed_list::kItems));
    if (!array) return;
    const auto arrayBase = reinterpret_cast<std::uintptr_t>(array);
    std::uintptr_t length = 0;
    if (!readValue(reinterpret_cast<void*>(arrayBase + offsets::managed::kArrayLength), length) ||
        length < static_cast<std::uintptr_t>(count) || length > 256) return;
    bool transformsPresent = false;
    for (std::int32_t i = 0; i < count; ++i) {
        void* part = readObject(reinterpret_cast<void*>(arrayBase + offsets::managed::kArrayData +
                                                       static_cast<std::size_t>(i) * kPtr));
        if (objectClass(part) == g_runtime.transformsClass) { transformsPresent = true; break; }
    }
    if (!transformsPresent) return;
    const Vec3 position = g_runtime.playerPosition(player, nullptr);
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) ||
        std::fabs(position.x) >= 100000.0f || std::fabs(position.y) >= 100000.0f ||
        std::fabs(position.z) >= 100000.0f) return;
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(g_playerPoseMutex);
        PlayerPoseCache* destination = nullptr;
        for (auto& pose : g_playerPoses) {
            if (pose.player == player) { destination = &pose; break; }
            if (!destination && (!pose.player || now - pose.captured > std::chrono::seconds(1)))
                destination = &pose;
        }
        if (!destination) return;
        *destination = {player, world, position, now};
    }
    const auto captures = g_playerPoseCaptures.fetch_add(1, std::memory_order_relaxed) + 1;
    if (captures == 1) {
        __android_log_print(ANDROID_LOG_INFO, kLogTag,
            "ESP UnityMain position snapshot: player=%p world=(%.3f,%.3f,%.3f) tid=%d renderGetters=0",
            player, position.x, position.y, position.z, static_cast<int>(syscall(SYS_gettid)));
    }
}

inline bool readPlayerPoseSnapshot(void* player, Vec3& position) {
    if (!player) return false;
    const auto now = std::chrono::steady_clock::now();
    void* world = readObject(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(player) +
                                                    offsets::field::mfc::kWorld));
    if (!world) return false;
    std::lock_guard<std::mutex> lock(g_playerPoseMutex);
    for (const auto& pose : g_playerPoses) {
        if (pose.player == player && pose.world == world &&
            now - pose.captured < std::chrono::milliseconds(250)) {
            position = pose.position;
            return true;
        }
    }
    return false;
}


inline void trackPlayerFromUpdateHook(void* player) {
    if (!player) return;
    bool added = false;
    std::size_t tracked = 0;
    std::uint8_t localFlag = 0;
    if (!readValue(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(player) +
                                          offsets::field::mfc::kLocalOwned), localFlag)) return;
    const bool localOwned = localFlag != 0;
    {
        std::lock_guard<std::mutex> lock(g_hookPlayerMutex);
        if (std::find(g_hookPlayerObjects.begin(), g_hookPlayerObjects.end(), player) ==
            g_hookPlayerObjects.end()) {
            if (g_hookPlayerObjects.size() < 128) {
                g_hookPlayerObjects.push_back(player);
                added = true;
            }
        }
        if (localOwned) g_hookLocalPlayer = player;
        tracked = g_hookPlayerObjects.size();
    }
    g_hookedPlayerCount.store(static_cast<int>(tracked), std::memory_order_relaxed);
    if (added) {
        __android_log_print(ANDROID_LOG_INFO, kLogTag,
                            "ESP ake update hook entity: player=%p local=%d tracked=%zu",
                            player, localOwned ? 1 : 0, tracked);
    }
}

inline std::atomic<void (*)(void*)> g_playerFrameCallback{nullptr};

inline void hookedPlayerUpdate(void* player, const void* method) {
    if (player) trackPlayerFromUpdateHook(player);
    g_playerUpdateHookCalls.fetch_add(1, std::memory_order_relaxed);
    const PlayerUpdateHookFn original = g_originalPlayerUpdate.load(std::memory_order_acquire);
    if (original) original(player, method);
    // Capture only on the live game's thread, after the original callback.
    capturePlayerPoseFromCallback(player);
    captureCameraSnapshotFromPlayerCallback();
    if (auto callback = g_playerFrameCallback.load(std::memory_order_acquire)) callback(player);
}

inline bool installPlayerUpdateVtableHook() {
#if !defined(__aarch64__)
    return false;
#else
    if (g_playerUpdateHookReady.load(std::memory_order_acquire)) return true;
    const auto now = std::chrono::steady_clock::now();
    if (now - g_lastPlayerHookInstallAttempt < std::chrono::seconds(1)) return false;
    g_lastPlayerHookInstallAttempt = now;
    if (!g_runtime.playerClass || !g_runtime.unityBase) return false;

    const auto classBase = reinterpret_cast<std::uintptr_t>(g_runtime.playerClass);
    const std::uintptr_t target = g_runtime.unityBase + offsets::managed_rva::kPlayerUpdateHook;
    const std::uintptr_t replacement = reinterpret_cast<std::uintptr_t>(&hookedPlayerUpdate);
    constexpr std::size_t kMethodInfoNameOffset = 0x18;
    constexpr std::size_t kMethodInfoClassOffset = 0x20;
    constexpr std::size_t kMethodInfoSlotOffset = 0x50;
    constexpr std::uint16_t kInvalidMethodSlot = 0xFFFF;
    constexpr std::size_t kMaxVtableEntries = 256;

    const auto executableUnityPointer = [&](std::uintptr_t pointer) {
        return pointer >= g_runtime.unityBase + offsets::elf::kTextBegin &&
               pointer < g_runtime.unityBase + offsets::elf::kTextEnd;
    };
    const auto methodInfoMatchesTarget = [&](std::uintptr_t methodInfo) {
        if (!methodInfo || !readable(reinterpret_cast<void*>(methodInfo),
                                     kMethodInfoSlotOffset + sizeof(std::uint16_t))) return false;
        std::uintptr_t direct = 0;
        std::uintptr_t virtualMethod = 0;
        if (!readValue(reinterpret_cast<void*>(methodInfo), direct) ||
            !readValue(reinterpret_cast<void*>(methodInfo + kPtr), virtualMethod)) return false;
        return direct == target || virtualMethod == target;
    };

    // Unity can retain the abstract/base MethodInfo in a vtable entry even when
    // methodPtr points to the concrete override. Find ake::opmn's own MethodInfo
    // through class-owned pointer arrays and use its authoritative slot index.
    std::uintptr_t targetMethodInfo = 0;
    std::size_t methodTableHeaderOffset = 0;
    std::size_t methodTableIndex = 0;
    for (std::size_t headerOffset = 0; headerOffset < kRuntimeVtableOffset &&
                                     !targetMethodInfo; headerOffset += kPtr) {
        std::uintptr_t candidateTable = 0;
        if (!readValue(reinterpret_cast<void*>(classBase + headerOffset), candidateTable) ||
            !candidateTable || !readable(reinterpret_cast<void*>(candidateTable), kPtr)) continue;
        if (methodInfoMatchesTarget(candidateTable)) {
            targetMethodInfo = candidateTable;
            methodTableHeaderOffset = headerOffset;
            break;
        }
        for (std::size_t index = 0; index < 512; ++index) {
            std::uintptr_t methodInfo = 0;
            if (!readValue(reinterpret_cast<void*>(candidateTable + index * kPtr), methodInfo)) break;
            if (!methodInfo || executableUnityPointer(methodInfo)) continue;
            if (methodInfoMatchesTarget(methodInfo)) {
                targetMethodInfo = methodInfo;
                methodTableHeaderOffset = headerOffset;
                methodTableIndex = index;
                break;
            }
        }
    }

    std::uint16_t targetMethodSlot = kInvalidMethodSlot;
    std::uintptr_t targetMethodClass = 0;
    char targetMethodName[32]{};
    if (targetMethodInfo) {
        (void)readValue(reinterpret_cast<void*>(targetMethodInfo + kMethodInfoSlotOffset),
                        targetMethodSlot);
        (void)readValue(reinterpret_cast<void*>(targetMethodInfo + kMethodInfoClassOffset),
                        targetMethodClass);
        std::uintptr_t namePointer = 0;
        if (readValue(reinterpret_cast<void*>(targetMethodInfo + kMethodInfoNameOffset), namePointer) &&
            namePointer) {
            (void)safeReadMemory(targetMethodName, reinterpret_cast<void*>(namePointer),
                                 sizeof(targetMethodName) - 1);
            targetMethodName[sizeof(targetMethodName) - 1] = '\0';
        }
        __android_log_print(ANDROID_LOG_INFO, kLogTag,
                            "ESP ake method resolved: target=%p methodInfo=%p name=%s klass=%p headerOffset=0x%zx methodIndex=%zu slot=%u",
                            reinterpret_cast<void*>(target),
                            reinterpret_cast<void*>(targetMethodInfo), targetMethodName,
                            reinterpret_cast<void*>(targetMethodClass), methodTableHeaderOffset,
                            methodTableIndex, static_cast<unsigned>(targetMethodSlot));
    }

    std::uintptr_t* matchedSlot = nullptr;
    std::uintptr_t originalSlotMethod = 0;
    std::uintptr_t matchedMethodInfo = 0;
    const char* matchReason = nullptr;
    const auto inspectSlot = [&](std::uintptr_t slotAddress, bool authoritativeSlot,
                                 const char* reason) {
        if (matchedSlot) return;
        std::uintptr_t methodPointer = 0;
        std::uintptr_t methodInfo = 0;
        if (!readValue(reinterpret_cast<void*>(slotAddress), methodPointer) || !methodPointer ||
            !readValue(reinterpret_cast<void*>(slotAddress + kPtr), methodInfo)) return;
        const bool exactMethodPointer = methodPointer == target;
        const bool exactMethodInfo =
            (targetMethodInfo && methodInfo == targetMethodInfo) || methodInfoMatchesTarget(methodInfo);
        const bool trustedSlot = authoritativeSlot && executableUnityPointer(methodPointer);
        if (!exactMethodPointer && !exactMethodInfo && !trustedSlot) return;
        matchedSlot = reinterpret_cast<std::uintptr_t*>(slotAddress);
        originalSlotMethod = methodPointer;
        matchedMethodInfo = methodInfo;
        matchReason = exactMethodPointer ? "exact-methodPtr" :
                      (exactMethodInfo ? "exact-MethodInfo" : reason);
    };

    if (targetMethodInfo && targetMethodClass == classBase &&
        std::strcmp(targetMethodName, "opmn") == 0 &&
        targetMethodSlot != kInvalidMethodSlot && targetMethodSlot < kMaxVtableEntries) {
        inspectSlot(classBase + kRuntimeVtableOffset +
                        static_cast<std::size_t>(targetMethodSlot) * kVirtualInvokeDataSize,
                    true, "MethodInfo-slot");
    }
    for (std::size_t index = 0; index < kMaxVtableEntries && !matchedSlot; ++index) {
        inspectSlot(classBase + kRuntimeVtableOffset + index * kVirtualInvokeDataSize,
                    false, "vtable-scan");
    }

    if (!matchedSlot) {
        if (now - g_lastPlayerHookMissLog >= std::chrono::seconds(5)) {
            g_lastPlayerHookMissLog = now;
            __android_log_print(ANDROID_LOG_WARN, kLogTag,
                                "ESP ake hook scan miss: target=%p class=%p methodInfo=%p methodName=%s slot=%u vtable=+0x%zx entries=%zu",
                                reinterpret_cast<void*>(target), g_runtime.playerClass,
                                reinterpret_cast<void*>(targetMethodInfo), targetMethodName,
                                static_cast<unsigned>(targetMethodSlot), kRuntimeVtableOffset,
                                kMaxVtableEntries);
        }
        return false;
    }
    if (!executableUnityPointer(originalSlotMethod)) {
        __android_log_print(ANDROID_LOG_WARN, kLogTag,
                            "ESP ake hook refused: original slot method=%p is outside libunity text",
                            reinterpret_cast<void*>(originalSlotMethod));
        return false;
    }
    refreshMaps(true);
    if (!writable(matchedSlot, sizeof(*matchedSlot))) {
        __android_log_print(ANDROID_LOG_WARN, kLogTag,
                            "ESP ake update hook refused: vtable slot=%p is not writable",
                            static_cast<void*>(matchedSlot));
        return false;
    }

    g_originalPlayerUpdate.store(reinterpret_cast<PlayerUpdateHookFn>(originalSlotMethod),
                                 std::memory_order_release);
    __atomic_store_n(matchedSlot, replacement, __ATOMIC_RELEASE);

    std::uintptr_t current = 0;
    if (!readValue(matchedSlot, current) || current != replacement) {
        __atomic_store_n(matchedSlot, originalSlotMethod, __ATOMIC_RELEASE);
        g_originalPlayerUpdate.store(nullptr, std::memory_order_release);
        __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                            "ESP ake update hook verification failed; vtable restored");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_hookPlayerMutex);
        g_hookPlayerObjects.reserve(32);
    }
    g_playerUpdateVtableSlot = reinterpret_cast<std::uintptr_t>(matchedSlot);
    g_playerUpdatePatchedSlots = 1;
    g_playerUpdateHookReady.store(true, std::memory_order_release);
    const std::size_t classOffset = static_cast<std::size_t>(g_playerUpdateVtableSlot - classBase);
    __android_log_print(ANDROID_LOG_INFO, kLogTag,
                        "ESP ake update hook ready: reason=%s RVA=0x%lx slot=%p classOffset=0x%zx methodSlot=%u slotMethod=%p slotMethodInfo=%p replacement=%p",
                        matchReason ? matchReason : "unknown",
                        static_cast<unsigned long>(offsets::managed_rva::kPlayerUpdateHook),
                        static_cast<void*>(matchedSlot), classOffset,
                        static_cast<unsigned>(targetMethodSlot),
                        reinterpret_cast<void*>(originalSlotMethod),
                        reinterpret_cast<void*>(matchedMethodInfo),
                        reinterpret_cast<void*>(replacement));
    return true;
#endif
}

inline std::vector<void*> snapshotPlayersFromUpdateHook() {
    std::vector<void*> result;
    if (!g_playerUpdateHookReady.load(std::memory_order_acquire)) return result;
    {
        std::lock_guard<std::mutex> lock(g_hookPlayerMutex);
        auto iterator = g_hookPlayerObjects.begin();
        while (iterator != g_hookPlayerObjects.end()) {
            void* object = *iterator;
            void* world = object ? readObject(reinterpret_cast<void*>(
                reinterpret_cast<std::uintptr_t>(object) + offsets::field::mfc::kWorld)) : nullptr;
            if (!object || objectClass(object) != g_runtime.playerClass || !world) {
                if (g_hookLocalPlayer == object) g_hookLocalPlayer = nullptr;
                iterator = g_hookPlayerObjects.erase(iterator);
            } else {
                ++iterator;
            }
        }
        result = g_hookPlayerObjects;
    }
    g_hookedPlayerCount.store(static_cast<int>(result.size()), std::memory_order_relaxed);
    const auto now = std::chrono::steady_clock::now();
    if (now - g_lastPlayerHookLog >= std::chrono::seconds(5)) {
        g_lastPlayerHookLog = now;
        __android_log_print(ANDROID_LOG_INFO, kLogTag,
                            "ESP ake update hook snapshot: calls=%llu tracked=%zu local=%p slot=%p",
                            static_cast<unsigned long long>(
                                g_playerUpdateHookCalls.load(std::memory_order_relaxed)),
                            result.size(), g_hookLocalPlayer,
                            reinterpret_cast<void*>(g_playerUpdateVtableSlot));
    }
    return result;
}


inline void resetRootScan() {
    g_scanRangeIndex = 0;
    g_scanAddress = 0;
    g_scanMaps.clear();
    g_scanMaps.reserve(g_maps.size());
    for (const auto& range : g_maps) {
        if (range.scanEligible) g_scanMaps.push_back(range);
    }
    g_scanCycleBytes = 0;
    g_scanMapGeneration = g_mapGeneration;
}

inline bool likelyObjectWords(const std::uintptr_t* words, std::size_t wordCount,
                              std::size_t index, void* expectedClass) {
    if (!words || !expectedClass || index + 1 >= wordCount ||
        words[index] != reinterpret_cast<std::uintptr_t>(expectedClass)) {
        return false;
    }
    const std::uintptr_t monitor = words[index + 1];
    if (monitor == 0) return true;
    return (monitor & (kPtr - 1)) == 0 &&
           plausibleAddress(reinterpret_cast<void*>(monitor), kPtr);
}

inline bool validateStoreLayout(void* store, void*& list, void*& array, int& count) {
    list = nullptr;
    array = nullptr;
    count = 0;
    if (!store || objectClass(store) != g_runtime.storeClass) return false;

    const auto storeBase = reinterpret_cast<std::uintptr_t>(store);
    list = readObject(reinterpret_cast<void*>(storeBase + offsets::field::gzx::kObjects));
    if (!list) return false;

    std::int32_t rawCount = 0;
    const auto listBase = reinterpret_cast<std::uintptr_t>(list);
    if (!readValue(reinterpret_cast<void*>(listBase + offsets::field::yjk::kCount), rawCount) ||
        rawCount < 0 || rawCount > 4096) {
        return false;
    }

    array = readObject(reinterpret_cast<void*>(listBase + offsets::field::yjk::kItems));
    if (rawCount == 0) {
        count = 0;
        return true;
    }
    if (!array) return false;

    std::uintptr_t arrayLength = 0;
    if (!readValue(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(array) +
                                           offsets::managed::kArrayLength), arrayLength) ||
        arrayLength < static_cast<std::uintptr_t>(rawCount) || arrayLength > 8192) {
        return false;
    }
    count = rawCount;
    return true;
}

inline bool validateRegistry(void* registry, void*& clientWorld, void*& store,
                             void*& list, void*& array, int& count) {
    clientWorld = nullptr;
    store = nullptr;
    list = nullptr;
    array = nullptr;
    count = 0;
    if (!registry || objectClass(registry) != g_runtime.registryClass) return false;

    const auto registryBase = reinterpret_cast<std::uintptr_t>(registry);
    store = readObject(reinterpret_cast<void*>(registryBase + offsets::field::gat::kStore));
    if (!validateStoreLayout(store, list, array, count)) return false;

    void* world = readObject(reinterpret_cast<void*>(registryBase + offsets::field::gat::kWorld));
    if (world && (!g_runtime.clientWorldClass || objectClass(world) == g_runtime.clientWorldClass)) {
        clientWorld = world;
    }
    return true;
}

inline bool adoptRegistry(void* registry, void* hintedWorld = nullptr) {
    void* world = nullptr;
    void* store = nullptr;
    void* list = nullptr;
    void* array = nullptr;
    int count = 0;
    if (!validateRegistry(registry, world, store, list, array, count)) return false;
    if (!world && hintedWorld &&
        (!g_runtime.clientWorldClass || objectClass(hintedWorld) == g_runtime.clientWorldClass)) {
        world = hintedWorld;
    }

    g_clientWorldObject = world;
    g_registryObject = registry;
    g_storeObject = store;
    g_netObjectCount.store(count, std::memory_order_relaxed);
    g_scanMaps.clear();
    g_scanRangeIndex = 0;
    g_scanAddress = 0;
    __android_log_print(ANDROID_LOG_INFO, kLogTag,
                        "ESP NetObject registry ready: clientWorld=%p registry=%p store=%p objects=%d reader=%s",
                        g_clientWorldObject, g_registryObject, g_storeObject, count,
                        memoryReadBackendName());
    return true;
}

inline bool tryRootCandidate(void* candidate, void* candidateClass) {
    if (!candidate || !candidateClass) return false;
    if (candidateClass == g_runtime.registryClass) return adoptRegistry(candidate);
    if (candidateClass == g_runtime.clientWorldClass) {
        void* registry = readObject(reinterpret_cast<void*>(
            reinterpret_cast<std::uintptr_t>(candidate) + offsets::field::njh::kRegistry));
        return adoptRegistry(registry, candidate);
    }
    return false;
}

inline bool scanRegistryStep(std::size_t budgetBytes = 4 * 1024 * 1024) {
    if (g_registryObject) return true;
    if (!g_runtime.registryClass || !g_runtime.storeClass || g_maps.empty()) return false;
    const auto now = std::chrono::steady_clock::now();
    if (now < g_scanPauseUntil) return false;
    if (g_scanMaps.empty()) resetRootScan();

    constexpr std::size_t kMaxReadChunk = 1024 * 1024;
    constexpr std::uintptr_t kPageSize = 4096;
    std::size_t consumed = 0;
    bool found = false;
    while (!found && consumed < budgetBytes && g_scanRangeIndex < g_scanMaps.size()) {
        const MapRange& range = g_scanMaps[g_scanRangeIndex];
        std::uintptr_t cursor = g_scanAddress ? g_scanAddress :
            (range.begin + (kPtr - 1)) & ~(static_cast<std::uintptr_t>(kPtr) - 1);
        if (cursor < range.begin) cursor = range.begin;
        if (!range.scanEligible || cursor >= range.end || range.end - cursor < 2 * kPtr) {
            ++g_scanRangeIndex;
            g_scanAddress = 0;
            continue;
        }

        const std::size_t remainingBudget = budgetBytes - consumed;
        const std::size_t remainingRange = static_cast<std::size_t>(range.end - cursor);
        std::size_t requested = std::min({remainingBudget, remainingRange, kMaxReadChunk});
        requested &= ~(kPtr - 1);
        if (requested < 2 * kPtr) {
            ++g_scanRangeIndex;
            g_scanAddress = 0;
            continue;
        }

        g_scanBuffer.resize(requested / kPtr);
        const ssize_t copiedResult = safeReadMemory(
            g_scanBuffer.data(), reinterpret_cast<const void*>(cursor), requested);
        std::size_t advanced = 0;
        if (copiedResult > 0) {
            const std::size_t copied = std::min(requested, static_cast<std::size_t>(copiedResult));
            const std::size_t wordCount = copied / kPtr;
            for (std::size_t index = 0; index + 1 < wordCount; ++index) {
                void* matchedClass = nullptr;
                if (likelyObjectWords(g_scanBuffer.data(), wordCount, index,
                                      g_runtime.registryClass)) {
                    matchedClass = g_runtime.registryClass;
                } else if (likelyObjectWords(g_scanBuffer.data(), wordCount, index,
                                             g_runtime.clientWorldClass)) {
                    matchedClass = g_runtime.clientWorldClass;
                }
                if (!matchedClass) continue;
                void* candidate = reinterpret_cast<void*>(cursor + index * kPtr);
                if (tryRootCandidate(candidate, matchedClass)) {
                    found = true;
                    break;
                }
            }
            advanced = copied & ~(kPtr - 1);
        }

        if (advanced == 0) {
            ++g_scanReadFailures;
            const std::uintptr_t nextPage = (cursor + kPageSize) & ~(kPageSize - 1);
            const std::size_t pageAdvance = static_cast<std::size_t>(
                std::min(range.end, nextPage > cursor ? nextPage : cursor + kPageSize) - cursor);
            advanced = std::min(requested, std::max<std::size_t>(kPtr, pageAdvance));
        }

        consumed += advanced;
        g_scanCycleBytes += advanced;
        g_scannedBytes.fetch_add(advanced, std::memory_order_relaxed);
        const std::uintptr_t next = cursor + advanced;
        if (!found) {
            if (next >= range.end) {
                ++g_scanRangeIndex;
                g_scanAddress = 0;
            } else {
                g_scanAddress = next;
            }
        }
    }

    if (found) return true;
    if (now - g_lastScanProgressLog >= std::chrono::seconds(5)) {
        g_lastScanProgressLog = now;
        std::uint64_t eligibleBytes = 0;
        for (const auto& range : g_scanMaps) eligibleBytes += range.end - range.begin;
        __android_log_print(ANDROID_LOG_INFO, kLogTag,
                            "ESP NetObject root scan: range=%zu/%zu cycleMiB=%.1f eligibleMiB=%.1f snapshotGen=%llu currentGen=%llu reader=%s readFailures=%llu",
                            g_scanRangeIndex, g_scanMaps.size(),
                            static_cast<double>(g_scanCycleBytes) / 1048576.0,
                            static_cast<double>(eligibleBytes) / 1048576.0,
                            static_cast<unsigned long long>(g_scanMapGeneration),
                            static_cast<unsigned long long>(g_mapGeneration),
                            memoryReadBackendName(),
                            static_cast<unsigned long long>(g_scanReadFailures));
    }
    if (g_scanRangeIndex >= g_scanMaps.size()) {
        const auto cycle = g_scanCycles.fetch_add(1, std::memory_order_relaxed) + 1;
        __android_log_print(ANDROID_LOG_INFO, kLogTag,
                            "ESP NetObject root scan complete: cycle=%llu no registry, totalMiB=%.1f",
                            static_cast<unsigned long long>(cycle),
                            static_cast<double>(g_scanCycleBytes) / 1048576.0);
        g_scanPauseUntil = now + std::chrono::seconds(1);
        resetRootScan();
    }
    return false;
}

inline void invalidateRegistry(const char* reason) {
    ++g_registryReadFailures;
    __android_log_print(ANDROID_LOG_WARN, kLogTag,
                        "ESP NetObject registry invalidated: reason=%s clientWorld=%p registry=%p store=%p failures=%llu",
                        reason ? reason : "unknown", g_clientWorldObject, g_registryObject,
                        g_storeObject,
                        static_cast<unsigned long long>(
                            g_registryReadFailures.load(std::memory_order_relaxed)));
    g_clientWorldObject = nullptr;
    g_registryObject = nullptr;
    g_storeObject = nullptr;
    g_playerObjects.clear();
    g_netObjectCount.store(0, std::memory_order_relaxed);
    g_candidateCount.store(0, std::memory_order_relaxed);
    resetRootScan();
    g_scanPauseUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
}

inline bool refreshPlayersFromNetObjects() {
    if (!g_registryObject && !scanRegistryStep()) {
        g_playerObjects.clear();
        g_candidateCount.store(0, std::memory_order_relaxed);
        return false;
    }

    void* world = nullptr;
    void* store = nullptr;
    void* list = nullptr;
    void* array = nullptr;
    int count = 0;
    if (!validateRegistry(g_registryObject, world, store, list, array, count)) {
        invalidateRegistry("layout validation failed");
        return false;
    }
    if (world) g_clientWorldObject = world;
    g_storeObject = store;
    g_netObjectCount.store(count, std::memory_order_relaxed);

    std::vector<void*> players;
    if (count > 0) {
        const std::size_t bytes = static_cast<std::size_t>(count) * kPtr;
        std::vector<std::uintptr_t> objects(static_cast<std::size_t>(count));
        const void* data = reinterpret_cast<void*>(
            reinterpret_cast<std::uintptr_t>(array) + offsets::managed::kArrayData);
        const ssize_t copied = safeReadMemory(objects.data(), data, bytes);
        if (copied != static_cast<ssize_t>(bytes)) {
            invalidateRegistry("short NetObject array read");
            return false;
        }

        players.reserve(32);
        for (const std::uintptr_t raw : objects) {
            void* object = reinterpret_cast<void*>(raw);
            if (!plausibleAddress(object, 2 * kPtr)) continue;
            // yjk<krm> stores interface references as ordinary managed object pointers.
            // Filter the registry snapshot by the exact concrete player class (ake).
            if (objectClass(object) != g_runtime.playerClass) continue;
            if (std::find(players.begin(), players.end(), object) == players.end()) {
                players.push_back(object);
                if (players.size() >= 128) break;
            }
        }
    }

    g_playerObjects.swap(players);
    g_candidateCount.store(static_cast<int>(g_playerObjects.size()), std::memory_order_relaxed);
    const auto now = std::chrono::steady_clock::now();
    if (now - g_lastNetObjectLog >= std::chrono::seconds(5)) {
        g_lastNetObjectLog = now;
        __android_log_print(ANDROID_LOG_INFO, kLogTag,
                            "ESP NetObject snapshot: clientWorld=%p registry=%p store=%p total=%d ake=%zu",
                            g_clientWorldObject, g_registryObject, g_storeObject, count,
                            g_playerObjects.size());
    }
    return true;
}

struct PlayerSnapshot {
    bool localOwned{};
    bool statsValid{};
    bool weaponValid{};
    bool contextTeamValid{};
    bool positionContainerValid{};
    std::uint8_t rawTeam{};
    std::uint8_t contextTeam{};
    std::uint8_t team{};
    std::uint8_t weaponId{};
    std::uint8_t magazineAmmo{};
    std::uint16_t reserveAmmo{};
    std::uint16_t hp{};
    std::uint16_t maxHp{};
    std::uint16_t armor{};
    std::uint16_t maxArmor{};
    void* positionContainer{};
    void* weaponPart{};
    void* activeWeapon{};
};

struct PlayerEntry {
    void* object{};
    PlayerSnapshot snapshot{};
};

inline bool validTeam(std::uint8_t team) {
    return team == 1 || team == 2;
}

inline bool matchesClassOrName(void* object, void* expected, const char* fallbackName) {
    if (!object) return false;
    if (expected && classMatches(object, expected)) return true;
    return nameEquals(className(objectClass(object)), fallbackName);
}

inline void* findWeaponPart(void* player) {
    if (!player) return nullptr;
    const auto base = reinterpret_cast<std::uintptr_t>(player);
    void* container = readObject(reinterpret_cast<void*>(base + offsets::field::mfc::kPartsContainer));
    if (!container) return nullptr;

    // rct+0x10 is List<eur>. The old code treated the List object itself as the
    // backing array, so it walked List bookkeeping instead of part pointers.
    void* list = readObject(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(container) +
                                                   offsets::field::part_container::kPartList));
    if (!list) return nullptr;
    std::int32_t count = 0;
    if (!readValue(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(list) +
                                           offsets::field::managed_list::kSize), count) ||
        count <= 0 || count > 64) {
        return nullptr;
    }
    void* array = readObject(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(list) +
                                                    offsets::field::managed_list::kItems));
    if (!array) return nullptr;
    std::uintptr_t arrayLength = 0;
    if (!readValue(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(array) +
                                           offsets::managed::kArrayLength), arrayLength) ||
        arrayLength < static_cast<std::uintptr_t>(count) || arrayLength > 256) {
        return nullptr;
    }
    const auto data = reinterpret_cast<std::uintptr_t>(array) + offsets::managed::kArrayData;
    if (!readable(reinterpret_cast<void*>(data), static_cast<std::size_t>(count) * kPtr)) return nullptr;
    for (std::int32_t i = 0; i < count; ++i) {
        void* part = readObject(reinterpret_cast<void*>(data + static_cast<std::size_t>(i) * kPtr));
        if (matchesClassOrName(part, g_runtime.weaponPartClass, "nrk")) return part;
    }
    return nullptr;
}

inline bool readPlayerSnapshot(void* player, PlayerSnapshot& out) {
    out = {};
    if (!player) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(player);
    std::uint8_t localOwned = 0;
    if (!readValue(reinterpret_cast<void*>(base + offsets::field::ake::kTeam), out.rawTeam) ||
        !readValue(reinterpret_cast<void*>(base + offsets::field::mfc::kLocalOwned), localOwned)) {
        return false;
    }
    out.localOwned = localOwned != 0;
    out.team = out.rawTeam;
    out.positionContainer = readObject(reinterpret_cast<void*>(base + offsets::field::mfc::kPartsContainer));
    out.positionContainerValid = out.positionContainer != nullptr;

    void* stats = readObject(reinterpret_cast<void*>(base + offsets::field::ake::kStats));
    if (matchesClassOrName(stats, g_runtime.statsClass, "ylw")) {
        const auto statsBase = reinterpret_cast<std::uintptr_t>(stats);
        const bool readStats =
            readValue(reinterpret_cast<void*>(statsBase + offsets::field::ylw::kHp), out.hp) &&
            readValue(reinterpret_cast<void*>(statsBase + offsets::field::ylw::kMaxHp), out.maxHp) &&
            readValue(reinterpret_cast<void*>(statsBase + offsets::field::ylw::kArmor), out.armor) &&
            readValue(reinterpret_cast<void*>(statsBase + offsets::field::ylw::kMaxArmor), out.maxArmor);
        out.statsValid = readStats && out.maxHp > 0 && out.maxHp <= 10000 && out.hp <= 10000 &&
                         out.maxArmor <= 5000 && out.armor <= 5000;
    }

    out.weaponPart = findWeaponPart(player);
    if (out.weaponPart) {
        std::uint8_t contextTeam = 0;
        if (readValue(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(out.weaponPart) +
                                              offsets::field::nrk::kTeam), contextTeam) &&
            validTeam(contextTeam)) {
            out.contextTeam = contextTeam;
            out.contextTeamValid = true;
            out.team = contextTeam;
        }
        out.activeWeapon = readObject(reinterpret_cast<void*>(
            reinterpret_cast<std::uintptr_t>(out.weaponPart) + offsets::field::nrk::kActiveWeapon));
        if (matchesClassOrName(out.activeWeapon, g_runtime.weaponInstanceClass, "qjf")) {
            const auto weaponBase = reinterpret_cast<std::uintptr_t>(out.activeWeapon);
            out.weaponValid =
                readValue(reinterpret_cast<void*>(weaponBase + offsets::field::qjf::kWeaponId), out.weaponId) &&
                readValue(reinterpret_cast<void*>(weaponBase + offsets::field::qjf::kMagazineAmmo), out.magazineAmmo) &&
                readValue(reinterpret_cast<void*>(weaponBase + offsets::field::qjf::kReserveAmmo), out.reserveAmmo) &&
                out.reserveAmmo <= 5000;
        }
    }
    return true;
}

inline float distanceSquared(const Vec3& a, const Vec3& b) {
    const float x = a.x - b.x;
    const float y = a.y - b.y;
    const float z = a.z - b.z;
    return x * x + y * y + z * z;
}

inline bool finiteMatrix(const Matrix4x4& matrix) {
    float magnitude = 0.0f;
    for (float value : matrix.value) {
        if (!std::isfinite(value) || std::fabs(value) > 1000000.0f) return false;
        magnitude += std::fabs(value);
    }
    return magnitude > 0.01f;
}

inline float matrixAt(const Matrix4x4& matrix, int row, int column) {
    return matrix.value[column * 4 + row];
}

inline Vec4 multiply(const Matrix4x4& matrix, const Vec4& value) {
    return {
        matrixAt(matrix, 0, 0) * value.x + matrixAt(matrix, 0, 1) * value.y +
            matrixAt(matrix, 0, 2) * value.z + matrixAt(matrix, 0, 3) * value.w,
        matrixAt(matrix, 1, 0) * value.x + matrixAt(matrix, 1, 1) * value.y +
            matrixAt(matrix, 1, 2) * value.z + matrixAt(matrix, 1, 3) * value.w,
        matrixAt(matrix, 2, 0) * value.x + matrixAt(matrix, 2, 1) * value.y +
            matrixAt(matrix, 2, 2) * value.z + matrixAt(matrix, 2, 3) * value.w,
        matrixAt(matrix, 3, 0) * value.x + matrixAt(matrix, 3, 1) * value.y +
            matrixAt(matrix, 3, 2) * value.z + matrixAt(matrix, 3, 3) * value.w,
    };
}

inline bool project(const CameraMatrixCache& matrices, const Vec3& world, int width, int height,
                    Vec3& screen, Vec4* viewDebug = nullptr, Vec4* clipDebug = nullptr) {
    screen = {};
    const Vec4 view = multiply(matrices.worldToCamera, {world.x, world.y, world.z, 1.0f});
    const Vec4 clip = multiply(matrices.projection, view);
    if (viewDebug) *viewDebug = view;
    if (clipDebug) *clipDebug = clip;
    if (!std::isfinite(view.x) || !std::isfinite(view.y) || !std::isfinite(view.z) ||
        !std::isfinite(view.w) || !std::isfinite(clip.x) || !std::isfinite(clip.y) ||
        !std::isfinite(clip.z) || !std::isfinite(clip.w) || std::fabs(clip.w) <= 0.0001f) {
        return false;
    }
    // Unity camera space looks down -Z. This is also the WorldToScreenPoint z value.
    const float depth = -view.z;
    if (depth <= 0.05f) return false;
    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    screen.x = (ndcX + 1.0f) * 0.5f * static_cast<float>(width);
    const float unityScreenY = (ndcY + 1.0f) * 0.5f * static_cast<float>(height);
    screen.y = static_cast<float>(height) - unityScreenY;
    screen.z = depth;
    return screen.x > -static_cast<float>(width) && screen.x < static_cast<float>(width) * 2.0f &&
           screen.y > -static_cast<float>(height) && screen.y < static_cast<float>(height) * 2.0f;
}

inline bool produceBoxes(int width, int height, std::vector<ScreenBox>& boxes) {
    boxes.clear();
    g_projectedCount.store(0, std::memory_order_relaxed);
    const bool hookInstalled = installPlayerUpdateVtableHook();
    std::vector<void*> hookPlayers = snapshotPlayersFromUpdateHook();
    const bool hookSourceReady = hookInstalled && hookPlayers.size() >= 2;
    bool registryReady = false;
    if (hookSourceReady) {
        g_playerObjects = std::move(hookPlayers);
        g_candidateCount.store(static_cast<int>(g_playerObjects.size()), std::memory_order_relaxed);
    } else {
        registryReady = refreshPlayersFromNetObjects();
        for (void* player : hookPlayers) {
            if (std::find(g_playerObjects.begin(), g_playerObjects.end(), player) == g_playerObjects.end()) {
                g_playerObjects.push_back(player);
            }
        }
        g_candidateCount.store(static_cast<int>(g_playerObjects.size()), std::memory_order_relaxed);
    }
    const bool playerSourceReady = hookSourceReady || registryReady || !g_playerObjects.empty();

    if (g_playerObjects.empty()) {
        g_playerCount.store(0, std::memory_order_relaxed);
        g_projectedCount.store(0, std::memory_order_relaxed);
        g_localTeam.store(0, std::memory_order_relaxed);
        setStatus(playerSourceReady ? Status::WaitingForMatch : Status::ScanningPlayers);
        return true;
    }

    std::vector<PlayerEntry> entries;
    entries.reserve(g_playerObjects.size());
    int localTeam = 0;
    int statsReads = 0;
    int weaponReads = 0;
    int contextTeamReads = 0;
    std::array<int, 4> rawTeamHistogram{};
    std::array<int, 4> selectedTeamHistogram{};
    std::array<int, 4> contextTeamHistogram{};
    auto bucket = [](std::uint8_t team) -> std::size_t {
        return team <= 2 ? static_cast<std::size_t>(team) : 3u;
    };

    for (void* player : g_playerObjects) {
        if (!classMatches(player, g_runtime.playerClass)) continue;
        PlayerEntry entry{};
        entry.object = player;
        if (!readPlayerSnapshot(player, entry.snapshot)) continue;
        if (entry.snapshot.statsValid) ++statsReads;
        if (entry.snapshot.weaponValid) ++weaponReads;
        if (entry.snapshot.contextTeamValid) ++contextTeamReads;
        ++rawTeamHistogram[bucket(entry.snapshot.rawTeam)];
        ++selectedTeamHistogram[bucket(entry.snapshot.team)];
        if (entry.snapshot.contextTeamValid) ++contextTeamHistogram[bucket(entry.snapshot.contextTeam)];
        if (entry.snapshot.localOwned && validTeam(entry.snapshot.team)) localTeam = entry.snapshot.team;
        entries.push_back(entry);
    }
    if (!validTeam(static_cast<std::uint8_t>(localTeam))) {
        localTeam = g_localTeam.load(std::memory_order_relaxed);
    } else {
        g_localTeam.store(localTeam, std::memory_order_relaxed);
    }
    g_statsReadCount.store(statsReads, std::memory_order_relaxed);
    g_weaponReadCount.store(weaponReads, std::memory_order_relaxed);

    const auto now = std::chrono::steady_clock::now();
    const bool frameLogDue = now - g_lastFrameLog >= std::chrono::seconds(5);
    auto logEntries = [&](const char* phase) {
        if (!frameLogDue) return;
        for (const PlayerEntry& entry : entries) {
            const PlayerSnapshot& p = entry.snapshot;
            __android_log_print(ANDROID_LOG_INFO, kLogTag,
                                "ESP player diag[%s]: player=%p local=%d rawTeam=%u contextTeam=%u contextValid=%d selectedTeam=%u hp=%u/%u stats=%d parts=%p nrk=%p weapon=%p weaponValid=%d",
                                phase, entry.object, p.localOwned ? 1 : 0,
                                static_cast<unsigned>(p.rawTeam), static_cast<unsigned>(p.contextTeam),
                                p.contextTeamValid ? 1 : 0, static_cast<unsigned>(p.team),
                                static_cast<unsigned>(p.hp), static_cast<unsigned>(p.maxHp),
                                p.statsValid ? 1 : 0, p.positionContainer, p.weaponPart,
                                p.activeWeapon, p.weaponValid ? 1 : 0);
        }
    };

    if (!validTeam(static_cast<std::uint8_t>(localTeam))) {
        g_playerCount.store(0, std::memory_order_relaxed);
        g_projectedCount.store(0, std::memory_order_relaxed);
        setStatus(Status::WaitingForMatch, "local team not resolved");
        if (frameLogDue) {
            __android_log_print(ANDROID_LOG_INFO, kLogTag,
                                "ESP team diag: local unresolved raw[0=%d 1=%d 2=%d other=%d] context[0=%d 1=%d 2=%d other=%d] selected[0=%d 1=%d 2=%d other=%d] contextReads=%d",
                                rawTeamHistogram[0], rawTeamHistogram[1], rawTeamHistogram[2], rawTeamHistogram[3],
                                contextTeamHistogram[0], contextTeamHistogram[1], contextTeamHistogram[2], contextTeamHistogram[3],
                                selectedTeamHistogram[0], selectedTeamHistogram[1], selectedTeamHistogram[2], selectedTeamHistogram[3],
                                contextTeamReads);
            logEntries("no-local-team");
            g_lastFrameLog = now;
        }
        return true;
    }

    // Screen projection is re-evaluated for EVERY presentation. UnityMain only
    // publishes camera identity; safely re-copy matrix values here so the early
    // player's update snapshot cannot add an extra camera-frame delay.
    CameraMatrixCache cameraMatrices{};
    std::uintptr_t cameraObject = 0;
    std::uintptr_t nativeCamera = 0;
    if (!readCameraForPresent(cameraMatrices, nativeCamera)) {
        setStatus(Status::WaitingForMatch, "waiting for fresh presentation camera");
        if (frameLogDue) {
            __android_log_print(ANDROID_LOG_WARN, kLogTag,
                "ESP present camera unavailable: reads=%llu rejects=%llu mainAgeMs=%d getters=0 oldBoxes=0",
                static_cast<unsigned long long>(g_presentCameraReads.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(g_presentCameraRejects.load(std::memory_order_relaxed)),
                g_mainCameraAgeMs.load(std::memory_order_relaxed));
            g_lastFrameLog = now;
        }
        return true;
    }

    // Component_GetTransform(Camera) remains disabled: it caused the confirmed
    // team-selection transition crash in Unity's native component chain.
    int enemyCandidates = 0;
    int skipLocal = 0;
    int skipInvalidTeam = 0;
    int skipSameTeam = 0;
    int skipDead = 0;
    int skipNoParts = 0;
    int skipBadPosition = 0;
    int skipProjection = 0;
    int skipBoxSize = 0;
    bool projectionProbeLogged = false;

    for (const PlayerEntry& entry : entries) {
        const PlayerSnapshot& snapshot = entry.snapshot;
        if (snapshot.localOwned) { ++skipLocal; continue; }
        if (!validTeam(snapshot.team)) { ++skipInvalidTeam; continue; }
        if (snapshot.team == localTeam) { ++skipSameTeam; continue; }
        if (snapshot.statsValid && snapshot.hp == 0) { ++skipDead; continue; }
        ++enemyCandidates;
        if (!snapshot.positionContainerValid) { ++skipNoParts; continue; }

        // No managed/native position getter is allowed on UnityGfxDeviceW.
        Vec3 feet{};
        if (!readPlayerPoseSnapshot(entry.object, feet)) { ++skipBadPosition; continue; }
        if (!std::isfinite(feet.x) || !std::isfinite(feet.y) || !std::isfinite(feet.z) ||
            std::fabs(feet.x) >= 100000.0f || std::fabs(feet.y) >= 100000.0f ||
            std::fabs(feet.z) >= 100000.0f) {
            ++skipBadPosition;
            continue;
        }

        const Vec3 head{feet.x, feet.y + offsets::managed::kFallbackPlayerHeight, feet.z};
        Vec3 feetScreen{};
        Vec3 headScreen{};
        Vec4 feetView{};
        Vec4 feetClip{};
        Vec4 headView{};
        Vec4 headClip{};
        const bool feetProjected = project(cameraMatrices, feet, width, height, feetScreen,
                                           &feetView, &feetClip);
        const bool headProjected = project(cameraMatrices, head, width, height, headScreen,
                                           &headView, &headClip);
        if (!feetProjected || !headProjected) {
            if (frameLogDue && !projectionProbeLogged) {
                __android_log_print(ANDROID_LOG_INFO, kLogTag,
                                    "ESP matrix projection probe: feetOk=%d headOk=%d worldFeet=(%.3f,%.3f,%.3f) worldHead=(%.3f,%.3f,%.3f) feetView=(%.3f,%.3f,%.3f,%.3f) feetClip=(%.3f,%.3f,%.3f,%.3f) headView=(%.3f,%.3f,%.3f,%.3f) headClip=(%.3f,%.3f,%.3f,%.3f) viewDiag=(%.3f,%.3f,%.3f,%.3f) projDiag=(%.3f,%.3f,%.3f,%.3f) screen=%dx%d camera=%p",
                                    feetProjected ? 1 : 0, headProjected ? 1 : 0,
                                    feet.x, feet.y, feet.z, head.x, head.y, head.z,
                                    feetView.x, feetView.y, feetView.z, feetView.w,
                                    feetClip.x, feetClip.y, feetClip.z, feetClip.w,
                                    headView.x, headView.y, headView.z, headView.w,
                                    headClip.x, headClip.y, headClip.z, headClip.w,
                                    matrixAt(cameraMatrices.worldToCamera, 0, 0),
                                    matrixAt(cameraMatrices.worldToCamera, 1, 1),
                                    matrixAt(cameraMatrices.worldToCamera, 2, 2),
                                    matrixAt(cameraMatrices.worldToCamera, 3, 3),
                                    matrixAt(cameraMatrices.projection, 0, 0),
                                    matrixAt(cameraMatrices.projection, 1, 1),
                                    matrixAt(cameraMatrices.projection, 2, 2),
                                    matrixAt(cameraMatrices.projection, 3, 3),
                                    width, height, reinterpret_cast<void*>(nativeCamera));
                projectionProbeLogged = true;
            }
            ++skipProjection;
            continue;
        }

        const float boxHeight = std::fabs(feetScreen.y - headScreen.y);
        if (boxHeight < 8.0f || boxHeight > static_cast<float>(height) * 1.5f) {
            ++skipBoxSize;
            continue;
        }
        const float boxWidth = boxHeight * 0.48f;
        const float centerX = (feetScreen.x + headScreen.x) * 0.5f;
        boxes.push_back({centerX - boxWidth * 0.5f, std::min(feetScreen.y, headScreen.y),
                         centerX + boxWidth * 0.5f, std::max(feetScreen.y, headScreen.y),
                         true, snapshot.statsValid, snapshot.weaponValid, snapshot.team,
                         snapshot.weaponId, snapshot.magazineAmmo, snapshot.reserveAmmo,
                         snapshot.hp, snapshot.maxHp, snapshot.armor, snapshot.maxArmor});
        if (boxes.size() >= 64) break;
    }

    g_playerCount.store(enemyCandidates, std::memory_order_relaxed);
    g_projectedCount.store(static_cast<int>(boxes.size()), std::memory_order_relaxed);
    setStatus(boxes.empty() ? Status::WaitingForMatch : Status::Active);

    if (frameLogDue) {
        __android_log_print(ANDROID_LOG_INFO, kLogTag,
            "ESP present sync: source=present-native-cache reads=%llu rejects=%llu mainAgeMs=%d projection=each-present screenCacheMs=0 getters=0",
            static_cast<unsigned long long>(g_presentCameraReads.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(g_presentCameraRejects.load(std::memory_order_relaxed)),
            g_mainCameraAgeMs.load(std::memory_order_relaxed));
        __android_log_print(ANDROID_LOG_INFO, kLogTag,
                            "ESP pose cache: captures=%llu renderPositionGetters=0 cameraGetters=0",
                            static_cast<unsigned long long>(g_playerPoseCaptures.load(std::memory_order_relaxed)));
        __android_log_print(ANDROID_LOG_INFO, kLogTag,
                            "ESP player frame: source=%s hookReady=%d hookCalls=%llu total=%d ake=%zu snapshots=%zu localTeam=%d enemies=%d projected=%zu stats=%d weapon=%d contextTeam=%d rawTeams[0=%d 1=%d 2=%d x=%d] selectedTeams[0=%d 1=%d 2=%d x=%d] reject[local=%d invalidTeam=%d sameTeam=%d dead=%d noParts=%d badPos=%d projection=%d boxSize=%d] cameraObject=%p nativeCamera=%p registry=%p",
                            hookSourceReady ? "ake-update-hook" : "netobject-registry",
                            g_playerUpdateHookReady.load(std::memory_order_relaxed) ? 1 : 0,
                            static_cast<unsigned long long>(
                                g_playerUpdateHookCalls.load(std::memory_order_relaxed)),
                            g_netObjectCount.load(std::memory_order_relaxed), g_playerObjects.size(),
                            entries.size(), localTeam, enemyCandidates, boxes.size(), statsReads,
                            weaponReads, contextTeamReads,
                            rawTeamHistogram[0], rawTeamHistogram[1], rawTeamHistogram[2], rawTeamHistogram[3],
                            selectedTeamHistogram[0], selectedTeamHistogram[1], selectedTeamHistogram[2], selectedTeamHistogram[3],
                            skipLocal, skipInvalidTeam, skipSameTeam, skipDead, skipNoParts,
                            skipBadPosition, skipProjection, skipBoxSize,
                            reinterpret_cast<void*>(cameraObject),
                            reinterpret_cast<void*>(nativeCamera), g_registryObject);
        logEntries("frame");
        g_lastFrameLog = now;
    }
    return true;
}


}  // namespace detail

inline bool update(std::uintptr_t unityBase, int width, int height, std::vector<ScreenBox>& boxes) {
    using namespace std::chrono;
    const auto now = steady_clock::now();
    detail::refreshMaps();

    if (!detail::g_runtime.ready || detail::g_runtime.unityBase != unityBase) {
        if (now - detail::g_lastRetry < seconds(1)) return false;
        detail::g_lastRetry = now;
        if (!detail::initialize(unityBase)) return false;
    }

    static thread_local bool attached = false;
    if (!attached) {
        detail::g_runtime.api.threadAttach(detail::g_runtime.domain);
        attached = true;
    }
    // No 33ms screen-coordinate cache. A static world point must be projected
    // again even when actor/HP/weapon data did not change since the last tick.
    detail::g_lastFrame = now;
    return detail::produceBoxes(width, height, boxes);
}

inline bool ready() {
    return detail::g_runtime.ready;
}

inline void logDiagnostics(const char* reason = "manual") {
    __android_log_print(ANDROID_LOG_INFO, kLogTag,
                        "ESP diagnostics[%s]: profile=%s status=%s ready=%d init=%llu playerHook[ready=%d calls=%llu tracked=%d slot=%p slots=%zu] rootScans=%llu scannedMiB=%.1f netObjects=%d ake=%d enemies=%d projected=%d localTeam=%d stats=%d weapon=%d registryFailures=%llu roots[world=%p registry=%p store=%p] classes[world=%p registry=%p store=%p netObject=%p player=%p stats=%p nrk=%p qjf=%p]",
                        reason ? reason : "manual", offsets::kProfileName, statusText(), ready() ? 1 : 0,
                        static_cast<unsigned long long>(g_initAttempts.load(std::memory_order_relaxed)),
                        g_playerUpdateHookReady.load(std::memory_order_relaxed) ? 1 : 0,
                        static_cast<unsigned long long>(
                            g_playerUpdateHookCalls.load(std::memory_order_relaxed)),
                        g_hookedPlayerCount.load(std::memory_order_relaxed),
                        reinterpret_cast<void*>(detail::g_playerUpdateVtableSlot),
                        detail::g_playerUpdatePatchedSlots,
                        static_cast<unsigned long long>(g_scanCycles.load(std::memory_order_relaxed)),
                        static_cast<double>(g_scannedBytes.load(std::memory_order_relaxed)) / 1048576.0,
                        g_netObjectCount.load(std::memory_order_relaxed),
                        g_candidateCount.load(std::memory_order_relaxed),
                        g_playerCount.load(std::memory_order_relaxed),
                        g_projectedCount.load(std::memory_order_relaxed),
                        g_localTeam.load(std::memory_order_relaxed),
                        g_statsReadCount.load(std::memory_order_relaxed),
                        g_weaponReadCount.load(std::memory_order_relaxed),
                        static_cast<unsigned long long>(
                            g_registryReadFailures.load(std::memory_order_relaxed)),
                        detail::g_clientWorldObject, detail::g_registryObject, detail::g_storeObject,
                        detail::g_runtime.clientWorldClass, detail::g_runtime.registryClass,
                        detail::g_runtime.storeClass, detail::g_runtime.netObjectClass,
                        detail::g_runtime.playerClass, detail::g_runtime.statsClass,
                        detail::g_runtime.weaponPartClass, detail::g_runtime.weaponInstanceClass);
}

}  // namespace lemming::esp
