#include "ichor/ui/npc_directions.h"

#include "ichor/core/hash.h"
#include "ichor/core/rng.h"

#include <algorithm>
#include <cmath>

namespace ichor {

namespace {

constexpr int kRollRange = 1000;
// Keeps the longest sentence under AnnotationLimits::maxTextBytes.
constexpr std::size_t kMaxNameBytes = 64;

struct Compass {
    const char* name;
    float dx;
    float dy;
};

constexpr float kDiag = 0.70710678f;
constexpr Compass kCompass[8] = {
    {"north", 0.0f, 1.0f},
    {"northeast", kDiag, kDiag},
    {"east", 1.0f, 0.0f},
    {"southeast", kDiag, -kDiag},
    {"south", 0.0f, -1.0f},
    {"southwest", -kDiag, -kDiag},
    {"west", -1.0f, 0.0f},
    {"northwest", -kDiag, kDiag},
};

// 22.5 degrees either side of an axis is tan(67.5) = 2.4142 : 1.
int bearingIndex(double dx, double dy)
{
    const double ax = std::fabs(dx);
    const double ay = std::fabs(dy);
    constexpr double kTan = 2.41421356;
    if (ay * kTan < ax) {
        return dx > 0.0 ? 2 : 6;
    }
    if (ax * kTan < ay) {
        return dy > 0.0 ? 0 : 4;
    }
    if (dy > 0.0) {
        return dx > 0.0 ? 1 : 7;
    }
    return dx > 0.0 ? 3 : 5;
}

struct Distance {
    const char* phrase;
    float representative;
};

Distance distanceBucket(double squared)
{
    if (squared < 50.0 * 50.0) {
        return {"a stone's throw", 25.0f};
    }
    if (squared < 250.0 * 250.0) {
        return {"a short walk", 150.0f};
    }
    if (squared < 1000.0 * 1000.0) {
        return {"a good walk", 600.0f};
    }
    return {"a long way", 1500.0f};
}

bool validName(std::string_view name, const DirectionLimits& limits)
{
    return !name.empty() && name.size() <= std::min<std::size_t>(limits.maxNameBytes, kMaxNameBytes) && isCleanAnnotationText(name);
}

bool validPosition(AnnotationPoint p, float maxCoordinate)
{
    return std::isfinite(p.x) && std::isfinite(p.y) && std::fabs(p.x) <= maxCoordinate
        && std::fabs(p.y) <= maxCoordinate;
}

float clampCoordinate(float v, float maxCoordinate)
{
    return std::clamp(v, -maxCoordinate, maxCoordinate);
}

std::string hex16(std::uint64_t v)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = digits[v & 0xF];
        v >>= 4;
    }
    return out;
}

std::string upperFirst(std::string s)
{
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') {
        s[0] = static_cast<char>(s[0] - 'a' + 'A');
    }
    return s;
}

DirectionStatus validate(const NpcKnowledge& npc, const DirectionTarget& target, const DirectionLimits& limits)
{
    const float maxCoord = AnnotationLimits{}.maxCoordinate;
    const std::size_t maxId = AnnotationLimits{}.maxIdBytes;
    if (!isValidAnnotationId(npc.npcId, maxId)) {
        return DirectionStatus::InvalidNpc;
    }
    if (!isValidAnnotationId(target.id, maxId) || !validName(target.name, limits)
        || !validPosition(target.position, maxCoord)) {
        return DirectionStatus::InvalidTarget;
    }
    if (npc.landmarks.size() > limits.maxLandmarks || npc.knownTargets.size() > limits.maxKnownTargets) {
        return DirectionStatus::TooMuchKnowledge;
    }
    std::vector<std::string_view> ids;
    ids.reserve(npc.landmarks.size());
    for (const auto& lm : npc.landmarks) {
        if (!isValidAnnotationId(lm.id, maxId) || !validName(lm.name, limits) || !validPosition(lm.position, maxCoord)) {
            return DirectionStatus::InvalidLandmark;
        }
        ids.push_back(lm.id);
    }
    std::sort(ids.begin(), ids.end());
    if (std::adjacent_find(ids.begin(), ids.end()) != ids.end()) {
        return DirectionStatus::DuplicateLandmark;
    }
    return DirectionStatus::Ok;
}

NpcDirection unknown(const DirectionTarget& target)
{
    NpcDirection d;
    d.quality = DirectionQuality::Unknown;
    d.text = "I cannot tell you the way to " + target.name + ".";
    return d;
}

} // namespace

int directionAccuracyPermille(DirectionRole role, bool targetInHomeRegion)
{
    int base = 0;
    switch (role) {
    case DirectionRole::Local: base = 850; break;
    case DirectionRole::Traveller: base = 600; break;
    case DirectionRole::Hermit: base = 350; break;
    }
    return std::clamp(base + (targetInHomeRegion ? 100 : -250), 0, kRollRange);
}

DirectionQuality directionQualityForRoll(int accuracyPermille, std::uint32_t roll)
{
    const int accuracy = std::clamp(accuracyPermille, 0, kRollRange);
    const int remaining = kRollRange - accuracy;
    const auto r = static_cast<int>(std::min<std::uint32_t>(roll, kRollRange - 1));
    if (r < accuracy) {
        return DirectionQuality::Exact;
    }
    if (r < accuracy + remaining * 6 / 10) {
        return DirectionQuality::Vague;
    }
    return DirectionQuality::Wrong;
}

std::string directionAnnotationId(std::string_view npcId, std::string_view targetId)
{
    std::uint64_t h = hash64(npcId);
    h = hashCombine(h, static_cast<std::uint64_t>(npcId.size()));
    h = hash64(targetId, h);
    return "dir-" + hex16(h);
}

NpcDirection giveDirection(const NpcKnowledge& npc, const DirectionTarget& target, std::uint64_t seed,
    const DirectionLimits& limits)
{
    if (const auto status = validate(npc, target, limits); status != DirectionStatus::Ok) {
        NpcDirection d;
        d.status = status;
        return d;
    }

    const bool knows = std::find(npc.knownTargets.begin(), npc.knownTargets.end(), target.id) != npc.knownTargets.end();
    const KnownLandmark* nearest = nullptr;
    double nearestSq = 0.0;
    for (const auto& lm : npc.landmarks) {
        if (lm.id == target.id) {
            continue;
        }
        const double dx = static_cast<double>(target.position.x) - lm.position.x;
        const double dy = static_cast<double>(target.position.y) - lm.position.y;
        const double sq = dx * dx + dy * dy;
        if (nearest == nullptr || sq < nearestSq || (sq == nearestSq && lm.id < nearest->id)) {
            nearest = &lm;
            nearestSq = sq;
        }
    }
    if (!knows || nearest == nullptr) {
        return unknown(target);
    }

    const float maxCoord = AnnotationLimits{}.maxCoordinate;
    Rng rng = Rng(seed).derive(npc.npcId + "/" + target.id);
    const int accuracy = directionAccuracyPermille(npc.role, npc.homeRegion == target.region);
    const auto quality = directionQualityForRoll(accuracy, rng.range(0, kRollRange - 1));

    const double dx = static_cast<double>(target.position.x) - nearest->position.x;
    const double dy = static_cast<double>(target.position.y) - nearest->position.y;
    const bool atLandmark = nearestSq == 0.0;
    const int trueBearing = atLandmark ? 0 : bearingIndex(dx, dy);
    const Distance bucket = distanceBucket(nearestSq);

    NpcDirection d;
    d.quality = quality;
    d.landmarkId = nearest->id;

    MapAnnotation a;
    a.id = directionAnnotationId(npc.npcId, target.id);
    a.kind = AnnotationKind::Mark;
    a.symbol = AnnotationSymbol::Direction;

    AnnotationPoint pin = target.position;
    std::string phrase;
    if (atLandmark && quality != DirectionQuality::Wrong) {
        phrase = "right by " + nearest->name;
        if (quality == DirectionQuality::Vague) {
            phrase = "somewhere around " + nearest->name;
        }
    } else if (quality == DirectionQuality::Exact) {
        phrase = std::string(bucket.phrase) + " " + kCompass[trueBearing].name + " of " + nearest->name;
    } else if (quality == DirectionQuality::Vague) {
        phrase = "toward the " + std::string(kCompass[trueBearing].name) + " of " + nearest->name
            + ", but I could not say how far";
        pin = {static_cast<float>((static_cast<double>(nearest->position.x) + target.position.x) * 0.5),
            static_cast<float>((static_cast<double>(nearest->position.y) + target.position.y) * 0.5)};
    } else {
        const int claimed = (trueBearing + 3 + static_cast<int>(rng.range(0, 2))) % 8;
        phrase = std::string(bucket.phrase) + " " + kCompass[claimed].name + " of " + nearest->name;
        const double rep = bucket.representative;
        pin = {clampCoordinate(static_cast<float>(static_cast<double>(nearest->position.x) + kCompass[claimed].dx * rep), maxCoord),
            clampCoordinate(static_cast<float>(static_cast<double>(nearest->position.y) + kCompass[claimed].dy * rep), maxCoord)};
    }

    d.text = upperFirst(target.name) + ": " + phrase + ".";
    a.text = d.text;
    a.points = {pin};
    d.annotation = std::move(a);
    return d;
}

} // namespace ichor
