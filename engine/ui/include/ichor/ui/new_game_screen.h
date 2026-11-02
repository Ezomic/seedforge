#pragma once

#include "ichor/data/axiom.h"
#include "ichor/data/featured_seeds.h"
#include "ichor/data/slot_roll.h"
#include "ichor/ui/ui_context.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <RmlUi/Core/EventListener.h>

namespace Rml {
class Element;
class ElementDocument;
}

namespace ichor {

// One axiom slot as the new-game screen shows it.
struct NewGameRow {
    char letter = 'A';
    std::string slot;  // "Cataclysm"
    std::string title; // the axiom's title from the data, or a placeholder when none was rolled
    bool operator==(const NewGameRow&) const = default;
};

// Everything the new-game screen may show. Slots D and E have no place in it: the view is
// built from the shown slots only, so the screen, its tooltips and anything that reads it
// cannot reveal them.
struct NewGameView {
    std::uint64_t seed = 0;
    std::vector<NewGameRow> rows; // A to C, in order
    bool operator==(const NewGameView&) const = default;
};

// One featured seed as the new-game screen lists it: the curated title and the titles of the
// visible axioms it rolls (slots A to C only, so the hidden slots stay hidden).
struct FeaturedRow {
    std::uint64_t seed = 0;
    std::string title;
    std::string summary;
    bool operator==(const FeaturedRow&) const = default;
};

// The rows for a featured list. An entry that fails validateFeaturedSeed against `pool` (empty or
// bad title, a roll that fails, a pin that does not match) is dropped, as is one whose roll shows
// no axiom from `axioms`. Order is kept.
std::vector<FeaturedRow> buildFeaturedRows(std::span<const FeaturedSeed> list, std::span<const Axiom> pool, const AxiomSet& axioms);

// The view for a world seed and its slot roll. A slot the roll did not fill, or filled with an
// axiom missing from `axioms`, shows a placeholder title; the roll's error text is never used,
// because it may name a hidden slot.
NewGameView buildNewGameView(std::uint64_t seed, const SlotRollResult& roll, const AxiomSet& axioms);

// The new-game screen: a card over the game with the world seed, the three visible axiom slots and
// a Begin button. Hidden until shown. The context must have the Cinzel and Alegreya Sans faces loaded.
class NewGameScreen {
public:
    explicit NewGameScreen(UiContext& ui);
    ~NewGameScreen();
    NewGameScreen(const NewGameScreen&) = delete;
    NewGameScreen& operator=(const NewGameScreen&) = delete;

    bool ok() const { return document_ != nullptr; }

    void setVisible(bool visible);
    bool visible() const { return visible_; }

    void setView(const NewGameView& view);

    // The featured list shown above Begin. An empty list hides the section.
    void setFeatured(const std::vector<FeaturedRow>& rows);

    // The seed of the featured row clicked since the last call, once. The caller fills the seed
    // by setting a new view for it.
    std::optional<std::uint64_t> takePickedSeed();

    // True once after the Begin button was clicked.
    bool takeBegin();

    // Where the Begin button sits on screen, once the card has been laid out.
    std::optional<std::pair<float, float>> beginCentre() const;

    // The markup of the seed and the slot list, for tests.
    std::string markup() const;

private:
    struct Click final : Rml::EventListener {
        void ProcessEvent(Rml::Event&) override { requested = true; }
        bool requested = false;
    };

    struct Pick final : Rml::EventListener {
        void ProcessEvent(Rml::Event& event) override;
        std::vector<std::uint64_t> seeds;
        std::optional<std::uint64_t> picked;
    };

    UiContext& ui_;
    Click click_;
    Pick pick_;
    Rml::Element* featured_ = nullptr;
    Rml::ElementDocument* document_ = nullptr;
    Rml::Element* begin_ = nullptr;
    Rml::Element* seed_ = nullptr;
    Rml::Element* list_ = nullptr;
    bool visible_ = false;
};

}
