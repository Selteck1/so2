#include "core/memory.h"
#include "util/log.h"

#include <cstring>
#include <unistd.h>

namespace xim::mem {

static const long PAGE = sysconf(_SC_PAGESIZE);
static const long PAGE_MASK = ~(PAGE - 1);

static bool check_perms(const void* addr, std::size_t size, char want) {
    if (!addr) return false;
    uintptr_t a = reinterpret_cast<uintptr_t>(addr);
    if (a + size < a) return false;

    uintptr_t start = a & PAGE_MASK;
    uintptr_t end   = (a + size + PAGE - 1) & PAGE_MASK;

    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return false;

    char line[512];
    bool ok = false;
    while (fgets(line, sizeof(line), f)) {
        uintptr_t lo, hi; char perms[8];
        if (sscanf(line, "%lx-%lx %7s", &lo, &hi, perms) != 3) continue;
        if (lo > start) break;
        if (hi < end)   continue;
        if (perms[0] == want) ok = true;
        break;
    }
    fclose(f);
    return ok;
}

bool is_readable(const void* addr, std::size_t size) {
    return check_perms(addr, size, 'r');
}
bool is_writable(const void* addr, std::size_t size) {
    return check_perms(addr, size, 'w');
}

bool read_cstr(uintptr_t addr, char* out, std::size_t max_len) {
    if (!addr || !out || max_len == 0) return false;
    out[0] = '\0';
    std::size_t written = 0;
    while (written + 1 < max_len) {
        if (!is_readable(reinterpret_cast<const void*>(addr + written), 1)) break;
        char c = *reinterpret_cast<const char*>(addr + written);
        out[written++] = c;
        if (c == '\0') break;
    }
    out[written] = '\0';
    return written > 0;
}

bool read_il2cpp_string(uintptr_t str_obj, char* out, std::size_t max_len) {
    if (!str_obj || !out || max_len < 2) return false;
    out[0] = '\0';
    int32_t len = 0;
    if (!read(str_obj + 0x10, len)) return false;
    if (len <= 0 || len > 4096) return false;

    uintptr_t data = str_obj + 0x14;
    std::size_t max_chars = max_len - 1;
    std::size_t n = (std::size_t)len < max_chars ? (std::size_t)len : max_chars;
    for (std::size_t i = 0; i < n; ++i) {
        uint16_t wc = 0;
        if (!read(data + i * 2, wc)) { out[i] = '\0'; return i > 0; }
        out[i] = (wc < 0x80) ? (char)wc : '?';
    }
    out[n] = '\0';
    return n > 0;
}

} // namespace xim::mem