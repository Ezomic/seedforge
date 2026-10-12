#include "ichor/ui/recording_renderer.h"

namespace ichor {

Rml::CompiledGeometryHandle RecordingUiRenderer::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
{
    Geometry geometry;
    geometry.vertexCount = vertices.size();
    geometry.indexCount = indices.size();
    if (!vertices.empty()) {
        geometry.firstColour = vertices[0].colour;
    }
    const Rml::CompiledGeometryHandle handle = next_++;
    geometry_.emplace(handle, geometry);
    return handle;
}

void RecordingUiRenderer::RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle)
{
    const auto found = geometry_.find(geometry);
    if (found == geometry_.end()) {
        return;
    }
    Draw draw;
    draw.vertexCount = found->second.vertexCount;
    draw.indexCount = found->second.indexCount;
    draw.translation = translation;
    draw.scissored = scissorOn_;
    draw.scissor = scissor_;
    draw.firstColour = found->second.firstColour;
    draws_.push_back(draw);
}

void RecordingUiRenderer::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
{
    geometry_.erase(geometry);
}

Rml::TextureHandle RecordingUiRenderer::LoadTexture(Rml::Vector2i& dimensions, const Rml::String&)
{
    dimensions = {1, 1};
    return nextTexture_++;
}

Rml::TextureHandle RecordingUiRenderer::GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i)
{
    return nextTexture_++;
}

void RecordingUiRenderer::ReleaseTexture(Rml::TextureHandle)
{
}

void RecordingUiRenderer::EnableScissorRegion(bool enable)
{
    scissorOn_ = enable;
}

void RecordingUiRenderer::SetScissorRegion(Rml::Rectanglei region)
{
    scissor_ = region;
}

}
