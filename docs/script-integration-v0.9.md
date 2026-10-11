# Native scripting integration — C++ and C#

GridEngine v0.9 ships three layers of behaviour code. They are complementary,
not competing:

| Layer | Use for | Runs where |
|---|---|---|
| Grid scripts | small numeric rules on an entity (movement, counters) | interpreted, in-process |
| C++ modules | engine access, complex types, file system | compiled DLL, in-process |
| C# bridge | richer language, tooling, external data | separate `dotnet` process |

## Grid scripts

See `docs/grid-language-v0.9.md`. A script lives on the entity (`GridScene 4`
field `script`) and runs every frame through `run_on_entity`, which gives it a
read-only `delta` and read/write `position_x` / `position_y`.

## C++ modules

A module is a platform shared library exporting one symbol:

```cpp
#include "gridengine/module.hpp"

void spin_update(const gridengine::EngineApi& api, void* context,
                 gridengine::Scene& scene, std::uint64_t id, double delta) {
    auto* entity = scene.find(id);
    if (entity && entity->name == "Spinner")
        entity->transform.rotation += 90.0 * delta;
}

gridengine::Behavior spin_behavior{"spinner", &spin_update};

extern "C" __declspec(dllexport) gridengine::ModuleHandle
grid_module_entry(const gridengine::EngineApi* api) {
    return api && api->version == 1 &&
           api->register_behavior(api->context, &spin_behavior) ? 1 : 0;
}
```

Build (MSVC x64, same CRT as the engine library):

```
cl /nologo /LD /EHsc /MDd /std:c++20 /I <engine>/include ^
   spinner.cpp <engine>/build/Debug/gridengine.lib /Fe:spinner.dll
```

Drop the DLL into `<package>/modules/`. The game runtime calls
`gridengine::ModuleLoader::load_directory(root / "modules")` and invokes every
registered behavior for every entity every frame, before Grid scripts. A module
must share the engine's C++ ABI (same headers and standard library); the engine
reports module load failures on stderr and keeps running.

The reference module is `samples/modules/spinner`. The test
`gridengine_module` compiles that sample with the local MSVC, loads it and
verifies the behavior actually rotates the scene.

## C# bridge

C++ modules need the engine ABI; C# code cannot link the engine directly, so it
runs as a child process speaking a line protocol. The engine generates the
project, builds it with `dotnet build` and drives it:

```
engine  -> frame <id> <x> <y> <rotation> <delta>
bridge  -> ok <x> <y> <rotation>        (or: error <message>)
```

```cpp
gridengine::CSharpBridge bridge;
bridge.build("behavior/", "GridBehavior");   // generates + dotnet build
bridge.start();
gridengine::CSharpFrameResult frame;
if (bridge.step(entity, x, y, rotation, delta, frame)) {
    // apply frame.x, frame.y, frame.rotation to the entity
}
bridge.stop();
```

The bridge validates every frame (non-finite values and negative delta are
rejected) and keeps running after an error line, so a broken behavior cannot
kill the game. The generated `Program.cs` contains a sample behavior (drift +
spin) that is meant to be replaced by project logic.

The test `gridengine_csharp` builds and runs the generated project and checks
the round trip, including the rejection of an invalid frame.

## What is intentionally not there

- No shared memory or sockets between C# and the engine: the line protocol is
  enough for per-frame behaviour and keeps the failure modes simple.
- No hot reload for C++ modules: a DLL is loaded once at startup.
- C++ modules and the C# bridge are not yet compiled from the editor UI; both
  are driven by the engine API, the CLI and tests.
