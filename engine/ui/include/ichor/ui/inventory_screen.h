#pragma once

#include "ichor/ui/inventory_screen_model.h"
#include "ichor/ui/ui_context.h"

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

struct InventoryAction {
    enum class Kind : std::uint8_t { Use, Drop };
    Kind kind = Kind::Use;
    std::string itemId;
    bool operator==(const InventoryAction&) const = default;
};

// The inventory screen: a list with a weight column, a load bar that names the encumbrance level,
// and a Use (consumables) and Drop button on each row. Hidden until shown. The context must have the
// Cinzel and Alegreya Sans faces loaded.
class InventoryScreen {
public:
    explicit InventoryScreen(UiContext& ui);
    ~InventoryScreen();
    InventoryScreen(const InventoryScreen&) = delete;
    InventoryScreen& operator=(const InventoryScreen&) = delete;

    bool ok() const { return document_ != nullptr; }

    void setVisible(bool visible);
    bool visible() const { return visible_; }

    // Writes the view to the card; nothing is rewritten when it is the one already on it.
    void bind(const InventoryScreenView& view);

    // The markup currently on the card, for tests.
    std::string contentRml() const;

    // True once after the close button was clicked.
    bool takeCloseRequest();
    // The Use and Drop buttons clicked since the last call, in order.
    std::vector<InventoryAction> takeActions();

    // Where the close button, and a row's Use or Drop button, sit on screen once laid out.
    std::optional<std::pair<float, float>> closeButtonCentre() const;
    std::optional<std::pair<float, float>> actionButtonCentre(std::size_t row, InventoryAction::Kind kind) const;

private:
    struct Close final : Rml::EventListener {
        void ProcessEvent(Rml::Event&) override { requested = true; }
        bool requested = false;
    };
    struct Rows final : Rml::EventListener {
        void ProcessEvent(Rml::Event& event) override;
        std::vector<InventoryAction> clicks;
        const std::vector<InventoryScreenRow>* shown = nullptr;
    };

    UiContext& ui_;
    Close close_;
    Rows rows_;
    Rml::Element* closeButton_ = nullptr;
    Rml::ElementDocument* document_ = nullptr;
    Rml::Element* list_ = nullptr;
    Rml::Element* head_ = nullptr;
    InventoryScreenView shown_;
    bool visible_ = false;
    bool written_ = false;
};

}
