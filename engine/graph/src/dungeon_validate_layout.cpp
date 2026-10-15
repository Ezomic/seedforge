#include "ichor/graph/dungeon_validate_layout.h"

#include "ichor/graph/mission.h"

#include <algorithm>
#include <deque>
#include <map>
#include <set>

namespace ichor::graph::dungeon {

namespace {

namespace m = ichor::graph::mission;

bool knownRoom(const std::string& type)
{
    for (const char* known : {m::kEntry, m::kClimax, m::kChallenge, m::kJunction, m::kGate, m::kKey, m::kReward, m::kReveal, m::kRest}) {
        if (type == known) {
            return true;
        }
    }
    return false;
}

bool knownLink(const std::string& type)
{
    return type == m::kPath || type == m::kUnlocks || type == m::kShortcut;
}

void add(std::vector<Finding>& out, Code code, std::uint32_t room, std::string message)
{
    out.push_back({code, room, std::move(message)});
}

std::string name(std::uint32_t id)
{
    return std::to_string(id);
}

}

Layout fromGraph(const Graph& graph)
{
    Layout layout;
    for (const auto& [id, node] : graph.nodes()) {
        layout.rooms.push_back({id, node.type});
    }
    for (const auto& [id, edge] : graph.edges()) {
        layout.links.push_back({edge.from, edge.to, edge.type});
    }
    return layout;
}

std::vector<Finding> validateLayout(const Layout& layout)
{
    std::vector<Finding> out;
    if (layout.rooms.empty()) {
        add(out, Code::Empty, 0, "the dungeon has no rooms");
        return out;
    }
    if (layout.rooms.size() > kMaxRooms || layout.links.size() > kMaxLinks) {
        add(out, Code::TooLarge, 0, "the dungeon exceeds the room or link limit");
        return out;
    }

    std::map<std::uint32_t, std::uint32_t> index; // room id -> dense index
    for (const Room& room : layout.rooms) {
        if (room.id == 0 || !index.emplace(room.id, static_cast<std::uint32_t>(index.size())).second) {
            add(out, Code::DuplicateId, room.id, "room id " + name(room.id) + " is zero or used twice");
        }
        if (!knownRoom(room.type)) {
            add(out, Code::UnknownType, room.id, "room " + name(room.id) + " has unknown type '" + room.type + "'");
        }
    }
    for (const Link& link : layout.links) {
        if (!index.contains(link.from) || !index.contains(link.to)) {
            add(out, Code::DanglingLink, link.from, "a link joins " + name(link.from) + " and " + name(link.to) + ", which is not a room");
        }
        if (!knownLink(link.type)) {
            add(out, Code::UnknownType, link.from, "a link from " + name(link.from) + " has unknown type '" + link.type + "'");
        }
    }
    std::size_t entries = 0;
    std::size_t climaxes = 0;
    std::size_t keys = 0;
    for (const Room& room : layout.rooms) {
        entries += room.type == m::kEntry ? 1 : 0;
        climaxes += room.type == m::kClimax ? 1 : 0;
        keys += room.type == m::kKey ? 1 : 0;
    }
    if (entries != 1) {
        add(out, Code::EntryCount, 0, "a dungeon needs exactly one entry");
    }
    if (climaxes == 0) {
        add(out, Code::NoObjective, 0, "the dungeon has no climax to reach");
    }
    if (keys > kMaxKeys) {
        add(out, Code::TooLarge, 0, "the dungeon has more keys than the search supports");
    }
    if (!out.empty()) {
        return out;
    }

    const std::size_t count = layout.rooms.size();
    std::vector<const std::string*> type(count);
    std::vector<std::uint32_t> ids(count);
    for (const Room& room : layout.rooms) {
        type[index[room.id]] = &room.type;
        ids[index[room.id]] = room.id;
    }
    std::vector<std::uint32_t> keyBit(count, 0);
    std::uint32_t nextBit = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (*type[i] == m::kKey) {
            keyBit[i] = 1u << nextBit++;
        }
    }
    std::vector<std::uint32_t> need(count, 0);
    std::vector<std::vector<std::uint32_t>> moves(count);
    for (const Link& link : layout.links) {
        const std::uint32_t from = index[link.from];
        const std::uint32_t to = index[link.to];
        if (link.type == m::kUnlocks) {
            if (*type[from] != m::kKey || *type[to] != m::kGate) {
                add(out, Code::BadUnlock, link.from, "an unlock link must run from a key to a gate (" + name(link.from) + " to " + name(link.to) + ")");
            } else {
                need[to] |= keyBit[from];
            }
        } else {
            moves[from].push_back(to);
            if (link.type == m::kPath && !link.oneWay) {
                moves[to].push_back(from);
            }
        }
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (*type[i] == m::kGate && need[i] == 0) {
            add(out, Code::GateWithoutKey, ids[i], "gate " + name(ids[i]) + " has no key");
        }
    }
    if (!out.empty()) {
        return out;
    }

    // Exact search over (room, keys held); keys are picked up on entry.
    const std::uint32_t keySpace = 1u << nextBit;
    const auto stateOf = [&](std::uint32_t room, std::uint32_t held) { return static_cast<std::size_t>(room) * keySpace + held; };
    std::vector<std::uint8_t> seen(count * keySpace, 0);
    std::vector<std::size_t> order; // reached states, in discovery order
    std::vector<std::vector<std::size_t>> back; // reverse edges, indexed like `order`
    std::vector<std::uint32_t> orderIndex(count * keySpace, 0);

    std::uint32_t entryRoom = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (*type[i] == m::kEntry) {
            entryRoom = static_cast<std::uint32_t>(i);
        }
    }
    std::deque<std::size_t> queue;
    const auto reach = [&](std::size_t state) {
        if (seen[state] == 0) {
            seen[state] = 1;
            orderIndex[state] = static_cast<std::uint32_t>(order.size());
            order.push_back(state);
            back.emplace_back();
            queue.push_back(state);
        }
    };
    reach(stateOf(entryRoom, 0));
    while (!queue.empty()) {
        const std::size_t state = queue.front();
        queue.pop_front();
        const std::uint32_t room = static_cast<std::uint32_t>(state / keySpace);
        const std::uint32_t held = static_cast<std::uint32_t>(state % keySpace);
        for (const std::uint32_t to : moves[room]) {
            if ((held & need[to]) != need[to]) {
                continue;
            }
            const std::size_t next = stateOf(to, held | keyBit[to]);
            reach(next);
            back[orderIndex[next]].push_back(orderIndex[state]);
        }
    }

    std::vector<std::uint8_t> visited(count, 0);
    for (const std::size_t state : order) {
        visited[state / keySpace] = 1;
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (visited[i] != 0) {
            continue;
        }
        add(out, Code::Unreachable, ids[i], "room " + name(ids[i]) + " (" + *type[i] + ") cannot be reached from the entry");
        if (*type[i] == m::kGate) {
            add(out, Code::GateBeforeKey, ids[i], "gate " + name(ids[i]) + " is never opened: its key is not obtainable before it is needed");
        }
        if (*type[i] == m::kKey) {
            add(out, Code::KeyNotObtainable, ids[i], "key " + name(ids[i]) + " is never obtainable");
        }
    }

    // States from which a climax can still be reached, found backwards.
    std::vector<std::uint8_t> winning(order.size(), 0);
    std::deque<std::uint32_t> work;
    for (std::uint32_t i = 0; i < order.size(); ++i) {
        if (*type[order[i] / keySpace] == m::kClimax) {
            winning[i] = 1;
            work.push_back(i);
        }
    }
    const bool objectiveReachable = !work.empty();
    while (!work.empty()) {
        const std::uint32_t at = work.front();
        work.pop_front();
        for (const std::uint32_t from : back[at]) {
            if (winning[from] == 0) {
                winning[from] = 1;
                work.push_back(from);
            }
        }
    }
    if (objectiveReachable) {
        std::set<std::uint32_t> trapped;
        for (std::uint32_t i = 0; i < order.size(); ++i) {
            if (winning[i] == 0) {
                trapped.insert(ids[order[i] / keySpace]);
            }
        }
        for (const std::uint32_t id : trapped) {
            add(out, Code::SoftLock, id, "a player can end up in room " + name(id) + " unable to reach the climax");
        }
    }
    return out;
}

}
