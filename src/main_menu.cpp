// GridEngine main menu frontend.
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.
//
// A launcher built the way the reference screenshots show it: a dark stage with
// a single glowing logo, four large action cards, and a quiet footer. Rendering
// is entirely OpenGL + Dear ImGui (OpenGL2 backend): a raw GL layer draws the
// backdrop, ImGui draws logo, cards and dialogs on top. The palette is strictly
// dark slate + mint: a near-black blue-tinted stage, white type and one mint
// accent. Every value derives from the current window size, so the layout never
// overflows.
//
// Actions:
//   New Project  - create dialog, then opens the project in the editor
//   Open Project - file dialog for a .grid descriptor, then opens the editor
//   Import       - folder or archive picker, opens the imported project
//   Settings     - display settings applied immediately (vsync, interface scale)

#include "gridengine/project.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_opengl2.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <filesystem>
#include <string>
#include <vector>

// The project editor lives in the same executable: src/tools/editor_frontend.cpp
// exposes it as editor_main(), and the menu hands the current project over
// instead of spawning a second process.
int editor_main(int argc, char** argv);

namespace {

// ---------------------------------------------------------------- palette ---
// Dark slate + mint, matching the reference art: a neutral charcoal stage
// (exactly the #1B1C20 of the reference photo), white type, the muted slate
// teal of the reference wordmark/segment, and the mint accent for interaction.
constexpr ImVec4 kBlack{0.106f, 0.110f, 0.125f, 1.0f};
constexpr ImVec4 kPanel{0.129f, 0.133f, 0.145f, 1.0f};
constexpr ImVec4 kPanelHover{0.161f, 0.169f, 0.180f, 1.0f};
constexpr ImVec4 kPanelActive{0.200f, 0.212f, 0.223f, 1.0f};
constexpr ImVec4 kMint{0.243f, 0.812f, 0.557f, 1.0f};
constexpr ImVec4 kMintDim{0.153f, 0.435f, 0.325f, 1.0f};
constexpr ImVec4 kBorder{0.216f, 0.231f, 0.243f, 1.0f};
constexpr ImVec4 kWhite{0.910f, 0.910f, 0.918f, 1.0f};
constexpr ImVec4 kWhiteDim{0.545f, 0.561f, 0.580f, 1.0f};
constexpr ImVec4 kWhiteFaint{0.333f, 0.341f, 0.353f, 1.0f};
// The teal of the reference mark: the "G" segment, the bar and the "Engine"
// half of the wordmark all use this desaturated slate teal, not bright mint.
constexpr ImVec4 kEngine{0.315f, 0.533f, 0.510f, 1.0f};

constexpr const char* kEngineVersion = "1.0.0";

// Fonts shipped with Dear ImGui. A crisp TTF at several sizes replaces the
// pixelated default bitmap font, which is what made the old UI look dated.
constexpr const char* kFontBold = "dependencies/imgui/imgui-master/misc/fonts/Roboto-Medium.ttf";
constexpr const char* kFontText = "dependencies/imgui/imgui-master/misc/fonts/DroidSans.ttf";

// Loads the UI fonts if the bundled files are found; otherwise ImGui keeps its
// built-in bitmap font so the frontend still runs everywhere.
void load_fonts(ImGuiIO& io) {
    std::error_code ec;
    const auto base = std::filesystem::current_path(ec);
    const auto bold = base / kFontBold;
    const auto text = base / kFontText;
    if (std::filesystem::exists(bold, ec)) {
        for (const float size : {13.0f, 17.0f, 22.0f, 28.0f, 45.0f, 64.0f})
            io.Fonts->AddFontFromFileTTF(bold.string().c_str(), size);
    } else {
        io.Fonts->AddFontDefault();
    }
    if (std::filesystem::exists(text, ec)) io.Fonts->AddFontFromFileTTF(text.string().c_str(), 28.0f);
}

// Nearest loaded font for a pixel size, so large text renders crisp instead of
// being a small font scaled up.
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

struct App {
    SDL_Window* window{};
    std::string project_name{"MyProject"};
    std::string project_path;
    std::string status{"Choose an action to get started."};
    std::string error;
    // Set when a project was picked: the menu loop exits and hands control to
    // the editor running in this same process.
    std::string handoff;
    bool create_dialog{false};
    bool import_popup{false};
    bool settings_popup{false};
    bool vsync{true};
    int interface_scale{100}; // percent
    enum class Dialog { None, Open, ImportFolder, ImportArchive } pending{Dialog::None};
};

void open_project_in_editor(App& app, const std::filesystem::path& project) {
    auto result = gridengine::open_project(project);
    if (!result) {
        app.error = result.error;
        return;
    }
    app.error.clear();
    app.status = "Opened project: " + result.project.name;
    app.handoff = project.string();
}

void dialog_callback(void* userdata, const char* const* files, int) {
    auto& app = *static_cast<App*>(userdata);
    const auto action = app.pending;
    app.pending = App::Dialog::None;
    if (!files || !files[0]) return;
    const std::filesystem::path selected(files[0]);
    if (action == App::Dialog::Open) {
        std::filesystem::path descriptor = selected;
        if (std::filesystem::is_directory(descriptor)) {
            for (const auto& entry : std::filesystem::directory_iterator(descriptor)) {
                if (entry.path().extension() == ".grid") { descriptor = entry.path(); break; }
            }
        }
        open_project_in_editor(app, descriptor);
    } else if (action == App::Dialog::ImportFolder) {
        std::filesystem::path descriptor;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(selected)) {
            if (entry.is_regular_file() && entry.path().extension() == ".grid") {
                descriptor = entry.path();
                break;
            }
        }
        if (descriptor.empty()) {
            app.error = "No .grid project descriptor found in the selected folder.";
            return;
        }
        open_project_in_editor(app, descriptor);
    } else {
        app.error = "Archive import is not available yet: unpack the archive, then choose Import folder.";
    }
}

void request_open(App& app) {
    const SDL_DialogFileFilter filters[]{{"GridEngine project", "grid"}};
    app.pending = App::Dialog::Open;
    SDL_ShowOpenFileDialog(dialog_callback, &app, app.window, filters, 1, nullptr, false);
}
void request_folder(App& app) {
    app.pending = App::Dialog::ImportFolder;
    SDL_ShowOpenFolderDialog(dialog_callback, &app, app.window, nullptr, false);
}
void request_archive(App& app) {
    const SDL_DialogFileFilter filters[]{{"Project archives", "zip;7z"}};
    app.pending = App::Dialog::ImportArchive;
    SDL_ShowOpenFileDialog(dialog_callback, &app, app.window, filters, 1, nullptr, false);
}
void request_location(App& app) {
    struct LocationPicker {
        static void picked(void* userdata, const char* const* files, int) {
            auto* self = static_cast<App*>(userdata);
            if (files && files[0]) self->project_path = std::filesystem::path(files[0]).string();
        }
    };
    SDL_ShowOpenFolderDialog(LocationPicker::picked, &app, app.window, nullptr, false);
}

// -------------------------------------------------------------- drawing -----
// Raw OpenGL backdrop: a plain near-black stage with a soft mint glow behind
// the logo, drawn before the ImGui frame so the menu sits on top of it.
void draw_background(int width, int height) {
    glViewport(0, 0, width, height);
    glClearColor(kBlack.x, kBlack.y, kBlack.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

// --- logo -------------------------------------------------------------------
// The brand mark from the reference photo: a white "G" stroke whose right side
// is a muted slate-teal segment merging into the teal crossbar, with the
// engineering grid laid over only the upper-left stroke of the ring. No glow:
// in the reference the letter sits flat on the dark canvas.
void draw_logo(ImDrawList* draw, ImVec2 center, float size) {
    const float R = size * 0.5f;   // outer radius of the ring
    const float r = size * 0.345f;  // inner radius (counter) of the ring
    const float mid = (R + r) * 0.5f;
    const float stroke = R - r;
    const ImU32 white = ImGui::GetColorU32(kWhite);
    const ImU32 teal = ImGui::GetColorU32(kEngine);

    // Angles are screen space: 0 = right, positive = clockwise (y grows down).
    // The right side of the G is teal (upper right through lower right); the
    // rest of the ring is the white stroke, exactly like the reference mark.
    const float teal_lo = -1.05f;
    const float teal_hi = 0.72f;

    // The ring is one clean band: a thick stroked arc, so there are no radial
    // seams between segments the way per-quad fills produce.
    draw->PathClear();
    draw->PathArcTo(center, mid, teal_hi, teal_lo + 6.2831853f, 64);
    draw->PathStroke(white, ImDrawFlags_None, stroke);

    draw->PathClear();
    draw->PathArcTo(center, mid, teal_lo, teal_hi, 32);
    draw->PathStroke(teal, ImDrawFlags_None, stroke);

    // Crossbar: the G's horizontal stroke, merging into the teal right side and
    // stopping at the outer radius so it never spills past the letter.
    const float bar_h = (R - r) * 0.90f;
    draw->AddRectFilled({center.x + r * 0.10f, center.y - bar_h * 0.5f},
                        {center.x + R, center.y + bar_h * 0.5f}, teal);

    // Engineering grid, clipped to the upper-left stroke of the ring only: both
    // radii bound each line, so it never floods the counter or the right side.
    const ImU32 grid_line = IM_COL32(52, 148, 114, 150);
    const float step = size / 11.0f;
    const float grid_th = std::max(1.5f, size * 0.011f);
    for (float x = -R + step * 0.5f; x < 0.0f; x += step) {
        const float outer = std::sqrt(std::max(0.0f, R * R - x * x));
        const float inner = std::sqrt(std::max(0.0f, r * r - x * x));
        if (outer - inner < 2.0f) continue;
        draw->AddLine({center.x + x, center.y - outer}, {center.x + x, center.y - inner},
                      grid_line, grid_th);
    }
    for (float y = -R + step * 0.5f; y < 0.0f; y += step) {
        const float outer = std::sqrt(std::max(0.0f, R * R - y * y));
        const float inner = std::sqrt(std::max(0.0f, r * r - y * y));
        if (outer - inner < 2.0f) continue;
        draw->AddLine({center.x - outer, center.y + y}, {center.x - inner, center.y + y},
                      grid_line, grid_th);
    }
}

// --- card icons (outline style, matching the reference) ---------------------
// Document with a folded corner and a plus badge, for New Project.
void icon_new_project(ImDrawList* draw, ImVec2 center, float s, ImU32 color) {
    const float w = s * 0.62f, h = s * 0.80f;
    const ImVec2 min{center.x - w * 0.5f, center.y - h * 0.5f};
    const float fold = w * 0.26f;
    const float th = s * 0.055f;
    draw->PathClear();
    draw->PathLineTo({min.x, min.y});
    draw->PathLineTo({min.x + w - fold, min.y});
    draw->PathLineTo({min.x + w, min.y + fold});
    draw->PathLineTo({min.x + w, min.y + h});
    draw->PathLineTo({min.x, min.y + h});
    draw->PathStroke(color, th, ImDrawFlags_Closed);
    draw->AddLine({min.x + w - fold, min.y}, {min.x + w - fold, min.y + fold}, color, th * 0.8f);
    draw->AddLine({min.x + w - fold, min.y + fold}, {min.x + w, min.y + fold}, color, th * 0.8f);
    // Plus badge at the bottom-right corner.
    const float p = s * 0.16f;
    const ImVec2 pc{min.x + w * 0.86f, min.y + h * 0.92f};
    draw->AddCircleFilled(pc, p * 1.15f, ImGui::GetColorU32(kPanel), 20);
    draw->AddCircle(pc, p * 1.15f, color, 20, th * 0.85f);
    draw->AddLine({pc.x - p * 0.55f, pc.y}, {pc.x + p * 0.55f, pc.y}, color, th * 0.85f);
    draw->AddLine({pc.x, pc.y - p * 0.55f}, {pc.x, pc.y + p * 0.55f}, color, th * 0.85f);
}
// Folder outline with a back tab, for Open Project.
void icon_open(ImDrawList* draw, ImVec2 center, float s, ImU32 color) {
    const float w = s * 0.86f, h = s * 0.64f;
    const ImVec2 min{center.x - w * 0.5f, center.y - h * 0.32f};
    const float tab = w * 0.34f;
    const float th = s * 0.055f;
    draw->PathClear();
    draw->PathLineTo({min.x, min.y});
    draw->PathLineTo({min.x + tab, min.y});
    draw->PathLineTo({min.x + tab + w * 0.12f, min.y + h * 0.22f});
    draw->PathLineTo({min.x + w, min.y + h * 0.22f});
    draw->PathLineTo({min.x + w, min.y + h});
    draw->PathLineTo({min.x, min.y + h});
    draw->PathStroke(color, th, ImDrawFlags_Closed);
}
// Tray with an arrow leaving it, for Import.
void icon_import(ImDrawList* draw, ImVec2 center, float s, ImU32 color) {
    const float w = s * 0.78f, h = s * 0.42f;
    const float th = s * 0.055f;
    const ImVec2 min{center.x - w * 0.5f, center.y - h * 0.5f + s * 0.16f};
    draw->PathClear();
    draw->PathLineTo({min.x, min.y});
    draw->PathLineTo({min.x, min.y + h});
    draw->PathLineTo({min.x + w, min.y + h});
    draw->PathLineTo({min.x + w, min.y});
    draw->PathStroke(color, th);
    const float top = center.y - s * 0.42f;
    draw->AddLine({center.x, top + s * 0.34f}, {center.x, top + s * 0.02f}, color, th);
    draw->AddLine({center.x - s * 0.13f, top + s * 0.16f}, {center.x, top + s * 0.02f}, color, th);
    draw->AddLine({center.x + s * 0.13f, top + s * 0.16f}, {center.x, top + s * 0.02f}, color, th);
}
// Gear with eight teeth, for Settings.
void icon_settings(ImDrawList* draw, ImVec2 center, float s, ImU32 color) {
    const float r_out = s * 0.40f, r_root = s * 0.30f, r_hub = s * 0.125f;
    const float th = s * 0.05f;
    draw->PathClear();
    for (int i = 0; i < 8; ++i) {
        const double a0 = 6.2831853 * (i - 0.5) / 8.0;
        const double a1 = 6.2831853 * (i - 0.22) / 8.0;
        const double a2 = 6.2831853 * (i + 0.22) / 8.0;
        const double a3 = 6.2831853 * (i + 0.5) / 8.0;
        draw->PathLineTo({center.x + static_cast<float>(std::cos(a0) * r_root),
                          center.y + static_cast<float>(std::sin(a0) * r_root)});
        draw->PathLineTo({center.x + static_cast<float>(std::cos(a1) * r_out),
                          center.y + static_cast<float>(std::sin(a1) * r_out)});
        draw->PathLineTo({center.x + static_cast<float>(std::cos(a2) * r_out),
                          center.y + static_cast<float>(std::sin(a2) * r_out)});
        draw->PathLineTo({center.x + static_cast<float>(std::cos(a3) * r_root),
                          center.y + static_cast<float>(std::sin(a3) * r_root)});
    }
    draw->PathStroke(color, th, ImDrawFlags_Closed);
    draw->AddCircle(center, r_hub, color, 20, th);
}

// --- action card ------------------------------------------------------------
// Horizontal card: outline icon on the left, label on the right, the pair
// centred inside a rounded bordered box (the reference layout).
bool action_card(const char* label, const char* hint, int icon, ImVec2 size, float scale) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    const ImGuiID id = window->GetID(label);
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, {pos.x + size.x, pos.y + size.y});
    ImGui::ItemSize(size, 0.0f);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered{}, held{};
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    const ImU32 fill = held ? ImGui::GetColorU32(kPanelActive)
                            : hovered ? ImGui::GetColorU32(kPanelHover)
                                      : ImGui::GetColorU32(kPanel);
    const ImU32 border = ImGui::GetColorU32(hovered || held ? kMintDim : kBorder);
    const ImU32 icon_color = ImGui::GetColorU32(kWhite);
    ImDrawList* draw = window->DrawList;
    draw->AddRectFilled(bb.Min, bb.Max, fill, 10.0f);
    draw->AddRect(bb.Min, bb.Max, border, 10.0f, 0, hovered || held ? 1.5f : 1.0f);

    const float icon_size = std::min(size.y * 0.52f, 48.0f * scale);
    const float label_size = 17.0f * scale;
    ImFont* label_font = font_at(label_size);
    const float gap = 16.0f * scale;
    const float text_w = label_font->CalcTextSizeA(label_size, FLT_MAX, 0.0f, label).x;
    const float group_w = icon_size + gap + text_w;
    const float x0 = bb.Min.x + (size.x - group_w) * 0.5f;
    const float cy = bb.GetCenter().y;
    const ImVec2 icon_center{x0 + icon_size * 0.5f, cy};
    if (icon == 0) icon_new_project(draw, icon_center, icon_size, icon_color);
    else if (icon == 1) icon_open(draw, icon_center, icon_size, icon_color);
    else if (icon == 2) icon_import(draw, icon_center, icon_size, icon_color);
    else icon_settings(draw, icon_center, icon_size, icon_color);

    draw->AddText(label_font, label_size, {x0 + icon_size + gap, cy - label_size * 0.36f},
                  ImGui::GetColorU32(kWhite), label);
    (void)hint;
    return pressed;
}

// --- dialogs ------------------------------------------------------------
// Modals use the same slate + mint theme as the rest of the menu: no title
// bar (so there is no duplicate close button at the top), a themed header
// line, and a single row of actions at the bottom.
bool begin_dialog(const char* id, float width) {
    ImGui::SetNextWindowSize({width, 0.0f}, ImGuiCond_Appearing);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, kPanel);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kPanel);
    ImGui::PushStyleColor(ImGuiCol_Border, kBorder);
    ImGui::PushStyleColor(ImGuiCol_Separator, kBorder);
    ImGui::PushStyleColor(ImGuiCol_Button, kPanelActive);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kPanelHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kMintDim);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, kBlack);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, kPanelHover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, kPanelActive);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {18.0f, 16.0f});
    const bool open = ImGui::BeginPopupModal(
        id, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
    if (!open) {
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(10);
    }
    return open;
}
void end_dialog() {
    ImGui::EndPopup();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(10);
}
// Dialog header: mint label plus a hairline separator, matching the panel
// headers of the project editor.
void dialog_header(const char* label) {
    ImGui::PushFont(font_at(17.0f));
    ImGui::TextColored(kMint, "%s", label);
    ImGui::PopFont();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}

void draw_import_dialog(App& app) {
    if (app.import_popup) ImGui::OpenPopup("Import project");
    if (!begin_dialog("Import project", 470.0f)) return;
    dialog_header("Import project");
    ImGui::TextColored(kWhiteDim, "%s", "Select an unpacked project folder or an archive.");
    ImGui::Spacing();
    if (ImGui::Button("Choose folder", {208.0f, 34.0f})) {
        app.import_popup = false;
        ImGui::CloseCurrentPopup();
        request_folder(app);
    }
    ImGui::SameLine();
    if (ImGui::Button("Choose archive (.zip/.7z)", {208.0f, 34.0f})) {
        app.import_popup = false;
        ImGui::CloseCurrentPopup();
        request_archive(app);
    }
    ImGui::Spacing();
    if (ImGui::Button("Cancel", {424.0f, 32.0f})) {
        app.import_popup = false;
        ImGui::CloseCurrentPopup();
    }
    end_dialog();
}

void draw_settings_dialog(App& app) {
    // The popup ID must not collide with the "Settings" card's item ID, so it
    // uses its own name; the visible title comes from the dialog header.
    if (app.settings_popup) ImGui::OpenPopup("##settings_popup");
    if (!begin_dialog("##settings_popup", 400.0f)) return;
    dialog_header("Settings");
    ImGui::TextColored(kWhite, "%s", "Display");
    ImGui::Spacing();
    if (ImGui::Checkbox("Vertical sync", &app.vsync)) SDL_GL_SetSwapInterval(app.vsync ? 1 : 0);
    ImGui::Spacing();
    ImGui::TextColored(kWhiteDim, "%s", "Interface scale");
    int scale_percent = app.interface_scale;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderInt("##iscale", &scale_percent, 70, 160, "%d%%")) {
        app.interface_scale = std::clamp(scale_percent, 70, 160);
        ImGui::GetStyle().ScaleAllSizes(app.interface_scale / 100.0f);
    }
    ImGui::Spacing();
    ImGui::TextColored(kWhiteFaint, "%s", "Applied immediately.");
    ImGui::Spacing();
    if (ImGui::Button("Close", {364.0f, 32.0f})) {
        app.settings_popup = false;
        ImGui::CloseCurrentPopup();
    }
    end_dialog();
}

void draw_create_dialog(App& app) {
    if (app.create_dialog) ImGui::OpenPopup("Create project");
    if (!begin_dialog("Create project", 470.0f)) return;
    dialog_header("Create project");
    char name[256]{};
    char path[1024]{};
    std::copy_n(app.project_name.c_str(), std::min(app.project_name.size(), sizeof(name) - 1), name);
    std::copy_n(app.project_path.c_str(), std::min(app.project_path.size(), sizeof(path) - 1), path);

    ImGui::TextColored(kWhiteDim, "%s", "Project name");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##name", name, sizeof(name));
    ImGui::Spacing();
    ImGui::TextColored(kWhiteDim, "%s", "Save location");
    ImGui::SetNextItemWidth(320.0f);
    ImGui::InputText("##path", path, sizeof(path));
    ImGui::SameLine();
    if (ImGui::Button("Browse...", {94.0f, 0.0f})) request_location(app);
    app.project_name = name;
    app.project_path = path;

    ImGui::Spacing();
    if (ImGui::Button("Create", {208.0f, 34.0f})) {
        const auto result = gridengine::create_project(std::filesystem::path(path) / name, name);
        if (result) {
            app.create_dialog = false;
            ImGui::CloseCurrentPopup();
            app.handoff = result.project.file.string();
        } else {
            app.error = result.error;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", {208.0f, 34.0f})) {
        app.create_dialog = false;
        ImGui::CloseCurrentPopup();
    }
    end_dialog();
}

void menu(App& app) {
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 size = io.DisplaySize;

    ImGui::SetNextWindowPos({0.0f, 0.0f});
    ImGui::SetNextWindowSize(size);
    ImGui::Begin("##menu", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    const float scale = std::clamp(std::min(size.x / 1280.0f, size.y / 760.0f), 0.5f, 1.6f);
    const float margin = std::max(16.0f, std::min(size.x, size.y) * 0.045f);

    // --- logo + wordmark ----------------------------------------------------
    const float logo_size = std::min(210.0f * scale, size.y * 0.30f);
    const ImVec2 logo_center{size.x * 0.5f, size.y * 0.30f + logo_size * 0.12f};
    draw_logo(draw, logo_center, logo_size);

    const char* wordmark_l = "Grid";
    const char* wordmark_r = "Engine";
    const float mark_size = 46.0f * scale;
    ImFont* mark_font = font_at(mark_size);
    const float ratio = mark_size / mark_font->LegacySize;
    const float wordmark_w = mark_font->CalcTextSizeA(mark_size, FLT_MAX, 0.0f, wordmark_l).x +
                             mark_font->CalcTextSizeA(mark_size, FLT_MAX, 0.0f, wordmark_r).x;
    const float wordmark_x = (size.x - wordmark_w) * 0.5f;
    const float wordmark_y = logo_center.y + logo_size * 0.56f + 20.0f * scale;
    draw->AddText(mark_font, mark_size, {wordmark_x, wordmark_y},
                  ImGui::GetColorU32(kWhite), wordmark_l);
    draw->AddText(mark_font, mark_size,
                  {wordmark_x + mark_font->CalcTextSizeA(mark_size, FLT_MAX, 0.0f, wordmark_l).x,
                   wordmark_y},
                  ImGui::GetColorU32(kEngine), wordmark_r);
    (void)ratio;

    // --- four action cards --------------------------------------------------
    const float gap = 22.0f * scale;
    const float footer_h = 40.0f * scale;
    const float cards_top = wordmark_y + mark_size + 72.0f * scale;
    const float available = std::max(120.0f, size.y - cards_top - footer_h - margin);
    const bool stacked = size.x < 900.0f;
    const float card_w = stacked ? std::min(size.x - margin * 2.0f, 460.0f)
                                 : std::min((size.x - margin * 2.0f - gap * 3.0f) / 4.0f, 262.0f * scale);
    const float card_h = stacked ? std::min(available / 4.0f - gap, 96.0f)
                                 : std::min(available, 92.0f * scale);
    const float row_w = stacked ? card_w : card_w * 4.0f + gap * 3.0f;
    const float left = std::max(margin, (size.x - row_w) * 0.5f);

    struct Entry { const char* label; const char* hint; int icon; };
    const Entry entries[] = {
        {"New Project", "Create a project folder", 0},
        {"Open Project", "Open a .grid descriptor", 1},
        {"Import", "Import an existing project", 2},
        {"Settings", "Display and editor options", 3},
    };
    for (int i = 0; i < 4; ++i) {
        ImGui::SetCursorPos({left + (stacked ? 0.0f : static_cast<float>(i) * (card_w + gap)),
                             cards_top + (stacked ? static_cast<float>(i) * (card_h + gap) : 0.0f)});
        if (action_card(entries[i].label, entries[i].hint, entries[i].icon, {card_w, card_h}, scale)) {
            if (i == 0) app.create_dialog = true;
            else if (i == 1) request_open(app);
            else if (i == 2) app.import_popup = true;
            else app.settings_popup = true;
        }
    }

    // --- footer -------------------------------------------------------------
    // The reference has no footer chrome; keep status only as quiet text.
    const float footer_y = size.y - margin - 18.0f * scale;
    if (!app.error.empty()) {
        draw->AddText(nullptr, ImGui::GetFontSize() * scale, {margin, footer_y},
                      ImGui::GetColorU32(kMint), app.error.c_str());
    }
    if (!app.status.empty()) {
        draw->AddText(nullptr, ImGui::GetFontSize() * (0.85f * scale),
                      {margin, footer_y + ImGui::GetFontSize() * scale + 2.0f},
                      ImGui::GetColorU32(kWhiteFaint), app.status.c_str());
    }
    const std::string version = std::string("GridEngine ") + kEngineVersion;
    const ImVec2 version_size = ImGui::CalcTextSize(version.c_str());
    draw->AddText({size.x - margin - version_size.x, footer_y},
                  ImGui::GetColorU32(kWhiteFaint), version.c_str());

    // --- dialogs ------------------------------------------------------------
    draw_import_dialog(app);
    draw_settings_dialog(app);
    draw_create_dialog(app);

    ImGui::End();
}

} // namespace

int main(int argc, char** argv) {
    // `--project <file.grid>` skips the menu and opens the editor directly.
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--project" && i + 1 < argc) {
            const char* editor_argv[] = {argv[0], "--project", argv[i + 1]};
            return editor_main(3, const_cast<char**>(editor_argv));
        }
        if (arg == "--help" || arg == "-h") {
            std::printf("GridEngine\n  gridengine_editor [--project <file.grid>]\n");
            return 0;
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    App app;
    app.window = SDL_CreateWindow("GridEngine", 1180, 760,
                                  SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!app.window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowMinimumSize(app.window, 900, 560);
    SDL_GLContext context = SDL_GL_CreateContext(app.window);
    if (!context) {
        std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(app.window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(app.window, context);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = {0.0f, 0.0f};
    style.WindowRounding = 0.0f;
    style.Colors[ImGuiCol_WindowBg] = {0.0f, 0.0f, 0.0f, 0.0f};
    style.Colors[ImGuiCol_PopupBg] = kPanel;
    style.Colors[ImGuiCol_Border] = kBorder;
    style.Colors[ImGuiCol_Text] = kWhite;
    style.Colors[ImGuiCol_TextDisabled] = kWhiteFaint;
    style.Colors[ImGuiCol_FrameBg] = kBlack;
    style.Colors[ImGuiCol_FrameBgHovered] = kPanelHover;
    style.Colors[ImGuiCol_FrameBgActive] = kPanelActive;
    style.Colors[ImGuiCol_Button] = kPanelActive;
    style.Colors[ImGuiCol_ButtonHovered] = kPanelHover;
    style.Colors[ImGuiCol_ButtonActive] = kMintDim;
    style.Colors[ImGuiCol_CheckMark] = kMint;
    style.Colors[ImGuiCol_SliderGrab] = kMint;
    style.Colors[ImGuiCol_SliderGrabActive] = kWhite;
    style.Colors[ImGuiCol_ModalWindowDimBg] = {0.012f, 0.020f, 0.024f, 0.72f};

    // Load the bundled TTF fonts before the backend builds the atlas; without
    // this call the UI falls back to the pixelated default bitmap font.
    load_fonts(io);

    ImGui_ImplSDL3_InitForOpenGL(app.window, context);
    ImGui_ImplOpenGL2_Init();

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                running = false;
            }
        }
        // A project was picked: leave the menu and open the editor in this
        // same process.
        if (!app.handoff.empty()) running = false;
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        menu(app);
        ImGui::Render();

        int width = 0, height = 0;
        SDL_GetWindowSizeInPixels(app.window, &width, &height);
        draw_background(width, height);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(app.window);
    }

    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(app.window);
    SDL_Quit();

    if (!app.handoff.empty()) {
        const char* editor_argv[] = {argv[0], "--project", app.handoff.c_str()};
        return editor_main(3, const_cast<char**>(editor_argv));
    }
    return 0;
}
