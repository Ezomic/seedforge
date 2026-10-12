#pragma once

#include "ichor/core/reflect.h"

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace ichor {

// Little-endian binary encoding of arithmetic values, enums, strings,
// vectors, any type that exposes `data()`/`size()` of trivially copyable
// elements of fixed length (glm vectors and quaternions), and reflected
// structs. The reader never trusts a length it has not checked.

class BinaryWriter {
public:
    template <class T>
    void write(const T& value)
    {
        if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>) {
            append(&value, sizeof(T));
        } else if constexpr (std::is_same_v<T, std::string>) {
            write(static_cast<std::uint32_t>(value.size()));
            append(value.data(), value.size());
        } else if constexpr (isReflected<T>) {
            forEachField(value, [&](std::string_view, const auto& member) { write(member); });
        } else if constexpr (IsVector<T>::value) {
            write(static_cast<std::uint32_t>(value.size()));
            for (const auto& element : value) {
                write(element);
            }
        } else {
            static_assert(std::is_trivially_copyable_v<T>, "type is not serialisable; reflect it");
            append(&value, sizeof(T));
        }
    }

    const std::vector<std::uint8_t>& bytes() const { return bytes_; }
    std::vector<std::uint8_t> take() { return std::move(bytes_); }

private:
    template <class T>
    struct IsVector : std::false_type {};
    template <class T, class A>
    struct IsVector<std::vector<T, A>> : std::true_type {};

    void append(const void* data, std::size_t size)
    {
        const auto* p = static_cast<const std::uint8_t*>(data);
        bytes_.insert(bytes_.end(), p, p + size);
    }

    std::vector<std::uint8_t> bytes_;
};

class BinaryReader {
public:
    explicit BinaryReader(std::span<const std::uint8_t> bytes)
        : bytes_(bytes)
    {
    }

    template <class T>
    bool read(T& value)
    {
        if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>) {
            return take(&value, sizeof(T));
        } else if constexpr (std::is_same_v<T, std::string>) {
            std::uint32_t size = 0;
            if (!read(size) || size > remaining()) return false;
            value.assign(reinterpret_cast<const char*>(bytes_.data() + offset_), size);
            offset_ += size;
            return true;
        } else if constexpr (isReflected<T>) {
            bool ok = true;
            forEachField(value, [&](std::string_view, auto& member) { ok = ok && read(member); });
            return ok;
        } else if constexpr (IsVector<T>::value) {
            std::uint32_t size = 0;
            if (!read(size) || size > remaining()) return false;
            value.resize(size);
            for (auto& element : value) {
                if (!read(element)) return false;
            }
            return true;
        } else {
            static_assert(std::is_trivially_copyable_v<T>, "type is not deserialisable; reflect it");
            return take(&value, sizeof(T));
        }
    }

    std::size_t remaining() const { return bytes_.size() - offset_; }
    bool atEnd() const { return offset_ == bytes_.size(); }

private:
    template <class T>
    struct IsVector : std::false_type {};
    template <class T, class A>
    struct IsVector<std::vector<T, A>> : std::true_type {};

    bool take(void* out, std::size_t size)
    {
        if (size > remaining()) return false;
        std::memcpy(out, bytes_.data() + offset_, size);
        offset_ += size;
        return true;
    }

    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0;
};

}
