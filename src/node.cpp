// GridEngine - src/node.cpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#include "gridengine/node.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace gridengine {
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr double epsilon = 1e-12;

bool finite(Transform2D t) noexcept {
    return std::isfinite(t.position.x) && std::isfinite(t.position.y) && std::isfinite(t.rotation) &&
           std::isfinite(t.scale.x) && std::isfinite(t.scale.y);
}
Vec2 rotate(Vec2 p, double degrees) noexcept {
    const double a = degrees * pi / 180.0;
    return {p.x * std::cos(a) - p.y * std::sin(a), p.x * std::sin(a) + p.y * std::cos(a)};
}
Transform2D combine(const Transform2D& parent, const Transform2D& local) noexcept {
    const auto offset = rotate({local.position.x * parent.scale.x, local.position.y * parent.scale.y},
                               parent.rotation);
    return {{parent.position.x + offset.x, parent.position.y + offset.y},
            parent.rotation + local.rotation,
            {parent.scale.x * local.scale.x, parent.scale.y * local.scale.y}};
}
Transform2D relative(const Transform2D& parent, const Transform2D& world) {
    const auto d = rotate({world.position.x - parent.position.x, world.position.y - parent.position.y},
                          -parent.rotation);
    return {{d.x / parent.scale.x, d.y / parent.scale.y},
            world.rotation - parent.rotation,
            {world.scale.x / parent.scale.x, world.scale.y / parent.scale.y}};
}
std::vector<std::string> split_path(const std::string& path) {
    std::vector<std::string> parts;
    std::string current;
    for (const char c : path) {
        if (c == '/') { if (!current.empty()) parts.push_back(current); current.clear(); }
        else current.push_back(c);
    }
    if (!current.empty()) parts.push_back(current);
    return parts;
}
} // namespace

void NodeTree::reindex() noexcept {
    index_.clear();
    index_.reserve(records_.size() * 2);
    for (std::size_t i = 0; i < records_.size(); ++i) index_.emplace(records_[i].ref.id, i);
}

NodeRef* NodeTree::find(NodeId id) noexcept {
    const auto it = index_.find(id);
    return it == index_.end() ? nullptr : &records_[it->second].ref;
}
const NodeRef* NodeTree::find(NodeId id) const noexcept {
    const auto it = index_.find(id);
    return it == index_.end() ? nullptr : &records_[it->second].ref;
}

NodeId NodeTree::create(std::string name, std::optional<NodeId> parent) {
    if (parent && !find(*parent)) return 0;
    if (next_id_ == 0 || next_id_ == std::numeric_limits<NodeId>::max()) return 0;
    const NodeId id = next_id_++;
    Record record;
    record.ref.id = id;
    record.ref.name = std::move(name);
    record.parent = parent ? *parent : 0;
    records_.push_back(std::move(record));
    if (parent) records_[index_.at(*parent)].children.push_back(id);
    index_.emplace(id, records_.size() - 1);
    return id;
}

NodeRef* NodeTree::node(NodeId id) noexcept { return find(id); }
const NodeRef* NodeTree::node(NodeId id) const noexcept { return find(id); }
NodeId NodeTree::find_id(NodeId id) const noexcept { return find(id) ? id : 0; }

std::vector<NodeId> NodeTree::children(NodeId id) const {
    const auto* n = find(id);
    return n ? records_[index_.at(id)].children : std::vector<NodeId>{};
}
std::optional<NodeId> NodeTree::parent(NodeId id) const {
    const auto it = index_.find(id);
    if (it == index_.end()) return std::nullopt;
    const auto p = records_[it->second].parent;
    return p ? std::optional<NodeId>(p) : std::nullopt;
}
std::vector<NodeId> NodeTree::roots() const {
    std::vector<NodeId> out;
    for (const auto& r : records_) if (!r.parent) out.push_back(r.ref.id);
    return out;
}

bool NodeTree::would_cycle(NodeId child, NodeId parent) const noexcept {
    for (auto cursor = parent; cursor;) {
        if (cursor == child) return true;
        const auto it = index_.find(cursor);
        if (it == index_.end()) return false;
        cursor = records_[it->second].parent;
    }
    return false;
}

bool NodeTree::add_child(NodeId parent, NodeId child) {
    auto* p = find(parent);
    auto* c = find(child);
    if (!p || !c || parent == child || would_cycle(child, parent)) return false;
    auto& from = records_[index_.at(child)];
    if (from.parent == parent) return false;
    if (from.parent) {
        auto& old = records_[index_.at(from.parent)];
        std::erase(old.children, child);
    }
    from.parent = parent;
    records_[index_.at(parent)].children.push_back(child);
    return true;
}

bool NodeTree::remove_child(NodeId parent, NodeId child) {
    const auto pt = index_.find(parent);
    const auto ct = index_.find(child);
    if (pt == index_.end() || ct == index_.end()) return false;
    auto& from = records_[ct->second];
    if (from.parent != parent) return false;
    from.parent = 0;
    auto& list = records_[pt->second].children;
    std::erase(list, child);
    return true;
}

bool NodeTree::reparent(NodeId child, std::optional<NodeId> parent, bool keep_world_transform) {
    auto* c = find(child);
    if (!c) return false;
    if (parent) {
        if (!find(*parent) || would_cycle(child, *parent)) return false;
    }
    const Transform2D world = global(child);
    Transform2D local = world;
    if (parent && keep_world_transform) {
        const Transform2D target = global(*parent);
        if (std::abs(target.scale.x) < epsilon || std::abs(target.scale.y) < epsilon ||
            !finite(target) || !finite(world)) return false;
        local = relative(target, world);
        if (!finite(local)) return false;
    }
    if (const NodeId current = records_[index_.at(child)].parent; current) {
        auto& old = records_[index_.at(current)];
        std::erase(old.children, child);
        records_[index_.at(child)].parent = 0;
    }
    if (parent) {
        records_[index_.at(child)].parent = *parent;
        records_[index_.at(*parent)].children.push_back(child);
    }
    if (keep_world_transform) records_[index_.at(child)].ref.local = local;
    return true;
}

NodeId NodeTree::duplicate(NodeId id) {
    if (!find(id)) return 0;
    return duplicate_of(id);
}

NodeId NodeTree::duplicate_of(NodeId id) {
    const auto& source = records_[index_.at(id)];
    const NodeId current_parent = source.parent;
    const NodeId clone = create(source.ref.name + " Copy",
                                current_parent ? std::optional<NodeId>(current_parent) : std::nullopt);
    if (!clone) return 0;
    records_[index_.at(clone)].ref.local = source.ref.local;
    records_[index_.at(clone)].ref.entity = source.ref.entity;
    records_[index_.at(clone)].ref.alive = source.ref.alive;
    for (const auto child : source.children) {
        const auto cloned = duplicate_of(child);
        if (!cloned || !add_child(clone, cloned)) return 0;
    }
    return clone;
}

bool NodeTree::set_alive(NodeId id, bool alive) {
    auto* n = find(id);
    if (!n) return false;
    n->alive = alive;
    return true;
}

bool NodeTree::queue_free(NodeId id) {
    auto* n = find(id);
    if (!n || queued_.count(id) != 0) return false;
    queued_.insert(id);
    return true;
}

bool NodeTree::flush_deletions() {
    const bool had = !queued_.empty();
    if (!had) return false;
    std::unordered_set<NodeId> doomed{queued_};
    queued_.clear();
    // Include descendants of queued nodes: freeing a parent frees the subtree.
    bool grew = true;
    while (grew) {
        grew = false;
        for (const auto& r : records_) {
            if (doomed.count(r.parent) != 0 && doomed.insert(r.ref.id).second) grew = true;
        }
    }
    std::vector<Record> kept;
    kept.reserve(records_.size());
    for (auto& r : records_) {
        if (doomed.count(r.ref.id) != 0) continue;
        std::erase_if(r.children, [&](NodeId id) { return doomed.count(id) != 0; });
        kept.push_back(std::move(r));
    }
    records_ = std::move(kept);
    reindex();
    return had;
}
bool NodeTree::flush_deferred() { return flush_deletions(); }

NodeId NodeTree::find_name(const std::string& name) const noexcept {
    for (const auto& r : records_) if (r.ref.name == name) return r.ref.id;
    return 0;
}
NodeId NodeTree::find_path(const std::string& path) const noexcept {
    const auto parts = split_path(path);
    if (parts.empty()) return 0;
    std::optional<NodeId> current;
    for (const auto& r : records_) {
        if (!r.parent && r.ref.name == parts.front()) { current = r.ref.id; break; }
    }
    if (!current) return 0;
    for (std::size_t i = 1; i < parts.size(); ++i) {
        const auto& kids = records_[index_.at(*current)].children;
        const auto it = std::find_if(kids.begin(), kids.end(), [&](NodeId id) {
            return records_[index_.at(id)].ref.name == parts[i];
        });
        if (it == kids.end()) return 0;
        current = *it;
    }
    return *current;
}

Transform2D NodeTree::global(NodeId id) const { return global_of(id); }
Transform2D NodeTree::global_of(NodeId id) const {
    std::vector<std::size_t> chain;
    for (auto cursor = id; cursor;) {
        const auto it = index_.find(cursor);
        if (it == index_.end()) return {};
        chain.push_back(it->second);
        cursor = records_[it->second].parent;
    }
    if (chain.empty()) return {};
    Transform2D world = records_[chain.back()].ref.local;
    for (std::size_t i = chain.size() - 1; i-- > 0;) world = combine(world, records_[chain[i]].ref.local);
    return world;
}

bool NodeTree::save(const std::filesystem::path& file) const {
    for (const auto& r : records_) {
        if (!r.ref.id || r.ref.name.empty() || !finite(r.ref.local)) return false;
    }
    std::ofstream out(file, std::ios::binary);
    if (!out) return false;
    out << "GridNode 1\n" << std::setprecision(17);
    for (const auto& r : records_) {
        out << "N " << r.ref.id << ' ' << r.parent << ' ' << std::quoted(r.ref.name) << ' '
            << r.ref.local.position.x << ' ' << r.ref.local.position.y << ' ' << r.ref.local.rotation << ' '
            << r.ref.local.scale.x << ' ' << r.ref.local.scale.y << ' '
            << r.ref.entity.value_or(0) << '\n';
    }
    return static_cast<bool>(out);
}

std::optional<NodeTree> NodeTree::load(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::string signature, tag;
    int version{};
    if (!(in >> signature >> version) || signature != "GridNode" || version != 1) return std::nullopt;
    struct Row { NodeId id{}; NodeId parent{}; std::string name; Transform2D local{}; EntityId entity{}; };
    std::vector<Row> rows;
    std::unordered_set<NodeId> ids;
    while (in >> tag) {
        if (tag != "N") return std::nullopt;
        Row row;
        if (!(in >> row.id >> row.parent >> std::quoted(row.name) >> row.local.position.x >>
              row.local.position.y >> row.local.rotation >> row.local.scale.x >> row.local.scale.y >>
              row.entity) ||
            !row.id || row.name.empty() || !finite(row.local) || !ids.insert(row.id).second)
            return std::nullopt;
        rows.push_back(std::move(row));
    }
    in >> std::ws;
    if (!in.eof() || rows.empty()) return std::nullopt;

    // Records are inserted with their stored ids: saved ids must round-trip.
    NodeTree tree;
    tree.records_.reserve(rows.size());
    for (const auto& row : rows) {
        Record record;
        record.ref.id = row.id;
        record.ref.name = row.name;
        record.ref.local = row.local;
        if (row.entity) record.ref.entity = row.entity;
        tree.records_.push_back(std::move(record));
        tree.next_id_ = std::max(tree.next_id_, row.id + 1);
    }
    tree.reindex();
    for (const auto& row : rows) {
        if (!row.parent) continue;
        if (!tree.add_child(row.parent, row.id)) return std::nullopt;
    }
    // A saved tree may be a forest; only cycles and dangling parents are invalid,
    // and add_child already rejects both.
    return tree;
}

} // namespace gridengine
