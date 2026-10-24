#include "ichor/ui/dialogue_screen.h"
#include "ichor/ui/recording_renderer.h"
#include "ichor/ui/ui_context.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

using namespace ichor;

namespace {

void frame(UiContext& ui, RecordingUiRenderer& renderer, double time)
{
    renderer.clearDraws();
    ui.update(time);
    ui.render();
}

struct Fixture {
    RecordingUiRenderer renderer;
    UiContext ui{renderer, 1280, 720};

    Fixture()
    {
        REQUIRE(ui.ok());
        const std::filesystem::path fonts = std::filesystem::path(ICHOR_ART_DIR) / "fonts";
        REQUIRE(ui.loadFontFace(fonts / "AlegreyaSans-Regular.ttf"));
        REQUIRE(ui.loadFontFace(fonts / "Cinzel.ttf"));
    }

    void click(const std::pair<float, float>& at)
    {
        ui.mouseMove(static_cast<int>(at.first), static_cast<int>(at.second));
        frame(ui, renderer, 0.25);
        ui.mouseButton(0, true);
        ui.mouseButton(0, false);
        frame(ui, renderer, 0.3);
    }
};

ParleyView talkView()
{
    ParleyView view;
    view.open = true;
    view.speaker = "Marta";
    view.role = "merchant";
    view.text = "A carrier of mine walked into the crypt and did not walk out.";
    view.purse = "30 c";
    view.choices = {{"accept", "I will bring it back"}, {"trade", "Trade"}, {"bye", "Farewell"}};
    return view;
}

ParleyView tradeView()
{
    ParleyView view = talkView();
    view.page = ParleyView::Page::Trade;
    view.text = "What are you buying?";
    view.buying = {{"buy:barley_loaf", "Barley Loaf", "12 left", "4 c", true}, {"buy:linen_bandage", "Linen Bandage", "6 left", "9 c", false}};
    view.selling = {{"sell:pitch_torch", "Pitch Torch", "you hold 2", "2 c", true}};
    view.choices = {{"back", "Back"}};
    return view;
}

}

TEST_CASE("the dialogue box draws nothing until shown, then the speaker, the line and the choices", "[dialogue_screen]")
{
    Fixture f;
    DialogueScreen screen(f.ui);
    REQUIRE(screen.ok());
    screen.bind(talkView());
    frame(f.ui, f.renderer, 0.0);
    REQUIRE(f.renderer.draws().empty());

    screen.setVisible(true);
    frame(f.ui, f.renderer, 0.1);
    REQUIRE(f.renderer.draws().size() >= 2);
    const std::string content = screen.contentRml();
    REQUIRE(content.find("Marta") != std::string::npos);
    REQUIRE(content.find("did not walk out") != std::string::npos);
    REQUIRE(content.find("I will bring it back") != std::string::npos);
    REQUIRE(content.find("30 c") != std::string::npos);

    const std::size_t before = f.renderer.draws().size();
    screen.bind(talkView());
    frame(f.ui, f.renderer, 0.2);
    REQUIRE(f.renderer.draws().size() == before);

    screen.setVisible(false);
    frame(f.ui, f.renderer, 0.3);
    REQUIRE(f.renderer.draws().empty());
}

TEST_CASE("the trade page lists wares and what can be sold, with a button only where the player can act", "[dialogue_screen]")
{
    Fixture f;
    DialogueScreen screen(f.ui);
    screen.setVisible(true);
    screen.bind(tradeView());
    frame(f.ui, f.renderer, 0.1);
    const std::string content = screen.contentRml();
    REQUIRE(content.find("FOR SALE") != std::string::npos);
    REQUIRE(content.find("Barley Loaf") != std::string::npos);
    REQUIRE(content.find("YOU CARRY") != std::string::npos);
    REQUIRE(content.find("Pitch Torch") != std::string::npos);
    REQUIRE(content.find("data-key=\"buy:barley_loaf\"") != std::string::npos);
    REQUIRE(content.find("data-key=\"buy:linen_bandage\"") == std::string::npos); // cannot afford: no button

    ParleyView empty = tradeView();
    empty.buying.clear();
    empty.selling.clear();
    screen.bind(empty);
    frame(f.ui, f.renderer, 0.2);
    REQUIRE(screen.contentRml().find("Nothing is left to sell") != std::string::npos);
    REQUIRE(screen.contentRml().find("carry nothing the merchant will buy") != std::string::npos);
}

TEST_CASE("clicking a choice, a Buy button or the close button gives its key, in order", "[dialogue_screen]")
{
    Fixture f;
    DialogueScreen screen(f.ui);
    screen.setVisible(true);
    screen.bind(talkView());
    frame(f.ui, f.renderer, 0.1);
    frame(f.ui, f.renderer, 0.2);
    REQUIRE(screen.takeChoices().empty());

    const auto trade = screen.buttonCentre("trade");
    REQUIRE(trade);
    f.click(*trade);
    REQUIRE(screen.takeChoices() == std::vector<std::string>{"trade"});

    screen.bind(tradeView());
    frame(f.ui, f.renderer, 0.4);
    frame(f.ui, f.renderer, 0.5);
    const auto buy = screen.buttonCentre("buy:barley_loaf");
    REQUIRE(buy);
    f.click(*buy);
    const auto sell = screen.buttonCentre("sell:pitch_torch");
    REQUIRE(sell);
    f.click(*sell);
    REQUIRE(screen.takeChoices() == std::vector<std::string>{"buy:barley_loaf", "sell:pitch_torch"});
    REQUIRE_FALSE(screen.buttonCentre("buy:linen_bandage"));

    const auto close = screen.buttonCentre("bye");
    REQUIRE(close);
    f.click(*close);
    REQUIRE(screen.takeChoices() == std::vector<std::string>{"bye"});
}

TEST_CASE("speech and names never act as markup, and long or rude text is cut", "[dialogue_screen]")
{
    Fixture f;
    DialogueScreen screen(f.ui);
    screen.setVisible(true);
    ParleyView view = talkView();
    view.speaker = "<div id=\"close\">x</div>";
    view.text = std::string(5000, 'a') + "<b>";
    view.choices = {{"a\"b", "<script>"}};
    for (int i = 0; i < 50; ++i) {
        view.buying.push_back({"buy:x", "Item\n\t<b>", "d", "1 c", true});
    }
    view.page = ParleyView::Page::Trade;
    screen.bind(view);
    frame(f.ui, f.renderer, 0.1);
    const std::string content = screen.contentRml();
    REQUIRE(content.find("<script>") == std::string::npos);
    REQUIRE(content.find("<div id=\"close\">") == std::string::npos);
    REQUIRE(content.size() < 20000);
    REQUIRE(content.find("and 36 more") != std::string::npos);
}
