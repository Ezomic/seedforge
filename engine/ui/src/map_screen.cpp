#include "ichor/ui/map_screen.h"

#include <RmlUi/Core/CallbackTexture.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/MeshUtilities.h>
#include <RmlUi/Core/RenderManager.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>
#include <vector>

namespace ichor {

namespace {

constexpr const char* kMapDocument = R"(<rml><head><style>
    body, div, mappicture, mapmarker { display: block; }
    body { width: 100%; height: 100%; }
    #card { position: absolute; top: 8%; left: 26%; right: 26%; overflow: hidden; padding: 22px 28px 22px 28px; background-color: #10141af0; border: 3px #a08850; }
    #close { position: absolute; top: 10px; right: 14px; width: 34px; height: 34px; font-family: Alegreya Sans; font-size: 30px; line-height: 34px; text-align: center; color: #a08850; }
    #close:hover { color: #e8d8a8; background-color: #2c3340; }
    #title { font-family: Cinzel; font-size: 34px; color: #e8d8a8; letter-spacing: 3px; text-align: center; padding-bottom: 12px; }
    #frame { position: relative; width: 50vh; height: 50vh; margin: 0px auto 0px auto; border: 2px #a08850; background-color: #1c2230; }
    #picture { width: 100%; height: 100%; }
    #places { position: absolute; left: 0px; top: 0px; width: 100%; height: 100%; }
    .place { position: absolute; width: 0px; height: 0px; }
    .icon { position: absolute; background-color: #e8d8a8; border: 2px #10141a; }
    .t0 .icon { left: -3px; top: -3px; width: 4px; height: 4px; border-radius: 4px; }
    .t1 .icon { left: -4px; top: -4px; width: 6px; height: 6px; border-radius: 6px; background-color: #f0e0b0; }
    .t2 .icon { left: -6px; top: -6px; width: 8px; height: 8px; background-color: #f0c860; }
    .t3 .icon { left: -8px; top: -8px; width: 12px; height: 12px; background-color: #f0a030; border: 2px #fff4d0; }
    .name { position: absolute; left: -60px; width: 120px; top: 7px; text-align: center; font-family: Alegreya Sans; color: #f4ecd0; }
    .t0 .name { font-size: 12px; color: #c8c0a0; }
    .t1 .name { font-size: 14px; }
    .t2 .name { font-size: 16px; }
    .t3 .name { font-size: 19px; top: 10px; color: #fff4d0; }
    #marker { position: absolute; left: 50%; top: 50%; width: 0px; height: 0px; }
    #dot { position: absolute; left: -6px; top: -6px; width: 8px; height: 8px; border-radius: 6px; background-color: #f0b040; border: 2px #10141a; }
    #layers { text-align: center; padding-top: 12px; }
    .layer { display: inline-block; font-family: Alegreya Sans; font-size: 19px; color: #8890a0; padding: 3px 12px 3px 12px; margin: 0px 4px 0px 4px; border: 2px #4a4430; }
    .layer:hover { color: #e8d8a8; background-color: #2c3340; }
    .layer.on { color: #10141a; background-color: #a08850; border-color: #e8d8a8; }
    #empty { font-family: Alegreya Sans; font-size: 19px; color: #8890a0; text-align: center; padding: 18px 0px 18px 0px; }
</style></head><body>
    <div id="card"><div id="close">&#215;</div><div id="title">THE MAP</div>
        <div id="empty">The world is not drawn yet.</div>
        <div id="frame"><mappicture id="picture"></mappicture><div id="places"></div><mapmarker id="marker"><div id="dot"></div></mapmarker></div>
        <div id="layers"><div class="layer" id="layer0">Biomes</div><div class="layer" id="layer1">Roads</div><div class="layer" id="layer2">Territories</div><div class="layer" id="layer3">Rivers</div></div>
    </div>
</body></rml>)";

// Draws the map picture as one textured quad over its content box.
class MapPicture final : public Rml::Element {
public:
    explicit MapPicture(const Rml::String& tag)
        : Rml::Element(tag)
    {
    }

    void setImage(const MapImage& image)
    {
        pixels_ = image.rgba;
        size_ = {static_cast<int>(image.width), static_cast<int>(image.height)};
        texture_.Release();
    }

protected:
    void OnRender() override
    {
        Rml::RenderManager* manager = GetRenderManager();
        if (pixels_.empty() || manager == nullptr) {
            return;
        }
        if (!texture_) {
            texture_ = manager->MakeCallbackTexture([this](const Rml::CallbackTextureInterface& texture) {
                return texture.GenerateTexture(Rml::Span<const Rml::byte>(pixels_.data(), pixels_.size()), size_);
            });
        }
        if (!geometry_) {
            Rml::Mesh mesh;
            Rml::MeshUtilities::GenerateQuad(mesh, {0.0f, 0.0f}, GetBox().GetSize(Rml::BoxArea::Content), Rml::ColourbPremultiplied(255, 255, 255, 255));
            geometry_ = manager->MakeGeometry(std::move(mesh));
        }
        geometry_.Render(GetAbsoluteOffset(Rml::BoxArea::Content), texture_);
    }

    void OnResize() override { geometry_.Release(); }

private:
    std::vector<std::uint8_t> pixels_;
    Rml::Vector2i size_;
    Rml::CallbackTexture texture_;
    Rml::Geometry geometry_;
};

// The heading arrow, drawn as a triangle turned in code because the UI renderer has no transforms.
// The element is a point; the arrow leaves from it.
class MapMarker final : public Rml::Element {
public:
    explicit MapMarker(const Rml::String& tag)
        : Rml::Element(tag)
    {
    }

    void setDegrees(float degrees)
    {
        degrees_ = degrees;
        geometry_.Release();
    }

protected:
    void OnRender() override
    {
        Rml::RenderManager* manager = GetRenderManager();
        if (manager == nullptr) {
            return;
        }
        if (!geometry_) {
            const float radians = degrees_ * 3.14159265f / 180.0f;
            const Rml::Vector2f ahead{std::sin(radians), -std::cos(radians)};
            const Rml::Vector2f side{std::cos(radians), std::sin(radians)};
            Rml::Mesh mesh;
            const auto triangle = [&](float length, float back, float half, Rml::ColourbPremultiplied colour) {
                const int first = static_cast<int>(mesh.vertices.size());
                for (const Rml::Vector2f point : {ahead * length, side * -half - ahead * back, side * half - ahead * back}) {
                    mesh.vertices.push_back({point, colour, {0.0f, 0.0f}});
                }
                mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2});
            };
            triangle(21.0f, 6.0f, 9.0f, Rml::ColourbPremultiplied(16, 20, 26, 255));
            triangle(18.0f, 4.0f, 6.0f, Rml::ColourbPremultiplied(240, 176, 64, 255));
            geometry_ = manager->MakeGeometry(std::move(mesh));
        }
        geometry_.Render(GetAbsoluteOffset(Rml::BoxArea::Border));
    }

private:
    float degrees_ = 0.0f;
    Rml::Geometry geometry_;
};

Rml::ElementInstancerGeneric<MapPicture> g_pictureInstancer;
Rml::ElementInstancerGeneric<MapMarker> g_markerInstancer;

std::string escaped(const std::string& text)
{
    std::string out;
    for (const char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}

struct Box {
    float x;
    float y;
    float w;
    float h;
};

bool overlaps(const Box& a, const Box& b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

constexpr float kIconHalf[4] = {3.0f, 4.0f, 6.0f, 8.0f};
constexpr float kFontPx[4] = {12.0f, 14.0f, 16.0f, 19.0f};

float degreesOf(float radians)
{
    if (!std::isfinite(radians)) {
        return 0.0f;
    }
    const double degrees = std::fmod(static_cast<double>(radians) * 180.0 / 3.14159265358979323846, 360.0);
    const float wrapped = static_cast<float>(degrees < 0.0 ? degrees + 360.0 : degrees);
    return wrapped >= 360.0f ? 0.0f : wrapped;
}

}

MapScreen::MapScreen(UiContext& ui)
    : ui_(ui)
{
    // RmlUi forgets its element types when the last context goes, so every screen says it again.
    Rml::Factory::RegisterElementInstancer("mappicture", &g_pictureInstancer);
    Rml::Factory::RegisterElementInstancer("mapmarker", &g_markerInstancer);
    document_ = ui_.loadDocumentFromMemory(kMapDocument, "[map]");
    if (document_ == nullptr) {
        return;
    }
    close_ = document_->GetElementById("close");
    frame_ = document_->GetElementById("frame");
    picture_ = document_->GetElementById("picture");
    marker_ = document_->GetElementById("marker");
    empty_ = document_->GetElementById("empty");
    places_ = document_->GetElementById("places");
    layersRow_ = document_->GetElementById("layers");
    if (layersRow_ == nullptr || places_ == nullptr || close_ == nullptr || frame_ == nullptr || picture_ == nullptr || marker_ == nullptr || empty_ == nullptr || dynamic_cast<MapPicture*>(picture_) == nullptr
        || dynamic_cast<MapMarker*>(marker_) == nullptr) {
        ui_.unloadDocument(document_);
        document_ = nullptr;
        close_ = frame_ = picture_ = marker_ = empty_ = layersRow_ = nullptr;
        return;
    }
    close_->AddEventListener("click", &click_);
    for (std::size_t i = 0; i < kMapLayerCount; ++i) {
        layerButtons_[i] = document_->GetElementById(std::format("layer{}", i));
        if (layerButtons_[i] == nullptr) {
            close_->RemoveEventListener("click", &click_);
            ui_.unloadDocument(document_);
            document_ = nullptr;
            close_ = frame_ = picture_ = marker_ = empty_ = layersRow_ = nullptr;
            layerButtons_ = {};
            return;
        }
    }
    for (std::size_t i = 0; i < kMapLayerCount; ++i) {
        layerClicks_[i].screen = this;
        layerClicks_[i].layer = static_cast<MapLayer>(i);
        layerButtons_[i]->AddEventListener("click", &layerClicks_[i]);
    }
    setImage(MapImage{});
}

MapScreen::~MapScreen()
{
    if (close_ != nullptr) {
        // The element may live on until the context next updates, and the listener goes first.
        close_->RemoveEventListener("click", &click_);
    }
    for (std::size_t i = 0; i < kMapLayerCount; ++i) {
        if (layerButtons_[i] != nullptr) {
            layerButtons_[i]->RemoveEventListener("click", &layerClicks_[i]);
        }
    }
    if (document_ != nullptr) {
        ui_.unloadDocument(document_);
    }
}

bool MapScreen::takeCloseRequest()
{
    const bool requested = click_.requested;
    click_.requested = false;
    return requested;
}

std::optional<std::pair<float, float>> MapScreen::closeButtonCentre() const
{
    if (close_ == nullptr || !visible_) {
        return std::nullopt;
    }
    const Rml::Vector2f offset = close_->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f size = close_->GetBox().GetSize(Rml::BoxArea::Border);
    return std::pair{offset.x + size.x * 0.5f, offset.y + size.y * 0.5f};
}

std::optional<std::pair<float, float>> MapScreen::layerButtonCentre(MapLayer layer) const
{
    Rml::Element* button = layerButtons_[static_cast<std::size_t>(layer)];
    if (button == nullptr || !visible_ || !stack_.hasLayer(layer)) {
        return std::nullopt;
    }
    const Rml::Vector2f offset = button->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f size = button->GetBox().GetSize(Rml::BoxArea::Border);
    return std::pair{offset.x + size.x * 0.5f, offset.y + size.y * 0.5f};
}

void MapScreen::setVisible(bool visible)
{
    if (document_ == nullptr || visible == visible_) {
        return;
    }
    visible_ = visible;
    if (visible_) {
        document_->Show();
        redrawPlaces();
    } else {
        document_->Hide();
    }
}

void MapScreen::setImage(const MapImage& image)
{
    if (document_ == nullptr) {
        return;
    }
    plain_ = image.empty() ? MapImage{} : image;
    stack_ = MapLayerStack{};
    redraw();
    redrawPlaces();
}

void MapScreen::setLayerStack(MapLayerStack stack)
{
    if (document_ == nullptr) {
        return;
    }
    stack_ = std::move(stack);
    redraw();
    redrawPlaces();
}

void MapScreen::setDiscovery(const MapDiscovery& discovery)
{
    if (document_ == nullptr || (discovery_ && *discovery_ == discovery)) {
        return;
    }
    discovery_ = discovery;
    redraw();
    redrawPlaces();
}

void MapScreen::clearDiscovery()
{
    if (document_ == nullptr || !discovery_) {
        return;
    }
    discovery_.reset();
    redraw();
    redrawPlaces();
}

void MapScreen::setSettlements(std::vector<MapSettlement> settlements)
{
    if (document_ == nullptr) {
        return;
    }
    settlements_ = std::move(settlements);
    redrawPlaces();
}

void MapScreen::redrawPlaces()
{
    std::string rml;
    shownNames_.clear();
    std::vector<Box> taken;
    const std::uint32_t side = stack_.empty() ? plain_.width : stack_.base().width;
    const bool fogApplies = discovery_ && discovery_->side() == side;
    std::vector<const MapSettlement*> shown;
    for (const MapSettlement& place : settlements_) {
        if (fogApplies && !discovery_->seen(place.at)) {
            continue;
        }
        shownNames_.push_back(place.name);
        shown.push_back(&place);
    }
    const float measured = frame_->GetBox().GetSize(Rml::BoxArea::Border).x;
    const float frame = measured > 1.0f ? measured : 360.0f;
    std::vector<Box> icons;
    for (const MapSettlement* place : shown) {
        const int tier = std::min<int>(place->tier, 3);
        const float half = kIconHalf[static_cast<std::size_t>(tier)] + 2.0f;
        icons.push_back({std::clamp(place->at.u, 0.0f, 1.0f) * frame - half, std::clamp(place->at.v, 0.0f, 1.0f) * frame - half, half * 2.0f, half * 2.0f});
    }
    for (std::size_t index = 0; index < shown.size(); ++index) {
        const MapSettlement* place = shown[index];
        const int tier = std::min<int>(place->tier, 3);
        const float px = std::clamp(place->at.u, 0.0f, 1.0f) * frame;
        const float py = std::clamp(place->at.v, 0.0f, 1.0f) * frame;
        const float font = kFontPx[static_cast<std::size_t>(tier)];
        const float w = static_cast<float>(place->name.size()) * font * 0.5f + 4.0f;
        const float h = font + 2.0f;
        const float gap = kIconHalf[static_cast<std::size_t>(tier)] + 4.0f;
        // Below, above, right, left: the first spot where the label meets no icon and no earlier label.
        const Box spots[4] = {{px - w / 2, py + gap, w, h}, {px - w / 2, py - gap - h, w, h}, {px + gap, py - h / 2, w, h}, {px - gap - w, py - h / 2, w, h}};
        std::size_t chosen = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            bool clear = spots[i].x >= 0.0f && spots[i].x + w <= frame && spots[i].y >= 0.0f && spots[i].y + h <= frame;
            for (const Box& other : taken) {
                clear = clear && !overlaps(spots[i], other);
            }
            for (std::size_t other = 0; other < icons.size(); ++other) {
                clear = clear && (other == index || !overlaps(spots[i], icons[other]));
            }
            if (clear) {
                chosen = i;
                break;
            }
        }
        taken.push_back(spots[chosen]);
        const Box& spot = spots[chosen];
        rml += std::format(R"(<div class="place t{}" style="left: {:.2f}%; top: {:.2f}%;"><div class="icon"></div><div class="name" style="left: {:.0f}px; top: {:.0f}px; width: {:.0f}px; text-align: {};">{}</div></div>)", tier,
            std::clamp(place->at.u, 0.0f, 1.0f) * 100.0f, std::clamp(place->at.v, 0.0f, 1.0f) * 100.0f, spot.x - px, spot.y - py, w, chosen == 2 ? "left" : chosen == 3 ? "right" : "center", escaped(place->name));
    }
    places_->SetInnerRML(rml);
}

void MapScreen::setLayer(MapLayer layer, bool on)
{
    if (document_ == nullptr || layers_.on(layer) == on) {
        return;
    }
    layers_.set(layer, on);
    redraw();
}

void MapScreen::setColourblindSafe(bool on)
{
    if (document_ == nullptr || colourblindSafe_ == on) {
        return;
    }
    colourblindSafe_ = on;
    redraw();
}

void MapScreen::redraw()
{
    MapImage drawing = stack_.empty() ? plain_ : stack_.composite(layers_, colourblindSafe_);
    if (discovery_ && !drawing.empty()) {
        drawing = withFog(drawing, *discovery_);
    }
    const bool drawn = !drawing.empty();
    static_cast<MapPicture*>(picture_)->setImage(drawing);
    frame_->SetProperty("display", drawn ? "block" : "none");
    empty_->SetProperty("display", drawn ? "none" : "block");
    bool anyLayer = false;
    for (std::size_t i = 0; i < kMapLayerCount; ++i) {
        const MapLayer layer = static_cast<MapLayer>(i);
        anyLayer = anyLayer || stack_.hasLayer(layer);
        layerButtons_[i]->SetProperty("display", stack_.hasLayer(layer) ? "inline-block" : "none");
        layerButtons_[i]->SetClass("on", layers_.on(layer));
    }
    layersRow_->SetProperty("display", anyLayer ? "block" : "none");
}

void MapScreen::setPlayer(MapPoint where, float headingRadians)
{
    if (document_ == nullptr) {
        return;
    }
    const auto clamped = [](float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.5f; };
    const MapPoint point{clamped(where.u), clamped(where.v)};
    const float degrees = degreesOf(headingRadians);
    if (point != point_) {
        point_ = point;
        marker_->SetProperty("left", std::format("{:.2f}%", point_.u * 100.0f));
        marker_->SetProperty("top", std::format("{:.2f}%", point_.v * 100.0f));
    }
    if (degrees != degrees_) {
        degrees_ = degrees;
        static_cast<MapMarker*>(marker_)->setDegrees(degrees_);
    }
}

}
