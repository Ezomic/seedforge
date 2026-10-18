#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ichor::fbx {

// A binary FBX file read into its node tree and nothing more. Version 7100 to
// 7700, 32 and 64 bit node records, zlib compressed arrays. ASCII FBX is not
// read: the art pipeline exports binary.

struct Property {
    char type = 0; // the FBX type code: Y C I F D L scalars, S R strings, f d l i b arrays
    std::int64_t integer = 0; // Y C I L
    double real = 0.0; // F D
    std::string text; // S R
    std::vector<double> reals; // f d
    std::vector<std::int64_t> ints; // l i b
};

struct Node {
    std::string name;
    std::vector<Property> props;
    std::vector<Node> children;

    const Node* child(std::string_view childName) const;
};

struct Document {
    std::uint32_t version = 0;
    std::vector<Node> roots;

    const Node* root(std::string_view rootName) const;
};

// Hostile bytes are rejected, never trusted: node, depth and array sizes are
// bounded, and every length is checked against the bytes that remain.
bool parse(const std::vector<std::uint8_t>& bytes, Document& out, std::string& error);

}
