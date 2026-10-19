#include "ichor/world/forged_walking.h"

#include "ichor/core/dmath.h"
#include "ichor/traversal/climb_rules.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace ichor {

namespace {

constexpr double kDegreesToRadians = 0.017453292519943295;
constexpr float kHazardFloor = 0.001f;
constexpr double kMinSpeed = 1.0e-9;

double cosOfDegrees(float degrees)
{
    return static_cast<double>(dmath::cos(static_cast<float>(degrees * kDegreesToRadians)));
}

double flatLength(double x, double z)
{
    return std::sqrt(x * x + z * z);
}

bool finite(const glm::dvec3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

}

const char* walkBlockName(WalkBlock block)
{
    switch (block) {
    case WalkBlock::None: return "clear";
    case WalkBlock::TooSteep: return "too steep (needs climbing)";
    case WalkBlock::DeepWater: return "deep water (needs swimming or a boat)";
    case WalkBlock::OffMap: return "the edge of the world";
    case WalkBlock::Wedged: return "wedged against something";
    }
    return "unknown";
}

ForgedWalking::ForgedWalking(const Heightfield& heightfield, const WaterField* water, const CapabilitySet& capabilities, const WalkParams& params)
    : heightfield_(heightfield)
    , water_(water)
    , capabilities_(capabilities)
    , params_(params)
{
    const bool maxOk = std::isfinite(params_.maxSlopeDegrees) && params_.maxSlopeDegrees > 0.0f && params_.maxSlopeDegrees < 90.0f;
    if (!maxOk) {
        params_.maxSlopeDegrees = ClimbLimits{}.minSlopeDegrees;
    }
    const bool easeOk = std::isfinite(params_.easeSlopeDegrees) && params_.easeSlopeDegrees >= 0.0f && params_.easeSlopeDegrees < params_.maxSlopeDegrees;
    if (!easeOk) {
        params_.easeSlopeDegrees = params_.maxSlopeDegrees * 0.7f;
    }
    if (!(std::isfinite(params_.minSlopeFactor) && params_.minSlopeFactor > 0.0f && params_.minSlopeFactor <= 1.0f)) {
        params_.minSlopeFactor = 0.3f;
    }
    if (!(std::isfinite(params_.deepWaterDepth) && params_.deepWaterDepth > 0.0f)) {
        params_.deepWaterDepth = kWadeFull;
    }
    if (!(std::isfinite(params_.probeMetres) && params_.probeMetres > 0.0f)) {
        params_.probeMetres = 1.0f;
    }
    cosMax_ = cosOfDegrees(params_.maxSlopeDegrees);
    cosEase_ = cosOfDegrees(params_.easeSlopeDegrees);
}

WalkAssessment ForgedWalking::assess(double x, double z) const
{
    WalkAssessment out;
    const double size = heightfield_.desc().sizeMetres;
    if (!std::isfinite(x) || !std::isfinite(z) || heightfield_.empty()) {
        out.block = WalkBlock::OffMap;
        out.hazard = kHazardFloor;
        out.slopeFactor = 0.0f;
        out.wadeFactor = 0.0f;
        return out;
    }
    const double outside = std::max({-x, -z, x - size, z - size});
    if (outside > 0.0) {
        out.block = WalkBlock::OffMap;
        out.hazard = static_cast<float>(std::min(outside, 1.0e6)) + kHazardFloor;
        out.slopeFactor = 0.0f;
        out.wadeFactor = 0.0f;
        return out;
    }

    const double ny = static_cast<double>(heightfield_.normalAt(x, z).y);
    if (!(ny >= cosMax_)) {
        out.block = WalkBlock::TooSteep;
        out.hazard = static_cast<float>(cosMax_ - ny) + kHazardFloor;
        out.slopeFactor = 0.0f;
    } else if (ny < cosEase_) {
        const double t = (ny - cosMax_) / (cosEase_ - cosMax_);
        out.slopeFactor = static_cast<float>(params_.minSlopeFactor + (1.0 - params_.minSlopeFactor) * t);
    }

    if (water_ != nullptr) {
        out.waterDepth = water_->at(x, z).depth;
        out.wadeFactor = wadingSpeedFactor(out.waterDepth);
        const bool afloat = capabilities_.has(Capability::Swim) || capabilities_.has(Capability::Boat);
        if (out.waterDepth >= params_.deepWaterDepth && !afloat && out.block == WalkBlock::None) {
            out.block = WalkBlock::DeepWater;
            out.hazard = out.waterDepth - params_.deepWaterDepth + kHazardFloor;
            out.wadeFactor = 0.0f;
        }
    }
    return out;
}

glm::dvec3 ForgedWalking::filterVelocity(const glm::dvec3& position, const glm::dvec3& velocity) const
{
    if (!finite(position) || !finite(velocity)) {
        return glm::dvec3(0.0);
    }
    const double speed = flatLength(velocity.x, velocity.z);
    if (!(speed >= kMinSpeed)) {
        return velocity;
    }
    const WalkAssessment here = assess(position.x, position.z);
    const auto admit = [&](const glm::dvec3& v, WalkAssessment& at) {
        const double s = flatLength(v.x, v.z);
        if (!(s >= kMinSpeed)) {
            return false;
        }
        at = assess(position.x + v.x / s * params_.probeMetres, position.z + v.z / s * params_.probeMetres);
        return at.walkable() || (!here.walkable() && at.hazard <= here.hazard && at.block == here.block);
    };
    const auto crawl = [&](const WalkAssessment& at) { return at.walkable() ? static_cast<double>(at.slopeFactor) : static_cast<double>(params_.minSlopeFactor); };

    WalkAssessment ahead;
    if (admit(velocity, ahead)) {
        const double pace = crawl(ahead);
        return {velocity.x * pace, velocity.y, velocity.z * pace};
    }
    const glm::dvec3 alongX{velocity.x, velocity.y, 0.0};
    const glm::dvec3 alongZ{0.0, velocity.y, velocity.z};
    WalkAssessment atX;
    WalkAssessment atZ;
    const bool okX = admit(alongX, atX);
    const bool okZ = admit(alongZ, atZ);
    if (okX && (!okZ || std::abs(velocity.x) >= std::abs(velocity.z))) {
        return {alongX.x * crawl(atX), velocity.y, 0.0};
    }
    if (okZ) {
        return {0.0, velocity.y, alongZ.z * crawl(atZ)};
    }
    return {0.0, velocity.y, 0.0};
}

bool findWalkableSpawn(const ForgedWalking& walking, double aroundX, double aroundZ, double maxRadius, glm::dvec3& out)
{
    if (!std::isfinite(aroundX) || !std::isfinite(aroundZ) || !std::isfinite(maxRadius) || maxRadius < 0.0) {
        return false;
    }
    const auto dry = [&](double x, double z) {
        const WalkAssessment a = walking.assess(x, z);
        return a.walkable() && a.waterDepth == 0.0f && a.slopeFactor >= 1.0f;
    };
    const auto accept = [&](double x, double z) {
        out = {x, 0.0, z};
        return true;
    };
    if (dry(aroundX, aroundZ)) {
        return accept(aroundX, aroundZ);
    }
    constexpr double kStep = 8.0;
    const int rings = static_cast<int>(std::min({maxRadius / kStep, walking.sizeMetres() / kStep + 1.0, 4096.0}));
    for (int ring = 1; ring <= rings; ++ring) {
        const double r = ring * kStep;
        for (int i = -ring; i <= ring; ++i) {
            const double d = i * kStep;
            if (dry(aroundX + d, aroundZ - r)) return accept(aroundX + d, aroundZ - r);
            if (dry(aroundX + d, aroundZ + r)) return accept(aroundX + d, aroundZ + r);
            if (dry(aroundX - r, aroundZ + d)) return accept(aroundX - r, aroundZ + d);
            if (dry(aroundX + r, aroundZ + d)) return accept(aroundX + r, aroundZ + d);
        }
    }
    return false;
}

std::string describeStuck(const StuckReport& report)
{
    if (!report.stuck) {
        return "not stuck";
    }
    return std::format("stuck at ({:.1f}, {:.1f}) for {:.1f} s: {}", report.where.x, report.where.z, report.seconds, walkBlockName(report.cause));
}

StuckDetector::StuckDetector(const StuckParams& params)
    : params_(params)
{
    if (!(std::isfinite(params_.windowSeconds) && params_.windowSeconds > 0.0)) params_.windowSeconds = 3.0;
    if (!(std::isfinite(params_.minProgressMetres) && params_.minProgressMetres >= 0.0)) params_.minProgressMetres = 0.75;
    if (!(std::isfinite(params_.safeSpacingMetres) && params_.safeSpacingMetres > 0.0)) params_.safeSpacingMetres = 4.0;
    params_.safeHistory = std::clamp<std::size_t>(params_.safeHistory, 1, 256);
    params_.recoverBack = std::clamp<std::size_t>(params_.recoverBack, 1, params_.safeHistory);
}

void StuckDetector::reset(const glm::dvec3& position)
{
    windowStart_ = position;
    lastPosition_ = position;
    tryingSeconds_ = 0.0;
    pathMetres_ = 0.0;
    windowOpen_ = false;
    safe_.clear();
}

StuckReport StuckDetector::step(const glm::dvec3& position, bool wantsToMove, const WalkAssessment& here, WalkBlock ahead, double dt)
{
    StuckReport report;
    report.where = position;
    report.recoverTo = position;
    if (!finite(position) || !(std::isfinite(dt) && dt > 0.0)) {
        return report;
    }

    const double stepped = windowOpen_ ? flatLength(position.x - lastPosition_.x, position.z - lastPosition_.z) : 0.0;
    lastPosition_ = position;

    if (here.walkable() && here.slopeFactor >= 1.0f && here.waterDepth <= kWadeStart) {
        const bool far = safe_.empty() || flatLength(position.x - safe_.back().x, position.z - safe_.back().z) >= params_.safeSpacingMetres;
        if (far) {
            safe_.push_back(position);
            if (safe_.size() > params_.safeHistory) safe_.pop_front();
        }
    }

    if (!wantsToMove) {
        windowOpen_ = false;
        tryingSeconds_ = 0.0;
        pathMetres_ = 0.0;
        return report;
    }
    if (!windowOpen_) {
        windowOpen_ = true;
        windowStart_ = position;
        tryingSeconds_ = 0.0;
        pathMetres_ = 0.0;
    }
    tryingSeconds_ += dt;
    pathMetres_ += stepped;
    if (tryingSeconds_ < params_.windowSeconds) {
        return report;
    }

    if (pathMetres_ >= params_.minProgressMetres) {
        windowStart_ = position;
        tryingSeconds_ = 0.0;
        pathMetres_ = 0.0;
        return report;
    }

    report.stuck = true;
    report.cause = ahead != WalkBlock::None ? ahead : (!here.walkable() ? here.block : WalkBlock::Wedged);
    report.seconds = tryingSeconds_;
    if (!safe_.empty()) {
        const std::size_t back = std::min(params_.recoverBack, safe_.size());
        report.recoverTo = safe_[safe_.size() - back];
        report.canRecover = true;
    }
    windowStart_ = position;
    tryingSeconds_ = 0.0;
    pathMetres_ = 0.0;
    return report;
}

}
