#include "ichor/assets/fbx.h"

#include <zlib.h>

#include <cstring>
#include <format>

namespace ichor::fbx {

namespace {

constexpr std::size_t kHeaderSize = 27;
constexpr std::size_t kMaxDepth = 32;
constexpr std::size_t kMaxNodes = 2'000'000;
constexpr std::size_t kMaxProperties = 4'000'000;
// What the decoded arrays may occupy once widened to the 8 byte elements Property stores,
// per array and for the whole file.
constexpr std::uint64_t kMaxArrayBytes = 256ull * 1024 * 1024;
constexpr std::uint64_t kMaxTotalArrayBytes = 512ull * 1024 * 1024;
// Deflate cannot expand more than 1032 to 1, so a longer claim is a lie.
constexpr std::uint64_t kMaxInflateRatio = 1032;

class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    std::size_t pos() const { return pos_; }
    std::size_t size() const { return size_; }
    std::size_t remaining() const { return size_ - pos_; }
    void seek(std::size_t pos) { pos_ = pos; }

    bool take(std::size_t count, const std::uint8_t*& out)
    {
        if (count > remaining()) {
            return false;
        }
        out = data_ + pos_;
        pos_ += count;
        return true;
    }

    template <class T>
    bool read(T& out)
    {
        const std::uint8_t* p = nullptr;
        if (!take(sizeof(T), p)) {
            return false;
        }
        std::memcpy(&out, p, sizeof(T));
        return true;
    }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
};

struct Context {
    std::uint32_t version = 0;
    std::size_t nodes = 0;
    std::size_t properties = 0;
    std::uint64_t arrayBytes = 0;
    std::string error;

    bool wide() const { return version >= 7500; }
    std::size_t recordHeader() const { return wide() ? 25 : 13; }
    bool fail(std::string message)
    {
        if (error.empty()) {
            error = std::move(message);
        }
        return false;
    }
};

std::size_t elementSize(char type)
{
    switch (type) {
    case 'f': case 'i': return 4;
    case 'd': case 'l': return 8;
    case 'b': return 1;
    default: return 0;
    }
}

template <class T>
T load(const std::uint8_t* p)
{
    T v;
    std::memcpy(&v, p, sizeof(T));
    return v;
}

bool readArray(Reader& r, char type, Property& prop, Context& ctx)
{
    std::uint32_t length = 0;
    std::uint32_t encoding = 0;
    std::uint32_t compressed = 0;
    if (!r.read(length) || !r.read(encoding) || !r.read(compressed)) {
        return ctx.fail("truncated array header");
    }
    const std::size_t width = elementSize(type);
    const std::uint64_t rawBytes = static_cast<std::uint64_t>(length) * width;
    const std::uint64_t storedBytes = static_cast<std::uint64_t>(length) * 8;
    if (storedBytes > kMaxArrayBytes || ctx.arrayBytes + storedBytes > kMaxTotalArrayBytes) {
        return ctx.fail("array too large");
    }
    ctx.arrayBytes += storedBytes;
    const std::uint8_t* src = nullptr;
    std::vector<std::uint8_t> inflated;
    if (encoding == 0) {
        if (!r.take(static_cast<std::size_t>(rawBytes), src)) {
            return ctx.fail("truncated array");
        }
    } else if (encoding == 1) {
        if (!r.take(compressed, src)) {
            return ctx.fail("truncated compressed array");
        }
        if (rawBytes > static_cast<std::uint64_t>(compressed) * kMaxInflateRatio) {
            return ctx.fail("compressed array claims an impossible size");
        }
        inflated.resize(static_cast<std::size_t>(rawBytes));
        uLongf outSize = static_cast<uLongf>(rawBytes);
        if (uncompress(inflated.data(), &outSize, src, compressed) != Z_OK || outSize != rawBytes) {
            return ctx.fail("array failed to inflate");
        }
        src = inflated.data();
    } else {
        return ctx.fail("unknown array encoding");
    }
    if (type == 'f') {
        prop.reals.resize(length);
        for (std::uint32_t i = 0; i < length; ++i) prop.reals[i] = load<float>(src + i * 4ull);
    } else if (type == 'd') {
        prop.reals.resize(length);
        for (std::uint32_t i = 0; i < length; ++i) prop.reals[i] = load<double>(src + i * 8ull);
    } else if (type == 'i') {
        prop.ints.resize(length);
        for (std::uint32_t i = 0; i < length; ++i) prop.ints[i] = load<std::int32_t>(src + i * 4ull);
    } else if (type == 'l') {
        prop.ints.resize(length);
        for (std::uint32_t i = 0; i < length; ++i) prop.ints[i] = load<std::int64_t>(src + i * 8ull);
    } else {
        prop.ints.resize(length);
        for (std::uint32_t i = 0; i < length; ++i) prop.ints[i] = src[i];
    }
    return true;
}

bool readProperty(Reader& r, Property& prop, Context& ctx)
{
    std::uint8_t code = 0;
    if (!r.read(code)) {
        return ctx.fail("truncated property");
    }
    prop.type = static_cast<char>(code);
    switch (prop.type) {
    case 'Y': { std::int16_t v = 0; if (!r.read(v)) return ctx.fail("truncated property"); prop.integer = v; return true; }
    case 'C': { std::uint8_t v = 0; if (!r.read(v)) return ctx.fail("truncated property"); prop.integer = v; return true; }
    case 'I': { std::int32_t v = 0; if (!r.read(v)) return ctx.fail("truncated property"); prop.integer = v; return true; }
    case 'L': { std::int64_t v = 0; if (!r.read(v)) return ctx.fail("truncated property"); prop.integer = v; return true; }
    case 'F': { float v = 0; if (!r.read(v)) return ctx.fail("truncated property"); prop.real = v; return true; }
    case 'D': { double v = 0; if (!r.read(v)) return ctx.fail("truncated property"); prop.real = v; return true; }
    case 'S':
    case 'R': {
        std::uint32_t length = 0;
        const std::uint8_t* p = nullptr;
        if (!r.read(length) || !r.take(length, p)) {
            return ctx.fail("truncated string");
        }
        prop.text.assign(reinterpret_cast<const char*>(p), length);
        return true;
    }
    case 'f': case 'd': case 'i': case 'l': case 'b':
        return readArray(r, prop.type, prop, ctx);
    default:
        return ctx.fail(std::format("unknown property type {}", static_cast<int>(code)));
    }
}

// Reads one record. `isNull` reports the all-zero sentinel that ends a list.
bool readNode(Reader& r, std::size_t depth, Node& node, bool& isNull, Context& ctx)
{
    if (depth > kMaxDepth) {
        return ctx.fail("nodes nest too deeply");
    }
    if (++ctx.nodes > kMaxNodes) {
        return ctx.fail("too many nodes");
    }
    std::uint64_t endOffset = 0;
    std::uint64_t propCount = 0;
    std::uint64_t propBytes = 0;
    std::uint8_t nameLength = 0;
    if (ctx.wide()) {
        if (!r.read(endOffset) || !r.read(propCount) || !r.read(propBytes) || !r.read(nameLength)) {
            return ctx.fail("truncated node header");
        }
    } else {
        std::uint32_t a = 0;
        std::uint32_t b = 0;
        std::uint32_t c = 0;
        if (!r.read(a) || !r.read(b) || !r.read(c) || !r.read(nameLength)) {
            return ctx.fail("truncated node header");
        }
        endOffset = a;
        propCount = b;
        propBytes = c;
    }
    isNull = endOffset == 0;
    if (isNull) {
        return true;
    }
    if (endOffset > r.size() || endOffset < r.pos()) {
        return ctx.fail("node end is out of bounds");
    }
    const std::uint8_t* name = nullptr;
    if (!r.take(nameLength, name)) {
        return ctx.fail("truncated node name");
    }
    node.name.assign(reinterpret_cast<const char*>(name), nameLength);
    if (propBytes > endOffset - r.pos() || propCount > propBytes) {
        return ctx.fail("property list is out of bounds");
    }
    const std::size_t propsEnd = r.pos() + static_cast<std::size_t>(propBytes);
    // Grown one at a time: a header that claims a million properties costs only what is really there.
    for (std::uint64_t i = 0; i < propCount; ++i) {
        if (++ctx.properties > kMaxProperties) {
            return ctx.fail("too many properties");
        }
        Property prop;
        if (!readProperty(r, prop, ctx)) {
            return false;
        }
        node.props.push_back(std::move(prop));
    }
    if (r.pos() != propsEnd) {
        return ctx.fail("property list length does not match its contents");
    }
    while (r.pos() < endOffset) {
        if (endOffset - r.pos() < ctx.recordHeader()) {
            return ctx.fail("node children overrun the node");
        }
        Node child;
        bool childNull = false;
        if (!readNode(r, depth + 1, child, childNull, ctx)) {
            return false;
        }
        if (childNull) {
            break;
        }
        node.children.push_back(std::move(child));
    }
    if (r.pos() > endOffset) {
        return ctx.fail("node children overrun the node");
    }
    r.seek(static_cast<std::size_t>(endOffset));
    return true;
}

}

const Node* Node::child(std::string_view childName) const
{
    for (const Node& c : children) {
        if (c.name == childName) {
            return &c;
        }
    }
    return nullptr;
}

const Node* Document::root(std::string_view rootName) const
{
    for (const Node& n : roots) {
        if (n.name == rootName) {
            return &n;
        }
    }
    return nullptr;
}

bool parse(const std::vector<std::uint8_t>& bytes, Document& out, std::string& error)
{
    out = Document{};
    static constexpr char kMagic[] = "Kaydara FBX Binary  ";
    if (bytes.size() < kHeaderSize || std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) != 0) {
        error = "not a binary FBX file";
        return false;
    }
    Context ctx;
    ctx.version = load<std::uint32_t>(bytes.data() + 23);
    if (ctx.version < 7100 || ctx.version > 7700) {
        error = std::format("FBX version {} is not supported", ctx.version);
        return false;
    }
    out.version = ctx.version;
    Reader reader(bytes.data(), bytes.size());
    reader.seek(kHeaderSize);
    while (reader.remaining() >= ctx.recordHeader()) {
        Node node;
        bool isNull = false;
        if (!readNode(reader, 0, node, isNull, ctx)) {
            error = ctx.error;
            out = Document{};
            return false;
        }
        if (isNull) {
            break;
        }
        out.roots.push_back(std::move(node));
    }
    return true;
}

}
