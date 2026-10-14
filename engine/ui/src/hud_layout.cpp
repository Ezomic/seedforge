#include "ichor/ui/hud_layout.h"

#include "ichor/core/file_io.h"

#include <nlohmann/json.hpp>

#include <array>
#include <string>

namespace ichor {

namespace {

constexpr std::uintmax_t kMaxLayoutBytes = std::uintmax_t{1} << 20;

constexpr std::array<HudElement, 4> kElements{HudElement::Compass, HudElement::Clock, HudElement::Water, HudElement::Health};

struct Problems {
    std::vector<std::string> list;
    std::size_t dropped = 0;

    void add(std::string text)
    {
        if (list.size() < kMaxHudLayoutProblems) {
            list.push_back(std::move(text));
        } else {
            ++dropped;
        }
    }

    std::vector<std::string> finish()
    {
        if (dropped > 0) {
            list.push_back("and " + std::to_string(dropped) + " more");
        }
        return std::move(list);
    }
};

HudLayoutResult failed(std::vector<std::string> problems)
{
    HudLayoutResult result;
    result.layout = HudLayout::defaultLayout();
    result.problems = std::move(problems);
    return result;
}

bool elementFromId(const std::string& id, HudElement& element)
{
    for (const HudElement candidate : kElements) {
        if (id == hudElementId(candidate)) {
            element = candidate;
            return true;
        }
    }
    return false;
}

}

const char* hudElementId(HudElement element)
{
    switch (element) {
    case HudElement::Compass: return "compass";
    case HudElement::Clock: return "clock";
    case HudElement::Water: return "water";
    case HudElement::Health: return "health";
    }
    return "";
}

HudLayout HudLayout::defaultLayout()
{
    HudLayout layout;
    for (const HudElement element : kElements) {
        layout.slots.push_back({element, true});
    }
    return layout;
}

HudLayoutResult parseHudLayout(std::string_view json)
{
    const nlohmann::json root = nlohmann::json::parse(json.begin(), json.end(), nullptr, false);
    if (root.is_discarded()) {
        return failed({"not valid JSON"});
    }
    if (!root.is_object()) {
        return failed({"$: must be an object with an elements list"});
    }
    const auto found = root.find("elements");
    if (found == root.end()) {
        return failed({"elements: is required"});
    }
    if (!found->is_array()) {
        return failed({"elements: must be a list"});
    }

    Problems problems;
    HudLayout layout;
    std::array<bool, kElements.size()> seen{};
    for (std::size_t index = 0; index < found->size(); ++index) {
        const nlohmann::json& item = (*found)[index];
        const std::string path = "elements[" + std::to_string(index) + "]";
        if (!item.is_object()) {
            problems.add(path + ": must be an object");
            continue;
        }
        bool valid = true;
        HudElement element = HudElement::Compass;
        const auto id = item.find("id");
        if (id == item.end()) {
            problems.add(path + ".id: is required");
            valid = false;
        } else if (!id->is_string()) {
            problems.add(path + ".id: must be a string");
            valid = false;
        } else if (!elementFromId(id->get<std::string>(), element)) {
            problems.add(path + ".id: unknown element '" + id->get<std::string>() + "'");
            valid = false;
        } else if (seen[static_cast<std::size_t>(element)]) {
            problems.add(path + ".id: duplicate '" + id->get<std::string>() + "'");
            valid = false;
        } else {
            seen[static_cast<std::size_t>(element)] = true;
        }
        bool visible = true;
        const auto flag = item.find("visible");
        if (flag != item.end()) {
            if (flag->is_boolean()) {
                visible = flag->get<bool>();
            } else {
                problems.add(path + ".visible: must be true or false");
                valid = false;
            }
        }
        if (valid) {
            layout.slots.push_back({element, visible});
        }
    }
    if (problems.list.empty()) {
        HudLayoutResult result;
        result.layout = std::move(layout);
        return result;
    }
    return failed(problems.finish());
}

HudLayoutResult loadHudLayout(const std::filesystem::path& path)
{
    std::error_code code;
    if (!std::filesystem::exists(path, code)) {
        HudLayoutResult result;
        result.layout = HudLayout::defaultLayout();
        return result;
    }
    std::vector<std::uint8_t> bytes;
    if (!readFileBytes(path, bytes, kMaxLayoutBytes)) {
        return failed({path.generic_string() + ": cannot be read as a layout file"});
    }
    HudLayoutResult result = parseHudLayout(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    for (std::string& problem : result.problems) {
        problem = path.generic_string() + ": " + problem;
    }
    return result;
}

}
