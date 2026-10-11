// GridEngine - tests/scene_frameset.cpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#include "gridengine/scene.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

// Frameset animation (GridScene 7): grid slicing of a sheet into cells, clip
// authoring, playback that walks the clip frames at the clip fps, the save /
// load round trip, and rejection of malformed animation data.
namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("  %-56s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) ++failures;
}
} // namespace

int main() {
    using gridengine::Scene;
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / "gridengine_scene_frameset_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "scenes", ec);

    Scene scene;
    const auto hero = scene.create_entity("Hero");
    check(hero != 0, "create an entity for the walk sheet");
    scene.find(hero)->sprite = gridengine::Sprite2D{};

    // A 64x32 sheet sliced 4x1 with no insets: four 16x32 cells.
    gridengine::AnimationFrameset set;
    set.texture = "assets/hero.png";
    set.columns = 4;
    set.rows = 1;
    set.margin = 0;
    set.spacing = 0;
    set.slice_grid(64, 32);
    check(set.frames.size() == 4, "slice_grid cuts one frame per column");
    if (set.frames.size() == 4) {
        check(set.frames[0].x == 0 && set.frames[0].y == 0 && set.frames[0].w == 16 &&
                  set.frames[0].h == 32,
              "the first cell is the sheet's top-left 16x32");
        check(set.frames[3].x == 48 && set.frames[3].w == 16, "the last cell covers the right edge");
    }
    // Margin and spacing shrink the cells and offset them: for a 32x32 sheet
    // with margin 2 and spacing 2 and a 2x2 grid, the usable area is 28x28 and
    // the cells between the gaps are 13x13, starting at (2,2), (17,2), (2,17).
    gridengine::AnimationFrameset padded;
    padded.columns = 2;
    padded.rows = 2;
    padded.margin = 2;
    padded.spacing = 2;
    padded.slice_grid(32, 32);
    if (padded.frames.size() == 4) {
        check(padded.frames[0].x == 2 && padded.frames[0].y == 2, "the margin offsets the first cell");
        check(padded.frames[1].x == 17, "the spacing separates the columns");
        check(padded.frames[2].y == 17, "the spacing separates the rows");
        check(padded.frames[0].w == 13 && padded.frames[0].h == 13,
              "margin+spacing shrink the cells to 13x13");
    }
    check(gridengine::AnimationFrameset{}.frames.empty(), "a default frameset has no frames");
    gridengine::AnimationFrameset too_big;
    too_big.columns = 8;
    too_big.rows = 1;
    too_big.margin = 100;
    too_big.slice_grid(64, 64);
    check(too_big.frames.empty(), "an impossible margin produces no frames");

    // Clips: a walk over the first three frames and an idle over the last one.
    gridengine::AnimationClip walk;
    walk.name = "walk";
    walk.fps = 10.0;
    walk.loop = true;
    walk.frames = {0, 1, 2, 3};
    gridengine::AnimationClip idle;
    idle.name = "idle";
    idle.fps = 2.0;
    idle.loop = true;
    idle.frames = {3};
    set.clips = {idle, walk};
    const int slot = scene.add_frameset(set);
    check(slot == 0, "add_frameset returns the frameset index");
    check(scene.frameset(slot) != nullptr, "the frameset is readable back from the scene");

    check(scene.set_animation(hero, slot, 1), "attach the walk clip to the hero");
    const auto* entity = scene.find(hero);
    check(entity && entity->sprite.has_value(), "the hero has a sprite");
    if (entity && entity->sprite) {
        check(entity->sprite->frameset == slot, "the sprite records the frameset index");
        check(entity->sprite->texture == "assets/hero.png", "the frameset's texture is adopted");
        check(entity->sprite->frame_w == 16 && entity->sprite->frame_h == 32,
              "the sprite resolves the first frame's rect");
        check(entity->sprite->frame == 0, "the clip starts on its first frame");
    }
    check(!scene.set_animation(hero, slot, 99), "an unknown clip index is rejected");
    check(!scene.set_animation(999, slot, 0), "an unknown entity is rejected");

    // Playback: at 10 fps, 0.35s advances three frames...
    (void)scene.advance_animation(hero, 0.35);
    check(scene.find(hero)->sprite->frame == 3, "the clip advanced three frames");
    check(scene.find(hero)->sprite->frame_x == 48, "the sprite shows the fourth cell");
    // ...and the looping clip wraps back to the first cell.
    (void)scene.advance_animation(hero, 0.15);
    check(scene.find(hero)->sprite->frame == 0, "the clip wraps around when it loops");
    // A negative delta never moves playback backwards.
    const int held = scene.find(hero)->sprite->frame;
    check(!scene.advance_animation(hero, -1.0), "a negative delta is rejected");
    check(scene.find(hero)->sprite->frame == held, "the frame is unchanged by a negative delta");

    // A non-looping clip stops on its last frame and pauses playback.
    check(scene.set_animation(hero, 0, 0), "play the single-frame idle clip");
    gridengine::AnimationClip once = idle;
    once.loop = false;
    once.frames = {0, 1};
    once.fps = 4.0;
    scene.frameset(slot)->clips.front() = once;
    (void)scene.set_animation(hero, 0, 0);
    (void)scene.advance_animation(hero, 0.5);
    check(scene.find(hero)->sprite->frame == 1, "a non-looping clip reaches its last frame");
    check(!scene.find(hero)->sprite->playing, "a non-looping clip pauses on its last frame");
    scene.find(hero)->sprite->playing = true;

    // Pausing playback freezes the frame.
    (void)scene.set_animation(hero, 1, 0);
    scene.find(hero)->sprite->playing = false;
    const int frozen = scene.find(hero)->sprite->frame;
    check(!scene.advance_animation(hero, 1.0), "a paused sprite does not advance");
    check(scene.find(hero)->sprite->frame == frozen, "the paused frame is unchanged");

    // Round trip through the GridScene 7 text format.
    const auto file = dir / "scenes" / "main.scene";
    scene.find(hero)->sprite->playing = true;
    check(scene.set_animation(hero, slot, 1), "re-play the walk clip before saving");
    (void)scene.advance_animation(hero, 0.15);
    check(scene.save(file), "save a scene with a frameset animation");
    {
        std::ifstream in(file, std::ios::binary);
        std::string signature;
        int format{};
        check(static_cast<bool>(in >> signature >> format) && signature == "GridScene" && format == 7,
              "the writer bumps the format to 7 when framesets exist");
    }
    const auto loaded = Scene::load(file);
    check(loaded.has_value(), "load the GridScene 7 file back");
    if (loaded) {
        check(loaded->animation_framesets().size() == 1, "the frameset survives the round trip");
        check(loaded->find(hero) != nullptr, "the entity survives the round trip");
        const auto* sprite = loaded->find(hero)->sprite.has_value()
                                 ? &*loaded->find(hero)->sprite
                                 : nullptr;
        check(sprite && sprite->frameset == 0, "the sprite keeps its frameset index");
        check(sprite && sprite->clip == 1, "the sprite keeps its clip index");
        check(sprite && sprite->frame == 1, "the sprite keeps its current frame");
        check(sprite && sprite->frame_x == 16, "the sprite keeps its resolved frame rect");
        const auto* clip = loaded->frameset(0)->clip(1);
        check(clip != nullptr, "the clip is readable back");
        check(clip && clip->name == "walk" && clip->fps == 10.0 && clip->loop,
              "the clip name, fps and loop flag survive");
        check(clip && clip->frames.size() == 4 && clip->frames.front() == 0 &&
                  clip->frames.back() == 3,
              "the clip frame list survives");
        // The loaded animation keeps playing from where it was saved.
        Scene replay = std::move(*loaded);
        check(replay.advance_animation(hero, 0.1), "the loaded clip still advances");
        check(replay.find(hero)->sprite->frame == 2, "playback continues after a reload");
    }

    // Malformed animation records must be rejected.
    {
        const auto bad_frameset = dir / "scenes" / "bad_frameset.scene";
        { std::ofstream out(bad_frameset, std::ios::binary);
          out << "GridScene 7\nFS \"assets/hero.png\" 0 1 0 0 0\n"; }
        check(!Scene::load(bad_frameset).has_value(), "a frameset without columns is rejected");
    }
    {
        const auto bad_index = dir / "scenes" / "bad_index.scene";
        { std::ofstream out(bad_index, std::ios::binary);
          out << "GridScene 7\nE 1 0 \"Hero\" 0 0 0 1 1 0 0 1 \"assets/hero.png\" 16 32 1 4294967295 "
                 "5 0 0 0 1 0 0 16 32\n"; }
        check(!Scene::load(bad_index).has_value(), "a frameset index beyond the file is rejected");
    }
    {
        const auto bad_clip = dir / "scenes" / "bad_clip.scene";
        { std::ofstream out(bad_clip, std::ios::binary);
          out << "GridScene 7\nFS \"assets/hero.png\" 1 1 0 0 1 0 0 1 1 1 \"walk\" 0 1 1 7\n"
                 "E 1 0 \"Hero\" 0 0 0 1 1 0 0 1 \"assets/hero.png\" 16 32 1 4294967295 "
                 "0 0 0 0 1 0 0 1 1\n"; }
        check(!Scene::load(bad_clip).has_value(), "a clip pointing past its frames is rejected");
    }
    // Legacy formats still load: a 1-6 scene keeps animated sprites disabled.
    {
        const auto legacy = dir / "scenes" / "legacy.scene";
        { std::ofstream out(legacy, std::ios::binary);
          out << "GridScene 5\nE 1 0 \"Hero\" 80 340 0 1 1 0 0 1 \"hero.png\" 48 48 1 4294967295 1 1 1 \"\"\n"; }
        const auto old = Scene::load(legacy);
        check(old.has_value(), "a format 5 scene still loads");
        check(old && old->find(1) && !old->find(1)->sprite->animating(),
              "legacy sprites keep their plain (unanimated) behaviour");
    }
    // A scene with plain sprites is still written at the legacy format.
    {
        Scene plain;
        (void)plain.create_entity("Box");
        plain.find(1)->sprite = gridengine::Sprite2D{};
        const auto plain_file = dir / "scenes" / "plain.scene";
        check(plain.save(plain_file), "save a scene without framesets");
        std::ifstream in(plain_file, std::ios::binary);
        std::string header;
        in >> header;
        check(header == "GridScene" && header != "GridScene 7", "plain scenes keep the old format");
    }

    // A duplicated entity shares the frameset but keeps its own playback.
    {
        Scene shared;
        const auto hero2 = shared.create_entity("Hero");
        shared.find(hero2)->sprite = gridengine::Sprite2D{};
        gridengine::AnimationFrameset sheet;
        sheet.texture = "assets/hero.png";
        sheet.columns = 1;
        sheet.rows = 1;
        sheet.slice_grid(16, 32);
        gridengine::AnimationClip one;
        one.name = "idle";
        one.frames = {0};
        sheet.clips = {one};
        const int sheet_slot = shared.add_frameset(sheet);
        (void)shared.set_animation(hero2, sheet_slot, 0);
        const auto clone = shared.duplicate_entity(hero2);
        check(clone.has_value(), "duplicate an entity with an animated sprite");
        if (clone && shared.find(*clone)) {
            auto& sprite = *shared.find(*clone)->sprite;
            check(sprite.frameset == sheet_slot, "the clone shares the scene's frameset");
            sprite.frame = 2;
            check(shared.find(hero2)->sprite->frame == 0, "each sprite keeps its own frame");
        }
    }
    fs::remove_all(dir, ec);
    std::printf("gridengine_scene_frameset: %s (%d failures)\n", failures == 0 ? "ok" : "FAILED",
                failures);
    return failures == 0 ? 0 : 1;
}
