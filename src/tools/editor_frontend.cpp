// GridEngine editor frontend.
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.
//
// The project window follows the reference layout: a menu bar row with the
// brand and the window controls, an icon toolbar row with the display toggle,
// a scene tab row, a "Scene Editor" strip inside the viewport with the edit
// tools and the run controls, and four working areas:
//   * Hierarchy (top left)   - scene tree, selection, search, add/duplicate/
//                              delete in the header
//   * Resources (bottom left)- the project's assets/, search, click to attach
//   * Scene view (top centre)- raw OpenGL: grid, tilemap, sprites, colliders,
//                              red/green axis gizmo pinned to the viewport
//   * Console (bottom centre)- Output / Errors / Warnings / Log tabs, command
//                              line with a send button
//   * Inspector (right)      - tab header, search, transform, sprite, collider,
//                              motion, camera, audio (GridScene 7), Grid script
// Three floating windows live on top: the node picker ("Добавить узел"), the
// "Frames - Animation Sequence" editor and the "New Grid script" dialog.
// Play steps the simulation with real audio; Stop restores the authored scene.
// The window controls (minimize / maximize / close) act on the real window.
//
// Usage:
//   gridengine_editor_view [--project <file.grid>]

#include "gridengine/editor.hpp"
#include "gridengine/engine.hpp"
#include "gridengine/export.hpp"
#include "gridengine/project.hpp"
#include "gridengine/scene.hpp"
#include "gridengine/texture_gpu.hpp"
#include "gridengine/tilemap.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_opengl2.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

namespace fs = std::filesystem;

// ---------------------------------------------------------------- palette ---
// Dark slate + mint, matching the reference art. One mint accent carries the
// brand, play controls, active tabs and selections; everything else is white
// type on near-black panels.
constexpr ImVec4 kBlack{0.043f, 0.055f, 0.067f, 1.0f};
constexpr ImVec4 kStage{0.055f, 0.070f, 0.082f, 1.0f};
constexpr ImVec4 kPanel{0.082f, 0.102f, 0.118f, 1.0f};
constexpr ImVec4 kPanelHeader{0.098f, 0.121f, 0.137f, 1.0f};
constexpr ImVec4 kPanelHover{0.113f, 0.184f, 0.157f, 1.0f};
constexpr ImVec4 kPanelActive{0.145f, 0.239f, 0.200f, 1.0f};
constexpr ImVec4 kMint{0.243f, 0.812f, 0.557f, 1.0f};
constexpr ImVec4 kMintDim{0.153f, 0.435f, 0.325f, 1.0f};
// The muted slate teal of the reference mark, used for the "Engine" half of
// the brand and the teal segment of the G, exactly like the launcher logo.
constexpr ImVec4 kEngine{0.315f, 0.533f, 0.510f, 1.0f};
constexpr ImVec4 kSelection{0.098f, 0.212f, 0.169f, 1.0f};
constexpr ImVec4 kBorder{0.157f, 0.196f, 0.212f, 1.0f};
constexpr ImVec4 kWhite{0.925f, 0.953f, 0.945f, 1.0f};
constexpr ImVec4 kWhiteDim{0.545f, 0.604f, 0.616f, 1.0f};
constexpr ImVec4 kWhiteFaint{0.345f, 0.404f, 0.420f, 1.0f};
constexpr ImVec4 kAxisX{0.898f, 0.325f, 0.294f, 1.0f};
constexpr ImVec4 kAxisY{0.341f, 0.671f, 0.353f, 1.0f};

constexpr const char* kFontBold = "dependencies/imgui/imgui-master/misc/fonts/Roboto-Medium.ttf";
constexpr const char* kFontText = "dependencies/imgui/imgui-master/misc/fonts/DroidSans.ttf";

// Support links: the Window, Scripts and Help menus open this Discord profile.
constexpr const char* kDiscordUrl = "https://discord.com/users/1502361530950353178";
constexpr const char* kDiscordId = "1502361530950353178";

// ------------------------------------------------------------- layout sizes --
constexpr float kMenuBar = 34.0f;      // brand + menus + window controls
constexpr float kToolBar = 38.0f;      // icon row with play controls
constexpr float kTabBar = 32.0f;       // scene tab row
constexpr float kSubBar = 30.0f;       // "Scene Editor" strip inside the viewport
constexpr float kTopBar = kMenuBar + kToolBar;
constexpr float kViewTop = kMenuBar + kToolBar + kTabBar + kSubBar;
constexpr float kHierarchyWidth = 252.0f;
constexpr float kInspectorWidth = 302.0f;
constexpr float kResourcesHeight = 208.0f;
constexpr float kConsoleHeight = 208.0f;

enum class Tool { select, paint, erase };

struct ViewCamera {
    double center_x{0.0};
    double center_y{0.0};
    double zoom{1.0};
};

struct App {
    SDL_Window* window{nullptr};
    SDL_GLContext context{nullptr};
    bool running{true};
    bool maximized{false};
    bool vsync{true};

    gridengine::Editor editor;
    gridengine::GLTextureCache textures;
    gridengine::TileMap2D tiles;
    ViewCamera camera;
    bool show_grid{true};

    fs::path project_root;
    fs::path project_file;
    fs::path scene_file;
    fs::path tiles_file;
    std::string project_name;
    bool project_open{false};
    bool dirty{false};

    Tool tool{Tool::select};
    std::uint32_t tile_id{1};

    std::vector<std::string> assets;
    std::string selected_asset;
    std::string hierarchy_filter;
    std::string resources_filter;
    std::string export_path;
    bool export_release{true};
    bool export_build{true};
    bool export_popup{false};
    std::string export_status;

    std::string command;
    std::vector<std::string> history;
    int console_tab{0}; // 0 output, 1 errors, 2 warnings, 3 log
    bool play_paused{false};
    int inspector_tab{0}; // 0 properties, 1 resources
    bool add_node_popup{false}; // "Add node" picker is open
    // "Frames - Animation Sequence" window: grid slicing of the sheet, the
    // animation (clip) list, its timeline slots, preview and sheet import.
    bool frameset_open{false};
    bool frameset_preview{false};
    int frameset_columns{4};
    int frameset_rows{1};
    int frameset_margin{0};
    int frameset_spacing{0};
    int frameset_index{-1};         // selected frameset (sheet) index
    int frameset_slot{-1};          // selected animation (clip) index
    int frameset_playhead{-1};      // selected slot of that animation
    // Name buffer of the selected animation. Keyed to the selection so the
    // field shows the name of the animation that is actually open.
    char clip_name[64]{};
    int clip_name_frameset{-1};
    int clip_name_slot{-1};
    bool frameset_sheet_open{false}; // sheet-frame strip under the timeline
    std::string frameset_import;    // image file picked with "Import"
    // "New Grid script" dialog: name + attach to the selected node.
    bool script_dialog{false};
    bool script_attach{true};
    char script_name[64]{"script"};

    // Script buffer keyed to the current selection.
    std::string script_buffer;
    std::optional<gridengine::EntityId> script_owner;

    // Scene view rectangle in window pixels, recomputed every frame.
    int view_x{0}, view_y{0}, view_w{0}, view_h{0};
};

// ------------------------------------------------------------ tiny helpers ---
bool exists_file(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

fs::path find_up(const fs::path& from, const std::string& relative) {
    std::error_code ec;
    auto dir = from;
    for (int depth = 0; depth < 8 && !dir.empty(); ++depth) {
        const auto candidate = dir / relative;
        if (exists_file(candidate) || fs::is_directory(candidate, ec)) return candidate;
        const auto parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return {};
}

void load_fonts(ImGuiIO& io) {
    std::error_code ec;
    const auto base = std::filesystem::current_path(ec);
    const auto bold = base / kFontBold;
    const auto text = base / kFontText;
    if (std::filesystem::exists(bold, ec)) {
        for (const float size : {15.0f, 17.0f, 20.0f, 28.0f})
            io.Fonts->AddFontFromFileTTF(bold.string().c_str(), size);
    } else {
        io.Fonts->AddFontDefault();
    }
    if (std::filesystem::exists(text, ec)) io.Fonts->AddFontFromFileTTF(text.string().c_str(), 16.0f);
}

// Nearest loaded font for a pixel size, so scaled text stays crisp.
ImFont* font_at(float px) {
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    ImFont* best = atlas->Fonts[0];
    float best_dist = std::fabs(best->LegacySize - px);
    for (int i = 1; i < atlas->Fonts.Size; ++i) {
        const float dist = std::fabs(atlas->Fonts[i]->LegacySize - px);
        if (dist < best_dist) {
            best_dist = dist;
            best = atlas->Fonts[i];
        }
    }
    return best;
}

gridengine::ExportToolchain resolve_toolchain(gridengine::BuildType type) {
    gridengine::ExportToolchain toolchain;
    const auto here = fs::current_path();
    toolchain.cmake = gridengine::find_cmake_executable(here);
    toolchain.engine_include_dir = find_up(here, "include");
    toolchain.game_runtime_source = find_up(here, "src/tools/game_runtime.cpp");
    toolchain.sdl_include_dir = find_up(here, "dependencies/sdl3-3.4.18/SDL3-3.4.18/include");
    const std::string config = type == gridengine::BuildType::release ? "Release" : "Debug";
    const std::string sdl_relative =
        "dependencies/sdl3-3.4.18/SDL3-3.4.18/" + config + "/SDL3-static.lib";
    for (const char* dir : {"build", "build_release_v1", "build_vs", "build_clean", "build_debug"}) {
        const auto engine = find_up(here, std::string(dir) + "/" + config + "/gridengine.lib");
        const auto sdl = find_up(here, std::string(dir) + "/" + sdl_relative);
        if (exists_file(engine) && exists_file(sdl)) {
            toolchain.gridengine_static_lib = engine;
            toolchain.sdl_static_lib = sdl;
            break;
        }
    }
    return toolchain;
}

void refresh_assets(App& app) {
    app.assets.clear();
    if (app.project_root.empty()) return;
    std::error_code ec;
    if (!fs::is_directory(app.project_root / "assets", ec)) return;
    for (const auto& entry : fs::directory_iterator(app.project_root / "assets", ec)) {
        if (!entry.is_regular_file(ec)) continue;
        app.assets.push_back(entry.path().filename().string());
    }
    std::sort(app.assets.begin(), app.assets.end());
}

bool open_project(App& app, const fs::path& descriptor) {
    auto project = gridengine::open_project(descriptor);
    if (!project) {
        app.editor.log("project: " + project.error);
        return false;
    }
    app.project_file = fs::absolute(descriptor);
    app.project_root = app.project_file.parent_path();
    app.project_name = project.project.name;
    app.export_path =
        (app.project_root.parent_path() / (project.project.name + "-export")).string();
    app.selected_asset.clear();
    app.command.clear();
    app.script_owner.reset();
    refresh_assets(app);
    app.editor.reset();
    app.textures.clear();
    app.camera = {};

    const auto scene = app.project_root / "scenes" / "main.scene";
    app.scene_file.clear();
    if (exists_file(scene)) {
        if (!app.editor.load_scene(scene)) {
            app.editor.log("scene: cannot load " + scene.string());
            return false;
        }
        app.scene_file = scene;
    } else {
        app.editor.log("scene: no scenes/main.scene, starting from an empty scene");
    }
    const auto map = app.project_root / "scenes" / "main.tiles";
    app.tiles_file.clear();
    app.tiles = gridengine::TileMap2D{};
    if (exists_file(map)) {
        if (auto loaded = gridengine::TileMap2D::load(map)) {
            app.tiles = std::move(*loaded);
            app.tiles_file = map;
            app.editor.log("tilemap: " + std::to_string(app.tiles.width()) + "x" +
                           std::to_string(app.tiles.height()) + " cells");
        } else {
            app.editor.log("tilemap: cannot load " + map.string());
        }
    }
    app.dirty = false;
    app.project_open = true;
    app.editor.log("project: opened " + project.project.name);
    return true;
}

bool save_scene(App& app) {
    if (app.scene_file.empty()) return false;
    if (!app.editor.save_scene(app.scene_file)) {
        app.editor.log("scene: save failed");
        return false;
    }
    if (!app.tiles.empty() && !app.tiles_file.empty()) {
        if (!app.tiles.save(app.tiles_file)) app.editor.log("tilemap: save failed");
    }
    app.dirty = false;
    app.editor.log("scene: saved " + app.scene_file.filename().string());
    return true;
}

// Executes a console command; every command is also echoed to the log so the
// console behaves like a real tool, not a decoration.
void run_command(App& app) {
    if (app.command.empty()) return;
    const std::string line = "> " + app.command;
    const std::string text = app.command;
    app.command.clear();
    if (text == "help") {
        app.editor.log(line);
        app.editor.log("commands: help, save, play, stop, select <name>, add, delete, frameset, clear");
    } else if (text == "save") {
        app.editor.log(line);
        save_scene(app);
    } else if (text == "play") {
        app.editor.log(line);
        app.editor.start_play();
        app.play_paused = false;
    } else if (text == "stop") {
        app.editor.log(line);
        app.editor.stop_play();
        app.play_paused = false;
    } else if (text == "clear") {
        app.editor.log(line);
        app.editor.clear_console();
    } else if (text == "add") {
        app.editor.log(line);
        (void)app.editor.create_entity("Entity");
        app.dirty = true;
    } else if (text == "delete") {
        app.editor.log(line);
        if (app.editor.remove_selected()) app.dirty = true;
    } else if (text == "frameset") {
        app.editor.log(line);
        app.frameset_open = true;
    } else if (text.rfind("select ", 0) == 0) {
        app.editor.log(line);
        const std::string name = text.substr(7);
        for (const auto id : app.editor.scene().entities()) {
            const auto* entity = app.editor.scene().find(id);
            if (entity && entity->name == name) (void)app.editor.select(id);
        }
    } else {
        app.editor.log(line);
        app.editor.log("unknown command: " + text + " (type help)");
    }
}

// ------------------------------------------------------------- icon library --
// Outline glyphs in the reference style, all centred in a square of side s.
void fill_arc_band(ImDrawList* draw, ImVec2 c, float r_inner, float r_outer,
                   float a0, float a1, ImU32 col, int segs) {
    for (int i = 0; i < segs; ++i) {
        const float t0 = a0 + (a1 - a0) * static_cast<float>(i) / segs;
        const float t1 = a0 + (a1 - a0) * static_cast<float>(i + 1) / segs;
        draw->AddQuadFilled(
            {c.x + std::cos(t0) * r_outer, c.y + std::sin(t0) * r_outer},
            {c.x + std::cos(t1) * r_outer, c.y + std::sin(t1) * r_outer},
            {c.x + std::cos(t1) * r_inner, c.y + std::sin(t1) * r_inner},
            {c.x + std::cos(t0) * r_inner, c.y + std::sin(t0) * r_inner}, col);
    }
}

void stroke_ellipse(ImDrawList* draw, ImVec2 c, float rx, float ry, ImU32 col, float th) {
    draw->PathClear();
    for (int i = 0; i <= 28; ++i) {
        const double a = 6.2831853 * i / 28.0;
        draw->PathLineTo({c.x + static_cast<float>(std::cos(a)) * rx,
                          c.y + static_cast<float>(std::sin(a)) * ry});
    }
    draw->PathStroke(col, th, ImDrawFlags_Closed);
}

// The small "G" used by the brand in the menu bar, like the launcher logo: a
// white ring that opens on the right, closed by a slate-teal segment and bar.
void draw_mini_g(ImDrawList* draw, ImVec2 c, float s, ImU32 col, ImU32 teal_col) {
    const float R = s * 0.5f;
    const float r = s * 0.30f;
    const float open = 0.52f;
    fill_arc_band(draw, c, r, R, open, 6.2831853f - open, col, 28);
    fill_arc_band(draw, c, r, R, -open, open, teal_col, 10);
    const float bar_h = (R - r) * 0.95f;
    draw->AddRectFilled({c.x + r * 0.55f, c.y - bar_h * 0.5f}, {c.x + R, c.y + bar_h * 0.5f},
                        teal_col);
}

void draw_icon(ImDrawList* draw, int icon, ImVec2 c, float s, ImU32 col) {
    const float th = std::max(1.5f, s * 0.10f);
    switch (icon) {
    case 0: { // new file: document with folded corner and a plus badge
        const float w = s * 0.64f, h = s * 0.82f, fold = w * 0.26f;
        const ImVec2 min{c.x - w * 0.5f, c.y - h * 0.5f};
        draw->PathClear();
        draw->PathLineTo(min);
        draw->PathLineTo({min.x + w - fold, min.y});
        draw->PathLineTo({min.x + w, min.y + fold});
        draw->PathLineTo({min.x + w, min.y + h});
        draw->PathLineTo({min.x, min.y + h});

        draw->PathStroke(col, th, ImDrawFlags_Closed);
        draw->AddLine({min.x + w - fold, min.y}, {min.x + w - fold, min.y + fold}, col, th * 0.8f);
        draw->AddLine({min.x + w - fold, min.y + fold}, {min.x + w, min.y + fold}, col, th * 0.8f);
        const float p = s * 0.15f;
        const ImVec2 pc{min.x + w, min.y + h};
        draw->AddLine({pc.x - p * 1.4f, pc.y}, {pc.x - p * 0.2f, pc.y}, col, th * 0.85f);
        draw->AddLine({pc.x - p * 0.8f, pc.y - p * 0.6f}, {pc.x - p * 0.8f, pc.y + p * 0.6f}, col,
                      th * 0.85f);
        break;
    }
    case 1: { // folder
        const float w = s * 0.88f, h = s * 0.62f, tab = w * 0.36f;
        const ImVec2 min{c.x - w * 0.5f, c.y - h * 0.30f};
        draw->PathClear();
        draw->PathLineTo(min);
        draw->PathLineTo({min.x + tab, min.y});
        draw->PathLineTo({min.x + tab + w * 0.12f, min.y + h * 0.24f});
        draw->PathLineTo({min.x + w, min.y + h * 0.24f});
        draw->PathLineTo({min.x + w, min.y + h});
        draw->PathLineTo({min.x, min.y + h});

        draw->PathStroke(col, th, ImDrawFlags_Closed);
        break;
    }
    case 2: { // save: floppy disk
        const float w = s * 0.78f, h = s * 0.78f;
        const ImVec2 min{c.x - w * 0.5f, c.y - h * 0.5f};
        draw->AddRect({min.x, min.y}, {min.x + w, min.y + h}, col, s * 0.08f, 0, th);
        const ImVec2 slot{min.x + w * 0.62f, min.y + h * 0.30f};
        draw->AddRect({slot.x - w * 0.30f, min.y + h * 0.14f}, slot, col, s * 0.03f, 0, th * 0.85f);
        draw->AddRectFilled({min.x + w * 0.18f, min.y + h * 0.52f},
                            {min.x + w * 0.82f, min.y + h * 0.86f}, col, s * 0.02f);
        break;
    }
    case 3: { // undo: curved arrow pointing left
        draw->PathArcTo(c, s * 0.34f, -1.65f, 2.4f, 24);
        draw->PathStroke(col, th);
        const float tip_x = c.x - s * 0.34f;
        draw->AddTriangleFilled({tip_x - s * 0.16f, c.y + s * 0.10f},
                                {tip_x + s * 0.10f, c.y - s * 0.10f},
                                {tip_x + s * 0.12f, c.y + s * 0.16f}, col);
        break;
    }
    case 4: { // redo: curved arrow pointing right
        draw->PathArcTo(c, s * 0.34f, 0.95f, 4.9f, 24);
        draw->PathStroke(col, th);
        const float tip_x = c.x + s * 0.34f;
        draw->AddTriangleFilled({tip_x + s * 0.16f, c.y + s * 0.10f},
                                {tip_x - s * 0.10f, c.y - s * 0.10f},
                                {tip_x - s * 0.12f, c.y + s * 0.16f}, col);
        break;
    }
    case 5: // play
        draw->AddTriangleFilled({c.x - s * 0.26f, c.y - s * 0.34f},
                                {c.x - s * 0.26f, c.y + s * 0.34f},
                                {c.x + s * 0.38f, c.y}, col);
        break;
    case 6: // pause
        draw->AddRectFilled({c.x - s * 0.30f, c.y - s * 0.34f}, {c.x - s * 0.06f, c.y + s * 0.34f},
                            col, s * 0.04f);
        draw->AddRectFilled({c.x + s * 0.06f, c.y - s * 0.34f}, {c.x + s * 0.30f, c.y + s * 0.34f},
                            col, s * 0.04f);
        break;
    case 7: // stop
        draw->AddRectFilled({c.x - s * 0.30f, c.y - s * 0.30f}, {c.x + s * 0.30f, c.y + s * 0.30f},
                            col, s * 0.06f);
        break;
    case 8: { // gear with eight teeth
        const float r_out = s * 0.42f, r_root = s * 0.31f, r_hub = s * 0.13f;
        draw->PathClear();
        for (int i = 0; i < 8; ++i) {
            const double a0 = 6.2831853 * (i - 0.5) / 8.0;
            const double a1 = 6.2831853 * (i - 0.22) / 8.0;
            const double a2 = 6.2831853 * (i + 0.22) / 8.0;
            const double a3 = 6.2831853 * (i + 0.5) / 8.0;
            draw->PathLineTo({c.x + static_cast<float>(std::cos(a0) * r_root),
                              c.y + static_cast<float>(std::sin(a0) * r_root)});
            draw->PathLineTo({c.x + static_cast<float>(std::cos(a1) * r_out),
                              c.y + static_cast<float>(std::sin(a1) * r_out)});
            draw->PathLineTo({c.x + static_cast<float>(std::cos(a2) * r_out),
                              c.y + static_cast<float>(std::sin(a2) * r_out)});
            draw->PathLineTo({c.x + static_cast<float>(std::cos(a3) * r_root),
                              c.y + static_cast<float>(std::sin(a3) * r_root)});
        }

        draw->PathStroke(col, th, ImDrawFlags_Closed);
        draw->AddCircle(c, r_hub, col, 18, th);
        break;
    }
    case 9: { // search: magnifier
        draw->AddCircle({c.x - s * 0.10f, c.y - s * 0.10f}, s * 0.26f, col, 22, th);
        draw->AddLine({c.x + s * 0.08f, c.y + s * 0.08f}, {c.x + s * 0.32f, c.y + s * 0.32f}, col,
                      th * 1.1f);
        break;
    }
    case 10: { // move: four-way arrows
        const float arm = s * 0.30f;
        draw->AddLine({c.x - arm, c.y}, {c.x + arm, c.y}, col, th);
        draw->AddLine({c.x, c.y - arm}, {c.x, c.y + arm}, col, th);
        const float h = s * 0.14f;
        draw->AddTriangleFilled({c.x - arm - h, c.y}, {c.x - arm + h * 0.4f, c.y - h * 0.7f},
                                {c.x - arm + h * 0.4f, c.y + h * 0.7f}, col);
        draw->AddTriangleFilled({c.x + arm + h, c.y}, {c.x + arm - h * 0.4f, c.y - h * 0.7f},
                                {c.x + arm - h * 0.4f, c.y + h * 0.7f}, col);
        draw->AddTriangleFilled({c.x, c.y - arm - h}, {c.x - h * 0.7f, c.y - arm + h * 0.4f},
                                {c.x + h * 0.7f, c.y - arm + h * 0.4f}, col);
        draw->AddTriangleFilled({c.x, c.y + arm + h}, {c.x - h * 0.7f, c.y + arm - h * 0.4f},
                                {c.x + h * 0.7f, c.y + arm - h * 0.4f}, col);
        break;
    }
    case 11: { // select: cursor arrow
        draw->PathClear();
        draw->PathLineTo({c.x - s * 0.24f, c.y - s * 0.34f});
        draw->PathLineTo({c.x + s * 0.34f, c.y});
        draw->PathLineTo({c.x + s * 0.04f, c.y + s * 0.08f});
        draw->PathLineTo({c.x - s * 0.04f, c.y + s * 0.38f});

        draw->PathStroke(col, th, ImDrawFlags_Closed);
        break;
    }
    case 12: { // paint: brush (handle + bristle block)
        draw->AddLine({c.x + s * 0.26f, c.y - s * 0.30f}, {c.x - s * 0.02f, c.y - s * 0.02f}, col,
                      th * 1.2f);
        draw->PathClear();
        draw->PathLineTo({c.x - s * 0.30f, c.y - s * 0.02f});
        draw->PathLineTo({c.x - s * 0.02f, c.y + s * 0.30f});
        draw->PathLineTo({c.x + s * 0.30f, c.y - s * 0.02f});
        draw->PathLineTo({c.x + s * 0.02f, c.y - s * 0.30f});

        draw->PathStroke(col, th, ImDrawFlags_Closed);
        break;
    }
    case 13: { // erase: square with a diagonal strike
        draw->AddRect({c.x - s * 0.30f, c.y - s * 0.30f}, {c.x + s * 0.30f, c.y + s * 0.30f}, col,
                      s * 0.04f, 0, th);
        draw->AddLine({c.x - s * 0.30f, c.y + s * 0.30f}, {c.x + s * 0.30f, c.y - s * 0.30f}, col,
                      th);
        break;
    }
    case 14: { // zoom in
        draw->AddCircle({c.x - s * 0.08f, c.y - s * 0.08f}, s * 0.26f, col, 22, th);
        draw->AddLine({c.x + s * 0.10f, c.y + s * 0.10f}, {c.x + s * 0.34f, c.y + s * 0.34f}, col,
                      th * 1.1f);
        draw->AddLine({c.x - s * 0.20f, c.y - s * 0.08f}, {c.x + s * 0.04f, c.y - s * 0.08f}, col, th);
        draw->AddLine({c.x - s * 0.08f, c.y - s * 0.20f}, {c.x - s * 0.08f, c.y + s * 0.04f}, col, th);
        break;
    }
    case 15: { // zoom out
        draw->AddCircle({c.x - s * 0.08f, c.y - s * 0.08f}, s * 0.26f, col, 22, th);
        draw->AddLine({c.x + s * 0.10f, c.y + s * 0.10f}, {c.x + s * 0.34f, c.y + s * 0.34f}, col,
                      th * 1.1f);
        draw->AddLine({c.x - s * 0.20f, c.y - s * 0.08f}, {c.x + s * 0.04f, c.y - s * 0.08f}, col, th);
        break;
    }
    case 16: { // caret down
        draw->AddTriangleFilled({c.x - s * 0.30f, c.y - s * 0.12f}, {c.x + s * 0.30f, c.y - s * 0.12f},
                                {c.x, c.y + s * 0.24f}, col);
        break;
    }
    case 17: { // plus
        draw->AddLine({c.x - s * 0.30f, c.y}, {c.x + s * 0.30f, c.y}, col, th * 1.1f);
        draw->AddLine({c.x, c.y - s * 0.30f}, {c.x, c.y + s * 0.30f}, col, th * 1.1f);
        break;
    }
    case 18: { // three dots
        for (int i = -1; i <= 1; ++i)
            draw->AddCircleFilled({c.x + i * s * 0.26f, c.y}, s * 0.07f, col, 12);
        break;
    }
    case 19: { // duplicate: two overlapping rectangles
        draw->AddRect({c.x - s * 0.28f, c.y - s * 0.30f}, {c.x + s * 0.10f, c.y + s * 0.28f}, col,
                      s * 0.04f, 0, th);
        draw->PathClear();
        draw->PathLineTo({c.x - s * 0.10f, c.y - s * 0.30f});
        draw->PathLineTo({c.x + s * 0.30f, c.y - s * 0.30f});
        draw->PathLineTo({c.x + s * 0.30f, c.y + s * 0.10f});
        draw->PathLineTo({c.x + s * 0.10f, c.y + s * 0.28f});
        draw->PathStroke(col, th);
        break;
    }
    case 20: { // trash bin
        const float w = s * 0.56f, h = s * 0.58f;
        const ImVec2 min{c.x - w * 0.5f, c.y - h * 0.5f + s * 0.08f};
        draw->AddRect({min.x, min.y}, {min.x + w, min.y + h}, col, s * 0.04f, 0, th);
        draw->AddLine({c.x - w * 0.30f, c.y - h * 0.5f}, {c.x + w * 0.30f, c.y - h * 0.5f}, col, th);
        draw->AddLine({c.x, c.y - h * 0.5f}, {c.x, c.y - h * 0.5f + s * 0.08f}, col, th);
        draw->AddLine({c.x - w * 0.18f, min.y + h * 0.22f}, {c.x - w * 0.18f, min.y + h * 0.72f}, col,
                      th * 0.8f);
        draw->AddLine({c.x + w * 0.18f, min.y + h * 0.22f}, {c.x + w * 0.18f, min.y + h * 0.72f}, col,
                      th * 0.8f);
        break;
    }
    case 22: // minimize
        draw->AddLine({c.x - s * 0.28f, c.y + s * 0.08f}, {c.x + s * 0.28f, c.y + s * 0.08f}, col,
                      th * 1.1f);
        break;
    case 23: // maximize
        draw->AddRect({c.x - s * 0.24f, c.y - s * 0.24f}, {c.x + s * 0.24f, c.y + s * 0.24f}, col,
                      s * 0.03f, 0, th);
        break;
    case 24: // close
        draw->AddLine({c.x - s * 0.24f, c.y - s * 0.24f}, {c.x + s * 0.24f, c.y + s * 0.24f}, col,
                      th * 1.1f);
        draw->AddLine({c.x + s * 0.24f, c.y - s * 0.24f}, {c.x - s * 0.24f, c.y + s * 0.24f}, col,
                      th * 1.1f);
        break;
    case 25: // send chevron
        draw->PathClear();
        draw->PathLineTo({c.x - s * 0.20f, c.y - s * 0.28f});
        draw->PathLineTo({c.x + s * 0.26f, c.y});
        draw->PathLineTo({c.x - s * 0.20f, c.y + s * 0.28f});
        draw->PathStroke(col, th * 1.2f);
        break;
    case 26: // minus
        draw->AddLine({c.x - s * 0.30f, c.y}, {c.x + s * 0.30f, c.y}, col, th * 1.1f);
        break;
    case 27: // pencil: diagonal body with a tip
        draw->AddLine({c.x - s * 0.26f, c.y + s * 0.28f}, {c.x + s * 0.18f, c.y - s * 0.16f}, col,
                      th * 1.1f);
        draw->AddLine({c.x + s * 0.10f, c.y - s * 0.24f}, {c.x + s * 0.18f, c.y - s * 0.16f}, col,
                      th * 1.1f);
        draw->AddTriangleFilled({c.x - s * 0.34f, c.y + s * 0.34f}, {c.x - s * 0.26f, c.y + s * 0.28f},
                                {c.x - s * 0.28f, c.y + s * 0.20f}, col);
        break;
    case 28: // loop: circular arrow
        draw->PathArcTo(c, s * 0.32f, -2.5f, 3.0f, 24);
        draw->PathStroke(col, th);
        draw->AddTriangleFilled({c.x + s * 0.30f, c.y - s * 0.34f}, {c.x + s * 0.34f, c.y - s * 0.06f},
                                {c.x + s * 0.06f, c.y - s * 0.14f}, col);
        break;
    case 29: // once: straight arrow to the right
        draw->AddLine({c.x - s * 0.30f, c.y}, {c.x + s * 0.22f, c.y}, col, th * 1.1f);
        draw->AddTriangleFilled({c.x + s * 0.38f, c.y}, {c.x + s * 0.10f, c.y - s * 0.18f},
                                {c.x + s * 0.10f, c.y + s * 0.18f}, col);
        break;
    case 40: { // image: frame with a sun and mountains (Visual category)
        const float w = s * 0.84f, h = s * 0.68f;
        draw->AddRect({c.x - w * 0.5f, c.y - h * 0.5f}, {c.x + w * 0.5f, c.y + h * 0.5f}, col, s * 0.04f,
                      0, th);
        draw->AddCircleFilled({c.x - w * 0.24f, c.y - h * 0.18f}, s * 0.08f, col, 12);
        draw->PathClear();
        draw->PathLineTo({c.x - w * 0.38f, c.y + h * 0.30f});
        draw->PathLineTo({c.x - w * 0.08f, c.y - h * 0.02f});
        draw->PathLineTo({c.x + w * 0.08f, c.y + h * 0.14f});
        draw->PathLineTo({c.x + w * 0.26f, c.y - h * 0.08f});
        draw->PathLineTo({c.x + w * 0.38f, c.y + h * 0.30f});
        draw->PathStroke(col, th * 0.85f);
        break;
    }
    case 41: { // keyboard: rounded body, two key rows and a space bar (Input)
        const float w = s * 0.88f, h = s * 0.60f;
        draw->AddRect({c.x - w * 0.5f, c.y - h * 0.5f}, {c.x + w * 0.5f, c.y + h * 0.5f}, col, s * 0.08f,
                      0, th);
        for (int row = 0; row < 2; ++row)
            for (int k = 0; k < 3; ++k)
                draw->AddRectFilled({c.x - w * 0.34f + k * w * 0.24f + row * w * 0.05f,
                                     c.y - h * 0.26f + row * h * 0.30f},
                                    {c.x - w * 0.22f + k * w * 0.24f + row * w * 0.05f,
                                     c.y - h * 0.14f + row * h * 0.30f},
                                    col, s * 0.02f);
        draw->AddRectFilled({c.x - w * 0.20f, c.y + h * 0.20f}, {c.x + w * 0.20f, c.y + h * 0.32f}, col,
                            s * 0.02f);
        break;
    }
    case 42: // physics: filled circle
        draw->AddCircleFilled(c, s * 0.38f, col, 20);
        break;
    case 43: { // package: isometric box (Custom category)
        const float w = s * 0.72f;
        const ImVec2 top{c.x, c.y - s * 0.42f};
        const ImVec2 left{c.x - w * 0.5f, c.y - s * 0.14f};
        const ImVec2 right{c.x + w * 0.5f, c.y - s * 0.14f};
        const ImVec2 mid{c.x, c.y + s * 0.14f};
        const ImVec2 bl{c.x - w * 0.5f, c.y + s * 0.30f};
        const ImVec2 br{c.x + w * 0.5f, c.y + s * 0.30f};
        const ImVec2 bm{c.x, c.y + s * 0.44f};
        draw->PathClear();
        draw->PathLineTo(top);
        draw->PathLineTo(right);
        draw->PathLineTo(mid);
        draw->PathStroke(col, th, ImDrawFlags_Closed);
        draw->PathClear();
        draw->PathLineTo(top);
        draw->PathLineTo(left);
        draw->PathLineTo(mid);
        draw->PathStroke(col, th, ImDrawFlags_Closed);
        draw->PathClear();
        draw->PathLineTo(mid);
        draw->PathLineTo(bl);
        draw->PathLineTo(bm);
        draw->PathLineTo(br);
        draw->PathStroke(col, th, ImDrawFlags_Closed);
        break;
    }
    case 44: { // node: a square with a plus badge, for "add node"
        const float a = s * 0.30f;
        draw->AddRect({c.x - a, c.y - a}, {c.x + a, c.y + a}, col, s * 0.06f, 0, th);
        draw->AddLine({c.x + a, c.y + a}, {c.x + a + s * 0.18f, c.y + a + s * 0.18f}, col, th);
        draw->AddRectFilled({c.x + a - s * 0.04f, c.y + a - s * 0.04f},
                            {c.x + a + s * 0.26f, c.y + a + s * 0.26f}, col, s * 0.05f);
        draw->AddLine({c.x + a + s * 0.05f, c.y + a + s * 0.11f},
                      {c.x + a + s * 0.17f, c.y + a + s * 0.11f}, ImGui::GetColorU32(kBlack),
                      th * 0.9f);
        draw->AddLine({c.x + a + s * 0.11f, c.y + a + s * 0.05f},
                      {c.x + a + s * 0.11f, c.y + a + s * 0.17f}, ImGui::GetColorU32(kBlack),
                      th * 0.9f);
        break;
    }
    default: // grid: 2x2 squares
        for (int ix = 0; ix < 2; ++ix)
            for (int iy = 0; iy < 2; ++iy)
                draw->AddRect({c.x - s * 0.30f + ix * s * 0.32f, c.y - s * 0.30f + iy * s * 0.32f},
                              {c.x - s * 0.02f + ix * s * 0.32f, c.y - s * 0.02f + iy * s * 0.32f},
                              col, s * 0.03f, 0, th);
        break;
    }
}

void draw_eye(ImDrawList* draw, ImVec2 c, float s, ImU32 col, bool open) {
    stroke_ellipse(draw, c, s * 0.46f, s * 0.25f, col, std::max(1.4f, s * 0.10f));
    if (open) draw->AddCircleFilled(c, s * 0.12f, col, 14);
    else
        draw->AddLine({c.x - s * 0.34f, c.y + s * 0.24f}, {c.x + s * 0.34f, c.y - s * 0.24f}, col,
                      std::max(1.4f, s * 0.10f));
}

// Square icon button with hover/active tint, like the toolbar buttons of the
// reference. Returns true on click; disabled buttons render dimmed. An
// optional hover color overrides the default panel hover (e.g. red for close).
bool icon_button(const char* id, int icon, float size, ImU32 col, bool disabled = false,
                 const ImVec4* hover_color = nullptr) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    const ImGuiID gid = window->GetID(id);
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, {pos.x + size, pos.y + size});
    ImGui::ItemSize(bb.GetSize());
    if (!ImGui::ItemAdd(bb, gid)) return false;
    bool hovered = false, held = false;
    const bool pressed = !disabled && ImGui::ButtonBehavior(bb, gid, &hovered, &held);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (held && !disabled)
        draw->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(kPanelActive), 5.0f);
    else if (hovered && !disabled)
        draw->AddRectFilled(bb.Min, bb.Max,
                            hover_color ? ImGui::GetColorU32(*hover_color)
                                        : ImGui::GetColorU32(kPanelHover),
                            5.0f);
    const ImU32 use_col = disabled ? ImGui::GetColorU32(kWhiteFaint) : col;
    draw_icon(draw, icon, bb.GetCenter(), size * 0.62f, use_col);
    return pressed;
}

// Component section header in the Inspector: icon + label on the left, an x
// button on the right that removes the component. Returns true when the user
// asked to remove it.
bool component_header(const char* label, int icon, bool removable) {
    const ImVec2 pos = ImGui::GetCursorPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 row = ImGui::GetCursorScreenPos();
    draw_icon(draw, icon, {row.x + 9.0f, row.y + 10.0f}, 14.0f, ImGui::GetColorU32(kWhiteDim));
    draw->AddText({row.x + 24.0f, row.y + 2.0f}, ImGui::GetColorU32(kWhite), label);

    // Reserve the row in the layout first: without a submitted item the cursor
    // would move past everything drawn so far, which makes ImGui report the
    // "SetCursorPos to extend window/parent boundaries" error and leaves the
    // window in a broken interaction state.
    ImGui::Dummy({kInspectorWidth - 20.0f, 24.0f});
    bool remove = false;

    if (removable) {
        ImGui::PushID(label);
        ImGui::SetCursorPos({pos.x + kInspectorWidth - 56.0f, pos.y + 1.0f});
        remove = icon_button("##rm_comp", 24, 20, ImGui::GetColorU32(kWhiteFaint));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove component");
        ImGui::PopID();
    }

    ImGui::SetCursorPos({pos.x, pos.y + 24.0f});
    return remove;
}

// Text tab with a mint underline when active (console and inspector headers).
bool text_tab(const char* label, bool active, float height) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    const ImGuiID gid = window->GetID(label);
    const ImVec2 pos = window->DC.CursorPos;
    const float text_w = ImGui::CalcTextSize(label).x;
    const ImVec2 size{text_w + 20.0f, height};
    const ImRect bb(pos, {pos.x + size.x, pos.y + size.y});
    ImGui::ItemSize(size);
    if (!ImGui::ItemAdd(bb, gid)) return false;
    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, gid, &hovered, &held);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 col = active              ? ImGui::GetColorU32(kMint)
                      : hovered           ? ImGui::GetColorU32(kWhite)
                                          : ImGui::GetColorU32(kWhiteDim);
    draw->AddText({pos.x + 10.0f, pos.y + (height - ImGui::GetTextLineHeight()) * 0.5f}, col,
                  label);
    if (active)
        draw->AddLine({pos.x + 8.0f, pos.y + height - 2.0f}, {pos.x + size.x - 8.0f, pos.y + height - 2.0f},
                      ImGui::GetColorU32(kMint), 2.0f);
    (void)held;
    return pressed;
}

// Search field with a magnifier glyph on the left, as in the panels.
void search_field(const char* id, const char* hint, std::string& value, float width) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImVec2 pos = window->DC.CursorPos;
    window->DrawList->AddRectFilled({pos.x, pos.y + 1.0f}, {pos.x + width, pos.y + 26.0f},
                                    ImGui::GetColorU32(kBlack), 4.0f);
    window->DrawList->AddRect({pos.x, pos.y + 1.0f}, {pos.x + width, pos.y + 26.0f},
                              ImGui::GetColorU32(kBorder), 4.0f, 0, 1.0f);
    draw_icon(window->DrawList, 9, {pos.x + 14.0f, pos.y + 14.0f}, 13.0f,
              ImGui::GetColorU32(kWhiteFaint));
    ImGui::SetCursorPos({ImGui::GetCursorPosX() + 28.0f, ImGui::GetCursorPosY() + 3.0f});
    ImGui::SetNextItemWidth(width - 34.0f);
    char buffer[256]{};
    std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
    if (ImGui::InputTextWithHint(id, hint, buffer, sizeof(buffer),
                                 ImGuiInputTextFlags_AutoSelectAll)) {
        value = buffer;
    }
}

// Search field with the magnifier on the right, as in the "Add node" reference.
void search_field_right(const char* id, const char* hint, std::string& value, float width) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImVec2 pos = window->DC.CursorPos;
    window->DrawList->AddRectFilled({pos.x, pos.y + 1.0f}, {pos.x + width, pos.y + 27.0f},
                                    ImGui::GetColorU32(kBlack), 4.0f);
    window->DrawList->AddRect({pos.x, pos.y + 1.0f}, {pos.x + width, pos.y + 27.0f},
                              ImGui::GetColorU32(kBorder), 4.0f, 0, 1.0f);
    ImGui::SetCursorPos({ImGui::GetCursorPosX() + 6.0f, ImGui::GetCursorPosY() + 3.0f});
    ImGui::SetNextItemWidth(width - 34.0f);
    char buffer[256]{};
    std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
    if (ImGui::InputTextWithHint(id, hint, buffer, sizeof(buffer),
                                 ImGuiInputTextFlags_AutoSelectAll)) {
        value = buffer;
    }
    draw_icon(window->DrawList, 9, {pos.x + width - 15.0f, pos.y + 14.0f}, 13.0f,
              ImGui::GetColorU32(kWhiteFaint));
}

// ----------------------------------------------------------- scene drawing ---
void draw_grid(int width, int height, double origin_x, double origin_y) {
    glColor3f(0.094f, 0.114f, 0.125f);
    glLineWidth(1.0f);
    glBegin(GL_LINES);
    for (double x = std::fmod(origin_x, 32.0); x <= width; x += 32.0) {
        glVertex2d(x, 0.0);
        glVertex2d(x, static_cast<double>(height));
    }
    for (double y = std::fmod(origin_y, 32.0); y <= height; y += 32.0) {
        glVertex2d(0.0, y);
        glVertex2d(static_cast<double>(width), y);
    }
    glEnd();
    // Axis lines through the world origin, a touch brighter than the grid.
    glColor3f(0.176f, 0.208f, 0.220f);
    glBegin(GL_LINES);
    glVertex2d(0.0, origin_y);
    glVertex2d(static_cast<double>(width), origin_y);
    glVertex2d(origin_x, 0.0);
    glVertex2d(origin_x, static_cast<double>(height));
    glEnd();
}

void draw_tilemap(const App& app, double origin_x, double origin_y) {
    for (std::uint32_t y = 0; y < app.tiles.height(); ++y) {
        for (std::uint32_t x = 0; x < app.tiles.width(); ++x) {
            const auto cell = app.tiles.cell(x, y);
            if (!cell || cell->tile_id == 0) continue;
            const double left = origin_x + x * app.tiles.tile_width() * app.camera.zoom;
            const double top = origin_y + y * app.tiles.tile_height() * app.camera.zoom;
            const double w = app.tiles.tile_width() * app.camera.zoom;
            const double h = app.tiles.tile_height() * app.camera.zoom;
            const float r = static_cast<float>((cell->tint >> 24) & 0xff) / 255.0f;
            const float g = static_cast<float>((cell->tint >> 16) & 0xff) / 255.0f;
            const float b = static_cast<float>((cell->tint >> 8) & 0xff) / 255.0f;
            glColor3f(r, g, b);
            glBegin(GL_QUADS);
            glVertex2d(left, top);
            glVertex2d(left + w, top);
            glVertex2d(left + w, top + h);
            glVertex2d(left, top + h);
            glEnd();
            glColor3f(0.094f, 0.114f, 0.125f);
            glBegin(GL_LINE_LOOP);
            glVertex2d(left, top);
            glVertex2d(left + w, top);
            glVertex2d(left + w, top + h);
            glVertex2d(left, top + h);
            glEnd();
        }
    }
}

void draw_sprites(App& app, const gridengine::Scene& scene, double origin_x, double origin_y) {
    for (const auto id : scene.entities()) {
        const auto* entity = scene.find(id);
        if (!entity || !entity->sprite || !entity->sprite->visible) continue;
        const auto world = scene.world_transform(id);
        const double left = origin_x + world.position.x * app.camera.zoom;
        const double top = origin_y + world.position.y * app.camera.zoom;
        const double w = entity->sprite->size.x * app.camera.zoom * world.scale.x;
        const double h = entity->sprite->size.y * app.camera.zoom * world.scale.y;

        std::string error;
        const GLuint texture = app.textures.acquire(entity->sprite->texture, app.project_root, error);
        if (texture) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, texture);
            glColor3f(1.0f, 1.0f, 1.0f);
            glBegin(GL_QUADS);
            glTexCoord2f(0.0f, 0.0f); glVertex2d(left, top);
            glTexCoord2f(1.0f, 0.0f); glVertex2d(left + w, top);
            glTexCoord2f(1.0f, 1.0f); glVertex2d(left + w, top + h);
            glTexCoord2f(0.0f, 1.0f); glVertex2d(left, top + h);
            glEnd();
            glDisable(GL_TEXTURE_2D);
        } else {
            const float r = static_cast<float>((entity->sprite->color >> 24) & 0xff) / 255.0f;
            const float g = static_cast<float>((entity->sprite->color >> 16) & 0xff) / 255.0f;
            const float b = static_cast<float>((entity->sprite->color >> 8) & 0xff) / 255.0f;
            glColor3f(r, g, b);
            glBegin(GL_QUADS);
            glVertex2d(left, top);
            glVertex2d(left + w, top);
            glVertex2d(left + w, top + h);
            glVertex2d(left, top + h);
            glEnd();
            glColor3f(r * 0.6f, g * 0.6f, b * 0.6f);
            glBegin(GL_LINE_LOOP);
            glVertex2d(left, top);
            glVertex2d(left + w, top);
            glVertex2d(left + w, top + h);
            glVertex2d(left, top + h);
            glEnd();
        }

        if (entity->collider && entity->collider->enabled) {
            const double hx = entity->collider->half_extents.x * app.camera.zoom;
            const double hy = entity->collider->half_extents.y * app.camera.zoom;
            const double cx = left + w * 0.5, cy = top + h * 0.5;
            glColor3f(0.243f, 0.812f, 0.557f);
            glLineWidth(1.5f);
            glBegin(GL_LINE_LOOP);
            glVertex2d(cx - hx, cy - hy);
            glVertex2d(cx + hx, cy - hy);
            glVertex2d(cx + hx, cy + hy);
            glVertex2d(cx - hx, cy + hy);
            glEnd();
        }
    }
}

// Axis gizmo pinned to the top-right of the viewport, the way the reference
// marks scene orientation: red X to the right, green Y up, small origin ring.
void draw_viewport_gizmo(const App& app) {
    const double origin_x = static_cast<double>(app.view_w) - 48.0;
    const double origin_y = 52.0;
    const double arm = 24.0;
    const double head = 5.0;
    glLineWidth(1.5f);
    glColor3f(kAxisX.x, kAxisX.y, kAxisX.z);
    glBegin(GL_LINES);
    glVertex2d(origin_x, origin_y);
    glVertex2d(origin_x + arm, origin_y);
    glEnd();
    glBegin(GL_TRIANGLES);
    glVertex2d(origin_x + arm + head, origin_y);
    glVertex2d(origin_x + arm - head * 0.6, origin_y - head * 0.6);
    glVertex2d(origin_x + arm - head * 0.6, origin_y + head * 0.6);
    glEnd();
    glColor3f(kAxisY.x, kAxisY.y, kAxisY.z);
    glBegin(GL_LINES);
    glVertex2d(origin_x, origin_y);
    glVertex2d(origin_x, origin_y - arm);
    glEnd();
    glBegin(GL_TRIANGLES);
    glVertex2d(origin_x, origin_y - arm - head);
    glVertex2d(origin_x - head * 0.6, origin_y - arm + head * 0.6);
    glVertex2d(origin_x + head * 0.6, origin_y - arm + head * 0.6);
    glEnd();
    glColor3f(0.925f, 0.953f, 0.945f);
    glLineWidth(1.2f);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < 20; ++i) {
        const double a = 6.2831853 * i / 20.0;
        glVertex2d(origin_x + std::cos(a) * 4.0, origin_y + std::sin(a) * 4.0);
    }
    glEnd();

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const float sx = static_cast<float>(app.view_x);
    const float sy = static_cast<float>(app.view_y);
    draw->AddText({sx + static_cast<float>(origin_x + arm + head + 4.0f),
                   sy + static_cast<float>(origin_y) - 7.0f},
                  ImGui::GetColorU32(kAxisX), "X");
    draw->AddText({sx + static_cast<float>(origin_x) - 3.0f,
                   sy + static_cast<float>(origin_y - arm - head) - 18.0f},
                  ImGui::GetColorU32(kAxisY), "Y");
}

void draw_selection(const App& app) {
    const auto* entity = app.editor.inspected_entity();
    if (!entity) return;
    const auto world = app.editor.scene().world_transform(entity->id);
    const double w = (entity->sprite ? entity->sprite->size.x : 16.0) * app.camera.zoom * world.scale.x;
    const double h = (entity->sprite ? entity->sprite->size.y : 16.0) * app.camera.zoom * world.scale.y;
    const double origin_x = app.view_w * 0.5 - app.camera.center_x * app.camera.zoom;
    const double origin_y = app.view_h * 0.5 - app.camera.center_y * app.camera.zoom;
    const double left = origin_x + world.position.x * app.camera.zoom;
    const double top = origin_y + world.position.y * app.camera.zoom;
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    draw->AddRect(ImVec2(app.view_x + static_cast<float>(left), app.view_y + static_cast<float>(top)),
                  ImVec2(app.view_x + static_cast<float>(left + w), app.view_y + static_cast<float>(top + h)),
                  ImGui::GetColorU32(kMint), 0.0f, 0, 2.0f);
}

void render(App& app) {
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(app.window, &width, &height);
    glViewport(0, 0, width, height);
    glClearColor(kBlack.x, kBlack.y, kBlack.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    app.view_x = static_cast<int>(kHierarchyWidth);
    app.view_y = static_cast<int>(kViewTop);
    app.view_w = std::max(1, static_cast<int>(width - kHierarchyWidth - kInspectorWidth));
    app.view_h = std::max(1, static_cast<int>(height - kViewTop - kConsoleHeight));

    const int bottom_up = height - app.view_y - app.view_h;
    glViewport(app.view_x, bottom_up, app.view_w, app.view_h);
    glEnable(GL_SCISSOR_TEST);
    glScissor(app.view_x, bottom_up, app.view_w, app.view_h);
    glClearColor(kStage.x, kStage.y, kStage.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, static_cast<double>(app.view_w), static_cast<double>(app.view_h), 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    const double origin_x = app.view_w * 0.5 - app.camera.center_x * app.camera.zoom;
    const double origin_y = app.view_h * 0.5 - app.camera.center_y * app.camera.zoom;
    if (app.show_grid) draw_grid(app.view_w, app.view_h, origin_x, origin_y);
    draw_tilemap(app, origin_x, origin_y);
    draw_sprites(app, app.editor.scene(), origin_x, origin_y);
    draw_viewport_gizmo(app);
    glDisable(GL_SCISSOR_TEST);
    draw_selection(app);

    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
}

// ---------------------------------------------------------------- input -----
void handle_scene_input(App& app, const ImGuiIO& io) {
    if (!app.project_open || io.WantCaptureMouse) return;
    const ImVec2 mouse = io.MousePos;
    if (mouse.x < app.view_x || mouse.y < app.view_y ||
        mouse.x >= app.view_x + app.view_w || mouse.y >= app.view_y + app.view_h) return;
    const double local_x = mouse.x - static_cast<float>(app.view_x);
    const double local_y = mouse.y - static_cast<float>(app.view_y);
    const double world_x = (local_x - app.view_w * 0.5) / app.camera.zoom + app.camera.center_x;
    const double world_y = (local_y - app.view_h * 0.5) / app.camera.zoom + app.camera.center_y;

    // Tile painting / erasing with the right mouse button, like a tile editor.
    if (io.MouseDown[2] && app.tool != Tool::select && !app.tiles.empty()) {
        if (const auto cell = app.tiles.cell_at({world_x, world_y})) {
            if (app.tool == Tool::paint) {
                gridengine::TileCell value;
                value.tile_id = app.tile_id;
                value.tint = 0xffffffffu;
                if (app.tiles.set_cell(cell->first, cell->second, value)) app.dirty = true;
            } else if (app.tiles.clear_cell(cell->first, cell->second)) {
                app.dirty = true;
            }
        }
        return;
    }

    if (io.MouseClicked[0]) {
        gridengine::EntityId hit{0};
        for (const auto id : app.editor.scene().entities()) {
            const auto* entity = app.editor.scene().find(id);
            if (!entity || !entity->sprite || !entity->sprite->visible) continue;
            const auto world = app.editor.scene().world_transform(id);
            const double w = entity->sprite->size.x * world.scale.x;
            const double h = entity->sprite->size.y * world.scale.y;
            if (world_x >= world.position.x && world_x <= world.position.x + w &&
                world_y >= world.position.y && world_y <= world.position.y + h) hit = id;
        }
        if (hit) {
            (void)app.editor.select(hit);
            if (!app.selected_asset.empty()) {
                const auto extension = fs::path(app.selected_asset).extension().string();
                if (extension == ".wav") {
                    gridengine::AudioSource source;
                    source.resource = "assets/" + app.selected_asset;
                    source.volume = 1.0f;
                    source.loop = true;
                    (void)app.editor.set_audio(source);
                } else {
                    gridengine::Sprite2D sprite;
                    sprite.texture = "assets/" + app.selected_asset;
                    sprite.size = {32.0, 32.0};
                    (void)app.editor.set_sprite(sprite);
                }
                app.dirty = true;
            }
        } else {
            (void)app.editor.select(std::nullopt);
        }
    }
}

// ------------------------------------------------------------------ panels ---
// Panel chrome: header strip with a title on the left and room for icon
// buttons on the right, matching the reference panels.
bool begin_panel(const char* name, const char* title, ImVec2 pos, ImVec2 size,
                 ImGuiWindowFlags flags = 0) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    const bool open = ImGui::Begin(name, nullptr,
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                                       flags);
    ImGui::PopStyleVar();
    const float header = 30.0f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetWindowPos();
    const ImVec2 header_max{p.x + size.x, p.y + header};
    draw->AddRectFilled(p, header_max, ImGui::GetColorU32(kPanelHeader));
    draw->AddLine({p.x, header_max.y}, {header_max.x, header_max.y},
                  ImGui::GetColorU32(kBorder), 1.0f);
    draw->AddText(nullptr, ImGui::GetFontSize(), {p.x + 12.0f, p.y + 7.0f},
                  ImGui::GetColorU32(kWhite), title);
    ImGui::SetCursorPos({10.0f, header + 8.0f});
    return open;
}

void end_panel() { ImGui::End(); }

void separator_vertical() {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImVec2 pos = window->DC.CursorPos;
    window->DrawList->AddLine({pos.x + 3.0f, pos.y + 5.0f}, {pos.x + 3.0f, pos.y + 21.0f},
                              ImGui::GetColorU32(kBorder), 1.0f);
    ImGui::Dummy({7.0f, 26.0f});
}

// Opens a folder (or file) with the OS default handler, so the Window, Project
// and Scripts menus do real work instead of logging placeholders.
void open_externally(const fs::path& target) {
    std::error_code ec;
    if (!fs::exists(target, ec)) return;
    // Percent-encode the characters that would otherwise split the URL; '%' has
    // to go first, or a path with a literal % ends up double-encoded.
    std::string url = "file:///";
    char escape[4]{};
    for (const char c : target.generic_string()) {
        const bool reserved = c == '%' || c == ' ' || c == '#' || c == '?' || c == '\n' ||
                              c == '\r';
        if (reserved) {
            std::snprintf(escape, sizeof(escape), "%%%02X", static_cast<unsigned char>(c));
            url += escape;
        } else {
            url += c;
        }
    }
    SDL_OpenURL(url.c_str());
}

// Support: the Window, Scripts and Help menus all reach the author's Discord.
void open_discord(App& app) {
    SDL_OpenURL(kDiscordUrl);
    app.editor.log(std::string("help: opening Discord (") + kDiscordId + ")");
}

// Opens one of the engine's docs/*.md files with the OS handler, so the Help
// entries open the real document instead of only logging its name.
void open_docs(App& app, const char* file) {
    std::error_code ec;
    const auto path = find_up(std::filesystem::current_path(ec), std::string("docs/") + file);
    if (path.empty()) {
        app.editor.log(std::string("docs: ") + file + " not found");
        return;
    }
    open_externally(path);
    app.editor.log(std::string("docs: opening ") + file);
}

// Writes a fresh Grid script into the project and logs the result. Used by the
// Scripts menu so "New Grid script..." actually creates something editable.
void create_grid_script(App& app, std::string_view name) {
    const auto scripts = app.project_root / "scripts";
    std::error_code ec;
    fs::create_directories(scripts, ec);
    const auto file = scripts / (std::string(name) + ".gs");
    std::ofstream out(file, std::ios::binary);
    if (!out) {
        app.editor.log("script: cannot write " + file.string());
        return;
    }
    out << "# Grid script: " << name << "\n";
    out << "rule on_update {\n";
    out << "    when key_right { set velocity_x = 120 }\n";
    out << "    when key_left { set velocity_x = -120 }\n";
    out << "}\n";
    app.editor.log("script: created scripts/" + file.filename().string());
    refresh_assets(app);
}

void draw_menu_bar(App& app) {
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0.0f, 0.0f});
    ImGui::SetNextWindowSize({io.DisplaySize.x, kMenuBar});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10.0f, 5.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {8.0f, 4.0f});
    ImGui::Begin("##menubar", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoBackground);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // Brand: small G mark + two-tone wordmark (the reference brand mark: white
    // "Grid" and slate-teal "Engine", with a teal segment inside the G).
    const ImVec2 mark_pos = ImGui::GetCursorScreenPos();
    draw->AddRectFilled({mark_pos.x - 2.0f, mark_pos.y - 1.0f},
                        {mark_pos.x + 24.0f, mark_pos.y + 25.0f}, ImGui::GetColorU32(kPanel),
                        5.0f);
    draw_mini_g(draw, {mark_pos.x + 11.0f, mark_pos.y + 12.0f}, 15.0f,
                ImGui::GetColorU32(kWhite), ImGui::GetColorU32(kEngine));
    ImGui::Dummy({28.0f, 24.0f});
    ImGui::SameLine();
    ImFont* brand_font = font_at(17.0f);
    const float word_y = ImGui::GetCursorScreenPos().y + 1.0f;
    const float word_x = ImGui::GetCursorScreenPos().x + 2.0f;
    const float grid_w = brand_font->CalcTextSizeA(17.0f, FLT_MAX, 0.0f, "Grid").x;
    draw->AddText(brand_font, 17.0f, {word_x, word_y}, ImGui::GetColorU32(kWhite), "Grid");
    draw->AddText(brand_font, 17.0f, {word_x + grid_w, word_y}, ImGui::GetColorU32(kEngine),
                  "Engine");
    ImGui::Dummy({88.0f, 20.0f});
    ImGui::SameLine();

    const bool editing = app.project_open && !app.editor.playing();
    const bool has_selection = app.editor.selection().has_value();
    auto* entity = app.editor.inspected_entity();

    // Menu items: each one performs the action it names, or is disabled when
    // the action is impossible right now. Nothing here is decorative.
    const char* menus[] = {"File", "Edit", "View", "Project", "Scene", "Scripts", "Window", "Help"};
    for (const char* item : menus) {
        if (ImGui::BeginMenu(item)) {
            const std::string which = item;
            if (which == "File") {
                if (ImGui::MenuItem("Save scene", "Ctrl+S", false, editing)) save_scene(app);
                if (ImGui::MenuItem("Reload scene", nullptr, false, app.project_open &&
                                                                     !app.scene_file.empty())) {
                    if (app.editor.load_scene(app.scene_file))
                        app.editor.log("scene: reloaded " + app.scene_file.filename().string());
                    else
                        app.editor.log("scene: cannot reload " + app.scene_file.string());
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Export...", nullptr, false, app.project_open))
                    app.export_popup = true;
                ImGui::Separator();
                if (ImGui::MenuItem("Quit", "Alt+F4")) app.running = false;
            } else if (which == "Edit") {
                if (ImGui::MenuItem("Add entity", nullptr, false, editing)) {
                    (void)app.editor.create_entity("Entity");
                    app.dirty = true;
                }
                if (ImGui::MenuItem("Duplicate", nullptr, false, editing && has_selection)) {
                    if (app.editor.duplicate_selected()) app.dirty = true;
                }
                if (ImGui::MenuItem("Delete", "Del", false, editing && has_selection)) {
                    if (app.editor.remove_selected()) app.dirty = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Reset transform", nullptr, false, editing && has_selection)) {
                    gridengine::Transform2D value = entity ? entity->transform
                                                           : gridengine::Transform2D{};
                    value.position = {0.0, 0.0};
                    value.rotation = 0.0;
                    value.scale = {1.0, 1.0};
                    if (app.editor.set_transform(value)) app.dirty = true;
                }
            } else if (which == "View") {
                ImGui::MenuItem("Grid", nullptr, &app.show_grid);
                if (ImGui::MenuItem("Zoom in", nullptr, false, app.project_open))
                    app.camera.zoom = std::min(4.0, app.camera.zoom + 0.25);
                if (ImGui::MenuItem("Zoom out", nullptr, false, app.project_open))
                    app.camera.zoom = std::max(0.1, app.camera.zoom - 0.25);
                if (ImGui::MenuItem("Reset camera", nullptr, false, app.project_open)) {
                    app.camera.center_x = 0.0;
                    app.camera.center_y = 0.0;
                    app.camera.zoom = 1.0;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("VSync", nullptr, &app.vsync)) {
                    SDL_GL_SetSwapInterval(app.vsync ? 1 : 0);
                    app.editor.log(std::string("display: vsync ") + (app.vsync ? "on" : "off"));
                }
            } else if (which == "Project") {
                if (ImGui::MenuItem("Open project folder", nullptr, false, app.project_open))
                    open_externally(app.project_root);
                if (ImGui::MenuItem("Rescan assets", nullptr, false, app.project_open)) {
                    refresh_assets(app);
                    app.editor.log("assets: rescanned " + std::to_string(app.assets.size()));
                }
                if (ImGui::MenuItem("Save tilemap", nullptr, false, app.project_open &&
                                                                     !app.tiles_file.empty() &&
                                                                     !app.tiles.empty())) {
                    if (app.tiles.save(app.tiles_file)) app.editor.log("tilemap: saved");
                    else app.editor.log("tilemap: save failed");
                    app.dirty = false;
                }
            } else if (which == "Scene") {
                if (ImGui::MenuItem("Add entity", nullptr, false, editing)) {
                    (void)app.editor.create_entity("Entity");
                    app.dirty = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Frames - Animation Sequence...", nullptr, false, app.project_open)) {
                    app.frameset_open = true;
                    app.editor.log("frames: editor opened");
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Select tool", nullptr, app.tool == Tool::select))
                    app.tool = Tool::select;
                if (ImGui::MenuItem("Paint tool", nullptr, app.tool == Tool::paint))
                    app.tool = Tool::paint;
                if (ImGui::MenuItem("Erase tool", nullptr, app.tool == Tool::erase))
                    app.tool = Tool::erase;
            } else if (which == "Scripts") {
                if (ImGui::MenuItem("New Grid script...", nullptr, false, app.project_open)) {
                    app.script_dialog = true;
                }
                // The project's own scripts become menu entries: picking one
                // loads its source into the selected node's Grid script slot.
                const bool has_script_selection =
                    app.project_open && app.editor.inspected_entity() != nullptr;
                std::error_code ec;
                const auto scripts_dir = app.project_root / "scripts";
                if (fs::is_directory(scripts_dir, ec)) {
                    int listed = 0;
                    for (const auto& entry : fs::directory_iterator(scripts_dir, ec)) {
                        if (!entry.is_regular_file(ec)) continue;
                        if (entry.path().extension() != ".gs") continue;
                        if (++listed > 8) break;
                        const std::string stem = entry.path().stem().string();
                        if (ImGui::MenuItem(("Open " + stem + ".gs").c_str(), nullptr, false,
                                            has_script_selection)) {
                            std::ifstream in(entry.path(), std::ios::binary);
                            const std::string source((std::istreambuf_iterator<char>(in)),
                                                     std::istreambuf_iterator<char>());
                            if (app.editor.set_script(source)) {
                                app.dirty = true;
                                app.editor.log("script: " + stem + " attached to the selection");
                            } else {
                                app.editor.log("script: cannot attach " + stem);
                            }
                        }
                    }
                }
                if (ImGui::MenuItem("Open scripts folder", nullptr, false, app.project_open))
                    open_externally(scripts_dir);
                if (ImGui::MenuItem("Reload scripts", nullptr, false, app.project_open)) {
                    refresh_assets(app);
                    app.editor.log("scripts: re-scanned the project scripts");
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Grid language docs")) open_docs(app, "grid-language-v0.9.md");
                if (ImGui::MenuItem("Script integration docs"))
                    open_docs(app, "script-integration-v0.9.md");
                if (ImGui::MenuItem("Discord: scripts & modules")) open_discord(app);
            } else if (which == "Window") {
                if (ImGui::MenuItem("Minimize")) SDL_MinimizeWindow(app.window);
                if (ImGui::MenuItem(app.maximized ? "Restore" : "Maximize")) {
                    if (app.maximized) SDL_RestoreWindow(app.window);
                    else SDL_MaximizeWindow(app.window);
                    app.maximized = !app.maximized;
                }
                if (ImGui::MenuItem("Close window", "Alt+F4")) app.running = false;
                ImGui::Separator();
                if (ImGui::MenuItem("Frames - Animation Sequence", nullptr, app.frameset_open,
                                    app.project_open))
                    app.frameset_open = !app.frameset_open;
                if (ImGui::MenuItem("Node catalog", nullptr, app.add_node_popup, app.project_open))
                    app.add_node_popup = !app.add_node_popup;
                ImGui::Separator();
                if (ImGui::MenuItem("Discord support")) open_discord(app);
            } else if (which == "Help") {
                if (ImGui::MenuItem("Discord support - perizokvana")) open_discord(app);
                if (ImGui::MenuItem("Copy Discord ID")) {
                    ImGui::SetClipboardText(kDiscordId);
                    app.editor.log(std::string("help: ") + kDiscordId + " copied to the clipboard");
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Grid language docs")) open_docs(app, "grid-language-v0.9.md");
                if (ImGui::MenuItem("Export docs")) open_docs(app, "export-v0.9.md");
                if (ImGui::MenuItem("Script integration docs"))
                    open_docs(app, "script-integration-v0.9.md");
                if (ImGui::MenuItem("Audio docs")) open_docs(app, "v0.5-audio.md");
                ImGui::Separator();
                if (ImGui::MenuItem("About GridEngine")) {
                    app.editor.log(std::string("GridEngine ") + std::string(gridengine::version()) +
                                   " - editor frontend; support: " + kDiscordUrl);
                }
            }
        }
        ImGui::SameLine();
    }

    // Window controls, right aligned: minimize / maximize / close.
    const float btn_w = 42.0f;
    const float row_y = 4.0f;
    ImGui::SameLine(io.DisplaySize.x - btn_w * 3.0f - 4.0f);
    ImGui::SetCursorPosY(row_y);
    if (icon_button("##winmin", 22, 24, ImGui::GetColorU32(kWhiteDim))) SDL_MinimizeWindow(app.window);
    ImGui::SameLine();
    if (icon_button("##winmax", 23, 24, ImGui::GetColorU32(kWhiteDim))) {
        if (app.maximized) SDL_RestoreWindow(app.window);
        else SDL_MaximizeWindow(app.window);
        app.maximized = !app.maximized;
    }
    ImGui::SameLine();
    const ImVec4 kCloseHover{0.42f, 0.13f, 0.11f, 1.0f};
    if (icon_button("##winclose", 24, 24, ImGui::GetColorU32(kWhiteDim), false, &kCloseHover))
        app.running = false;
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void draw_tool_bar(App& app) {
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0.0f, kMenuBar});
    ImGui::SetNextWindowSize({io.DisplaySize.x, kToolBar});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10.0f, 4.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {4.0f, 4.0f});
    ImGui::Begin("##toolbar", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoBackground);

    // Left group: file operations; undo/redo are session-local, render dimmed.
    if (icon_button("##tb_open", 1, 28, ImGui::GetColorU32(kWhiteDim))) {
        if (app.project_open) open_externally(app.project_root);
        else app.editor.log("project: none is open");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open project folder");
    ImGui::SameLine();
    if (icon_button("##tb_save", 2, 28, ImGui::GetColorU32(kWhiteDim),
                    !app.project_open || app.editor.playing()))
        save_scene(app);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save scene");
    ImGui::SameLine();
    separator_vertical();

    // Right group: display settings only. The play / pause / stop controls
    // live in the scene tool strip next to the editing tools.
    const float right = 34.0f;
    ImGui::SameLine(io.DisplaySize.x - right);
    ImGui::SetCursorPosY(4.0f);
    if (icon_button("##tb_gear", 8, 28, ImGui::GetColorU32(kWhiteDim))) {
        app.vsync = !app.vsync;
        SDL_GL_SetSwapInterval(app.vsync ? 1 : 0);
        app.editor.log(std::string("display: vsync ") + (app.vsync ? "on" : "off"));
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle vsync");
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void draw_scene_tabs(App& app) {
    const float width = ImGui::GetIO().DisplaySize.x - kHierarchyWidth - kInspectorWidth;
    ImGui::SetNextWindowPos({kHierarchyWidth, kTopBar});
    ImGui::SetNextWindowSize({width, kTabBar});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8.0f, 4.0f});
    ImGui::Begin("##tabs", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // The single open scene as a tab pill. The pill is a label of the current
    // scene (one scene per project); it carries no fake controls.
    const std::string tab = app.project_open ? app.scene_file.filename().string() : "no project";
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float text_w = ImGui::CalcTextSize(tab.c_str()).x;
    const float pill_w = text_w + 44.0f;
    const ImU32 pill_col = ImGui::GetColorU32(kPanelActive);
    draw->AddRectFilled(pos, {pos.x + pill_w, pos.y + 24.0f}, pill_col, 6.0f);
    draw->AddText({pos.x + 12.0f, pos.y + 4.0f}, ImGui::GetColorU32(kWhite), tab.c_str());
    if (app.dirty) draw->AddCircleFilled({pos.x + pill_w - 4.0f, pos.y + 5.0f}, 3.0f,
                                         ImGui::GetColorU32(kMint), 10);
    ImGui::Dummy({pill_w, 24.0f});
    ImGui::SameLine();
    ImGui::TextColored(kWhiteFaint, "2D");
    ImGui::End();
    ImGui::PopStyleVar();
}

void draw_scene_subbar(App& app) {
    const float width = ImGui::GetIO().DisplaySize.x - kHierarchyWidth - kInspectorWidth;
    ImGui::SetNextWindowPos({kHierarchyWidth, kTopBar + kTabBar});
    ImGui::SetNextWindowSize({width, kSubBar});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10.0f, 3.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {6.0f, 3.0f});
    ImGui::Begin("##subbar", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground);
    // Label on the left, then the tool buttons.
    ImGui::SetCursorPos({10.0f, 6.0f});
    ImGui::TextColored(kWhiteDim, "Scene Editor");
    ImGui::SameLine();
    ImGui::SetCursorPosY(3.0f);
    separator_vertical();
    ImGui::SameLine();

    // Tools.
    if (icon_button("##sub_select", 11, 24,
                    ImGui::GetColorU32(app.tool == Tool::select ? kMint : kWhiteDim)))
        app.tool = Tool::select;
    ImGui::SameLine();
    if (icon_button("##sub_paint", 12, 24,
                    ImGui::GetColorU32(app.tool == Tool::paint ? kMint : kWhiteDim)))
        app.tool = Tool::paint;
    ImGui::SameLine();
    if (icon_button("##sub_erase", 13, 24,
                    ImGui::GetColorU32(app.tool == Tool::erase ? kMint : kWhiteDim)))
        app.tool = Tool::erase;
    ImGui::SameLine();
    separator_vertical();
    ImGui::SameLine();

    // Run control: only Stop sits in the strip. Starting and pausing the
    // simulation moved to the F5 / F6 hotkeys, so the strip carries no launch
    // button anymore.
    const bool playing = app.editor.playing();
    if (icon_button("##sub_stop", 7, 24, ImGui::GetColorU32(kMint), !playing)) {
        app.editor.stop_play();
        app.play_paused = false;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(playing ? "Stop (F5)" : "Stop - start with F5, pause with F6");
    ImGui::SameLine();

    // Right side: zoom controls and the grid toggle.
    ImGui::SameLine(width - 132.0f);
    ImGui::SetCursorPosY(3.0f);
    if (icon_button("##sub_zoomin", 14, 24, ImGui::GetColorU32(kWhiteDim))) {
        app.camera.zoom = std::min(4.0, app.camera.zoom + 0.25);
    }
    ImGui::SameLine();
    if (icon_button("##sub_zoomout", 15, 24, ImGui::GetColorU32(kWhiteDim))) {
        app.camera.zoom = std::max(0.1, app.camera.zoom - 0.25);
    }
    ImGui::SameLine();
    ImGui::SetCursorPosY(6.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, app.show_grid ? kMint : kWhiteDim);
    if (ImGui::BeginCombo("##gridtoggle", "Grid", ImGuiComboFlags_NoPreview)) {
        ImGui::Checkbox("Show grid", &app.show_grid);
        ImGui::EndCombo();
    }
    ImGui::PopStyleColor();
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void draw_hierarchy(App& app, float height) {
    if (!begin_panel("Hierarchy", "Hierarchy", {0.0f, kViewTop}, {kHierarchyWidth, height}))
        return end_panel();
    const bool editing = app.project_open && !app.editor.playing();
    ImGui::PushID("##hierhdr");
    // A separate "+" for nodes: it opens the catalog picker of the reference
    // (Core / Visual / Logic / Input / Physics / Custom) instead of dropping a
    // bare entity into the tree.
    ImGui::SetCursorPos({kHierarchyWidth - 114.0f, 4.0f});
    if (icon_button("##hier_node", 44, 22, ImGui::GetColorU32(kMint), !editing)) {
        app.add_node_popup = true;
        app.editor.log("node: pick a node type");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add node... (catalog)");
    ImGui::SetCursorPos({kHierarchyWidth - 88.0f, 4.0f});
    if (icon_button("##hier_add", 17, 22, ImGui::GetColorU32(kWhiteDim), !editing)) {
        (void)app.editor.create_entity("Entity");
        app.dirty = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add empty entity");
    ImGui::SetCursorPos({kHierarchyWidth - 62.0f, 4.0f});
    if (icon_button("##hier_dup", 19, 22, ImGui::GetColorU32(kWhiteDim), !editing)) {
        if (app.editor.duplicate_selected()) app.dirty = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Duplicate selected");
    ImGui::SetCursorPos({kHierarchyWidth - 36.0f, 4.0f});
    if (icon_button("##hier_del", 20, 22, ImGui::GetColorU32(kWhiteDim), !editing)) {
        if (app.editor.remove_selected()) app.dirty = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete selected");
    ImGui::PopID();

    ImGui::SetCursorPos({10.0f, 38.0f});
    search_field("##hiersearch", "Search...", app.hierarchy_filter, kHierarchyWidth - 20.0f);

    ImGui::SetCursorPos({10.0f, 70.0f});
    const float tree_h = std::max(40.0f, height - 78.0f);
    ImGui::BeginChild("##tree", {kHierarchyWidth - 12.0f, tree_h}, ImGuiChildFlags_None);
    if (!app.project_open) ImGui::TextColored(kWhiteFaint, "open a project");
    ImGui::PushStyleColor(ImGuiCol_Header, kSelection);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, kPanelHover);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, kSelection);
    ImDrawList* tree_draw = ImGui::GetWindowDrawList();
    for (const auto& item : app.editor.hierarchy()) {
        auto* entity = app.editor.scene().find(item.id);
        if (!entity) continue;
        if (!app.hierarchy_filter.empty() &&
            entity->name.find(app.hierarchy_filter) == std::string::npos)
            continue;
        ImGui::PushID(static_cast<int>(item.id));
        for (std::size_t i = 0; i < item.depth; ++i) ImGui::Indent();

        // Component markers, drawn as small glyphs before the row: mint square
        // for sprites, mint circle for audio sources. The scene root (depth 0)
        // keeps a permanent teal tint behind the row, like the reference tree.
        const ImVec2 row = ImGui::GetCursorScreenPos();
        if (item.depth == 0) {
            tree_draw->AddRectFilled({row.x - 6.0f, row.y - 1.0f},
                                     {row.x + kHierarchyWidth - 24.0f,
                                      row.y + ImGui::GetTextLineHeightWithSpacing()},
                                     ImGui::GetColorU32(kSelection));
        }
        const float glyph_y = row.y + ImGui::GetTextLineHeight() * 0.5f;
        float glyph_x = row.x + 2.0f;
        if (entity->sprite) {
            tree_draw->AddRectFilled({glyph_x, glyph_y - 4.0f}, {glyph_x + 8.0f, glyph_y + 4.0f},
                                     ImGui::GetColorU32(kMint));
            glyph_x += 12.0f;
        }
        if (entity->audio) {
            tree_draw->AddCircleFilled({glyph_x + 4.0f, glyph_y}, 4.0f, ImGui::GetColorU32(kMintDim));
            glyph_x += 12.0f;
        }
        ImGui::Dummy({glyph_x - row.x, 0.0f});
        ImGui::SameLine();

        const bool selected = app.editor.selection() == item.id;
        const std::string label = entity->name + "###entity" + std::to_string(item.id);
        if (ImGui::Selectable(label.c_str(), selected)) (void)app.editor.select(item.id);

        // Visibility eye on the right edge of every row; only sprites can
        // actually be toggled, other rows show the eye as an open hint.
        ImGui::SameLine(kHierarchyWidth - 52.0f);
        const ImVec2 eye = ImGui::GetCursorScreenPos();
        const bool visible = entity->sprite ? entity->sprite->visible : true;
        if (ImGui::InvisibleButton("##eye", {20.0f, 20.0f}) && entity->sprite) {
            if (!app.editor.playing()) {
                (void)app.editor.select(item.id);
                gridengine::Sprite2D sprite = *entity->sprite;
                sprite.visible = !visible;
                (void)app.editor.set_sprite(sprite);
                app.dirty = true;
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(visible ? "hide" : "show");
        draw_eye(tree_draw, {eye.x + 10.0f, eye.y + 10.0f}, 15.0f,
                 ImGui::GetColorU32(visible ? kWhiteDim : kWhiteFaint), visible);
        for (std::size_t i = 0; i < item.depth; ++i) ImGui::Unindent();
        ImGui::PopID();
    }
    ImGui::PopStyleColor(3);
    ImGui::EndChild();
    end_panel();
}

void draw_resources(App& app, float y) {
    const float height = kResourcesHeight;
    if (!begin_panel("Resources", "Resources", {0.0f, y}, {kHierarchyWidth, height}))
        return end_panel();
    ImGui::SetCursorPos({kHierarchyWidth - 36.0f, 4.0f});
    if (icon_button("##res_add", 17, 22, ImGui::GetColorU32(kWhiteDim))) {
        refresh_assets(app);
        app.editor.log("assets: rescanned");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rescan assets");

    ImGui::SetCursorPos({10.0f, 38.0f});
    search_field("##ressearch", "Search...", app.resources_filter, kHierarchyWidth - 20.0f);

    ImGui::SetCursorPos({10.0f, 70.0f});
    const float list_h = std::max(40.0f, height - 78.0f);
    ImGui::BeginChild("##assets", {kHierarchyWidth - 12.0f, list_h}, ImGuiChildFlags_None);
    int shown = 0;
    for (const auto& asset : app.assets) {
        if (!app.resources_filter.empty() &&
            asset.find(app.resources_filter) == std::string::npos)
            continue;
        ++shown;
        const bool selected = app.selected_asset == asset;
        const bool audio = fs::path(asset).extension() == ".wav";
        const std::string label =
            (audio ? "[a] " : "[s] ") + asset + "###" + asset;
        if (ImGui::Selectable(label.c_str(), selected)) app.selected_asset = asset;
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s asset: %s", audio ? "audio" : "sprite",
                              selected ? "click the scene view to attach it" : "select, then click an entity");
        }
    }
    ImGui::EndChild();
    if (shown == 0) {
        const char* empty = app.assets.empty() ? "No resources" : "No matches";
        const float text_w = ImGui::CalcTextSize(empty).x;
        ImGui::SetCursorPos({(kHierarchyWidth - text_w) * 0.5f, height * 0.5f + 6.0f});
        ImGui::TextColored(kWhiteFaint, "%s", empty);
    }
    end_panel();
}

void draw_console(App& app, float y) {
    const float x = kHierarchyWidth;
    const float width = ImGui::GetIO().DisplaySize.x - kHierarchyWidth - kInspectorWidth;
    if (!begin_panel("Console", "Console", {x, y}, {width, kConsoleHeight})) return end_panel();
    ImGui::SetCursorPos({width - 36.0f, 4.0f});
    if (icon_button("##con_add", 17, 22, ImGui::GetColorU32(kWhiteDim))) {
        app.editor.clear_console();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear console");

    const char* tabs[] = {"Output", "Errors", "Warnings", "Log"};
    ImGui::SetCursorPos({10.0f, 36.0f});
    for (int i = 0; i < 4; ++i) {
        if (text_tab(tabs[i], app.console_tab == i, 22.0f)) app.console_tab = i;
        ImGui::SameLine();
    }
    ImGui::SetCursorPos({10.0f, 62.0f});
    const float lines_h = std::max(40.0f, kConsoleHeight - 116.0f);
    ImGui::BeginChild("##lines", {width - 20.0f, lines_h}, ImGuiChildFlags_None);
    const auto& lines = app.editor.console_lines();
    auto matches = [](const std::string& line, int tab) {
        if (tab == 0) return true;
        if (tab == 1) return line.find("error") != std::string::npos ||
                            line.find("rejected") != std::string::npos ||
                            line.find("cannot") != std::string::npos ||
                            line.find("failed") != std::string::npos;
        if (tab == 2) return line.find("warning") != std::string::npos;
        return true;
    };
    int shown = 0;
    for (const auto& line : lines) {
        if (!matches(line, app.console_tab)) continue;
        ImGui::TextColored(kWhiteDim, "%s", line.c_str());
        ++shown;
    }
    if (shown == 0)
        ImGui::TextColored(kWhiteFaint, "%s", app.console_tab == 0
                                                  ? "Output and messages will appear here."
                                                  : "nothing here yet");
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    ImGui::SetCursorPos({10.0f, kConsoleHeight - 40.0f});
    ImGui::SetNextItemWidth(width - 56.0f);
    app.command.resize(256, '\0');
    if (ImGui::InputTextWithHint("##command", "Enter command...", app.command.data(),
                                 app.command.size())) {
        app.command.resize(std::strlen(app.command.c_str()));
    }
    app.command.resize(std::strlen(app.command.c_str()));
    const bool commit = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    ImGui::SetCursorPos({width - 40.0f, kConsoleHeight - 42.0f});
    if (icon_button("##con_send", 25, 26, ImGui::GetColorU32(kMint)) && !app.command.empty())
        run_command(app);
    else if (commit)
        run_command(app);
    end_panel();
}

// ------------------------------------------------------------ node picker ---
// The node menu follows the reference art: a dark floating window inside a
// bright mint neon frame with a soft outer glow, a search field with the
// magnifier on the right, collapsible category headers led by their own icon
// (folder / image / gear / keyboard / circle / package) and entries marked by
// small square bullets. Every catalog node is listed, plus the project's own
// Grid scripts under Custom. Selecting a node creates it in the scene with the
// components the engine supports.
namespace {

// The neon frame of the reference: a crisp mint border wrapped in three glow
// rings of the same hue, each fainter and wider than the one inside it.
constexpr ImVec4 kNodeGlow{0.243f, 0.812f, 0.557f, 0.55f};
constexpr ImVec4 kNodeGlowRing1{0.243f, 0.812f, 0.557f, 0.28f};
constexpr ImVec4 kNodeGlowRing2{0.243f, 0.812f, 0.557f, 0.14f};
constexpr ImVec4 kNodeGlowRing3{0.243f, 0.812f, 0.557f, 0.07f};
constexpr ImVec4 kNodePanelFill{0.043f, 0.055f, 0.063f, 0.985f};
// One accent per category, the way the reference tints its tree.
constexpr ImVec4 kNodeCore{0.353f, 0.729f, 0.439f, 1.0f};
constexpr ImVec4 kNodeVisual{0.247f, 0.776f, 0.722f, 1.0f};
constexpr ImVec4 kNodeLogic{0.949f, 0.620f, 0.302f, 1.0f};
constexpr ImVec4 kNodeInput{0.353f, 0.553f, 0.949f, 1.0f};
constexpr ImVec4 kNodePhysics{0.851f, 0.400f, 0.624f, 1.0f};

struct NodeRow {
    std::string name;
    std::string summary;
    gridengine::NodeKind kind{};
    bool script{false};
};

bool name_matches(const std::string& haystack, const std::string& needle_lower) {
    if (needle_lower.empty()) return true;
    std::string lower;
    lower.resize(haystack.size());
    std::transform(haystack.begin(), haystack.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find(needle_lower) != std::string::npos;
}

// The category block: a caret, the category icon (tinted with the category
// accent) and the name, then the rows. The whole header row is the click target
// that folds the category, so the invisible button is submitted exactly over
// the row (no SameLine trickery: that used to drag the following rows outside
// the picker and made every node unclickable). Rows are full-width selectables
// led by a small bullet in the category color, with a thin guide line through
// the bullets like the reference tree.
void node_category_rows(App& app, const char* label, int icon, const std::vector<NodeRow>& rows,
                        bool& open, const std::string& filter_lower, float list_w, ImU32 accent) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 row = ImGui::GetCursorScreenPos();
    const ImVec2 local = ImGui::GetCursorPos();
    // Caret: rotated 90 degrees while open.
    const ImVec2 caret{row.x + 10.0f, row.y + 10.0f};
    const float a = open ? 1.5707963267948966f : 0.0f;
    const float ca = std::cos(a), sa = std::sin(a);
    draw->PathClear();
    draw->PathLineTo({caret.x + (-6.0f * ca - -4.0f * sa), caret.y + (-6.0f * sa + -4.0f * ca)});
    draw->PathLineTo({caret.x + (0.0f * ca - 0.0f * sa), caret.y + (0.0f * sa + 0.0f * ca)});
    draw->PathLineTo({caret.x + (-6.0f * ca - 4.0f * sa), caret.y + (-6.0f * sa + 4.0f * ca)});
    draw->PathStroke(ImGui::GetColorU32(kWhiteDim), 1.4f);
    draw_icon(draw, icon, {caret.x + 13.0f, caret.y}, 13.0f, accent);
    draw->AddText({row.x + 30.0f, row.y + 2.0f}, ImGui::GetColorU32(kWhite), label);
    // The whole header row toggles the category, like the reference.
    ImGui::PushID(label);
    ImGui::SetCursorPos(local);
    ImGui::InvisibleButton("##fold", {list_w, 20.0f});
    if (ImGui::IsItemClicked()) open = !open;
    const bool fold_hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    if (fold_hovered)
        draw->AddRectFilled({row.x - 4.0f, row.y}, {row.x + list_w - 8.0f, row.y + 20.0f},
                            ImGui::GetColorU32(kPanelHover), 3.0f);
    if (!open) return;
    // The rows continue right below the header, at the list's left edge.
    ImGui::SetCursorPos({local.x, local.y + 20.0f});
    ImGui::Indent(14.0f);
    const float guide_x = row.x + 14.0f + 5.0f;
    const float rows_top = row.y + 22.0f;
    ImGui::PushStyleColor(ImGuiCol_Header, kSelection);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, kPanelHover);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, kSelection);
    for (const auto& row_entry : rows) {
        if (!name_matches(row_entry.name.empty() ? row_entry.summary : row_entry.name, filter_lower) &&
            !name_matches(row_entry.summary, filter_lower))
            continue;
        ImGui::PushID(row_entry.name.c_str());
        const ImVec2 item = ImGui::GetCursorScreenPos();
        const float cy = item.y + ImGui::GetTextLineHeight() * 0.5f;
        // Square bullet on the guide line, exactly like the reference rows.
        draw->AddRectFilled({guide_x - 1.5f, cy - 1.5f}, {guide_x + 1.5f, cy + 1.5f}, accent);
        bool picked = ImGui::Selectable(row_entry.name.c_str(), false, 0, {list_w - 20.0f, 0.0f});
        if (picked) {
            if (row_entry.script) {
                const std::string name = row_entry.name;
                const std::string source = row_entry.summary;
                const gridengine::EntityId id =
                    app.editor.add_node(gridengine::NodeKind::custom, source);
                if (id) {
                    if (auto* entity = app.editor.scene().find(id)) entity->name = name;
                    app.dirty = true;
                }
            } else {
                if (app.editor.add_node(row_entry.kind) != 0) app.dirty = true;
            }
            // One node per click: the picker closes and stays closed until the
            // "Add node" button is pressed again.
            app.add_node_popup = false;
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsItemHovered() && !row_entry.summary.empty())
            ImGui::SetTooltip("%s", row_entry.summary.c_str());
        ImGui::PopID();
    }
    ImGui::PopStyleColor(3);
    ImGui::Unindent(14.0f);
    // The guide line runs from the first row to the last of this category.
    const float rows_bottom = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y -
                              ImGui::GetTextLineHeight() * 0.5f;
    if (rows_bottom > rows_top)
        draw->AddLine({guide_x, rows_top}, {guide_x, rows_bottom},
                      ImGui::GetColorU32(kBorder), 1.0f);
}

} // namespace

void node_picker(App& app) {
    if (!app.add_node_popup) return;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 center = viewport->GetCenter();
    constexpr float kWidth = 348.0f;
    constexpr float kHeight = 470.0f;
    ImGui::SetNextWindowSize({kWidth, kHeight}, ImGuiCond_Appearing);
    ImGui::SetNextWindowPos({center.x - kWidth * 0.5f, center.y - kHeight * 0.5f},
                            ImGuiCond_Appearing);

    // Neon frame of the reference: a crisp mint border plus three glow rings,
    // each wider and fainter than the one before, drawn behind the window.
    if (ImDrawList* background = ImGui::GetBackgroundDrawList()) {
        const ImVec2 min{center.x - kWidth * 0.5f, center.y - kHeight * 0.5f};
        const ImVec2 max{center.x + kWidth * 0.5f, center.y + kHeight * 0.5f};
        constexpr ImVec4 rings[] = {kNodeGlowRing3, kNodeGlowRing2, kNodeGlowRing1};
        for (int i = 0; i < 3; ++i) {
            const float grow = 6.0f + 5.0f * static_cast<float>(i);
            background->AddRect({min.x - grow, min.y - grow}, {max.x + grow, max.y + grow},
                                ImGui::GetColorU32(rings[i]), 14.0f + grow, 0, 2.5f);
        }
        background->AddRect(min, max, ImGui::GetColorU32(kNodeGlow), 14.0f, 0, 2.0f);
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kNodePanelFill);
    ImGui::PushStyleColor(ImGuiCol_Border, kNodeGlow);
    // A plain floating window instead of a popup: it opens every time it is
    // asked for, it never ends up behind the panels, and it closes on its own
    // X button in the header.
    bool open = app.add_node_popup;
    const bool visible = ImGui::Begin("##node_picker", &open,
                                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                          ImGuiWindowFlags_NoScrollbar |
                                          ImGuiWindowFlags_NoBringToFrontOnFocus);
    if (visible) {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        draw->AddRectFilled(p, {p.x + size.x, p.y + 32.0f}, ImGui::GetColorU32(kBlack), 14.0f,
                            ImDrawFlags_RoundCornersTop);
        draw->AddLine({p.x, p.y + 32.0f}, {p.x + size.x, p.y + 32.0f},
                      ImGui::GetColorU32(kBorder), 1.0f);
        draw->AddText(nullptr, ImGui::GetFontSize(), {p.x + 14.0f, p.y + 9.0f},
                      ImGui::GetColorU32(kWhite), "Р”РѕР±Р°РІРёС‚СЊ СѓР·РµР»");
        ImGui::SetCursorPos({size.x - 32.0f, 6.0f});
        if (icon_button("##node_close", 24, 20, ImGui::GetColorU32(kWhiteDim))) open = false;

        ImGui::SetCursorPos({10.0f, 40.0f});
        static std::string node_filter;
        search_field_right("##node_filter", "РџРѕРёСЃРє СѓР·Р»РѕРІ...", node_filter, kWidth - 20.0f);
        std::string filter_lower;
        filter_lower.resize(node_filter.size());
        std::transform(node_filter.begin(), node_filter.end(), filter_lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        ImGui::SetCursorPos({10.0f, 74.0f});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, kNodePanelFill);
        ImGui::BeginChild("##node_list", {kWidth - 20.0f, kHeight - 84.0f}, ImGuiChildFlags_None);

        // Catalog rows, built once per frame from the engine catalog.
        const auto rows_of = [](gridengine::NodeCategory category) {
            std::vector<NodeRow> rows;
            for (const auto& info : gridengine::node_catalog_of(category))
                rows.push_back({std::string(info.name), std::string(info.summary), info.kind, false});
            return rows;
        };
        const std::vector<NodeRow> core = rows_of(gridengine::NodeCategory::core);
        const std::vector<NodeRow> visual = rows_of(gridengine::NodeCategory::visual);
        const std::vector<NodeRow> logic = rows_of(gridengine::NodeCategory::logic);
        const std::vector<NodeRow> input = rows_of(gridengine::NodeCategory::input);
        const std::vector<NodeRow> physics = rows_of(gridengine::NodeCategory::physics);

        static bool open_core = true;
        static bool open_visual = true;
        static bool open_logic = true;
        static bool open_input = true;
        static bool open_physics = true;
        static bool open_custom = true;
        node_category_rows(app, "Core", 1, core, open_core, filter_lower, kWidth - 20.0f,
                           ImGui::GetColorU32(kNodeCore));
        node_category_rows(app, "Visual", 40, visual, open_visual, filter_lower, kWidth - 20.0f,
                           ImGui::GetColorU32(kNodeVisual));
        node_category_rows(app, "Logic", 8, logic, open_logic, filter_lower, kWidth - 20.0f,
                           ImGui::GetColorU32(kNodeLogic));
        node_category_rows(app, "Input", 41, input, open_input, filter_lower, kWidth - 20.0f,
                           ImGui::GetColorU32(kNodeInput));
        node_category_rows(app, "Physics", 42, physics, open_physics, filter_lower,
                           kWidth - 20.0f, ImGui::GetColorU32(kNodePhysics));

        // Custom: the project's own Grid scripts become custom nodes. Scripts
        // created from the Scripts menu live in scripts/, assets and scenes
        // are scanned too, so a script dropped anywhere becomes a node.
        std::vector<NodeRow> custom;
        std::error_code ec;
        for (const auto* folder : {"scripts", "assets", "scenes", ""}) {
            const auto base = folder[0] == '\0' ? app.project_root : app.project_root / folder;
            if (!std::filesystem::is_directory(base, ec)) continue;
            for (const auto& entry : std::filesystem::directory_iterator(base, ec)) {
                if (!entry.is_regular_file(ec)) continue;
                const auto extension = entry.path().extension().string();
                if (extension != ".gs" && extension != ".gd") continue;
                std::ifstream in(entry.path(), std::ios::binary);
                const std::string source((std::istreambuf_iterator<char>(in)),
                                         std::istreambuf_iterator<char>());
                custom.push_back({entry.path().stem().string(), source, gridengine::NodeKind::custom,
                                  true});
            }
        }
        if (custom.empty())
            custom.push_back({"Empty node", "a plain node without components",
                              gridengine::NodeKind::node2d, false});
        node_category_rows(app, "Custom", 43, custom, open_custom, filter_lower, kWidth - 20.0f,
                           ImGui::GetColorU32(kNodeVisual));

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    if (visible) ImGui::End();
    // Only the header X (or the window being collapsed) closes the picker here;
    // picking a node already cleared the flag itself.
    if (!open) app.add_node_popup = false;
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
}

// --- "New Grid script" dialog ------------------------------------------------
// Names the script file, and optionally attaches the new file's source to the
// selected node in one step, so the Scripts menu does real work.
void draw_script_dialog(App& app) {
    if (!app.script_dialog) return;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 center = viewport->GetCenter();
    constexpr float kWidth = 430.0f;
    constexpr float kHeight = 214.0f;
    ImGui::SetNextWindowSize({kWidth, kHeight}, ImGuiCond_Appearing);
    ImGui::SetNextWindowPos({center.x - kWidth * 0.5f, center.y - kHeight * 0.5f},
                            ImGuiCond_Appearing);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kNodePanelFill);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    bool open = app.script_dialog;
    if (!ImGui::Begin("##script_dialog", &open,
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        app.script_dialog = open;
        return;
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetWindowPos();
    draw->AddRectFilled(p, {p.x + kWidth, p.y + 30.0f}, ImGui::GetColorU32(kBlack));
    draw->AddLine({p.x, p.y + 30.0f}, {p.x + kWidth, p.y + 30.0f}, ImGui::GetColorU32(kBorder),
                  1.0f);
    draw->AddText(nullptr, ImGui::GetFontSize(), {p.x + 12.0f, p.y + 8.0f},
                  ImGui::GetColorU32(kWhite), "New Grid script");
    ImGui::SetCursorPos({kWidth - 36.0f, 5.0f});
    if (icon_button("##script_close", 24, 20, ImGui::GetColorU32(kWhiteDim))) open = false;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close");

    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::SetCursorPos({14.0f, 44.0f});
    ImGui::TextColored(kWhiteDim, "%s", "Name");
    ImGui::SetCursorPos({14.0f, 64.0f});
    ImGui::SetNextItemWidth(kWidth - 28.0f);
    ImGui::InputTextWithHint("##script_name", "player_controller", app.script_name,
                             sizeof(app.script_name));

    ImGui::SetCursorPos({14.0f, 98.0f});
    auto* entity = app.editor.inspected_entity();
    if (!entity) ImGui::BeginDisabled();
    ImGui::Checkbox("Attach to the selected node", &app.script_attach);
    if (!entity) {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextColored(kWhiteFaint, "%s", "(no node selected)");
    }

    ImGui::SetCursorPos({14.0f, kHeight - 52.0f});
    if (ImGui::Button("Create", {(kWidth - 28.0f - 8.0f) * 0.5f, 30.0f})) {
        const std::string name = app.script_name[0] != '\0' ? std::string(app.script_name)
                                                            : std::string("script");
        create_grid_script(app, name);
        if (app.script_attach && app.editor.inspected_entity()) {
            std::ifstream in(app.project_root / "scripts" / (name + ".gs"), std::ios::binary);
            const std::string source((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
            if (!source.empty() && app.editor.set_script(source)) {
                app.dirty = true;
                app.editor.log("script: " + name + " attached to the selection");
            }
        }
        open = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", {(kWidth - 28.0f - 8.0f) * 0.5f, 30.0f})) open = false;

    ImGui::PopStyleVar(2);
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    app.script_dialog = open;
}

// --- "Frames - Animation Sequence" -------------------------------------------
// Floating window in the reference layout: the animation list on the left with
// the loop / once icons and the add + delete row, the frame timeline on the
// right with a draggable playhead, and the frame properties below it. Nothing
// here is decoration: slicing rebuilds the sheet frames from the real texture
// size, the timeline edits the selected animation's frame order, the sliders
// write the clip speed / delay the engine plays with, and the header play
// button previews the animation on the selected node in the scene.

// A stable tint per sheet frame, so the thumbnails stay recognisable without
// pulling the GL texture into the UI.
ImU32 sheet_frame_color(int index) {
    const float hue = std::fmod(0.083f * static_cast<float>(index) + 0.45f, 1.0f);
    // ImColor converts implicitly to both ImU32 and ImVec4, which makes the
    // GetColorU32 overloads ambiguous; take the ImVec4 value explicitly.
    return ImGui::GetColorU32(ImColor::HSV(hue, 0.55f, 0.85f).Value);
}

// Thumbnail of one sheet frame: rounded tile, tint, frame index on top.
void draw_frame_tile(ImDrawList* draw, ImVec2 min, ImVec2 max, int sheet_index) {
    draw->AddRectFilled(min, max, ImGui::GetColorU32(kPanel), 4.0f);
    draw->AddRect(min, max, ImGui::GetColorU32(kBorder), 4.0f, 0, 1.0f);
    const ImVec2 inner{min.x + 5.0f, min.y + 5.0f};
    const ImVec2 inner_max{max.x - 5.0f, max.y - 5.0f};
    if (inner.x < inner_max.x && inner.y < inner_max.y) {
        draw->AddRectFilled(inner, inner_max, sheet_frame_color(sheet_index), 3.0f);
        const std::string label = std::to_string(sheet_index);
        const ImVec2 text = ImGui::CalcTextSize(label.c_str());
        draw->AddText(nullptr, ImGui::GetFontSize() * 0.85f,
                      {(inner.x + inner_max.x - text.x) * 0.5f,
                       (inner.y + inner_max.y - text.y) * 0.5f},
                      ImGui::GetColorU32(kBlack), label.c_str());
    }
}

void draw_frameset(App& app) {
    if (!app.frameset_open) return;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 center = viewport->GetCenter();
    constexpr float kWidth = 640.0f;
    constexpr float kHeight = 486.0f;
    constexpr float kListW = 178.0f;   // animation list column
    constexpr float kPad = 12.0f;      // outer padding
    constexpr float kHeader = 30.0f;   // window caption strip
    const float list_x = kPad;
    const float seq_x = kListW + kPad * 2.0f;
    const float seq_w = kWidth - seq_x - kPad;
    ImGui::SetNextWindowSize({kWidth, kHeight}, ImGuiCond_Appearing);
    ImGui::SetNextWindowPos({center.x - kWidth * 0.5f, center.y - kHeight * 0.5f},
                            ImGuiCond_Appearing);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kNodePanelFill);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    bool open = app.frameset_open;
    if (!ImGui::Begin("##frames", &open,
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                          ImGuiWindowFlags_NoScrollbar)) {
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        app.frameset_open = open;
        return;
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    draw->AddRectFilled(wp, {wp.x + kWidth, wp.y + kHeader}, ImGui::GetColorU32(kBlack));
    draw->AddLine({wp.x, wp.y + kHeader}, {wp.x + kWidth, wp.y + kHeader},
                  ImGui::GetColorU32(kBorder), 1.0f);
    // Caption: the reference title is centred, window buttons sit on the right.
    const char* caption = "Frames - Animation Sequence";
    const ImVec2 caption_size = ImGui::CalcTextSize(caption);
    draw->AddText(nullptr, ImGui::GetFontSize(),
                  {wp.x + (kWidth - caption_size.x) * 0.5f, wp.y + 7.0f},
                  ImGui::GetColorU32(kWhite), caption);
    ImGui::SetCursorPos({kWidth - 58.0f, 5.0f});
    if (icon_button("##frames_min", 22, 20, ImGui::GetColorU32(kWhiteDim)))
        SDL_MinimizeWindow(app.window);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Minimize");
    ImGui::SameLine();
    if (icon_button("##frames_close", 24, 20, ImGui::GetColorU32(kWhiteDim))) open = false;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close");

    // The header play button previews the selected animation on the selected
    // node, without entering play mode.
    ImGui::SetCursorPos({kWidth - 90.0f, 5.0f});
    if (icon_button("##frames_play", 5, 20, ImGui::GetColorU32(app.frameset_preview ? kMint
                                                                                    : kWhiteDim)))
        app.frameset_preview = !app.frameset_preview;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Preview the animation");

    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kNodePanelFill);

    auto* entity = app.editor.inspected_entity();
    if (!entity) {
        ImGui::SetCursorPos({kPad, kHeader + 14.0f});
        ImGui::TextColored(kWhiteDim, "%s", "Select a node in the Hierarchy first.");
        ImGui::SetCursorPos({kPad, kHeight - 56.0f});
        if (ImGui::Button("Close", {kWidth - kPad * 2.0f, 28.0f})) open = false;
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        app.frameset_open = open;
        return;
    }

    // Which frameset this sprite animates, or the first one with its sheet.
    // The slot is remembered between frames, so the same frameset stays open
    // while the window is used.
    gridengine::Sprite2D sprite = entity->sprite ? *entity->sprite : gridengine::Sprite2D{};
    int slot = app.frameset_index;
    if (!entity->sprite || slot < 0 || slot >= static_cast<int>(app.editor.framesets().size()))
        slot = -1;
    else if (app.editor.framesets()[static_cast<std::size_t>(slot)].texture != sprite.texture)
        slot = -1;
    if (slot < 0 && entity->sprite) {
        for (std::size_t i = 0; i < app.editor.framesets().size(); ++i) {
            if (app.editor.framesets()[static_cast<std::size_t>(i)].texture == sprite.texture) {
                slot = static_cast<int>(i);
                break;
            }
        }
    }
    app.frameset_index = slot >= 0 ? slot : -1;

    // --- left column: the animation list ---------------------------------------
    ImGui::SetCursorPos({list_x, kHeader + 12.0f});
    ImGui::TextColored(kWhiteDim, "%s", "Animations");
    ImGui::SetCursorPos({list_x, kHeader + 34.0f});
    ImGui::PushStyleColor(ImGuiCol_Header, kSelection);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, kPanelHover);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, kSelection);
    ImGui::BeginChild("##anim_list", {kListW, kHeight - kHeader - 118.0f}, ImGuiChildFlags_None);
    ImDrawList* list_draw = ImGui::GetWindowDrawList();
    if (slot >= 0) {
        auto& set = app.editor.framesets()[static_cast<std::size_t>(slot)];
        if (app.frameset_slot >= static_cast<int>(set.clips.size())) app.frameset_slot = -1;
        if (app.frameset_slot < 0 && !set.clips.empty()) app.frameset_slot = 0;
        for (std::size_t i = 0; i < set.clips.size(); ++i) {
            const auto& clip = set.clips[i];
            const bool selected = static_cast<int>(i) == app.frameset_slot;
            const ImVec2 row = ImGui::GetCursorScreenPos();
            draw_icon(list_draw, clip.loop ? 28 : 29, {row.x + 10.0f, row.y + 10.0f}, 14.0f,
                      ImGui::GetColorU32(selected ? kMint : kWhiteDim));
            ImGui::PushID(static_cast<int>(i));
            const bool picked = ImGui::Selectable(("##anim" + std::to_string(i)).c_str(), selected,
                                                 0, {kListW - 4.0f, 0.0f});
            if (picked) {
                app.frameset_slot = static_cast<int>(i);
                app.frameset_playhead = 0;
            }
            ImGui::PopID();
            // The label is drawn over the selectable, which stays the hit target.
            list_draw->AddText(nullptr, ImGui::GetFontSize(), {row.x + 22.0f, row.y + 2.0f},
                               ImGui::GetColorU32(selected ? kWhite : kWhiteDim), clip.name.c_str());
        }
    } else {
        ImGui::TextColored(kWhiteFaint, "%s", "no animation yet");
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(3);

    // Add / delete row: the + and - icons and the labelled buttons all perform
    // the add and delete of the selected animation.
    auto add_animation = [&app, &slot]() {
        if (slot < 0) {
            app.editor.log("frames: slice a sheet first");
            return;
        }
        auto& set = app.editor.framesets()[static_cast<std::size_t>(slot)];
        gridengine::AnimationClip clip;
        clip.name = "Animation " + std::to_string(set.clips.size() + 1);
        clip.fps = 8.0;
        clip.loop = true;
        clip.delay = 0.0;
        set.clips.push_back(std::move(clip));
        app.frameset_slot = static_cast<int>(set.clips.size()) - 1;
        app.frameset_playhead = 0;
        app.dirty = true;
        app.editor.log("frames: animation added");
    };
    auto remove_animation = [&app, &slot]() {
        if (slot < 0 || app.frameset_slot < 0) return;
        auto& set = app.editor.framesets()[static_cast<std::size_t>(slot)];
        set.clips.erase(set.clips.begin() + app.frameset_slot);
        if (app.frameset_slot >= static_cast<int>(set.clips.size()))
            app.frameset_slot = static_cast<int>(set.clips.size()) - 1;
        app.frameset_playhead = 0;
        app.dirty = true;
        app.editor.log("frames: animation removed");
    };
    const float row_y = kHeight - 62.0f;
    ImGui::SetCursorPos({list_x, row_y});
    if (icon_button("##anim_plus", 17, 24, ImGui::GetColorU32(kWhiteDim))) add_animation();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add animation");
    ImGui::SameLine();
    if (icon_button("##anim_minus", 26, 24, ImGui::GetColorU32(kWhiteDim))) remove_animation();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove animation");
    ImGui::SameLine();
    if (ImGui::Button("Add Animation", {kListW - 62.0f, 24.0f})) add_animation();
    ImGui::SameLine();
    if (icon_button("##anim_trash", 20, 24, ImGui::GetColorU32(kWhiteDim))) remove_animation();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete animation");

    // --- right column: the animation sequence ----------------------------------
    ImGui::SetCursorPos({seq_x, kHeader + 12.0f});
    ImGui::TextColored(kWhiteDim, "%s", "Animation Sequence");

    gridengine::AnimationFrameset* set = slot >= 0 ? &app.editor.framesets()[static_cast<std::size_t>(slot)]
                                                   : nullptr;
    gridengine::AnimationClip* clip =
        set && app.frameset_slot >= 0 &&
                app.frameset_slot < static_cast<int>(set->clips.size())
            ? &set->clips[static_cast<std::size_t>(app.frameset_slot)]
            : nullptr;

    if (set && !set->frames.empty() && clip) {
        const int slots = static_cast<int>(clip->frames.size());
        if (app.frameset_playhead < 0 || app.frameset_playhead >= slots)
            app.frameset_playhead = slots > 0 ? 0 : -1;
        const float cell_w = 74.0f;
        const float label_w = 30.0f;
        const float headers_y = kHeader + 34.0f;
        const float cards_y = headers_y + 22.0f;
        const float cards_h = 66.0f;
        const float strip_x = seq_x + label_w;

        // Row label + frame headers.
        ImGui::SetCursorPos({seq_x, headers_y});
        ImGui::TextColored(kWhiteFaint, "A%d", app.frameset_slot + 1);
        for (int i = 0; i < slots; ++i) {
            const ImVec2 cell{strip_x + static_cast<float>(i) * cell_w, wp.y + headers_y};
            draw->AddRectFilled({cell.x, cell.y}, {cell.x + cell_w - 4.0f, cell.y + 18.0f},
                                ImGui::GetColorU32(kBlack), 3.0f);
            const std::string head = "Frame " + std::to_string(i + 1);
            const ImVec2 head_size = ImGui::CalcTextSize(head.c_str());
            draw->AddText(nullptr, ImGui::GetFontSize() * 0.82f,
                          {cell.x + (cell_w - 4.0f - head_size.x) * 0.5f,
                           cell.y + (18.0f - ImGui::GetTextLineHeight() * 0.85f) * 0.5f},
                          ImGui::GetColorU32(i == app.frameset_playhead ? kMint : kWhiteDim),
                          head.c_str());
        }
        // Playhead: a red line with a marker, sitting between two frames.
        const float head_x = strip_x + static_cast<float>(app.frameset_playhead + 1) * cell_w - 4.0f;
        const float head_top = wp.y + headers_y - 3.0f;
        const float head_bottom = wp.y + cards_y + cards_h + 4.0f;
        draw->AddTriangleFilled({head_x - 5.0f, head_top}, {head_x + 5.0f, head_top},
                                {head_x, head_top + 7.0f}, ImGui::GetColorU32(kAxisX));
        draw->AddLine({head_x, head_top + 4.0f}, {head_x, head_bottom},
                      ImGui::GetColorU32(kAxisX), 1.6f);

        // Frame cards: click selects the slot, the pencil swaps the sheet frame,
        // the triangle previews the animation from that slot.
        for (int i = 0; i < slots; ++i) {
            const float card_x = strip_x + static_cast<float>(i) * cell_w;
            const float card_w = cell_w - 4.0f;
            const ImVec2 card_min{card_x, wp.y + cards_y};
            const ImVec2 card_max{card_x + card_w, wp.y + cards_y + cards_h};
            draw_frame_tile(draw, card_min, card_max, clip->frames[i]);
            const bool selected = i == app.frameset_playhead;
            if (selected)
                draw->AddRect(card_min, card_max, ImGui::GetColorU32(kMint), 4.0f, 0, 2.0f);

            ImGui::SetCursorPos({card_x + 4.0f, cards_y + 4.0f});
            if (icon_button(("##slot" + std::to_string(i)).c_str(), 27, 16.0f,
                            ImGui::GetColorU32(kWhiteDim))) {
                app.frameset_playhead = i;
                app.frameset_sheet_open = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pick the sheet frame for this slot");

            // Click the card body to move the playhead there.
            ImGui::SetCursorPos({card_x, cards_y});
            ImGui::InvisibleButton(("##card" + std::to_string(i)).c_str(),
                                   {card_w, cards_h});
            if (ImGui::IsItemClicked()) {
                app.frameset_playhead = i;
            }

            ImGui::SetCursorPos({card_x + card_w - 20.0f, cards_y + cards_h - 22.0f});
            if (icon_button(("##playslot" + std::to_string(i)).c_str(), 5, 16.0f,
                            ImGui::GetColorU32(kWhite))) {
                if (app.editor.set_animation(slot, app.frameset_slot)) {
                    // Jump straight to the picked slot instead of the first one.
                    gridengine::Sprite2D preview = *app.editor.inspected_entity()->sprite;
                    preview.frame = i;
                    preview.elapsed = 0.0;
                    preview.playing = true;
                    preview.frame_x = set->frames[static_cast<std::size_t>(clip->frames[i])].x;
                    preview.frame_y = set->frames[static_cast<std::size_t>(clip->frames[i])].y;
                    preview.frame_w = set->frames[static_cast<std::size_t>(clip->frames[i])].w;
                    preview.frame_h = set->frames[static_cast<std::size_t>(clip->frames[i])].h;
                    if (app.editor.set_sprite(preview)) {
                        app.frameset_preview = true;
                        app.dirty = true;
                    }
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Preview from this frame");
        }

        // Sheet frame strip: visible when a slot is being re-pointed.
        if (app.frameset_sheet_open && app.frameset_playhead >= 0 &&
            app.frameset_playhead < slots) {
            const float strip_y = cards_y + cards_h + 12.0f;
            ImGui::SetCursorPos({seq_x, strip_y});
            ImGui::TextColored(kWhiteFaint, "%s", "Sheet frames - click to use");
            ImGui::SetCursorPos({strip_x, strip_y + 16.0f});
            ImGui::BeginChild("##sheet_strip", {seq_w - label_w, 34.0f}, ImGuiChildFlags_None);
            const int sheet_count = static_cast<int>(set->frames.size());
            for (int i = 0; i < sheet_count; ++i) {
                const bool active = i == clip->frames[app.frameset_playhead];
                if (i > 0) ImGui::SameLine();
                const ImVec2 tile = ImGui::GetCursorScreenPos();
                draw_frame_tile(ImGui::GetWindowDrawList(), tile, {tile.x + 30.0f, tile.y + 30.0f}, i);
                if (active)
                    ImGui::GetWindowDrawList()->AddRect(
                        tile, {tile.x + 30.0f, tile.y + 30.0f}, ImGui::GetColorU32(kMint), 4.0f, 0,
                        2.0f);
                ImGui::InvisibleButton(("##sheet" + std::to_string(i)).c_str(), {30.0f, 30.0f});
                if (ImGui::IsItemClicked() && active == false)
                    clip->frames[static_cast<std::size_t>(app.frameset_playhead)] = i;
            }
            ImGui::EndChild();
        }
    } else if (set && set->frames.empty()) {
        ImGui::SetCursorPos({seq_x, kHeader + 36.0f});
        ImGui::TextColored(kWhiteFaint, "%s", "Slice the sheet below to build the frames.");
    } else if (!set) {
        ImGui::SetCursorPos({seq_x, kHeader + 36.0f});
        ImGui::TextColored(kWhiteDim, "%s", "This node has no Sprite component yet.");
        ImGui::SetCursorPos({seq_x, kHeader + 58.0f});
        ImGui::TextColored(kWhiteFaint, "%s", "Pick a sheet below to add it.");
    }

    // --- properties ------------------------------------------------------------
    const float props_y = kHeight - 176.0f;
    ImGui::SetCursorPos({seq_x, props_y});
    ImGui::TextColored(kWhiteDim, "%s", "Properties");
    if (clip) {
        const float row = props_y + 22.0f;
        float ms = static_cast<float>(clip->fps > 0.0 ? 1000.0 / clip->fps : 125.0);
        ImGui::SetCursorPos({seq_x, row});
        ImGui::SetNextItemWidth(seq_w - 200.0f);
        if (ImGui::SliderFloat("##speed", &ms, 16.0f, 1000.0f, "%.0f ms")) {
            clip->fps = ms > 0.0f ? 1000.0f / ms : 8.0;
            app.dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Frame", {104.0f, 0.0f})) {
            // Append the first sheet frame this animation does not use yet.
            int next = -1;
            for (std::size_t f = 0; f < set->frames.size(); ++f) {
                if (std::find(clip->frames.begin(), clip->frames.end(), static_cast<int>(f)) ==
                    clip->frames.end()) {
                    next = static_cast<int>(f);
                    break;
                }
            }
            if (next < 0 && !set->frames.empty())
                next = static_cast<int>(set->frames.size()) - 1;
            if (next >= 0) {
                clip->frames.push_back(next);
                app.frameset_playhead = static_cast<int>(clip->frames.size()) - 1;
                app.dirty = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Import", {78.0f, 0.0f})) {
            const SDL_DialogFileFilter filters[]{{"Images", "png;bmp;jpg;tga"}};
            struct SheetPicker {
                static void picked(void* userdata, const char* const* files, int) {
                    auto* self = static_cast<App*>(userdata);
                    if (files && files[0]) self->frameset_import = files[0];
                }
            };
            SDL_ShowOpenFileDialog(SheetPicker::picked, &app, app.window, filters, 1, nullptr,
                                   false);
        }

        const float row2 = row + 26.0f;
        float delay = static_cast<float>(clip->delay * 1000.0);
        ImGui::SetCursorPos({seq_x, row2});
        ImGui::SetNextItemWidth(seq_w - 200.0f);
        if (ImGui::SliderFloat("##delay", &delay, 0.0f, 500.0f, "%.0f ms")) {
            clip->delay = delay / 1000.0;
            app.dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("loop", &clip->loop)) app.dirty = true;

        const float row3 = row2 + 26.0f;
        // The buffer belongs to the animation that is open: without the owner
        // check the field kept the previous animation's name on screen.
        if (app.clip_name_frameset != slot || app.clip_name_slot != app.frameset_slot) {
            app.clip_name_frameset = slot;
            app.clip_name_slot = app.frameset_slot;
            std::snprintf(app.clip_name, sizeof(app.clip_name), "%s", clip->name.c_str());
        }
        ImGui::SetCursorPos({seq_x, row3});
        ImGui::TextColored(kWhiteFaint, "%s", "name");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(seq_w - 60.0f);
        if (ImGui::InputText("##anim_name", app.clip_name, sizeof(app.clip_name))) {
            clip->name = app.clip_name[0] == '\0' ? "Animation" : app.clip_name;
            app.dirty = true;
        }
    }

    // --- sheet slicing ---------------------------------------------------------
    const float sheet_y = props_y + (clip ? 78.0f : 20.0f);
    ImGui::SetCursorPos({seq_x, sheet_y});
    ImGui::TextColored(kWhiteDim, "%s", "Sheet");
    const float sheet_row = sheet_y + 20.0f;
    const std::string texture = set ? set->texture
                                    : (entity->sprite ? entity->sprite->texture : std::string());
    const char* sheet_preview = texture.empty() ? "(none)" : texture.c_str();
    ImGui::SetCursorPos({seq_x, sheet_row});
    ImGui::SetNextItemWidth(seq_w - 96.0f);
    if (ImGui::BeginCombo("##sheet_combo", sheet_preview)) {
        for (const auto& asset : app.assets) {
            const bool selected = texture == asset;
            if (ImGui::Selectable(asset.c_str(), selected) && !selected) {
                if (set) set->texture = asset;
                gridengine::Sprite2D value = entity->sprite ? *entity->sprite : gridengine::Sprite2D{};
                value.texture = asset;
                if (app.editor.set_sprite(value)) app.dirty = true;
                sprite = value;
            }
        }
        if (app.assets.empty()) ImGui::TextColored(kWhiteFaint, "%s", "no textures in the project");
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Slice grid", {84.0f, 0.0f})) {
        const auto [sheet_w, sheet_h] =
            app.textures.texture_size(texture, app.project_root);
        if (sheet_w <= 0 || sheet_h <= 0) {
            app.editor.log("frames: cannot read the sheet size of " + texture);
        } else {
            gridengine::AnimationFrameset sliced;
            if (set) sliced = *set;
            sliced.texture = texture;
            sliced.columns = app.frameset_columns;
            sliced.rows = app.frameset_rows;
            sliced.margin = app.frameset_margin;
            sliced.spacing = app.frameset_spacing;
            sliced.slice_grid(sheet_w, sheet_h);
            if (sliced.frames.empty()) {
                app.editor.log("frames: the grid does not fit the sheet");
            } else {
                if (slot < 0) {
                    slot = app.editor.add_frameset(sliced);
                    set = &app.editor.framesets()[static_cast<std::size_t>(slot)];
                    app.frameset_index = slot;
                    app.frameset_slot = -1;
                    app.frameset_playhead = -1;
                } else {
                    *set = sliced;
                }
                // Drop clip frames that no longer exist after re-slicing.
                for (auto& c : set->clips)
                    c.frames.erase(std::remove_if(c.frames.begin(), c.frames.end(),
                                                  [&sliced](int f) {
                                                      return f < 0 || f >= static_cast<int>(
                                                                        sliced.frames.size());
                                                  }),
                                   c.frames.end());
                // The sheet changed even when no clip is selected yet, so the
                // scene is marked dirty either way.
                (void)app.editor.set_animation(slot, app.frameset_slot);
                app.dirty = true;
                app.editor.log("frames: " + std::to_string(sliced.frames.size()) + " frames (" +
                               std::to_string(sliced.columns) + "x" + std::to_string(sliced.rows) +
                               ")");
            }
        }
    }

    const float grid_row = sheet_row + 26.0f;
    ImGui::SetCursorPos({seq_x, grid_row});
    ImGui::SetNextItemWidth(72.0f);
    if (ImGui::InputInt("columns", &app.frameset_columns)) app.dirty = true;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(72.0f);
    if (ImGui::InputInt("rows", &app.frameset_rows)) app.dirty = true;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(72.0f);
    if (ImGui::InputInt("margin", &app.frameset_margin)) app.dirty = true;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(72.0f);
    if (ImGui::InputInt("spacing", &app.frameset_spacing)) app.dirty = true;
    ImGui::SameLine();
    const auto [sheet_w, sheet_h] = app.textures.texture_size(texture, app.project_root);
    ImGui::TextColored(kWhiteFaint, "%s", sheet_w > 0
                                             ? ("sheet " + std::to_string(sheet_w) + "x" +
                                                std::to_string(sheet_h))
                                                   .c_str()
                                             : "sheet (not loaded)");
    app.frameset_columns = std::clamp(app.frameset_columns, 1, 64);
    app.frameset_rows = std::clamp(app.frameset_rows, 1, 64);
    app.frameset_margin = std::max(app.frameset_margin, 0);
    app.frameset_spacing = std::max(app.frameset_spacing, 0);

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    app.frameset_open = open;
}

void draw_inspector(App& app) {
    const float x = ImGui::GetIO().DisplaySize.x - kInspectorWidth;
    const float height = ImGui::GetIO().DisplaySize.y - kViewTop;
    if (!begin_panel("Inspector", "Inspector", {x, kViewTop}, {kInspectorWidth, height}))
        return end_panel();
    // Header tabs: Inspector shows the selected object's properties, Resources
    // lists the project assets and assigns the selected one to the selection.
    // Both tabs switch the panel content instead of logging a placeholder.
    ImGui::SetCursorPos({4.0f, 4.0f});
    if (text_tab("Inspector", app.inspector_tab == 0, 22.0f)) app.inspector_tab = 0;
    ImGui::SameLine();
    if (text_tab("Resources", app.inspector_tab == 1, 22.0f)) app.inspector_tab = 1;

    if (app.inspector_tab == 1) {
        ImGui::SetCursorPos({10.0f, 38.0f});
        search_field("##resprops", "Search assets...", app.resources_filter, kInspectorWidth - 20.0f);
        ImGui::SetCursorPos({10.0f, 70.0f});
        const float list_h = std::max(40.0f, height - 132.0f);
        ImGui::BeginChild("##insp_assets", {kInspectorWidth - 20.0f, list_h}, ImGuiChildFlags_None);
        ImGui::PushStyleColor(ImGuiCol_Header, kSelection);
        int shown = 0;
        for (const auto& asset : app.assets) {
            if (!app.resources_filter.empty() &&
                asset.find(app.resources_filter) == std::string::npos)
                continue;
            ++shown;
            if (ImGui::Selectable(asset.c_str(), app.selected_asset == asset))
                app.selected_asset = app.selected_asset == asset ? std::string{} : asset;
        }
        ImGui::PopStyleColor();
        if (shown == 0) {
            const char* empty = app.assets.empty() ? "No resources" : "No matches";
            ImGui::SetCursorPosX((kInspectorWidth - ImGui::CalcTextSize(empty).x) * 0.5f - 10.0f);
            ImGui::TextColored(kWhiteFaint, "%s", empty);
        }
        ImGui::EndChild();
        ImGui::SetCursorPos({10.0f, height - 52.0f});
        const bool assignable = app.project_open && !app.selected_asset.empty() &&
                                app.editor.selection().has_value() && !app.editor.playing();
        ImGui::BeginDisabled(!assignable);
        if (ImGui::Button("Assign to selection", {kInspectorWidth - 20.0f, 30.0f})) {
            const bool audio = fs::path(app.selected_asset).extension() == ".wav";
            if (audio) {
                gridengine::AudioSource source;
                source.resource = "assets/" + app.selected_asset;
                source.volume = 1.0f;
                source.loop = true;
                if (app.editor.set_audio(source)) app.dirty = true;
                app.editor.log("assets: " + app.selected_asset + " -> audio source");
            } else {
                gridengine::Sprite2D sprite;
                sprite.texture = "assets/" + app.selected_asset;
                sprite.size = {32.0, 32.0};
                if (app.editor.set_sprite(sprite)) app.dirty = true;
                app.editor.log("assets: " + app.selected_asset + " -> sprite");
            }
        }
        ImGui::EndDisabled();
        if (app.selected_asset.empty() || !app.editor.selection().has_value())
            ImGui::SetCursorPosX(10.0f);
        end_panel();
        return;
    }

    const bool editing = !app.editor.playing() && app.project_open;
    auto* entity = app.editor.inspected_entity();
    if (!entity) {
        const char* hint = "Select an object to view properties";
        const float text_w = ImGui::CalcTextSize(hint).x;
        ImGui::SetCursorPos({(kInspectorWidth - text_w) * 0.5f, height * 0.5f});
        ImGui::TextColored(kWhiteDim, "%s", hint);
        end_panel();
        return;
    }

    ImGui::SetCursorPos({10.0f, 72.0f});
    ImGui::BeginChild("##fields", {kInspectorWidth - 20.0f, height - 80.0f}, ImGuiChildFlags_None);

    // "Grid" section header row, like the reference component block. Only the
    // icon and the label: no decorative pseudo-buttons that react to nothing.
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 row = ImGui::GetCursorScreenPos();
        draw_icon(draw, 21, {row.x + 10.0f, row.y + 10.0f}, 15.0f, ImGui::GetColorU32(kWhiteDim));
        draw->AddText({row.x + 26.0f, row.y + 2.0f}, ImGui::GetColorU32(kWhite), "Grid");
        ImGui::Dummy({0.0f, 24.0f});
        ImGui::Separator();
    }
    ImGui::Dummy({0.0f, 4.0f});

    if (editing) {
        static char name[256]{};
        if (app.editor.selection() != app.script_owner) std::snprintf(name, sizeof(name), "%s", entity->name.c_str());
        if (ImGui::InputText("name", name, sizeof(name))) {
            (void)app.editor.rename_selected(name);
            app.dirty = true;
        }
    } else {
        ImGui::TextColored(kWhite, "%s", entity->name.c_str());
    }

    ImGui::TextUnformatted("Transform");
    float position[2] = {static_cast<float>(entity->transform.position.x),
                         static_cast<float>(entity->transform.position.y)};
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (editing && ImGui::DragFloat2("position", position, 1.0f)) {
        gridengine::Transform2D value = entity->transform;
        value.position = {position[0], position[1]};
        if (app.editor.set_transform(value)) app.dirty = true;
    }
    float rotation = static_cast<float>(entity->transform.rotation);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (editing && ImGui::DragFloat("rotation", &rotation, 1.0f, 0.0f, 0.0f, "%.1f deg")) {
        gridengine::Transform2D value = entity->transform;
        value.rotation = rotation;
        if (app.editor.set_transform(value)) app.dirty = true;
    }
    float scale[2] = {static_cast<float>(entity->transform.scale.x),
                      static_cast<float>(entity->transform.scale.y)};
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (editing && ImGui::DragFloat2("scale", scale, 0.01f, 0.0f, 0.0f, "%.2f")) {
        gridengine::Transform2D value = entity->transform;
        value.scale = {scale[0], scale[1]};
        if (app.editor.set_transform(value)) app.dirty = true;
    }
    ImGui::Separator();

    if (entity->sprite) {
        if (component_header("Sprite", 0, editing) && app.editor.set_sprite(std::nullopt))
            app.dirty = true;
        gridengine::Sprite2D sprite = *entity->sprite;
        static char texture[512]{};
        if (app.editor.selection() != app.script_owner)
            std::snprintf(texture, sizeof(texture), "%s", sprite.texture.c_str());
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (editing && ImGui::InputText("texture", texture, sizeof(texture))) {
            sprite.texture = texture;
            if (app.editor.set_sprite(sprite)) app.dirty = true;
        }
        float size[2] = {static_cast<float>(sprite.size.x), static_cast<float>(sprite.size.y)};
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (editing && ImGui::DragFloat2("size", size, 1.0f, 1.0f)) {
            sprite.size = {size[0], size[1]};
            if (app.editor.set_sprite(sprite)) app.dirty = true;
        }
        if (editing && ImGui::Checkbox("visible", &sprite.visible)) {
            if (app.editor.set_sprite(sprite)) app.dirty = true;
        }
        // Opens the frames window, which slices the sheet and authors the clips.
        if (editing && ImGui::Button("Frames - Animation Sequence...", {-FLT_MIN, 26.0f})) {
            app.frameset_open = true;
            app.editor.log("frames: editor opened");
        }
        if (sprite.animating()) {
            ImGui::TextColored(kWhiteDim, "playing clip %d, frame %d", sprite.clip,
                               sprite.frame < 0 ? 0 : sprite.frame);
        }
    }

    if (entity->collider) {
        if (component_header("Collision", 10, editing) && app.editor.set_collider(std::nullopt))
            app.dirty = true;
        gridengine::Collider2D collider = *entity->collider;
        float extents[2] = {static_cast<float>(collider.half_extents.x),
                            static_cast<float>(collider.half_extents.y)};
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (editing && ImGui::DragFloat2("half extents", extents, 1.0f, 0.0f)) {
            collider.half_extents = {extents[0], extents[1]};
            if (app.editor.set_collider(collider)) app.dirty = true;
        }
        if (editing && ImGui::Checkbox("collider enabled", &collider.enabled)) {
            if (app.editor.set_collider(collider)) app.dirty = true;
        }
    }

    if (entity->motion) {
        if (component_header("CharacterBody2D", 19, editing) && app.editor.set_motion(std::nullopt))
            app.dirty = true;
        gridengine::Motion2D motion = *entity->motion;
        float velocity[2] = {static_cast<float>(motion.velocity.x),
                             static_cast<float>(motion.velocity.y)};
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (editing && ImGui::DragFloat2("velocity", velocity, 1.0f)) {
            motion.velocity = {velocity[0], velocity[1]};
            if (app.editor.set_motion(motion)) app.dirty = true;
        }
        float angular = static_cast<float>(motion.angular_velocity);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (editing && ImGui::DragFloat("angular velocity", &angular, 1.0f)) {
            motion.angular_velocity = angular;
            if (app.editor.set_motion(motion)) app.dirty = true;
        }
    }

    if (entity->camera) {
        if (component_header("Camera", 23, editing) && app.editor.set_camera(std::nullopt))
            app.dirty = true;
        gridengine::Camera2D camera = *entity->camera;
        float zoom = static_cast<float>(camera.zoom);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (editing && ImGui::DragFloat("zoom", &zoom, 0.05f, 0.05f, 8.0f, "%.2f")) {
            camera.zoom = zoom;
            if (app.editor.set_camera(camera)) app.dirty = true;
        }
    }

    if (entity->audio) {
        if (component_header("Audio Source", 25, editing) && app.editor.set_audio(std::nullopt))
            app.dirty = true;
        gridengine::AudioSource source = *entity->audio;
        char resource[512]{};
        std::snprintf(resource, sizeof(resource), "%s", source.resource.string().c_str());
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (editing && ImGui::InputText("wav file", resource, sizeof(resource))) {
            source.resource = resource;
            if (app.editor.set_audio(source)) app.dirty = true;
        }
        float volume = source.volume;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (editing && ImGui::SliderFloat("volume", &volume, 0.0f, 1.0f, "%.2f")) {
            (void)source.set_volume(volume);
            if (app.editor.set_audio(source)) app.dirty = true;
        }
        if (editing && ImGui::Checkbox("loop", &source.loop)) {
            if (app.editor.set_audio(source)) app.dirty = true;
        }
        ImGui::SameLine();
        if (editing && ImGui::Checkbox("mute", &source.muted)) {
            if (app.editor.set_audio(source)) app.dirty = true;
        }
    }

    ImGui::Separator();
    if (component_header("Grid Script", 21, editing && !entity->script.empty()) &&
        app.editor.set_script(""))
        app.dirty = true;
    if (app.script_owner != app.editor.selection()) {
        app.script_owner = app.editor.selection();
        app.script_buffer = entity->script;
    }
    app.script_buffer.resize(2048, '\0');
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (editing && ImGui::InputTextMultiline("##script", app.script_buffer.data(),
                                             app.script_buffer.size(), {0.0f, 120.0f})) {
        const auto text = std::string(app.script_buffer.c_str());
        if (app.editor.set_script(text)) app.dirty = true;
    }
    app.script_buffer.resize(std::strlen(app.script_buffer.c_str()));

    // The node picker: a floating window that opens on "Add node". It lists the
    // whole engine catalog (Core / Visual / Logic / Input / Physics / Custom),
    // so every node type of the reference is reachable, and creates the node
    // with the components the engine actually supports.
    if (editing) {
        ImGui::Spacing();
        if (ImGui::Button("Add node", {-FLT_MIN, 30.0f})) app.add_node_popup = true;
    }
    ImGui::EndChild();
    end_panel();
}

void draw_export(App& app) {
    if (!app.export_popup) return;
    ImGui::OpenPopup("Export project");
    if (ImGui::BeginPopupModal("Export project", &app.export_popup,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        static char path[1024]{};
        if (path[0] == '\0' && !app.export_path.empty())
            std::snprintf(path, sizeof(path), "%s", app.export_path.c_str());
        ImGui::TextUnformatted("Destination");
        ImGui::SetNextItemWidth(420.0f);
        if (ImGui::InputText("##destination", path, sizeof(path))) app.export_path = path;
        ImGui::Checkbox("Release", &app.export_release);
        ImGui::SameLine();
        ImGui::Checkbox("Build executable", &app.export_build);
        ImGui::Separator();
        if (!app.export_status.empty()) ImGui::TextColored(kMint, "%s", app.export_status.c_str());
        if (ImGui::Button("Export", {140.0f, 0.0f})) {
            gridengine::BuildSettings settings;
            settings.build_executable = app.export_build;
            settings.type = app.export_release ? gridengine::BuildType::release
                                               : gridengine::BuildType::debug;
            settings.width = 960;
            settings.height = 600;
            settings.fps_limit = 60;
            const gridengine::ExportToolchain* toolchain_ptr = nullptr;
            gridengine::ExportToolchain toolchain;
            if (settings.build_executable) {
                toolchain = resolve_toolchain(settings.type);
                toolchain_ptr = toolchain.resolvable() ? &toolchain : nullptr;
                if (!toolchain_ptr) app.export_status = "toolchain incomplete: build the engine first";
            }
            if (!settings.build_executable || toolchain_ptr) {
                const auto result = gridengine::export_project(app.project_file, app.export_path,
                                                               settings, toolchain_ptr);
                app.export_status = result ? ("Exported to " + result.directory.string())
                                           : result.error.substr(0, 80);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Close", {140.0f, 0.0f})) {
            app.export_popup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void draw_launcher(App& app) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::Begin("##launcher", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 size = viewport->Size;
    const ImVec2 center{size.x * 0.5f, size.y * 0.40f};
    const float logo = std::min(size.x, size.y) * 0.16f;
    // G mark with a soft mint halo, three times dimmer than the first version.
    for (int ring = 12; ring > 0; --ring) {
        const float t = static_cast<float>(ring) / 12.0f;
        draw->AddCircleFilled({center.x - logo * 0.2f, center.y}, logo * 0.95f * t,
                              IM_COL32(56, 207, 148, static_cast<int>(2.5f + 1.7f * (1.0f - t))),
                              40);
    }
    draw_mini_g(draw, center, logo, ImGui::GetColorU32(kWhite), ImGui::GetColorU32(kEngine));
    const char* word = "GridEngine";
    const float word_w = ImGui::CalcTextSize(word).x * 42.0f / ImGui::GetFontSize();
    draw->AddText(nullptr, 42.0f, {center.x - word_w * 0.5f, center.y + logo * 0.72f},
                  ImGui::GetColorU32(kWhite), "Grid");
    const float grid_w = ImGui::CalcTextSize("Grid").x * 42.0f / ImGui::GetFontSize();
    draw->AddText(nullptr, 42.0f, {center.x - word_w * 0.5f + grid_w, center.y + logo * 0.72f},
                  ImGui::GetColorU32(kEngine), "Engine");
    const char* hint = "open a project to start editing:";
    const float hint_w = ImGui::CalcTextSize(hint).x;
    draw->AddText({center.x - hint_w * 0.5f, center.y + logo * 0.72f + 62.0f},
                  ImGui::GetColorU32(kWhiteDim), hint);
    const char* cmd = "gridengine_editor --project <file.grid>";
    const float cmd_w = ImGui::CalcTextSize(cmd).x;
    draw->AddText({center.x - cmd_w * 0.5f, center.y + logo * 0.72f + 84.0f},
                  ImGui::GetColorU32(kWhite), cmd);
    (void)app;
    ImGui::End();
}

} // namespace

int editor_main(int argc, char** argv) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    App app;
    app.window = SDL_CreateWindow("GridEngine Editor", 1280, 800,
                                  SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!app.window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowMinimumSize(app.window, 1000, 680);
    app.context = SDL_GL_CreateContext(app.window);
    if (!app.context) {
        std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(app.window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(app.window, app.context);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    load_fonts(io);
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = {10.0f, 8.0f};
    style.FramePadding = {6.0f, 4.0f};
    style.ItemSpacing = {6.0f, 4.0f};
    style.ItemInnerSpacing = {6.0f, 3.0f};
    style.WindowBorderSize = 1.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.Colors[ImGuiCol_WindowBg] = kPanel;
    style.Colors[ImGuiCol_ChildBg] = kBlack;
    style.Colors[ImGuiCol_Border] = kBorder;
    style.Colors[ImGuiCol_Text] = kWhite;
    style.Colors[ImGuiCol_TextDisabled] = kWhiteFaint;
    style.Colors[ImGuiCol_FrameBg] = kBlack;
    style.Colors[ImGuiCol_FrameBgHovered] = kPanelHover;
    style.Colors[ImGuiCol_FrameBgActive] = kPanelActive;
    style.Colors[ImGuiCol_Button] = kPanelActive;
    style.Colors[ImGuiCol_ButtonHovered] = kPanelHover;
    style.Colors[ImGuiCol_ButtonActive] = kMintDim;
    style.Colors[ImGuiCol_Header] = kSelection;
    style.Colors[ImGuiCol_HeaderHovered] = kPanelHover;
    style.Colors[ImGuiCol_HeaderActive] = kMintDim;
    style.Colors[ImGuiCol_CheckMark] = kMint;
    style.Colors[ImGuiCol_SliderGrab] = kMint;
    style.Colors[ImGuiCol_SliderGrabActive] = kWhite;
    style.Colors[ImGuiCol_PopupBg] = kPanel;
    style.Colors[ImGuiCol_ModalWindowDimBg] = {0.012f, 0.020f, 0.024f, 0.72f};

    ImGui_ImplSDL3_InitForOpenGL(app.window, app.context);
    ImGui_ImplOpenGL2_Init();

    fs::path project_argument;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--project" && i + 1 < argc) project_argument = argv[++i];
        else if (arg == "--help" || arg == "-h") {
            std::printf("GridEngine Editor\n  gridengine_editor [--project <file.grid>]\n");
            return 0;
        }
    }
    if (!project_argument.empty() && !open_project(app, project_argument)) {
        std::fprintf(stderr, "Cannot open project '%s'\n", project_argument.string().c_str());
        return 2;
    }

    double previous = static_cast<double>(SDL_GetTicks()) / 1000.0;
    while (app.running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                app.running = false;
            if (event.type == SDL_EVENT_KEY_DOWN) {
                const bool ctrl = (event.key.mod & SDL_KMOD_CTRL) != 0;
                if (ctrl && event.key.key == SDLK_S && app.project_open) save_scene(app);
                // F5 / F6 replaced the play and pause buttons of the scene strip.
                if (event.key.key == SDLK_F5 && app.project_open) {
                    if (app.editor.playing()) {
                        app.editor.stop_play();
                        app.play_paused = false;
                        app.editor.log("play: stopped (F5)");
                    } else {
                        app.editor.start_play();
                        app.play_paused = false;
                        app.editor.log("play: started (F5)");
                    }
                }
                if (event.key.key == SDLK_F6 && app.editor.playing()) {
                    app.play_paused = !app.play_paused;
                    app.editor.log(app.play_paused ? "play: paused (F6)" : "play: resumed (F6)");
                }
                if (event.key.key == SDLK_DELETE && app.project_open && !app.editor.playing()) {
                    if (app.editor.remove_selected()) app.dirty = true;
                }
            }
        }

        const double now = static_cast<double>(SDL_GetTicks()) / 1000.0;
        const double delta = std::min(std::max(now - previous, 0.0), 0.1);
        previous = now;
        if (app.editor.playing() && !app.play_paused) app.editor.step(delta);
        // The frameset editor previews sliced clips without entering play mode.
        if (app.frameset_preview && app.project_open && !app.editor.playing())
            (void)app.editor.advance_animations(delta);

        // "Import" in the frames window: the picked image is copied into the
        // project's assets/ folder and becomes the sheet of the open frameset.
        if (!app.frameset_import.empty()) {
            const auto source = fs::path(app.frameset_import);
            app.frameset_import.clear();
            std::error_code ec;
            if (!fs::is_regular_file(source, ec)) {
                app.editor.log("frames: import failed, no such file");
            } else {
                const auto assets = app.project_root / "assets";
                fs::create_directories(assets, ec);
                const auto target = assets / source.filename();
                fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec);
                if (ec) {
                    app.editor.log("frames: cannot import " + source.string());
                } else {
                    refresh_assets(app);
                    const std::string relative = "assets/" + source.filename().string();
                    if (app.frameset_index >= 0 &&
                        app.frameset_index < static_cast<int>(app.editor.framesets().size()))
                        app.editor.framesets()[static_cast<std::size_t>(app.frameset_index)].texture =
                            relative;
                    if (auto* node = app.editor.inspected_entity()) {
                        if (node->sprite) {
                            gridengine::Sprite2D value = *node->sprite;
                            value.texture = relative;
                            (void)app.editor.set_sprite(value);
                        }
                    }
                    app.dirty = true;
                    app.editor.log("frames: imported " + relative);
                }
            }
        }

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();


        if (app.project_open) {
            const float total = ImGui::GetIO().DisplaySize.y - kViewTop;
            const float left_height = total - kResourcesHeight;
            handle_scene_input(app, ImGui::GetIO());
            draw_menu_bar(app);
            draw_tool_bar(app);
            draw_scene_tabs(app);
            draw_scene_subbar(app);
            draw_hierarchy(app, left_height);
            draw_resources(app, kViewTop + left_height);
            draw_console(app, ImGui::GetIO().DisplaySize.y - kConsoleHeight);
            draw_inspector(app);
            draw_export(app);
            node_picker(app);
            draw_frameset(app);
            draw_script_dialog(app);
        } else {
            draw_launcher(app);
        }
        ImGui::Render();

        render(app);

        SDL_GL_SwapWindow(app.window);
    }

    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(app.context);
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    return 0;
}
