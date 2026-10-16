#pragma once

#include <cstddef>
#include <cstdint>

namespace xim::il2cpp {

// инициализация: резолвит все API-функции из libil2cpp.so
// true — успех, дальше можно пользоваться
bool init();

// ---- высокоуровневые обёртки ----
void*   domain_get();
void*   domain_assembly_open(const char* name);            // "Assembly-CSharp"
void*   assembly_get_image(void* assembly);
void*   class_from_name(void* image, const char* ns, const char* name);
void*   class_get_method(void* klass, const char* name, int argc);
void*   class_get_field(void* klass, const char* name);
size_t  field_get_offset(void* field);
void*   class_get_parent(void* klass);
const char* class_get_name(void* klass);

// статические поля (для TypeInfo)
void*   class_get_typeinfo(void* klass);   // = klass->byval_arg для reference-типов

// получить значения статического поля
template <typename T>
T field_static_get(void* klass, const char* name);

} // namespace xim::il2cpp