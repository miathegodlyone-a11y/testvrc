#include <android/log.h>
#include <dlfcn.h>
#include <string>
#include <cstring>
#include <pthread.h>
#include <stdio.h>
#include "zygisk.hpp"
#include "il2cpp_api.hpp"
#include "dobby.h"

#define TAG   "ZygiskVRC"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

static constexpr const char *VRCHAT_PKG = "com.vrchat.mobile.standalone";
static constexpr const char *LIBIL2CPP  = "libil2cpp.so";
static constexpr const char *DUMP_PATH  = "/sdcard/Download/zygisk-vrc-dump.txt";

// ── Target ────────────────────────────────────────────────────────────────────
// Obfuscated class name for the Launchpad UI element — update if wrong
static constexpr const char *LAUNCHPAD_CLASS = "UIElement1PublicCaGaCaObGa_cObVRGa_vUnique";
static constexpr Color LAUNCHPAD_COLOR = { 0.0f, 0.8f, 1.0f, 1.0f }; // cyan RGBA
// ─────────────────────────────────────────────────────────────────────────────

static Il2CppAPI g_api;

// ── Class dump ────────────────────────────────────────────────────────────────
// Writes every class in every assembly to DUMP_PATH.
// Read it with: adb pull /sdcard/Download/zygisk-vrc-dump.txt
static void dump_all_classes(Il2CppDomain *domain) {
    FILE *f = fopen(DUMP_PATH, "w");
    if (!f) { LOGE("dump: can't open %s", DUMP_PATH); return; }

    size_t asm_count = 0;
    Il2CppAssembly **assemblies = g_api.domain_get_assemblies(domain, &asm_count);

    size_t total = 0;
    for (size_t ai = 0; ai < asm_count; ai++) {
        Il2CppImage *img  = g_api.assembly_get_image(assemblies[ai]);
        const char  *aname = g_api.image_get_name(img);
        size_t       cnt   = g_api.image_get_class_count(img);

        fprintf(f, "\n=== %s (%zu classes) ===\n", aname ? aname : "?", cnt);

        for (size_t ci = 0; ci < cnt; ci++) {
            Il2CppClass *cls = g_api.image_get_class(img, ci);
            if (!cls) continue;
            const char *ns   = g_api.class_get_namespace(cls);
            const char *name = g_api.class_get_name(cls);
            fprintf(f, "  [%s] %s\n", ns && *ns ? ns : "<>", name ? name : "?");
            total++;
        }
    }

    fprintf(f, "\nTotal: %zu classes across %zu assemblies\n", total, asm_count);
    fclose(f);
    LOGI("Dump written to %s (%zu classes)", DUMP_PATH, total);
}

// ── Hook state ────────────────────────────────────────────────────────────────
struct HookCtx {
    Il2CppClass  *image_class      = nullptr;
    Il2CppClass  *component_class  = nullptr;
    Il2CppClass  *gameobject_class = nullptr;
    Il2CppClass  *transform_class  = nullptr;
    Il2CppClass  *launchpad_class  = nullptr; // the obfuscated class

    Il2CppMethod *img_set_color    = nullptr;
    Il2CppMethod *img_get_go       = nullptr;
    Il2CppMethod *go_get_transform = nullptr;
    Il2CppMethod *go_get_name      = nullptr;
    Il2CppMethod *tr_get_parent    = nullptr;
    Il2CppMethod *tr_get_go        = nullptr;
    Il2CppMethod *go_get_component = nullptr; // GetComponent(Type)
    Il2CppMethod *type_get_type    = nullptr; // Type.GetType(string)

    bool ready = false;
};
static HookCtx g_ctx;

static void (*orig_image_set_color)(void *, Color) = nullptr;

// ── Hierarchy check ───────────────────────────────────────────────────────────
// Two strategies, tried in order:
//   1. Walk transform parents; if any ancestor's class is LAUNCHPAD_CLASS → match
//   2. Walk transform parents; if any ancestor's name contains "Launchpad" → match
// Strategy 1 is more reliable once the class name is confirmed.
static bool is_launchpad_image(void *img) {
    auto *go = g_api.invoke0(g_ctx.img_get_go, img);
    if (!go) return false;

    auto *transform = g_api.invoke0(g_ctx.go_get_transform, go);
    for (int depth = 0; depth < 10 && transform; depth++) {
        auto *tgo = g_api.invoke0(g_ctx.tr_get_go, transform);
        if (!tgo) break;

        // Strategy 1: compare component class name to known launchpad class
        if (g_ctx.launchpad_class) {
            Il2CppClass *tgo_class = g_api.object_get_class(
                reinterpret_cast<Il2CppObject *>(tgo));
            if (tgo_class &&
                g_api.class_is_assignable_from(g_ctx.launchpad_class, tgo_class)) {
                return true;
            }
        }

        // Strategy 2: fallback — check GameObject name string
        auto *name_str = reinterpret_cast<Il2CppString *>(
            g_api.invoke0(g_ctx.go_get_name, tgo));
        std::string name = g_api.str(name_str);
        LOGI("  [depth %d] GO name: %s", depth, name.c_str());
        if (name.find("Launchpad") != std::string::npos ||
            name.find("launchpad") != std::string::npos) {
            return true;
        }

        transform = g_api.invoke0(g_ctx.tr_get_parent, transform);
    }
    return false;
}

// ── The hook ──────────────────────────────────────────────────────────────────
static void hook_image_set_color(void *img, Color color) {
    if (g_ctx.ready && is_launchpad_image(img)) {
        LOGI("Launchpad color intercepted — overriding");
        color = LAUNCHPAD_COLOR;
    }
    orig_image_set_color(img, color);
}

// ── Setup thread ──────────────────────────────────────────────────────────────
static void *setup_thread(void *) {
    // Wait for libil2cpp.so
    void *il2cpp_handle = nullptr;
    for (int i = 0; i < 60 && !il2cpp_handle; i++) {
        il2cpp_handle = dlopen(LIBIL2CPP, RTLD_NOLOAD | RTLD_NOW);
        if (!il2cpp_handle) sleep(1);
    }
    if (!il2cpp_handle) { LOGE("libil2cpp never loaded"); return nullptr; }
    if (!g_api.resolve(il2cpp_handle)) { LOGE("API resolve failed"); return nullptr; }
    LOGI("IL2CPP API resolved");

    sleep(3); // let Unity finish initializing

    Il2CppDomain *domain = g_api.domain_get();

    // Dump all classes for inspection — remove/comment once class names confirmed
    dump_all_classes(domain);

    // ── Resolve assemblies ────────────────────────────────────────────────────
    size_t asm_count = 0;
    Il2CppAssembly **assemblies = g_api.domain_get_assemblies(domain, &asm_count);

    Il2CppImage *ui_img   = nullptr;
    Il2CppImage *core_img = nullptr;

    // Also search for VRChat's assembly by name pattern for the launchpad class
    Il2CppImage *vrc_img  = nullptr;

    for (size_t i = 0; i < asm_count; i++) {
        Il2CppImage *img   = g_api.assembly_get_image(assemblies[i]);
        const char  *aname = g_api.image_get_name(img);
        if (!aname) continue;
        if (strcmp(aname, "UnityEngine.UI.dll")       == 0) ui_img   = img;
        if (strcmp(aname, "UnityEngine.CoreModule.dll") == 0) core_img = img;
        // VRChat's main assembly is usually "VRChat.dll" or "Assembly-CSharp.dll"
        if (strcmp(aname, "Assembly-CSharp.dll") == 0 ||
            strcmp(aname, "VRChat.dll")          == 0) vrc_img = img;
        LOGI("Assembly: %s", aname);
    }

    if (!ui_img || !core_img) { LOGE("Missing Unity assemblies"); return nullptr; }

    // ── Resolve Unity classes ─────────────────────────────────────────────────
    g_ctx.image_class      = g_api.class_from_name(ui_img,   "UnityEngine.UI", "Image");
    g_ctx.component_class  = g_api.class_from_name(core_img, "UnityEngine",    "Component");
    g_ctx.gameobject_class = g_api.class_from_name(core_img, "UnityEngine",    "GameObject");
    g_ctx.transform_class  = g_api.class_from_name(core_img, "UnityEngine",    "Transform");

    if (!g_ctx.image_class || !g_ctx.component_class ||
        !g_ctx.gameobject_class || !g_ctx.transform_class) {
        LOGE("Unity class resolution failed"); return nullptr;
    }

    // ── Try to find the launchpad class ───────────────────────────────────────
    // Try empty namespace first, then "VRC", then brute-force search all classes
    if (vrc_img) {
        g_ctx.launchpad_class = g_api.class_from_name(vrc_img, "",    LAUNCHPAD_CLASS);
        if (!g_ctx.launchpad_class)
            g_ctx.launchpad_class = g_api.class_from_name(vrc_img, "VRC", LAUNCHPAD_CLASS);
    }
    // Brute-force: scan all assemblies if still not found
    if (!g_ctx.launchpad_class) {
        LOGI("Launchpad class not found directly — scanning all assemblies");
        for (size_t ai = 0; ai < asm_count && !g_ctx.launchpad_class; ai++) {
            Il2CppImage *img = g_api.assembly_get_image(assemblies[ai]);
            size_t cnt = g_api.image_get_class_count(img);
            for (size_t ci = 0; ci < cnt && !g_ctx.launchpad_class; ci++) {
                Il2CppClass *cls  = g_api.image_get_class(img, ci);
                const char  *cname = g_api.class_get_name(cls);
                if (cname && strcmp(cname, LAUNCHPAD_CLASS) == 0) {
                    g_ctx.launchpad_class = cls;
                    LOGI("Found launchpad class in %s", g_api.image_get_name(img));
                }
            }
        }
    }
    if (!g_ctx.launchpad_class)
        LOGI("Launchpad class not found — falling back to name-string detection");

    // ── Resolve methods ───────────────────────────────────────────────────────
    g_ctx.img_set_color    = g_api.class_get_method_from_name(g_ctx.image_class,      "set_color",      1);
    g_ctx.img_get_go       = g_api.class_get_method_from_name(g_ctx.component_class,  "get_gameObject", 0);
    g_ctx.go_get_transform = g_api.class_get_method_from_name(g_ctx.gameobject_class, "get_transform",  0);
    g_ctx.go_get_name      = g_api.class_get_method_from_name(g_ctx.gameobject_class, "get_name",       0);
    g_ctx.tr_get_parent    = g_api.class_get_method_from_name(g_ctx.transform_class,  "get_parent",     0);
    g_ctx.tr_get_go        = g_ctx.img_get_go;

    if (!g_ctx.img_set_color || !g_ctx.img_get_go ||
        !g_ctx.go_get_transform || !g_ctx.tr_get_parent) {
        LOGE("Method resolution failed"); return nullptr;
    }

    // ── Install hook ──────────────────────────────────────────────────────────
    void *target = reinterpret_cast<Il2CppMethodInternal *>(g_ctx.img_set_color)->methodPointer;
    if (!target) { LOGE("set_color methodPointer null"); return nullptr; }

    DobbyHook(target, (void *)hook_image_set_color, (void **)&orig_image_set_color);
    g_ctx.ready = true;
    LOGI("Hook installed @ %p — launchpad class %s",
         target, g_ctx.launchpad_class ? "found" : "NOT found (name fallback active)");

    dlclose(il2cpp_handle);
    return nullptr;
}

// ── Zygisk module ─────────────────────────────────────────────────────────────
class VRChatModule : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        this->api = api; this->env = env;
    }
    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        const char *name = env->GetStringUTFChars(args->nice_name, nullptr);
        bool match = name && strcmp(name, VRCHAT_PKG) == 0;
        env->ReleaseStringUTFChars(args->nice_name, name);
        if (!match) { api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY); return; }
        LOGI("VRChat matched");
        api->setOption(zygisk::Option::FORCE_DENYLIST_UNMOUNT);
    }
    void postAppSpecialize(const zygisk::AppSpecializeArgs *) override {
        pthread_t tid;
        pthread_create(&tid, nullptr, setup_thread, nullptr);
        pthread_detach(tid);
    }
    void preServerSpecialize(zygisk::ServerSpecializeArgs *) override {
        api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }
private:
    zygisk::Api *api = nullptr;
    JNIEnv      *env = nullptr;
};

REGISTER_ZYGISK_MODULE(VRChatModule)
