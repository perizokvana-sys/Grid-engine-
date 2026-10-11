// GridEngine - tests/grid.cpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#include "gridengine/grid.hpp"
#include "gridengine/scene.hpp"
#include <cassert>
#include <cmath>
int main() {
    using gridengine::grid::run;
    auto ok=run("let speed = 250; let delta = 0.5; print(speed * delta + 3);");
    assert(ok.success && ok.printed.size()==1 && std::abs(ok.printed[0]-128.0)<1e-9);
    auto reassigned=run("let score = 2; score = score * 3 + 1; score += 2; print(score);");
    assert(reassigned.success && reassigned.printed.size()==1 && reassigned.printed[0]==9);
    auto branching=run("let score = 0; if (3 < 4 && true) { score = 5; } else { score = 9; } print(score);");
    assert(branching.success && branching.printed.size()==1 && branching.printed[0]==5);
    auto loop=run("let i = 0; let sum = 0; while (i < 5) { sum += i; i += 1; } print(sum);");
    assert(loop.success && loop.printed.size()==1 && loop.printed[0]==10);
    assert(!run("while (true) { let x = 1; }").success);
    assert(!run("if (1) { print(1); }").success);
    assert(!run("print(1 + );").success);
    auto bad=run("let x = 1; print(x / 0);");
    assert(!bad.success && bad.diagnostic.line==1 && bad.diagnostic.column>1);
    assert(!run("print(missing);").success);
    assert(!run("let x = 1; x = 2; print(1 / 0);").success);

    gridengine::Scene scene;
    const auto entity=scene.create_entity("Player");
    scene.find(entity)->transform.position={4,7};
    auto moved=gridengine::grid::run_on_entity("if (delta > 0) { position_x += 100 * delta; } position_y += delta;",scene,entity,0.5);
    assert(moved.success && std::abs(scene.find(entity)->transform.position.x-54)<1e-9);
    assert(std::abs(scene.find(entity)->transform.position.y-7.5)<1e-9);
    auto zero_delta=gridengine::grid::run_on_entity("position_x += 50 * delta;",scene,entity,0.0);
    assert(zero_delta.success && std::abs(scene.find(entity)->transform.position.x-54)<1e-9);
    auto rejected=gridengine::grid::run_on_entity("position_x += 20; print(1 / 0);",scene,entity,1);
    assert(!rejected.success && std::abs(scene.find(entity)->transform.position.x-54)<1e-9);
    assert(!gridengine::grid::run_on_entity("delta = 1;",scene,entity,1).success);
    assert(!gridengine::grid::run_on_entity("position_y += 1;",scene,999999,1).success);
    assert(!gridengine::grid::run_on_entity("position_x += 1;",scene,entity,-1).success);
    assert(!gridengine::grid::run("position_x += 1;").success);
}
