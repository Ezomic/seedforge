#include "ichor/core/controls_overlay.h"
#include "ichor/core/feedback_note.h"
#include "ichor/core/session_summary.h"
#include "ichor/core/version.h"
#include "ichor/gfx/null_renderer.h"
#include "ichor/loop/engine.h"
#include "ichor/platform/null_platform.h"
#include "ichor/platform/typing_guard.h"
#include "ichor/ui/note_box.h"
#include "ichor/ui/recording_renderer.h"
#include "ichor/ui/ui_context.h"
#include "test_temp.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace ichor;
namespace fs = std::filesystem;

namespace {

struct Dir {
    fs::path path;
    explicit Dir(const char* name)
        : path(test::tempPath(name))
    {
        fs::remove_all(path);
    }
    ~Dir()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

std::string readAll(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

InputStream scriptedStream(int ticks)
{
    InputStream stream;
    for (int i = 0; i < ticks; ++i) {
        InputFrame frame;
        frame.moveX = static_cast<float>(i % 7) / 7.0f;
        frame.moveY = static_cast<float>(i % 3) / 3.0f;
        frame.buttons = i % 11 == 0 ? InputFrame::Attack : 0u;
        stream.frames.push_back(frame);
    }
    return stream;
}

std::size_t codepoints(const std::string& text)
{
    std::size_t count = 0;
    for (const char c : text) {
        count += (static_cast<unsigned char>(c) & 0xC0) != 0x80 ? 1 : 0;
    }
    return count;
}

struct Rig {
    NullPlatform platform;
    NullRenderer renderer;
    Engine engine{platform, renderer, EngineConfig{}};
    std::vector<InputFrame> seen;

    Rig()
    {
        engine.scene().addSystem("watch", [this](Scene&, double, const InputFrame& input) { seen.push_back(input); });
    }
};

}

TEST_CASE("F8 opens the note box once per press, and a second press while it is open changes nothing", "[feedback_note]")
{
    FeedbackPrompt prompt;
    CHECK_FALSE(prompt.open());
    CHECK(prompt.pressF8(true, true));
    CHECK(prompt.open());
    CHECK_FALSE(prompt.pressF8(true, true)); // held
    CHECK(prompt.feed("ab") == NoteOutcome::Typing);
    CHECK_FALSE(prompt.pressF8(false, true));
    CHECK_FALSE(prompt.pressF8(true, true)); // pressed again while open
    CHECK(prompt.open());
    CHECK(prompt.text() == "ab");
}

TEST_CASE("F8 is ignored where no box can be shown, such as a headless run", "[feedback_note]")
{
    FeedbackPrompt prompt;
    CHECK_FALSE(prompt.pressF8(true, false));
    CHECK_FALSE(prompt.open());
    // The press was seen, so a key still held when the box becomes available does not open it.
    CHECK_FALSE(prompt.pressF8(true, true));
    CHECK_FALSE(prompt.open());
}

TEST_CASE("Enter saves the line and Esc cancels it, and either closes the box", "[feedback_note]")
{
    FeedbackPrompt prompt;
    prompt.pressF8(true, true);
    CHECK(prompt.feed("the bridge\r is broken") == NoteOutcome::Save);
    CHECK(prompt.text() == "the bridge"); // what follows Enter in the same pump is not part of the note
    CHECK_FALSE(prompt.open());

    prompt.pressF8(false, true);
    prompt.pressF8(true, true);
    CHECK(prompt.text().empty()); // a new box starts empty
    prompt.feed("never mind");
    CHECK(prompt.feed("\x1b") == NoteOutcome::Cancel);
    CHECK_FALSE(prompt.open());
}

TEST_CASE("an empty note is still saved", "[feedback_note]")
{
    FeedbackPrompt prompt;
    prompt.pressF8(true, true);
    CHECK(prompt.feed("\r") == NoteOutcome::Save);
    CHECK(prompt.text().empty());
}

TEST_CASE("Backspace removes a whole character, multi-byte ones included, and does nothing on an empty line", "[feedback_note]")
{
    FeedbackPrompt prompt;
    prompt.pressF8(true, true);
    prompt.feed("\b");
    CHECK(prompt.text().empty());
    prompt.feed("caf\xC3\xA9 \xE6\x97\xA5\xF0\x9F\x99\x82"); // café 日🙂
    prompt.feed("\b");
    CHECK(prompt.text() == "caf\xC3\xA9 \xE6\x97\xA5");
    prompt.feed("\b\b\b");
    CHECK(prompt.text() == "caf");
}

TEST_CASE("a very long note stops at the limit without cutting a character, and control bytes are dropped", "[feedback_note]")
{
    FeedbackPrompt prompt;
    prompt.pressF8(true, true);
    for (std::size_t i = 0; i < kNoteMaxCharacters + 50; ++i) {
        prompt.feed(i % 2 == 0 ? "a" : "\xC3\xA9");
    }
    CHECK(codepoints(prompt.text()) == kNoteMaxCharacters);
    CHECK(prompt.text().size() == kNoteMaxCharacters / 2 * 3);

    FeedbackPrompt other;
    other.pressF8(true, true);
    other.feed("a\tb\nc\x01");
    CHECK(other.text() == "abc");
}

TEST_CASE("the input tail keeps the last ticks oldest first, and all of a run shorter than the tail", "[feedback_note]")
{
    const InputStream stream = scriptedStream(50);
    InputTail tail(20);
    for (int i = 0; i < 7; ++i) {
        tail.push(stream.frames[static_cast<std::size_t>(i)]);
    }
    CHECK(tail.stream().frames == std::vector<InputFrame>(stream.frames.begin(), stream.frames.begin() + 7));
    for (std::size_t i = 7; i < stream.frames.size(); ++i) {
        tail.push(stream.frames[i]);
    }
    CHECK(tail.stream().frames == std::vector<InputFrame>(stream.frames.end() - 20, stream.frames.end()));

    InputTail none;
    none.push(stream.frames[0]);
    CHECK(none.stream().frames.empty());
}

TEST_CASE("the engine's input tail is the end of the recording and leaves the simulation alone", "[feedback_note]")
{
    const InputStream stream = scriptedStream(300);
    const auto run = [&](std::size_t tailTicks) {
        auto rig = std::make_unique<Rig>();
        rig->engine.keepInputTail(tailTicks);
        rig->engine.recordInputs(true);
        rig->engine.replayInputs(stream);
        while (!rig->engine.replayFinished()) {
            REQUIRE(rig->engine.frame(NullPlatform::kTick));
        }
        return rig;
    };
    const auto plain = run(0);
    const auto tailed = run(120);
    CHECK(tailed->engine.scene().stateHash() == plain->engine.scene().stateHash());
    CHECK(tailed->seen == plain->seen);
    const std::vector<InputFrame>& recorded = tailed->engine.recordedInputs().frames;
    REQUIRE(recorded.size() == 300);
    CHECK(tailed->engine.inputTail().stream().frames == std::vector<InputFrame>(recorded.end() - 120, recorded.end()));
    CHECK(plain->engine.inputTail().stream().frames.empty());
}

TEST_CASE("a replay held still while the note box is open ends at the same tick with the same state", "[feedback_note]")
{
    // The box pauses play the way the menu does: frames with no time in them, so no tick runs and none is owed.
    const InputStream stream = scriptedStream(240);
    Rig straight;
    straight.engine.replayInputs(stream);
    while (!straight.engine.replayFinished()) {
        REQUIRE(straight.engine.frame(NullPlatform::kTick));
    }

    Rig noted;
    noted.engine.replayInputs(stream);
    int frame = 0;
    while (!noted.engine.replayFinished()) {
        const bool boxOpen = frame >= 60 && frame < 140;
        const std::uint64_t before = noted.engine.scene().tickCount();
        REQUIRE(noted.engine.frame(boxOpen ? 0.0 : NullPlatform::kTick));
        if (boxOpen) {
            CHECK(noted.engine.scene().tickCount() == before);
        }
        ++frame;
    }
    CHECK(noted.seen == straight.seen);
    CHECK(noted.engine.scene().tickCount() == straight.engine.scene().tickCount());
    CHECK(noted.engine.scene().stateHash() == straight.engine.scene().stateHash());
}

TEST_CASE("while the box has the keyboard, nothing typed reaches gameplay input", "[feedback_note]")
{
    TypingGuard guard;
    InputFrame typing;
    typing.moveX = 1.0f; // D
    typing.moveY = -1.0f; // S
    typing.lookX = 0.5f;
    typing.buttons = InputFrame::Attack | InputFrame::Journal | InputFrame::Map | InputFrame::Confirm | InputFrame::Menu | InputFrame::QuickLoad;
    CHECK(guard.filter(typing) == typing); // not typing: input passes
    guard.setTyping(true, typing);
    CHECK(guard.filter(typing) == InputFrame{});
}

TEST_CASE("a key still held when the box closes stays swallowed until it is let go", "[feedback_note]")
{
    // Enter that saved the note must not retry on the death screen, and Esc that cancelled it must not open the menu.
    TypingGuard guard;
    guard.setTyping(true, {});
    InputFrame closing;
    closing.buttons = InputFrame::Confirm | InputFrame::Menu;
    closing.moveY = 1.0f;
    guard.setTyping(false, closing);
    CHECK(guard.filter(closing) == InputFrame{});

    InputFrame stillEnter;
    stillEnter.buttons = InputFrame::Confirm | InputFrame::Attack; // J pressed fresh
    stillEnter.moveX = 1.0f;
    InputFrame expected;
    expected.buttons = InputFrame::Attack;
    expected.moveX = 1.0f;
    CHECK(guard.filter(stillEnter) == expected); // Menu and W were let go, Enter is still down

    CHECK(guard.filter(InputFrame{}) == InputFrame{});
    InputFrame again;
    again.buttons = InputFrame::Confirm | InputFrame::Menu;
    again.moveY = 1.0f;
    CHECK(guard.filter(again) == again); // pressed anew after the release
}

TEST_CASE("a note folder sits next to the session summary and two in the same second do not overwrite each other", "[feedback_note]")
{
    Dir dir("feedback_note_folders");
    const auto time = std::chrono::sys_days{std::chrono::year{2026} / 10 / 28} + std::chrono::hours{15} + std::chrono::minutes{4} + std::chrono::seconds{5};
    const fs::path first = createSessionFolder(dir.path, "note", time);
    const fs::path second = createSessionFolder(dir.path, "note", time + std::chrono::milliseconds{400});
    CHECK(first.filename() == "note-20261028-150405Z");
    CHECK(second.filename() == "note-20261028-150405Z-2");
    CHECK(fs::is_directory(first));
    CHECK(fs::is_directory(second));

    FeedbackNote note;
    note.time = time;
    note.seed = 22;
    note.tick = 900;
    note.text = "first";
    CHECK(writeFeedbackNote(first, note, scriptedStream(5)));
    note.text = "second";
    CHECK(writeFeedbackNote(second, note, scriptedStream(5)));
    CHECK(readAll(first / "note.txt").find("first") != std::string::npos);
    CHECK(readAll(second / "note.txt").find("second") != std::string::npos);
}

TEST_CASE("a saved note holds the text byte for byte, the version stamp, the run and the replay tail", "[feedback_note]")
{
    Dir dir("feedback_note_contents");
    const auto time = std::chrono::sys_days{std::chrono::year{2026} / 10 / 28} + std::chrono::hours{9};
    const fs::path folder = createSessionFolder(dir.path, "note", time);
    REQUIRE_FALSE(folder.empty());
    FeedbackNote note;
    note.time = time;
    note.seed = 22;
    note.tick = 30; // shorter than the tail
    note.text = "the troll \"froze\" at caf\xC3\xA9 \xE6\x97\xA5";
    const InputStream tail = scriptedStream(30);
    REQUIRE(writeFeedbackNote(folder, note, tail));

    const std::string text = readAll(folder / "note.txt");
    CHECK(text.find(note.text) != std::string::npos);
    CHECK(text.find(std::string(versionStamp())) != std::string::npos);
    CHECK(text.find("2026-10-28T09:00:00Z") != std::string::npos);
    CHECK(text.find("seed: 22") != std::string::npos);
    CHECK(text.find("tick: 30") != std::string::npos);
    CHECK(text.find("replay tail: 30 ticks, from tick 0") != std::string::npos);

    InputStream loaded;
    REQUIRE(loaded.load(folder / "replay-tail.iinp"));
    CHECK(loaded.frames == tail.frames);

    note.text.clear();
    const fs::path empty = createSessionFolder(dir.path, "note", time);
    REQUIRE(writeFeedbackNote(empty, note, {}));
    CHECK(readAll(empty / "note.txt").find("note: \n") != std::string::npos);
}

TEST_CASE("the controls card lists F8 for a feedback note", "[feedback_note]")
{
    ControlsOverlay open;
    open.open = true;
    bool found = false;
    for (const std::string& line : controlsOverlayLines(open)) {
        found = found || (line.rfind("F8", 0) == 0 && line.find("note") != std::string::npos);
    }
    CHECK(found);
}

TEST_CASE("the note box draws nothing until shown, then the typed line, escaped, ending in the newest text", "[feedback_note]")
{
    RecordingUiRenderer renderer;
    UiContext ui{renderer, 1280, 720};
    REQUIRE(ui.ok());
    REQUIRE(ui.loadFontFace(fs::path(ICHOR_ART_DIR) / "fonts" / "AlegreyaSans-Regular.ttf"));
    NoteBox box{ui};
    REQUIRE(box.ok());
    const auto draws = [&](double time) {
        renderer.clearDraws();
        ui.update(time);
        ui.render();
        return renderer.draws().size();
    };
    CHECK_FALSE(box.visible());
    CHECK(draws(0.0) == 0);

    box.bind("<b>&");
    CHECK(box.visible());
    CHECK(draws(0.1) > 0);
    CHECK(box.markup().find("<b>") == std::string::npos);
    CHECK(box.markup().find("Enter") != std::string::npos);

    std::string longLine(kNoteMaxCharacters - 4, 'x');
    longLine += "TAIL";
    box.bind(longLine);
    CHECK(box.markup().find("TAIL") != std::string::npos);
    CHECK(box.markup().find(longLine) == std::string::npos); // only the end of a long line fits the box

    box.bind(std::nullopt);
    CHECK_FALSE(box.visible());
    CHECK(draws(0.2) == 0);
}
