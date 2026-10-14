#include "ichor/audio/music_director.h"

#include "ichor/data/data_set.h"

#include <algorithm>
#include <cmath>

namespace ichor {

namespace {

constexpr float kMinRootHz = 40.0f;
constexpr float kMaxRootHz = 2000.0f;

float smooth(double x)
{
    const double t = std::clamp(x, 0.0, 1.0);
    return static_cast<float>(t * t * (3.0 - 2.0 * t));
}

std::optional<MusicEvent> eventNamed(const std::string& name)
{
    if (name == "vista") {
        return MusicEvent::Vista;
    }
    if (name == "discovery") {
        return MusicEvent::Discovery;
    }
    if (name == "combat") {
        return MusicEvent::Combat;
    }
    if (name == "death") {
        return MusicEvent::Death;
    }
    return std::nullopt;
}

bool numberField(const DataRecord& record, const char* key, float& out)
{
    const auto it = record.fields.find(key);
    if (it == record.fields.end() || !std::holds_alternative<double>(it->second)) {
        return false;
    }
    out = static_cast<float>(std::get<double>(it->second));
    return true;
}

}

bool validMusicCue(const MusicCue& cue)
{
    return !cue.id.empty() && std::isfinite(cue.duration) && std::isfinite(cue.fadeIn) && std::isfinite(cue.fadeOut) && std::isfinite(cue.cooldown)
        && std::isfinite(cue.gain) && std::isfinite(cue.rootHz) && cue.duration > 0.0f && cue.fadeIn >= 0.0f && cue.fadeOut >= 0.0f
        && cue.fadeIn + cue.fadeOut <= cue.duration && cue.cooldown >= 0.0f && cue.gain >= 0.0f && cue.gain <= 1.0f && cue.rootHz >= kMinRootHz
        && cue.rootHz <= kMaxRootHz;
}

float cueEnvelope(const MusicCue& cue, double t)
{
    if (!(t >= 0.0) || t >= static_cast<double>(cue.duration)) {
        return 0.0f;
    }
    const float up = cue.fadeIn > 0.0f ? smooth(t / static_cast<double>(cue.fadeIn)) : 1.0f;
    const float down = cue.fadeOut > 0.0f ? smooth((static_cast<double>(cue.duration) - t) / static_cast<double>(cue.fadeOut)) : 1.0f;
    return up * down;
}

bool loadMusicCues(const DataSet& data, std::vector<MusicCue>& out)
{
    std::vector<MusicCue> loaded;
    if (const DataTable* table = data.table("music")) {
        for (const auto& [id, record] : table->records) {
            MusicCue cue;
            cue.id = id;
            const auto event = record.fields.find("event");
            if (event == record.fields.end() || !std::holds_alternative<std::string>(event->second)) {
                return false;
            }
            const std::optional<MusicEvent> parsed = eventNamed(std::get<std::string>(event->second));
            if (!parsed || !numberField(record, "duration", cue.duration) || !numberField(record, "fade_in", cue.fadeIn)
                || !numberField(record, "fade_out", cue.fadeOut) || !numberField(record, "cooldown", cue.cooldown)
                || !numberField(record, "gain", cue.gain) || !numberField(record, "root_hz", cue.rootHz)) {
                return false;
            }
            cue.event = *parsed;
            if (!validMusicCue(cue)) {
                return false;
            }
            loaded.push_back(std::move(cue));
        }
    }
    out = std::move(loaded);
    return true;
}

MusicDirector::MusicDirector(std::vector<MusicCue> cues, MusicConfig config, std::uint64_t seed)
    : cues_(std::move(cues))
    , config_(config)
    , rng_(seed, 0x6d75736963ull)
    , lastStart_(cues_.size(), -std::numeric_limits<double>::infinity())
{
}

void MusicDirector::advance(double now)
{
    if (current_ && now >= current_->end) {
        lastEnd_ = current_->end;
        lastPriority_ = musicPriority(cues_[current_->cue].event);
        current_.reset();
    }
}

MusicDecision MusicDirector::onEvent(MusicEvent event, double now)
{
    advance(now);
    const int priority = musicPriority(event);

    if (std::none_of(cues_.begin(), cues_.end(), [&](const MusicCue& cue) { return cue.event == event && validMusicCue(cue); })) {
        return {MusicVerdict::NoCue, npos, 0.0};
    }

    double startAt = now;
    double cutEnd = 0.0;
    bool cut = false;
    if (pending_ && priority <= pending_->priority) {
        return {MusicVerdict::Busy, npos, 0.0};
    }
    if (current_) {
        if (!pending_ && priority <= musicPriority(cues_[current_->cue].event)) {
            return {MusicVerdict::Busy, npos, 0.0};
        }
        double end = current_->end;
        if (current_->cutFade == 0.0) {
            const double faded = now + static_cast<double>(cues_[current_->cue].fadeOut);
            if (faded < end) {
                cut = true;
                cutEnd = faded;
                end = faded;
            }
        }
        startAt = end + config_.preemptGap;
    } else if (pending_ || (now < lastEnd_ + config_.minGap && priority > lastPriority_)) {
        startAt = std::max(now, lastEnd_ + config_.preemptGap);
    } else if (now < lastEnd_ + config_.minGap) {
        return {MusicVerdict::Gap, npos, 0.0};
    }

    std::vector<std::size_t> candidates;
    for (std::size_t i = 0; i < cues_.size(); ++i) {
        const MusicCue& cue = cues_[i];
        if (cue.event == event && validMusicCue(cue) && startAt >= lastStart_[i] + static_cast<double>(cue.cooldown)) {
            candidates.push_back(i);
        }
    }
    if (candidates.empty()) {
        return {MusicVerdict::Cooldown, npos, 0.0};
    }

    const std::size_t chosen = candidates[rng_.range(0, static_cast<std::uint32_t>(candidates.size() - 1))];
    if (cut) {
        current_->cutAt = now;
        current_->cutFade = cutEnd - now;
        current_->end = cutEnd;
        cutToReport_ = static_cast<float>(current_->cutFade);
    }
    pending_ = Pending{chosen, startAt, priority};
    return {MusicVerdict::Accepted, chosen, startAt};
}

MusicUpdate MusicDirector::update(double now)
{
    advance(now);
    MusicUpdate result;
    result.cutFadeSeconds = cutToReport_;
    cutToReport_ = 0.0f;
    if (pending_ && now >= pending_->startAt && !current_) {
        const MusicCue& cue = cues_[pending_->cue];
        current_ = Playing{pending_->cue, now, now + static_cast<double>(cue.duration), 0.0, 0.0};
        lastStart_[pending_->cue] = now;
        result.start = MusicStart{pending_->cue, static_cast<double>(cue.duration)};
        pending_.reset();
    }
    return result;
}

float MusicDirector::level(double now) const
{
    if (!current_ || now < current_->start || now >= current_->end) {
        return 0.0f;
    }
    float level = cueEnvelope(cues_[current_->cue], now - current_->start);
    if (current_->cutFade > 0.0) {
        level *= static_cast<float>(1.0 - std::clamp((now - current_->cutAt) / current_->cutFade, 0.0, 1.0));
    }
    return level;
}

bool MusicDirector::playing(double now) const
{
    return current_ && now >= current_->start && now < current_->end;
}

}
