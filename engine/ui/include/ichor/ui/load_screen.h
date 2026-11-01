#pragma once

#include "ichor/save/save_slots.h"
#include "ichor/ui/ui_context.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <RmlUi/Core/EventListener.h>

namespace Rml {
class Element;
class ElementDocument;
}

namespace ichor {

// One saved slot as the load screen shows it.
struct LoadRow {
    std::string slot;
    std::string title;     // the world's title, the same for every slot of a campaign
    std::string playtime;  // "1:02:03", empty for a damaged slot
    std::string thumbnail; // path of the picture beside the slot; empty shows the placeholder
    std::string note;      // what is wrong with the slot, empty when nothing is
    bool damaged = false;  // nothing in it can be read, so it cannot be chosen
    bool operator==(const LoadRow&) const = default;
};

struct LoadView {
    std::vector<LoadRow> rows;
    bool operator==(const LoadView&) const = default;
};

// Hours:minutes:seconds of simulated play.
std::string playtimeText(std::uint64_t ticks, double secondsPerTick);

// A damaged slot is listed, not hidden. `thumbnailOf` gives the picture path of a slot, or an empty path when it has none;
// it is not asked for a damaged slot.
LoadView buildLoadView(const std::vector<SlotEntry>& entries, const std::string& worldTitle, double secondsPerTick,
    const std::function<std::filesystem::path(const std::string&)>& thumbnailOf);

// The load screen: a card over the game listing each slot with its world title, playtime and picture.
// Choosing a slot is reported once. Hidden until shown. The context must have the Cinzel and Alegreya Sans faces loaded.
class LoadScreen {
public:
    explicit LoadScreen(UiContext& ui);
    ~LoadScreen();
    LoadScreen(const LoadScreen&) = delete;
    LoadScreen& operator=(const LoadScreen&) = delete;

    bool ok() const { return document_ != nullptr; }

    void setVisible(bool visible);
    bool visible() const { return visible_; }

    void setView(const LoadView& view);

    // The slot clicked since the last call, once.
    std::optional<std::string> takeChosen();
    bool takeBack();

    // Where a row and the Back button sit on screen once laid out; nothing while hidden or for a missing row.
    std::optional<std::pair<float, float>> rowCentre(std::size_t index) const;
    std::optional<std::pair<float, float>> backCentre() const;

    std::string markup() const;

private:
    struct Pick final : Rml::EventListener {
        explicit Pick(LoadScreen& owner)
            : screen(owner)
        {
        }
        void ProcessEvent(Rml::Event& event) override;
        LoadScreen& screen;
    };
    struct Click final : Rml::EventListener {
        void ProcessEvent(Rml::Event&) override { requested = true; }
        bool requested = false;
    };

    UiContext& ui_;
    Pick pick_{*this};
    Click back_;
    Rml::ElementDocument* document_ = nullptr;
    Rml::Element* list_ = nullptr;
    Rml::Element* backButton_ = nullptr;
    std::vector<std::string> choosable_; // slot name per row index, empty for a damaged row
    std::optional<std::string> chosen_;
    bool visible_ = false;
};

}
