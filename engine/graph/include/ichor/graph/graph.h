#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace ichor::graph {

using NodeId = std::uint32_t;
using EdgeId = std::uint32_t;

struct Node {
    NodeId id = 0;
    std::string type;
};

struct Edge {
    EdgeId id = 0;
    NodeId from = 0;
    NodeId to = 0;
    std::string type;
};

// A directed multigraph with typed nodes and edges. IDs come from a counter
// that never reuses a value, and storage is ordered by ID, so iteration order
// (and therefore every result built on it) is the same on every platform.
class Graph {
public:
    NodeId addNode(std::string_view type);
    // Returns 0 when either endpoint is missing; edge IDs start at 1.
    EdgeId addEdge(NodeId from, NodeId to, std::string_view type);

    // Removes the node and every edge touching it.
    bool removeNode(NodeId id);
    bool removeEdge(EdgeId id);

    const Node* node(NodeId id) const;
    Node* node(NodeId id);
    const Edge* edge(EdgeId id) const;

    const std::map<NodeId, Node>& nodes() const { return nodes_; }
    const std::map<EdgeId, Edge>& edges() const { return edges_; }

    // Content hash over IDs, types and endpoints in ID order, plus the ID
    // counters, so equal digests mean equal behaviour from here on.
    std::uint64_t digest() const;

private:
    std::map<NodeId, Node> nodes_;
    std::map<EdgeId, Edge> edges_;
    NodeId nextNode_ = 1;
    EdgeId nextEdge_ = 1;
};

}
