#include "ichor/script/journal_bindings.h"

#include "ichor/save/chronicle.h"
#include "ichor/sim/scene.h"

#include <stdexcept>
#include <string>

namespace ichor {

void bindJournal(ScriptHost& host, Chronicle& chronicle, const Scene& scene, const std::optional<JournalTemplates>& templates)
{
    Chronicle* story = &chronicle;
    const Scene* world = &scene;
    const std::optional<JournalTemplates>* prose = &templates;
    host.bind("journal", [story, world, prose](const std::vector<ScriptValue>& args) -> ScriptValue {
        if (args.size() > 4) {
            throw std::runtime_error("journal: expected at most who, what, where and decided");
        }
        JournalEntry entry;
        entry.tick = world->tickCount();
        std::string* fields[] = {&entry.who, &entry.what, &entry.where, &entry.decided};
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (std::holds_alternative<std::monostate>(args[i])) {
                continue;
            }
            if (!std::holds_alternative<std::string>(args[i])) {
                throw std::runtime_error("journal: who, what, where and decided must be strings");
            }
            *fields[i] = std::get<std::string>(args[i]);
        }
        if (!prose->has_value()) {
            throw std::runtime_error("journal: no journal templates are loaded (data/journal/prose.json)");
        }
        if (!story->add(entry.tick, kJournalCategory, composeJournal(entry, **prose))) {
            throw std::runtime_error("journal: the chronicle is full");
        }
        return std::monostate{};
    });
}

}
