#include "ichor/ui/rebind_screen.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include <format>
#include <string>

namespace ichor {

namespace {

constexpr std::size_t kRowsPerColumn = kBindActionCount / 2;

constexpr const char* kStyle = R"(<rml><head><style>
    body, div { display: block; }
    body { width: 100%; height: 100%; }
    #veil { position: absolute; top: 0px; left: 0px; right: 0px; bottom: 0px; background-color: #05070bb0; }
    #card { position: absolute; top: 2%; left: 50%; margin-left: -383px; width: 700px; padding: 14px 30px 16px 30px; background-color: #10141af4; border: 3px #a08850; }
    #title { font-family: Cinzel; font-size: 32px; color: #e8d8a8; letter-spacing: 4px; text-align: center; padding-bottom: 8px; }
    .column { float: left; width: 330px; }
    .gap { float: left; width: 40px; height: 10px; }
    .row { height: 36px; margin-top: 4px; }
    .name { float: left; width: 170px; font-family: Alegreya Sans; font-size: 20px; color: #a8a090; padding-top: 6px; }
    .key { float: right; width: 140px; font-family: Alegreya Sans; font-size: 22px; text-align: center; color: #f4e8c0; padding: 2px 0px 4px 0px; background-color: #1c222c; border: 2px #3a4252; }
    .key:hover { background-color: #2a3242; border-color: #a08850; }
    .waiting { color: #e8d8a8; background-color: #2a3242; border-color: #a08850; }
    .button { clear: both; font-family: Alegreya Sans; font-size: 24px; text-align: center; color: #d8d8d8; padding: 4px 0px 6px 0px; margin-top: 6px; background-color: #1c222c; border: 2px #3a4252; }
    .button:hover { color: #f4e8c0; background-color: #2a3242; border-color: #a08850; }
    #hint { clear: both; font-family: Alegreya Sans; font-size: 18px; color: #a8a090; text-align: center; padding-top: 10px; }
</style></head><body><div id="veil"></div><div id="card"><div id="title">CONTROLS</div>)";

std::string buildDocument()
{
    std::string rml = kStyle;
    for (std::size_t column = 0; column < 2; ++column) {
        rml += "<div class=\"column\">";
        for (std::size_t row = 0; row < kRowsPerColumn; ++row) {
            const std::size_t index = column * kRowsPerColumn + row;
            rml += std::format("<div class=\"row\"><div class=\"name\">{}</div><div id=\"key{}\" class=\"key\"></div></div>", bindActionLabel(static_cast<BindAction>(index)), index);
        }
        rml += column == 0 ? "</div><div class=\"gap\"></div>" : "</div>";
    }
    rml += "<div id=\"hint\">Click a key, then press the new one. A key already in use is swapped. Esc cancels.</div>";
    rml += "<div id=\"reset\" class=\"button\">Reset to defaults</div><div id=\"back\" class=\"button\">Back</div></div></body></rml>";
    return rml;
}

}

RebindScreen::RebindScreen(UiContext& ui)
    : ui_(ui)
{
    document_ = ui_.loadDocumentFromMemory(buildDocument().c_str(), "[rebind screen]");
    if (document_ == nullptr) {
        return;
    }
    reset_ = document_->GetElementById("reset");
    back_ = document_->GetElementById("back");
    bool found = reset_ != nullptr && back_ != nullptr;
    for (std::size_t i = 0; i < kBindActionCount; ++i) {
        keys_[i] = document_->GetElementById(std::format("key{}", i));
        found = found && keys_[i] != nullptr;
    }
    if (!found) {
        ui_.unloadDocument(document_);
        document_ = nullptr;
        return;
    }
    for (std::size_t i = 0; i < kBindActionCount; ++i) {
        keys_[i]->AddEventListener("click", &keyClicks_[i]);
    }
    reset_->AddEventListener("click", &resetClick_);
    back_->AddEventListener("click", &backClick_);
    refresh();
}

RebindScreen::~RebindScreen()
{
    if (document_ == nullptr) {
        return;
    }
    // The elements may live on until the context next updates, and the listeners go first.
    for (std::size_t i = 0; i < kBindActionCount; ++i) {
        keys_[i]->RemoveEventListener("click", &keyClicks_[i]);
    }
    reset_->RemoveEventListener("click", &resetClick_);
    back_->RemoveEventListener("click", &backClick_);
    ui_.unloadDocument(document_);
}

void RebindScreen::setVisible(bool visible)
{
    if (document_ == nullptr || visible == visible_) {
        return;
    }
    visible_ = visible;
    if (visible_) {
        for (Click& click : keyClicks_) {
            click.requested = false; // a click from an earlier showing is not a click now
        }
        resetClick_.requested = false;
        backClick_.requested = false;
        waiting_.reset();
        refresh();
        document_->Show();
    } else {
        document_->Hide();
    }
}

void RebindScreen::setBindings(const KeyBindings& bindings)
{
    bindings_ = bindings;
    refresh();
}

void RebindScreen::setWaiting(std::optional<BindAction> action)
{
    waiting_ = action;
    refresh();
}

std::optional<BindAction> RebindScreen::takeRebindClick()
{
    for (std::size_t i = 0; i < kBindActionCount; ++i) {
        if (keyClicks_[i].requested) {
            for (Click& click : keyClicks_) {
                click.requested = false;
            }
            return static_cast<BindAction>(i);
        }
    }
    return std::nullopt;
}

bool RebindScreen::takeReset()
{
    return std::exchange(resetClick_.requested, false);
}

bool RebindScreen::takeBack()
{
    return std::exchange(backClick_.requested, false);
}

void RebindScreen::refresh()
{
    if (document_ == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < kBindActionCount; ++i) {
        const auto action = static_cast<BindAction>(i);
        const bool waiting = waiting_ == action;
        keys_[i]->SetInnerRML(waiting ? "Press a key" : keyName(bindings_.key(action)));
        keys_[i]->SetClass("waiting", waiting);
    }
}

std::optional<std::pair<float, float>> RebindScreen::centreOf(Rml::Element* element) const
{
    if (element == nullptr || !visible_) {
        return std::nullopt;
    }
    const Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f size = element->GetBox().GetSize(Rml::BoxArea::Border);
    return std::pair{offset.x + size.x * 0.5f, offset.y + size.y * 0.5f};
}

}
