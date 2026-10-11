// GridEngine - include/gridengine/node.hpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#pragma once
#include "gridengine/scene.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gridengine {
using NodeId = std::uint64_t;

// Non-owning view of a tree node. The tree owns the storage; the view stays
// valid until the node (or its subtree) is removed.
struct NodeRef {
    NodeId id{};
    std::string name{"Node"};
    Transform2D local{};
    std::optional<EntityId> entity{}; // optional link to a Scene entity
    bool alive{true};
};

// Deterministic scene-graph tree. Ids are stable while a node is alive and are
// never reused. Deletions are deferred until flush_deletions(), so removals can
// be queued while callers iterate the tree.
class NodeTree final {
public:
    NodeTree() = default;

    [[nodiscard]] NodeId create(std::string name = "Node",
                                std::optional<NodeId> parent = {});
    [[nodiscard]] bool add_child(NodeId parent, NodeId child);
    [[nodiscard]] bool remove_child(NodeId parent, NodeId child);
    [[nodiscard]] bool reparent(NodeId child, std::optional<NodeId> parent,
                                bool keep_world_transform = true);
    [[nodiscard]] bool queue_free(NodeId id);
    [[nodiscard]] bool flush_deletions();
    [[nodiscard]] bool flush_deferred(); // alias of flush_deletions

    [[nodiscard]] NodeRef* node(NodeId id) noexcept;
    [[nodiscard]] const NodeRef* node(NodeId id) const noexcept;
    [[nodiscard]] std::vector<NodeId> children(NodeId id) const;
    [[nodiscard]] std::optional<NodeId> parent(NodeId id) const;
    [[nodiscard]] NodeId find_id(NodeId id) const noexcept;
    [[nodiscard]] NodeId find_name(const std::string& name) const noexcept;
    [[nodiscard]] NodeId find_path(const std::string& path) const noexcept;
    [[nodiscard]] Transform2D global(NodeId id) const;
    [[nodiscard]] Transform2D global_transform(NodeId id) const { return global(id); }
    [[nodiscard]] NodeId duplicate(NodeId id);
    [[nodiscard]] bool set_alive(NodeId id, bool alive);
    [[nodiscard]] std::vector<NodeId> roots() const;
    [[nodiscard]] std::size_t size() const noexcept { return records_.size(); }

    [[nodiscard]] bool save(const std::filesystem::path& file) const;
    [[nodiscard]] static std::optional<NodeTree> load(const std::filesystem::path& file);

private:
    NodeRef* find(NodeId id) noexcept;
    const NodeRef* find(NodeId id) const noexcept;
    bool would_cycle(NodeId child, NodeId parent) const noexcept;
    void reindex() noexcept;
    NodeId duplicate_of(NodeId id);
    Transform2D global_of(NodeId id) const;

    struct Record {
        NodeRef ref;
        NodeId parent{};
        std::vector<NodeId> children;
    };
    std::vector<Record> records_;
    std::unordered_map<NodeId, std::size_t> index_;
    std::unordered_set<NodeId> queued_;
    NodeId next_id_{1};
};

} // namespace gridengine
