#include "ichor/traversal/info_loot.h"

#include <algorithm>

namespace ichor {

namespace {

bool validIdChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
}

bool contains(const std::vector<std::string>& sorted, const std::string& id)
{
    return std::binary_search(sorted.begin(), sorted.end(), id);
}

void insertSorted(std::vector<std::string>& sorted, const std::string& id)
{
    sorted.insert(std::lower_bound(sorted.begin(), sorted.end(), id), id);
}

bool hasRoom(const std::vector<std::string>& v, std::size_t adding)
{
    return v.size() + adding <= kInfoMaxStateEntries;
}

InfoDefStatus normalise(InfoItemDef& d)
{
    switch (d.kind) {
    case InfoKind::Map:
        if (d.cells.size() > kInfoMaxMapCells) {
            return InfoDefStatus::TooLarge;
        }
        if (d.cells.empty()) {
            return InfoDefStatus::Malformed;
        }
        std::sort(d.cells.begin(), d.cells.end(), vantageCellLess);
        d.cells.erase(std::unique(d.cells.begin(), d.cells.end()), d.cells.end());
        return InfoDefStatus::Ok;
    case InfoKind::Chart:
        if (d.routeIds.size() > kInfoMaxEntries) {
            return InfoDefStatus::TooLarge;
        }
        if (d.routeIds.empty()) {
            return InfoDefStatus::Malformed;
        }
        if (!std::all_of(d.routeIds.begin(), d.routeIds.end(), isValidInfoId)) {
            return InfoDefStatus::InvalidId;
        }
        std::sort(d.routeIds.begin(), d.routeIds.end());
        d.routeIds.erase(std::unique(d.routeIds.begin(), d.routeIds.end()), d.routeIds.end());
        return InfoDefStatus::Ok;
    case InfoKind::Cipher:
        if (d.texts.size() > kInfoMaxEntries) {
            return InfoDefStatus::TooLarge;
        }
        if (d.texts.empty()) {
            return InfoDefStatus::Malformed;
        }
        for (const auto& t : d.texts) {
            if (!isValidInfoId(t.textId)) {
                return InfoDefStatus::InvalidId;
            }
            if (t.plain.size() > kInfoMaxTextLength) {
                return InfoDefStatus::TooLarge;
            }
            if (t.plain.empty()) {
                return InfoDefStatus::Malformed;
            }
        }
        std::stable_sort(d.texts.begin(), d.texts.end(),
            [](const InfoCipherText& a, const InfoCipherText& b) { return a.textId < b.textId; });
        d.texts.erase(std::unique(d.texts.begin(), d.texts.end(),
                          [](const InfoCipherText& a, const InfoCipherText& b) { return a.textId == b.textId; }),
            d.texts.end());
        return InfoDefStatus::Ok;
    case InfoKind::Key:
        if (d.lockId.empty()) {
            return InfoDefStatus::Malformed;
        }
        return isValidInfoId(d.lockId) ? InfoDefStatus::Ok : InfoDefStatus::InvalidId;
    }
    return InfoDefStatus::Malformed;
}

InfoUseResult fail(InfoUseStatus s)
{
    InfoUseResult r;
    r.status = s;
    return r;
}

InfoUseResult useMap(const InfoItemDef& item, InfoLootState& state)
{
    std::vector<VantageCell> merged = state.revealedCells;
    const std::size_t added = mergeRevealedCells(merged, item.cells);
    if (merged.size() > kInfoMaxStateRevealedCells) {
        return fail(InfoUseStatus::StateFull);
    }
    state.revealedCells = std::move(merged);
    InfoUseResult r;
    r.newCells = added;
    return r;
}

InfoUseResult useChart(const InfoItemDef& item, InfoLootState& state)
{
    InfoUseResult r;
    for (const auto& route : item.routeIds) {
        if (!contains(state.markedRoutes, route)) {
            r.newRoutes.push_back(route);
        }
    }
    if (!hasRoom(state.markedRoutes, r.newRoutes.size())) {
        return fail(InfoUseStatus::StateFull);
    }
    for (const auto& route : r.newRoutes) {
        insertSorted(state.markedRoutes, route);
    }
    return r;
}

InfoUseResult useCipher(const InfoItemDef& item, InfoLootState& state, const std::string& target)
{
    if (target.empty()) {
        return fail(InfoUseStatus::MissingTarget);
    }
    const auto it = std::lower_bound(item.texts.begin(), item.texts.end(), target,
        [](const InfoCipherText& t, const std::string& id) { return t.textId < id; });
    if (it == item.texts.end() || it->textId != target) {
        return fail(InfoUseStatus::WrongTarget);
    }
    if (contains(state.decodedTexts, target)) {
        return fail(InfoUseStatus::AlreadyDone);
    }
    if (!hasRoom(state.decodedTexts, 1)) {
        return fail(InfoUseStatus::StateFull);
    }
    insertSorted(state.decodedTexts, target);
    InfoUseResult r;
    r.plain = it->plain;
    return r;
}

InfoUseResult useKey(const InfoItemDef& item, InfoLootState& state, const std::string& target)
{
    if (target.empty()) {
        return fail(InfoUseStatus::MissingTarget);
    }
    if (target != item.lockId) {
        return fail(InfoUseStatus::WrongTarget);
    }
    if (contains(state.openedLocks, target)) {
        return fail(InfoUseStatus::AlreadyDone);
    }
    if (!hasRoom(state.openedLocks, 1)) {
        return fail(InfoUseStatus::StateFull);
    }
    insertSorted(state.openedLocks, target);
    InfoUseResult r;
    r.openedLock = target;
    return r;
}

}

bool isValidInfoId(const std::string& id)
{
    return !id.empty() && id.size() <= kInfoMaxIdLength && std::all_of(id.begin(), id.end(), validIdChar);
}

InfoDefStatus InfoLootRegistry::add(const InfoItemDef& def)
{
    if (!isValidInfoId(def.id)) {
        return InfoDefStatus::InvalidId;
    }
    InfoItemDef copy = def;
    const InfoDefStatus s = normalise(copy);
    if (s != InfoDefStatus::Ok) {
        return s;
    }
    const auto it = std::lower_bound(items_.begin(), items_.end(), copy.id,
        [](const InfoItemDef& d, const std::string& id) { return d.id < id; });
    if (it != items_.end() && it->id == copy.id) {
        return InfoDefStatus::Duplicate;
    }
    if (items_.size() >= kInfoMaxItems) {
        return InfoDefStatus::TooLarge;
    }
    items_.insert(it, std::move(copy));
    return InfoDefStatus::Ok;
}

const InfoItemDef* InfoLootRegistry::find(const std::string& id) const
{
    const auto it = std::lower_bound(items_.begin(), items_.end(), id,
        [](const InfoItemDef& d, const std::string& key) { return d.id < key; });
    return it != items_.end() && it->id == id ? &*it : nullptr;
}

InfoUseResult useInfoItem(const InfoLootRegistry& registry, InfoLootState& state, const std::string& itemId,
    const std::string& target)
{
    if (!isValidInfoId(itemId) || (!target.empty() && !isValidInfoId(target))) {
        return fail(InfoUseStatus::InvalidId);
    }
    const InfoItemDef* item = registry.find(itemId);
    if (item == nullptr) {
        return fail(InfoUseStatus::UnknownItem);
    }
    if (contains(state.spentItems, itemId)) {
        return fail(InfoUseStatus::Spent);
    }
    const bool spends = item->singleUse;
    if (spends && !hasRoom(state.spentItems, 1)) {
        return fail(InfoUseStatus::StateFull);
    }

    InfoUseResult r;
    switch (item->kind) {
    case InfoKind::Map:
        r = useMap(*item, state);
        break;
    case InfoKind::Chart:
        r = useChart(*item, state);
        break;
    case InfoKind::Cipher:
        r = useCipher(*item, state, target);
        break;
    case InfoKind::Key:
        r = useKey(*item, state, target);
        break;
    }
    if (r.status == InfoUseStatus::Ok && spends) {
        insertSorted(state.spentItems, itemId);
        r.spent = true;
    }
    return r;
}

}
