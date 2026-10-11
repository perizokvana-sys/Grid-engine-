// GridEngine - tests/node.cpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#include "gridengine/node.hpp"
#include <cassert>
#include <cmath>
#include <filesystem>
using namespace gridengine;
static bool eq(double a,double b){return std::abs(a-b)<1e-8;}
int main(){
 NodeTree t; auto a=t.create("A"), b=t.create("B"), c=t.create("C"); assert(a&&b&&c&&a!=b&&b!=c);
 assert(t.add_child(a,b)); assert(t.add_child(a,c)); assert(!t.add_child(a,b)); assert(!t.add_child(b,a));
 assert(t.children(a).size()==2&&t.children(a)[0]==b&&t.children(a)[1]==c);
 assert(t.find_name("C")==c&&t.find_id(b)==b&&t.find_path("A/C")==c);
 assert(t.remove_child(a,c)&&!t.parent(c)); assert(t.add_child(b,c));
 t.node(a)->local.position={10,20}; t.node(a)->local.rotation=90; t.node(a)->local.scale={2,3};
 t.node(b)->local.position={4,2}; t.node(b)->local.rotation=5; t.node(b)->local.scale={.5,2};
 auto w=t.global(b); assert(eq(w.position.x,4)&&eq(w.position.y,28)&&eq(w.rotation,95)&&eq(w.scale.x,1)&&eq(w.scale.y,6));
 assert(t.reparent(b,{},true)); assert(eq(t.global(b).position.x,4)&&eq(t.global(b).position.y,28));
 auto d=t.duplicate(b); assert(d&&d!=b&&t.node(d)->id==d);
 auto path=std::filesystem::temp_directory_path()/"gridengine_node_test.scene"; assert(t.save(path)); auto loaded=NodeTree::load(path); assert(loaded&&loaded->size()==t.size()); std::filesystem::remove(path);
 assert(t.queue_free(d)&&t.node(d)); assert(t.flush_deferred()&& !t.node(d));
 assert(t.set_alive(b,false)&&!t.node(b)->alive); assert(t.set_alive(b,true)&&t.node(b)->alive);
}
