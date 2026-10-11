// GridEngine - src/module.cpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#include "gridengine/module.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace gridengine {
namespace {

#ifdef _WIN32
using NativeLibrary = HMODULE;
NativeLibrary open_library(const std::filesystem::path& path) {
    return LoadLibraryExW(path.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
}
ModuleEntry find_entry(NativeLibrary library) {
    const auto proc = reinterpret_cast<ModuleEntry>(
        reinterpret_cast<void*>(GetProcAddress(library, "grid_module_entry")));
    return proc;
}
void close_library(NativeLibrary library) { if (library) FreeLibrary(library); }
std::string library_error() {
    const DWORD code = GetLastError();
    if (!code) return "unknown error";
    char* text = nullptr;
    const auto size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                         FORMAT_MESSAGE_IGNORE_INSERTS,
                                     nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                     reinterpret_cast<char*>(&text), 0, nullptr);
    std::string message(text, size > 0 ? static_cast<std::size_t>(size) : 0);
    if (text) LocalFree(text);
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) message.pop_back();
    return message + " (code " + std::to_string(code) + ")";
}
#else
using NativeLibrary = void*;
NativeLibrary open_library(const std::filesystem::path& path) {
    return ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
}
ModuleEntry find_entry(NativeLibrary library) {
    return reinterpret_cast<ModuleEntry>(::dlsym(library, "grid_module_entry"));
}
void close_library(NativeLibrary library) { if (library) ::dlclose(library); }
std::string library_error() { const char* text = ::dlerror(); return text ? text : "unknown error"; }
#endif

} // namespace

struct ModuleLoader::Impl {
    struct Loaded {
        NativeLibrary library{};
        ModuleHandle handle{};
    };
    std::vector<Loaded> modules;
    std::vector<Behavior*> behaviors;
    // Owns the failure strings so `failed` can expose stable const char* views.
    std::vector<std::string> failures;
    std::vector<const char*> failed;
    EngineApi api{};

    void fail(const std::string& message) {
        failures.push_back(message);
        failed.push_back(failures.back().c_str());
    }

    Impl() {
        api = {};
        api.version = 1;
        api.context = this;
        api.register_behavior = [](void* context, Behavior* behavior) {
            auto* self = static_cast<Impl*>(context);
            if (!behavior || !behavior->name || !behavior->update) return false;
            for (const auto* existing : self->behaviors)
                if (std::strcmp(existing->name, behavior->name) == 0) return false;
            self->behaviors.push_back(behavior);
            return true;
        };
        api.log = [](void*, const char* message) {
            std::fprintf(stderr, "[module] %s\n", message ? message : "");
        };
        api.scene = [](void*) -> Scene& {
            // The scene of the current frame is handed to run_behaviors.
            static Scene fallback;
            return fallback;
        };
        api.set_position = [](Scene& scene, std::uint64_t id, double x, double y) {
            if (auto* entity = scene.find(id)) {
                entity->transform.position.x = x;
                entity->transform.position.y = y;
            }
        };
        api.set_rotation = [](Scene& scene, std::uint64_t id, double degrees) {
            if (auto* entity = scene.find(id)) entity->transform.rotation = degrees;
        };
        api.world_position = [](const Scene& scene, std::uint64_t id, double* x, double* y) {
            const auto t = scene.world_transform(id);
            if (x) *x = t.position.x;
            if (y) *y = t.position.y;
            return scene.find(id) != nullptr;
        };
    }

    ~Impl() {
        for (auto& module : modules) close_library(module.library);
    }
};

ModuleLoader::ModuleLoader() : impl_(new Impl()) {}
ModuleLoader::~ModuleLoader() { delete impl_; }

std::size_t ModuleLoader::load_directory(const std::filesystem::path& directory) {
    if (!impl_) impl_ = new Impl();
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec)) return 0;
    std::size_t loaded = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec) || ec) continue;
        const auto& path = entry.path();
        const auto extension = path.extension().string();
#ifdef _WIN32
        if (extension != ".dll") continue;
#else
        if (extension != ".so") continue;
#endif
        NativeLibrary library = open_library(path);
        if (!library) {
            impl_->fail(path.string() + ": " + library_error());
            continue;
        }
        const ModuleEntry entry_point = find_entry(library);
        if (!entry_point) {
            impl_->fail(path.string() + ": missing grid_module_entry: " + library_error());
            close_library(library);
            continue;
        }
        const ModuleHandle handle = entry_point(&impl_->api);
        if (!handle) {
            impl_->fail(path.string() + ": module entry returned 0");
            close_library(library);
            continue;
        }
        impl_->modules.push_back({library, handle});
        ++loaded;
    }
    return loaded;
}

void ModuleLoader::run_behaviors(Scene& scene, std::uint64_t id, double delta) {
    if (!impl_ || impl_->behaviors.empty()) return;
    for (auto* behavior : impl_->behaviors) behavior->update(impl_->api, nullptr, scene, id, delta);
}

std::size_t ModuleLoader::behavior_count() const noexcept {
    return impl_ ? impl_->behaviors.size() : 0;
}
std::size_t ModuleLoader::module_count() const noexcept {
    return impl_ ? impl_->modules.size() : 0;
}
const std::vector<const char*>& ModuleLoader::failed_modules() const noexcept {
    static const std::vector<const char*> empty;
    return impl_ ? impl_->failed : empty;
}

} // namespace gridengine
