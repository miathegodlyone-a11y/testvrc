#pragma once
#include <cstdint>
#include <string>

struct Il2CppObject   { void *klass; void *monitor; };
struct Il2CppClass;
struct Il2CppMethod;
struct Il2CppDomain;
struct Il2CppAssembly;
struct Il2CppImage;
struct Il2CppString  { Il2CppObject obj; int32_t length; uint16_t chars[1]; };
struct Color         { float r, g, b, a; };

// Il2CppMethod internal layout (standard Magisk/Zygisk builds)
struct Il2CppMethodInternal { void *methodPointer; void *invoker; const char *name; };

using f_il2cpp_domain_get                  = Il2CppDomain *(*)();
using f_il2cpp_domain_get_assemblies       = Il2CppAssembly **(*)(Il2CppDomain *, size_t *);
using f_il2cpp_assembly_get_image          = Il2CppImage *(*)(Il2CppAssembly *);
using f_il2cpp_image_get_name              = const char *(*)(Il2CppImage *);
using f_il2cpp_image_get_class_count       = size_t (*)(Il2CppImage *);
using f_il2cpp_image_get_class             = Il2CppClass *(*)(Il2CppImage *, size_t);
using f_il2cpp_class_from_name             = Il2CppClass *(*)(Il2CppImage *, const char *, const char *);
using f_il2cpp_class_get_name              = const char *(*)(Il2CppClass *);
using f_il2cpp_class_get_namespace         = const char *(*)(Il2CppClass *);
using f_il2cpp_class_get_method_from_name  = Il2CppMethod *(*)(Il2CppClass *, const char *, int);
using f_il2cpp_class_is_assignable_from    = bool (*)(Il2CppClass *, Il2CppClass *);
using f_il2cpp_object_get_class            = Il2CppClass *(*)(Il2CppObject *);
using f_il2cpp_runtime_invoke              = Il2CppObject *(*)(const Il2CppMethod *, void *, void **, Il2CppObject **);
using f_il2cpp_string_chars                = uint16_t *(*)(Il2CppString *);
using f_il2cpp_string_length               = int (*)(Il2CppString *);

struct Il2CppAPI {
    f_il2cpp_domain_get                  domain_get;
    f_il2cpp_domain_get_assemblies       domain_get_assemblies;
    f_il2cpp_assembly_get_image          assembly_get_image;
    f_il2cpp_image_get_name              image_get_name;
    f_il2cpp_image_get_class_count       image_get_class_count;
    f_il2cpp_image_get_class             image_get_class;
    f_il2cpp_class_from_name             class_from_name;
    f_il2cpp_class_get_name              class_get_name;
    f_il2cpp_class_get_namespace         class_get_namespace;
    f_il2cpp_class_get_method_from_name  class_get_method_from_name;
    f_il2cpp_class_is_assignable_from    class_is_assignable_from;
    f_il2cpp_object_get_class            object_get_class;
    f_il2cpp_runtime_invoke              runtime_invoke;
    f_il2cpp_string_chars                string_chars;
    f_il2cpp_string_length               string_length;

    bool resolve(void *handle) {
        #define R(fn) fn = (decltype(fn))dlsym(handle, "il2cpp_" #fn); if (!fn) return false
        R(domain_get);
        R(domain_get_assemblies);
        R(assembly_get_image);
        R(image_get_name);
        R(image_get_class_count);
        R(image_get_class);
        R(class_from_name);
        R(class_get_name);
        R(class_get_namespace);
        R(class_get_method_from_name);
        R(class_is_assignable_from);
        R(object_get_class);
        R(runtime_invoke);
        R(string_chars);
        R(string_length);
        #undef R
        return true;
    }

    std::string str(Il2CppString *s) {
        if (!s) return "";
        int n = string_length(s);
        uint16_t *c = string_chars(s);
        std::string out; out.reserve(n);
        for (int i = 0; i < n; i++) out += (char)c[i];
        return out;
    }

    Il2CppObject *invoke0(const Il2CppMethod *m, void *obj) {
        Il2CppObject *ex = nullptr;
        return runtime_invoke(m, obj, nullptr, &ex);
    }
    Il2CppObject *invoke1(const Il2CppMethod *m, void *obj, void *a0) {
        Il2CppObject *ex = nullptr; void *args[] = {a0};
        return runtime_invoke(m, obj, args, &ex);
    }
};
