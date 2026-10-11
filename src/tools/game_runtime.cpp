// GridEngine game runtime.
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.
//
// This file is the source of the executable produced by "export with build":
// the exporter copies it into the package and compiles it against the engine
// library and SDL3 with the platform toolchain (Windows x64). At runtime it
// loads the project descriptor and the first scene, then steps the simulation
// and renders it every frame.
//
// Usage:
//   GridEngineGame.exe                        run from the packaged build.gridcfg
//   GridEngineGame.exe --project <file.grid>  force a project descriptor
//   GridEngineGame.exe --frames N            run N fixed frames, then exit
//                                            (use SDL_VIDEODRIVER=offscreen
//                                             for headless verification)

#include "gridengine/audio_device.hpp"
#include "gridengine/grid.hpp"
#include "gridengine/module.hpp"
#include "gridengine/project.hpp"
#include "gridengine/scene.hpp"
#include "gridengine/texture_gpu.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct Settings {
    std::string project;
    int width{960};
    int height{600};
    bool vsync{true};
    int fps_limit{0};
};

Settings read_settings(const std::filesystem::path& cfg) {
    Settings s;
    std::ifstream in(cfg, std::ios::binary);
    if (!in) return s;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto at = line.find('=');
        if (at == std::string::npos) continue;
        const auto key = line.substr(0, at);
        const auto value = line.substr(at + 1);
        try {
            if (key == "project") s.project = value;
            else if (key == "window_width") s.width = std::stoi(value);
            else if (key == "window_height") s.height = std::stoi(value);
            else if (key == "vsync") s.vsync = value == "true";
            else if (key == "fps_limit") s.fps_limit = std::stoi(value);
        } catch (...) {
            // Malformed numbers keep their default: a bad config must not
            // prevent the game from starting.
        }
    }
    s.width = std::clamp(s.width, 320, 3840);
    s.height = std::clamp(s.height, 240, 2160);
    s.fps_limit = std::clamp(s.fps_limit, 0, 1000);
    return s;
}

std::filesystem::path first_scene(const std::filesystem::path& root) {
    std::error_code ec;
    if (!std::filesystem::is_directory(root / "scenes", ec)) return {};
    std::vector<std::filesystem::path> scenes;
    for (const auto& entry : std::filesystem::directory_iterator(root / "scenes", ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".scene") scenes.push_back(entry.path());
    }
    if (ec || scenes.empty()) return {};
    std::sort(scenes.begin(), scenes.end());
    return scenes.front();
}

void run_frame(gridengine::Scene& scene, gridengine::ModuleLoader& modules, double delta) {
    // Native C++ behaviors run first, then Grid scripts, then the physics-free
    // motion integration. Native modules can therefore be stepped from scripts.
    for (const auto id : scene.entities()) modules.run_behaviors(scene, id, delta);
    for (const auto id : scene.entities()) {
        const auto* entity = scene.find(id);
        if (!entity || entity->script.empty()) continue;
        const auto result = gridengine::grid::run_on_entity(entity->script, scene, id, delta);
        if (!result.success) {
            std::fprintf(stderr, "[gridgame] script error on %s: %s\n",
                         entity->name.c_str(), result.diagnostic.message.c_str());
        }
    }
    if (!scene.update(delta)) {
        std::fprintf(stderr, "[gridgame] scene update rejected\n");
        std::exit(4);
    }
    // Sprite sheet framesets advance after motion, so animated characters walk
    // and idle exactly in step with their movement.
    scene.advance_animations(delta);
}

// The package root texture paths are resolved against; set before the first
// draw so texture loading never depends on the process working directory.
std::filesystem::path g_root;

void draw_scene(const gridengine::Scene& scene, gridengine::GLTextureCache& textures,
                SDL_Window* window) {
    glClearColor(0.016f, 0.024f, 0.043f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(window, &width, &height);
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, static_cast<double>(width), static_cast<double>(height), 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glColor3f(0.10f, 0.26f, 0.55f);
    glLineWidth(1.0f);
    glBegin(GL_LINES);
    for (int x = 0; x <= width; x += 64) { glVertex2i(x, 0); glVertex2i(x, height); }
    for (int y = 0; y <= height; y += 64) { glVertex2i(0, y); glVertex2i(width, y); }
    glEnd();

    // Sprites: real textures when the file loads, tinted quads as fallback.
    for (const auto id : scene.entities()) {
        const auto* entity = scene.find(id);
        if (!entity || !entity->sprite) continue;
        textures.draw(*entity->sprite, scene.world_transform(id), g_root);
    }
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path project_override;
    long frames = -1;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--project" && i + 1 < argc) project_override = argv[++i];
        else if (arg == "--frames" && i + 1 < argc) frames = std::strtol(argv[++i], nullptr, 10);
        else if (arg == "--help") {
            std::printf("GridEngineGame [--project file.grid] [--frames N]\n");
            return 0;
        }
    }
    if (frames == 0) frames = -1;

    // The package layout is <root>/bin/GridEngineGame.exe with the project data
    // next to the executable's parent directory.
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::fprintf(stderr, "[gridgame] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    std::error_code ec;
    auto root = std::filesystem::current_path(ec);
    {
        std::error_code exe_ec;
        const auto* base = SDL_GetBasePath();
        if (base) {
            const auto binary = std::filesystem::path(base);
            root = std::filesystem::weakly_canonical(binary.parent_path().parent_path(), exe_ec);
        }
    }

    Settings settings = read_settings(root / "build.gridcfg");
    std::filesystem::path project_file = project_override.empty()
        ? root / settings.project : std::filesystem::path(project_override);
    auto project = gridengine::open_project(project_file);
    if (!project) {
        std::fprintf(stderr, "[gridgame] cannot open project '%s': %s\n",
                     project_file.string().c_str(), project.error.c_str());
        return 2;
    }
    const auto scene_path = first_scene(root);
    gridengine::Scene scene;
    if (!scene_path.empty()) {
        auto loaded = gridengine::Scene::load(scene_path);
        if (!loaded) {
            std::fprintf(stderr, "[gridgame] cannot load scene '%s'\n", scene_path.string().c_str());
            return 3;
        }
        scene = std::move(*loaded);
    }

    // Native C++ modules (packages/<root>/modules/*.dll) are optional: any
    // behavior they register runs for every entity every frame.
    gridengine::ModuleLoader modules;
    const std::size_t module_count = modules.load_directory(root / "modules");
    g_root = root;

    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    if (!std::getenv("SDL_VIDEODRIVER")) {
        window = SDL_CreateWindow(project.project.name.c_str(), settings.width, settings.height,
                                  SDL_WINDOW_OPENGL);
        if (window) {
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
            context = SDL_GL_CreateContext(window);
            if (context) SDL_GL_MakeCurrent(window, context);
        }
    }

    gridengine::GLTextureCache textures;
    std::size_t texture_uploads = 0;
    if (context) {
        for (const auto id : scene.entities()) {
            const auto* entity = scene.find(id);
            if (!entity || !entity->sprite || entity->sprite->texture.empty()) continue;
            std::string error;
            if (textures.acquire(entity->sprite->texture, root, error)) ++texture_uploads;
        }
    }

    // Audio: real SDL3 playback of entity audio sources (WAV). Failures are
    // collected and reported like the texture errors instead of aborting.
#if defined(GRIDENGINE_WITH_AUDIO)
    gridengine::AudioDevice audio;
    std::string audio_error;
    const bool audio_ready = audio.initialize(audio_error);
    if (!audio_ready)
        std::fprintf(stderr, "[gridgame] audio not started: %s\n", audio_error.c_str());
    std::size_t audio_voices = 0;
    if (audio_ready) {
        gridengine::AudioListenerConfig listener;
        listener.position = {static_cast<double>(settings.width) * 0.5,
                             static_cast<double>(settings.height) * 0.5};
        listener.reference_distance = 200.0;
        listener.max_distance = 2000.0;
        listener.rolloff = 1.0;
        audio.set_listener(listener);
        for (const auto id : scene.entities()) {
            const auto* entity = scene.find(id);
            if (!entity || !entity->audio || entity->audio->resource.empty()) continue;
            const auto world = scene.world_transform(id);
            std::string error;
            if (audio.play(*entity->audio, root, world.position, true, error)) ++audio_voices;
        }
    }
#else
    std::size_t audio_voices = 0;
#endif
    std::fprintf(stderr, "[gridgame] window=%p context=%p textures=%zu voices=%zu\n",
                 static_cast<void*>(window), static_cast<void*>(context), texture_uploads, audio_voices);

    const double delta = settings.fps_limit > 0 ? 1.0 / static_cast<double>(settings.fps_limit) : 1.0 / 60.0;
    long frame = 0;
    bool running = true;
    while (running) {
        if (frames >= 0 && frame >= frames) break;
        SDL_Event event;
        while (window && SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) running = false;
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) running = false;
        }
        run_frame(scene, modules, delta);
#if defined(GRIDENGINE_WITH_AUDIO)
        audio.update();
#endif
        if (window && context) draw_scene(scene, textures, window);
        if (window) SDL_GL_SwapWindow(window);
        ++frame;
        if (window) SDL_Delay(static_cast<Uint32>(delta * 1000.0));
    }

    // The summary doubles as the machine-readable contract used by the export
    // test: pos_x is the world X of the first entity carrying a Grid script.
    double pos_x = 0.0;
    for (const auto id : scene.entities()) {
        const auto* entity = scene.find(id);
        if (entity && !entity->script.empty()) {
            pos_x = scene.world_transform(id).position.x;
            break;
        }
    }
    for (const auto* reason : modules.failed_modules())
        std::fprintf(stderr, "[gridgame] module not loaded: %s\n", reason ? reason : "");
    for (const auto& reason : textures.failed_paths())
        std::fprintf(stderr, "[gridgame] texture not loaded: %s\n", reason.c_str());
#if defined(GRIDENGINE_WITH_AUDIO)
    for (const auto& reason : audio.failures())
        std::fprintf(stderr, "[gridgame] audio not played: %s\n", reason.c_str());
#endif
    std::printf("GRIDGAME_OK frames=%ld entities=%zu modules=%zu textures=%zu voices=%zu pos_x=%.6f\n",
                 frame, scene.entities().size(), module_count, texture_uploads, audio_voices, pos_x);
    if (context) SDL_GL_DestroyContext(context);
    if (window) SDL_DestroyWindow(window);
#if defined(GRIDENGINE_WITH_AUDIO)
    audio.shutdown();
#endif
    SDL_Quit();
    return 0;
}
