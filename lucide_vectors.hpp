#include "core/il2cpp.h"
#include "core/memory.h"
#include "offsets.hpp"
#include "util/log.h"

#include <dlfcn.h>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace xim::il2cpp {

using namespace lemming::offsets::il2cpp_api;

// ---- typedefs, соответствующие il2cpp-api.h ----
using t_domain_get          = void* (*)();
using t_domain_assembly_open= void* (*)(void*, const char*);
using t_assembly_get_image  = void* (*)(void*);
using t_class_from_name     = void* (*)(void*, const char*, const char*);
using t_class_get_method_from_name = void* (*)(void*, const char*, int);
using t_class_get_field_from_name  = void* (*)(void*, const char*);
using t_field_get_offset    = std::size_t (*)(void*);
using t_class_get_name      = const char* (*)(void*);
using t_class_get_namespace = const char* (*)(void*);
using t_class_get_parent    = void* (*)(void*);

static t_domain_get                 p_domain_get = nullptr;
static t_domain_assembly_open       p_domain_assembly_open = nullptr;
static t_assembly_get_image         p_assembly_get_image = nullptr;
static t_class_from_name            p_class_from_name = nullptr;
static t_class_get_method_from_name p_class_get_method_from_name = nullptr;
static t_class_get_field_from_name  p_class_get_field_from_name = nullptr;
static t_field_get_offset           p_field_get_offset = nullptr;
static t_class_get_name             p_class_get_name = nullptr;
static t_class_get_namespace        p_class_get_namespace = nullptr;
static t_class_get_parent           p_class_get_parent = nullptr;

static uintptr_t g_libil2cpp_base = 0;
static std::once_flag g_init_once;
static bool g_ok = false;

// ---- helpers ----

// находим базу libil2cpp.so через /proc/self/maps
static uintptr_t find_libil2cpp_base() {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[512];
    uintptr_t base = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "libil2cpp.so") && strstr(line, "r-xp")) {
            uintptr_t lo = 0;
            if (sscanf(line, "%lx-", &lo) == 1) { base = lo; break; }
        }
    }
    fclose(f);
    return base;
}

// dlsym с фоллбэком на оффсеты из профиля (для случаев, когда символов нет)
static void* resolve_sym(const char* name, uintptr_t fallback_off) {
    // основной путь: dlsym в libil2cpp.so
    void* h = dlopen("libil2cpp.so", RTLD_NOLOAD | RTLD_NOW);
    if (h) {
        void* sym = dlsym(h, name);
        if (sym) return sym;
    }
    // фоллбэк: оффсет от базы
    if (g_libil2cpp_base && fallback_off)
        return reinterpret_cast<void*>(g_libil2cpp_base + fallback_off);
    return nullptr;
}

// ---- init ----

bool init() {
    std::call_once(g_init_once, [] {
        g_libil2cpp_base = find_libil2cpp_base();
        if (!g_libil2cpp_base) {
            // il2cpp может быть ещё не загружен — не паникуем, попробуем позже
            return;
        }

        p_domain_get = reinterpret_cast<t_domain_get>(
                resolve_sym("il2cpp_domain_get", kDomainGet));
        p_domain_assembly_open = reinterpret_cast<t_domain_assembly_open>(
                resolve_sym("il2cpp_domain_assembly_open", kDomainAssemblyOpen));
        p_assembly_get_image = reinterpret_cast<t_assembly_get_image>(
                resolve_sym("il2cpp_assembly_get_image", kAssemblyGetImage));
        p_class_from_name = reinterpret_cast<t_class_from_name>(
                resolve_sym("il2cpp_class_from_name", kClassFromName));
        p_class_get_method_from_name = reinterpret_cast<t_class_get_method_from_name>(
                resolve_sym("il2cpp_class_get_method_from_name", 0));
        p_class_get_field_from_name = reinterpret_cast<t_class_get_field_from_name>(
                resolve_sym("il2cpp_class_get_field_from_name", kClassGetFieldFromName));
        p_field_get_offset = reinterpret_cast<t_field_get_offset>(
                resolve_sym("il2cpp_field_get_offset", kFieldGetOffset));
        p_class_get_name = reinterpret_cast<t_class_get_name>(
                resolve_sym("il2cpp_class_get_name", kClassGetName));
        p_class_get_namespace = reinterpret_cast<t_class_get_namespace>(
                resolve_sym("il2cpp_class_get_namespace", kClassGetNamespace));
        p_class_get_parent = reinterpret_cast<t_class_get_parent>(
                resolve_sym("il2cpp_class_get_parent", kClassGetParent));

        g_ok = p_domain_get && p_domain_assembly_open && p_assembly_get_image
               && p_class_from_name && p_class_get_field_from_name
               && p_field_get_offset;
    });
    return g_ok;
}

// ---- wrappers ----

void* domain_get() {
    return p_domain_get ? p_domain_get() : nullptr;
}

void* domain_assembly_open(const char* name) {
    void* d = domain_get();
    if (!d || !p_domain_assembly_open) return nullptr;
    return p_domain_assembly_open(d, name);
}

void* assembly_get_image(void* assembly) {
    return (assembly && p_assembly_get_image) ? p_assembly_get_image(assembly) : nullptr;
}

void* class_from_name(void* image, const char* ns, const char* name) {
    if (!image || !p_class_from_name) return nullptr;

    // кэш по (image, ns, name) — убираем O(n) поиск в hot path
    struct Key { void* img; const char* ns; const char* nm; };
    static std::unordered_map<const char*, void*> cache;
    // ключ склеиваем из адреса image и хэша строк — простая, но рабочая схема
    static std::mutex mtx;
    std::lock_guard lk(mtx);
    char keybuf[256];
    snprintf(keybuf, sizeof(keybuf), "%p|%s|%s", image, ns ? ns : "", name);
    auto it = cache.find(keybuf);
    if (it != cache.end()) return it->second;

    void* k = p_class_from_name(image, ns, name);
    // кэшируем только успешные резолвы
    if (k) cache[strdup(keybuf)] = k;
    return k;
}

void* class_get_method(void* klass, const char* name, int argc) {
    return (klass && p_class_get_method_from_name)
           ? p_class_get_method_from_name(klass, name, argc) : nullptr;
}

void* class_get_field(void* klass, const char* name) {
    return (klass && p_class_get_field_from_name)
           ? p_class_get_field_from_name(klass, name) : nullptr;
}

size_t field_get_offset(void* field) {
    return (field && p_field_get_offset) ? p_field_get_offset(field) : 0;
}

void* class_get_parent(void* klass) {
    return (klass && p_class_get_parent) ? p_class_get_parent(klass) : nullptr;
}

const char* class_get_name(void* klass) {
    return (klass && p_class_get_name) ? p_class_get_name(klass) : "";
}

} // namespace xim::il2cpp