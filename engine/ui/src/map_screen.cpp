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
    #marker { position: absolute; left: 50%; top: 50%; width: 0px; height: 0px; }
    #dot { position: absolute; left: -6px; top: -6px; width: 8px; height: 8px; border-radius: 6px; background-color: #f0b040; border: 2px #10141a; }
    #empty { font-family: Alegreya Sans; font-size: 19px; color: #8890a0; text-align: center; padding: 18px 0px 18px 0px; }
</style></head><body>
    <div id="card"><div id="close">&#215;</div><div id="title">THE MAP</div>
        <div id="empty">The world is not drawn yet.</div>
        <div id="frame"><mappicture id="picture"></mappicture><mapmarker id="marker"><div id="dot"></div></mapmarker></div>
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
    if (close_ == nullptr || frame_ == nullptr || picture_ == nullptr || marker_ == nullptr || empty_ == nullptr || dynamic_cast<MapPicture*>(picture_) == nullptr
        || dynamic_cast<MapMarker*>(marker_) == nullptr) {
        ui_.unloadDocument(document_);
        document_ = nullptr;
        return;
    }
    close_->AddEventListener("click", &click_);
    setImage(MapImage{});
}

MapScreen::~MapScreen()
{
    if (close_ != nullptr) {
        // The element may live on until the context next updates, and the listener goes first.
        close_->RemoveEventListener("click", &click_);
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

void MapScreen::setVisible(bool visible)
{
    if (document_ == nullptr || visible == visible_) {
        return;
    }
    visible_ = visible;
    if (visible_) {
        document_->Show();
    } else {
        document_->Hide();
    }
}

void MapScreen::setImage(const MapImage& image)
{
    if (document_ == nullptr) {
        return;
    }
    const bool drawn = !image.empty();
    static_cast<MapPicture*>(picture_)->setImage(drawn ? image : MapImage{});
    frame_->SetProperty("display", drawn ? "block" : "none");
    empty_->SetProperty("display", drawn ? "none" : "block");
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
