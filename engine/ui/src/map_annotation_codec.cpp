#include "ichor/ui/map_annotation_codec.h"

#include <cstring>

namespace ichor {

namespace {

constexpr std::uint8_t kMagic[4] = {'I', 'C', 'M', 'A'};
constexpr std::uint8_t kVersion = 1;
constexpr std::size_t kHeaderBytes = 9;
// kind, symbol, idLen, textLen, pointCount with an empty id, text and points.
constexpr std::size_t kMinRecordBytes = 2 + 2 + 2 + 2;

void put16(std::vector<std::uint8_t>& out, std::uint16_t v)
{
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
}

void putFloat(std::vector<std::uint8_t>& out, float f)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &f, sizeof bits);
    put32(out, bits);
}

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes)
        : bytes_(bytes)
    {
    }

    std::size_t remaining() const { return bytes_.size() - pos_; }
    bool u8(std::uint8_t& v)
    {
        if (remaining() < 1) {
            return false;
        }
        v = bytes_[pos_++];
        return true;
    }
    bool u16(std::uint16_t& v)
    {
        if (remaining() < 2) {
            return false;
        }
        v = static_cast<std::uint16_t>(bytes_[pos_] | (bytes_[pos_ + 1] << 8));
        pos_ += 2;
        return true;
    }
    bool u32(std::uint32_t& v)
    {
        if (remaining() < 4) {
            return false;
        }
        v = 0;
        for (int i = 0; i < 4; ++i) {
            v |= static_cast<std::uint32_t>(bytes_[pos_ + static_cast<std::size_t>(i)]) << (8 * i);
        }
        pos_ += 4;
        return true;
    }
    bool f32(float& v)
    {
        std::uint32_t bits = 0;
        if (!u32(bits)) {
            return false;
        }
        std::memcpy(&v, &bits, sizeof v);
        return true;
    }
    bool string(std::size_t length, std::string& out)
    {
        if (remaining() < length) {
            return false;
        }
        out.assign(reinterpret_cast<const char*>(bytes_.data() + pos_), length);
        pos_ += length;
        return true;
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t pos_ = 0;
};

AnnotationDecodeResult fail(AnnotationDecodeStatus status, const AnnotationLimits& limits)
{
    return {status, MapAnnotationStore(limits)};
}

} // namespace

std::vector<std::uint8_t> encodeAnnotations(const MapAnnotationStore& store)
{
    std::vector<std::uint8_t> out(std::begin(kMagic), std::end(kMagic));
    out.push_back(kVersion);
    put32(out, static_cast<std::uint32_t>(store.size()));
    for (const auto& a : store.all()) {
        out.push_back(static_cast<std::uint8_t>(a.kind));
        out.push_back(static_cast<std::uint8_t>(a.symbol));
        put16(out, static_cast<std::uint16_t>(a.id.size()));
        out.insert(out.end(), a.id.begin(), a.id.end());
        put16(out, static_cast<std::uint16_t>(a.text.size()));
        out.insert(out.end(), a.text.begin(), a.text.end());
        put16(out, static_cast<std::uint16_t>(a.points.size()));
        for (const auto& p : a.points) {
            putFloat(out, p.x);
            putFloat(out, p.y);
        }
    }
    return out;
}

AnnotationDecodeResult decodeAnnotations(std::span<const std::uint8_t> bytes, const AnnotationLimits& limits)
{
    if (bytes.size() < kHeaderBytes) {
        return fail(AnnotationDecodeStatus::Truncated, limits);
    }
    if (std::memcmp(bytes.data(), kMagic, sizeof kMagic) != 0) {
        return fail(AnnotationDecodeStatus::BadMagic, limits);
    }
    if (bytes[4] != kVersion) {
        return fail(AnnotationDecodeStatus::BadVersion, limits);
    }
    Reader reader(bytes.subspan(5));
    std::uint32_t count = 0;
    reader.u32(count);
    if (count > limits.maxAnnotations || static_cast<std::uint64_t>(count) * kMinRecordBytes > reader.remaining()) {
        return fail(AnnotationDecodeStatus::TooMany, limits);
    }

    MapAnnotationStore store(limits);
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint8_t kind = 0;
        std::uint8_t symbol = 0;
        std::uint16_t idLength = 0;
        std::uint16_t textLength = 0;
        std::uint16_t pointCount = 0;
        MapAnnotation a;
        if (!reader.u8(kind) || !reader.u8(symbol) || !reader.u16(idLength) || !reader.string(idLength, a.id) ||
            !reader.u16(textLength) || !reader.string(textLength, a.text) || !reader.u16(pointCount)) {
            return fail(AnnotationDecodeStatus::Truncated, limits);
        }
        if (static_cast<std::uint64_t>(pointCount) * 8 > reader.remaining()) {
            return fail(AnnotationDecodeStatus::Truncated, limits);
        }
        a.kind = static_cast<AnnotationKind>(kind);
        a.symbol = static_cast<AnnotationSymbol>(symbol);
        a.points.resize(pointCount);
        for (auto& p : a.points) {
            reader.f32(p.x);
            reader.f32(p.y);
        }
        if (store.add(std::move(a)) != AnnotationResult::Ok) {
            return fail(AnnotationDecodeStatus::InvalidRecord, limits);
        }
    }
    if (reader.remaining() != 0) {
        return fail(AnnotationDecodeStatus::TrailingBytes, limits);
    }
    return {AnnotationDecodeStatus::Ok, std::move(store)};
}

} // namespace ichor
