// Reference native module: a "spinner" behavior that rotates every entity it
// is asked about. Build (Visual Studio, x64):
//
//   cl /nologo /LD /EHsc /std:c++20 /I <engine>/include ^
//      spinner.cpp <engine>/build/Debug/gridengine.lib ^
//      /Fe:modules/spinner.dll
//
// Then drop the DLL into <package>/modules/ and run the game: the loader calls
// the behavior for every entity every frame.

#include "gridengine/module.hpp"

#include <cmath>

namespace {
void spinner_update(const gridengine::EngineApi& api, void* context, gridengine::Scene& scene,
                    std::uint64_t id, double delta) {
    const auto* entity = scene.find(id);
    if (!entity) return;
    // Only entities explicitly named "Spinner" rotate, so a package can mix
    // native behaviors with plain Grid-scripted entities.
    if (entity->name != "Spinner") return;
    (void)api;
    (void)context;
    scene.find(id)->transform.rotation += 90.0 * delta;
}
gridengine::Behavior spinner_behavior{"spinner", &spinner_update};
} // namespace

extern "C" __declspec(dllexport) gridengine::ModuleHandle
grid_module_entry(const gridengine::EngineApi* api) {
    if (!api || api->version != 1) return 0;
    if (!api->register_behavior(api->context, &spinner_behavior)) return 0;
    return 1;
}
