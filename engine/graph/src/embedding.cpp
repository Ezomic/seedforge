#include "ichor/graph/embedding.h"

#include "ichor/core/rng.h"
#include "ichor/graph/mission.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <set>

namespace ichor::graph::embedding {

namespace {

constexpr std::size_t kDirections = 6;
constexpr std::array<std::array<int, 3>, kDirections> kSteps = {{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};

struct Limits {
    std::int64_t extent = 1;
    std::int64_t floors = 1;
};

Limits limitsOf(const Params& params)
{
    Limits l;
    l.extent = std::clamp<std::int64_t>(params.maxExtent, 1, kMaxExtentLimit);
    l.floors = std::clamp<std::int64_t>(params.maxFloors, 1, kMaxFloorsLimit);
    return l;
}

bool touching(const Cell& a, const Cell& b)
{
    const std::int64_t d = std::llabs(static_cast<std::int64_t>(a.x) - b.x) + std::llabs(static_cast<std::int64_t>(a.y) - b.y)
        + std::llabs(static_cast<std::int64_t>(a.floor) - b.floor);
    return d == 1;
}

struct Box {
    std::int32_t lo[3] = {0, 0, 0};
    std::int32_t hi[3] = {0, 0, 0};

    bool fits(const Limits& l) const
    {
        return std::int64_t{hi[0]} - lo[0] < l.extent && std::int64_t{hi[1]} - lo[1] < l.extent && std::int64_t{hi[2]} - lo[2] < l.floors;
    }

    void add(const Cell& c)
    {
        const std::int32_t v[3] = {c.x, c.y, c.floor};
        for (int i = 0; i < 3; ++i) {
            lo[i] = std::min(lo[i], v[i]);
            hi[i] = std::max(hi[i], v[i]);
        }
    }
};

class Search {
public:
    Search(std::vector<NodeId> order, std::map<NodeId, std::vector<NodeId>> neighbours, Limits limits, std::uint64_t seed)
        : order_(std::move(order))
        , neighbours_(std::move(neighbours))
        , limits_(limits)
        , rng_(seed)
    {
        for (std::size_t i = 0; i < order_.size(); ++i)
            index_[order_[i]] = i;
    }

    bool run(std::map<NodeId, Cell>& out)
    {
        const Cell origin;
        placed_[order_[0]] = origin;
        taken_.insert(origin);
        Box box;
        if (!place(1, box))
            return false;
        out = placed_;
        return true;
    }

    bool exhausted() const { return steps_ >= kMaxSteps; }

private:
    bool place(std::size_t i, const Box& box)
    {
        if (i == order_.size())
            return true;
        const NodeId id = order_[i];
        const std::vector<NodeId>& nbrs = neighbours_.at(id);
        const Cell parent = placed_.at(firstEarlier(id, i));

        std::array<std::size_t, kDirections> dirs = {0, 1, 2, 3, 4, 5};
        for (std::size_t k = kDirections - 1; k > 0; --k)
            std::swap(dirs[k], dirs[rng_.nextU32() % (k + 1)]);

        for (const std::size_t d : dirs) {
            if (++steps_ > kMaxSteps)
                return false;
            const Cell c{parent.x + kSteps[d][0], parent.y + kSteps[d][1], parent.floor + kSteps[d][2]};
            if (taken_.count(c) != 0)
                continue;
            Box grown = box;
            grown.add(c);
            if (!grown.fits(limits_))
                continue;
            bool ok = true;
            for (const NodeId n : nbrs) {
                const auto it = placed_.find(n);
                if (it != placed_.end() && !touching(c, it->second)) {
                    ok = false;
                    break;
                }
            }
            if (!ok)
                continue;
            placed_[id] = c;
            taken_.insert(c);
            if (place(i + 1, grown))
                return true;
            taken_.erase(c);
            placed_.erase(id);
            if (steps_ >= kMaxSteps)
                return false;
        }
        return false;
    }

    NodeId firstEarlier(NodeId id, std::size_t i) const
    {
        for (const NodeId n : neighbours_.at(id))
            if (index_.at(n) < i)
                return n;
        return order_[0];
    }

    std::vector<NodeId> order_;
    std::map<NodeId, std::vector<NodeId>> neighbours_;
    std::map<NodeId, std::size_t> index_;
    Limits limits_;
    Rng rng_;
    std::map<NodeId, Cell> placed_;
    std::set<Cell> taken_;
    std::uint64_t steps_ = 0;
};

Result fail(std::string message)
{
    Result r;
    r.error = std::move(message);
    return r;
}

}

std::vector<std::string> check(const Graph& graph, const std::map<NodeId, Cell>& cells, const Params& params)
{
    std::vector<std::string> errors;
    const Limits limits = limitsOf(params);

    for (const auto& [id, node] : graph.nodes())
        if (cells.count(id) == 0)
            errors.push_back("room " + std::to_string(id) + " has no cell");
    for (const auto& [id, cell] : cells)
        if (graph.node(id) == nullptr)
            errors.push_back("cell for unknown room " + std::to_string(id));

    std::set<Cell> seen;
    std::int64_t lo[3] = {0, 0, 0};
    std::int64_t hi[3] = {0, 0, 0};
    bool first = true;
    for (const auto& [id, c] : cells) {
        if (!seen.insert(c).second)
            errors.push_back("room " + std::to_string(id) + " shares a cell");
        const std::int64_t v[3] = {c.x, c.y, c.floor};
        for (int i = 0; i < 3; ++i) {
            lo[i] = first ? v[i] : std::min(lo[i], v[i]);
            hi[i] = first ? v[i] : std::max(hi[i], v[i]);
        }
        first = false;
    }
    if (!first && (hi[0] - lo[0] >= limits.extent || hi[1] - lo[1] >= limits.extent || hi[2] - lo[2] >= limits.floors))
        errors.push_back("footprint exceeds the limits");

    for (const auto& [id, e] : graph.edges()) {
        if (e.type != mission::kPath)
            continue;
        const auto a = cells.find(e.from);
        const auto b = cells.find(e.to);
        if (a != cells.end() && b != cells.end() && !touching(a->second, b->second))
            errors.push_back("path edge " + std::to_string(id) + " does not join touching cells");
    }
    return errors;
}

Result embed(const Graph& graph, const Params& params)
{
    const dungeon::Layout layout = dungeon::fromGraph(graph);
    const std::vector<dungeon::Finding> findings = dungeon::validateLayout(layout);
    if (!findings.empty())
        return fail("mission rejected by the layout validator: " + findings.front().message);

    std::map<NodeId, std::vector<NodeId>> neighbours;
    for (const auto& [id, node] : graph.nodes())
        neighbours[id];
    for (const auto& [id, e] : graph.edges()) {
        if (e.type != mission::kPath || e.from == e.to)
            continue;
        neighbours[e.from].push_back(e.to);
        neighbours[e.to].push_back(e.from);
    }
    for (auto& [id, list] : neighbours) {
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
        if (list.size() > kDirections)
            return fail("room " + std::to_string(id) + " has more path links than a cell has faces");
    }

    NodeId entry = 0;
    for (const auto& [id, node] : graph.nodes())
        if (node.type == mission::kEntry)
            entry = id;

    std::vector<NodeId> order{entry};
    std::set<NodeId> queued{entry};
    for (std::size_t head = 0; head < order.size(); ++head)
        for (const NodeId n : neighbours.at(order[head]))
            if (queued.insert(n).second)
                order.push_back(n);
    if (order.size() != graph.nodes().size())
        return fail("a room is not joined to the entry by path edges");

    Search search(std::move(order), std::move(neighbours), limitsOf(params), params.seed);
    Result result;
    if (!search.run(result.cells))
        return fail(search.exhausted() ? "no placement found within the search budget" : "no placement fits the footprint");
    result.layout = layout;
    return result;
}

}
