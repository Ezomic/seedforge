#include "ichor/graph/graph.h"

#include "ichor/core/hash.h"

namespace ichor::graph {

NodeId Graph::addNode(std::string_view type)
{
    const NodeId id = nextNode_++;
    nodes_.emplace(id, Node{id, std::string(type)});
    return id;
}

EdgeId Graph::addEdge(NodeId from, NodeId to, std::string_view type)
{
    if (!nodes_.contains(from) || !nodes_.contains(to)) {
        return 0;
    }
    const EdgeId id = nextEdge_++;
    edges_.emplace(id, Edge{id, from, to, std::string(type)});
    return id;
}

bool Graph::removeNode(NodeId id)
{
    if (nodes_.erase(id) == 0) {
        return false;
    }
    for (auto it = edges_.begin(); it != edges_.end();) {
        if (it->second.from == id || it->second.to == id) {
            it = edges_.erase(it);
        } else {
            ++it;
        }
    }
    return true;
}

bool Graph::removeEdge(EdgeId id)
{
    return edges_.erase(id) != 0;
}

const Node* Graph::node(NodeId id) const
{
    const auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : &it->second;
}

Node* Graph::node(NodeId id)
{
    const auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : &it->second;
}

const Edge* Graph::edge(EdgeId id) const
{
    const auto it = edges_.find(id);
    return it == edges_.end() ? nullptr : &it->second;
}

std::uint64_t Graph::digest() const
{
    std::uint64_t h = hash64("ichor.graph.v1");
    h = hashCombine(h, nextNode_);
    h = hashCombine(h, nextEdge_);
    h = hashCombine(h, nodes_.size());
    for (const auto& [id, n] : nodes_) {
        h = hashCombine(h, id);
        h = hashCombine(h, hash64(n.type));
    }
    h = hashCombine(h, edges_.size());
    for (const auto& [id, e] : edges_) {
        h = hashCombine(h, id);
        h = hashCombine(h, e.from);
        h = hashCombine(h, e.to);
        h = hashCombine(h, hash64(e.type));
    }
    return h;
}

}
