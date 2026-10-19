#pragma once

#include <RmlUi/Core/RenderInterface.h>

#include <cstdint>
#include <map>
#include <vector>

namespace ichor {

// A render interface that draws nothing and remembers what it was asked to
// draw. UI tests assert on this instead of on pixels.
class RecordingUiRenderer final : public Rml::RenderInterface {
public:
    struct Draw {
        std::size_t vertexCount = 0;
        std::size_t indexCount = 0;
        Rml::Vector2f translation;
        bool scissored = false;
        Rml::Rectanglei scissor;
        Rml::ColourbPremultiplied firstColour;
        // A hash of the pixels of the texture the draw was bound to, 0 when it had none or it came from LoadTexture.
        std::uint64_t textureHash = 0;
    };

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
    // Textures are not stored. LoadTexture reports a 1 by 1 image for any source.
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture) override;
    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;

    // The draws since the last clear, in order.
    const std::vector<Draw>& draws() const { return draws_; }
    void clearDraws() { draws_.clear(); }
    std::size_t liveGeometry() const { return geometry_.size(); }

private:
    struct Geometry {
        std::size_t vertexCount = 0;
        std::size_t indexCount = 0;
        Rml::ColourbPremultiplied firstColour;
    };
    std::map<Rml::CompiledGeometryHandle, Geometry> geometry_;
    std::map<Rml::TextureHandle, std::uint64_t> textureHashes_;
    Rml::CompiledGeometryHandle next_ = 1;
    Rml::TextureHandle nextTexture_ = 1;
    std::vector<Draw> draws_;
    bool scissorOn_ = false;
    Rml::Rectanglei scissor_;
};

}
