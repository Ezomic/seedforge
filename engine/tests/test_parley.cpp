#include "ichor/core/input.h"
#include "ichor/data/data_set.h"
#include "ichor/save/save_slots.h"
#include "ichor/sim/scene.h"
#include "ichor/sim/scene_io.h"
#include "ichor/world/forge_job.h"
#include "ichor/world/forged_game.h"
#include "ichor/world/parley.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <thread>

using namespace ichor;

namespace {

constexpr double kDt = 1.0 / 60.0;
constexpr int kGrid = 128;

WorldDesc descFor(std::uint64_t seed)
{
    WorldDesc desc;
    desc.seed = seed;
    return desc;
}

struct World {
    DataSet data;
    ForgedContentSetup setup;
    std::unique_ptr<ForgedGame> game;
    std::uint64_t seed = 0;
};

std::unique_ptr<ForgedGame> forge(const DataSet& data, ForgedContentSetup& setup, std::uint64_t seed)
{
    setup = loadForgedContentSetup(data, ICHOR_GAME_DATA_DIR);
    ForgeJob job(descFor(seed), kGrid, {}, setup.config);
    REQUIRE(job.start());
    for (int i = 0; i < 12000 && job.state() == ForgeJobState::Running; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(job.state() == ForgeJobState::Done);
    auto game = buildForgedGame(*job.result(), descFor(seed), setup.inputs);
    REQUIRE(game->ok());
    return game;
}

ParleyWorld worldOf(const ForgedGame& game)
{
    return {game.cast, game.pois, game.loot, game.spawn.x, game.spawn.z, 77};
}

// The first small seed whose world has a merchant and a dungeon with loot lying at it, found by search: no seed is pinned.
const World& shared()
{
    static const World world = [] {
        World out;
        REQUIRE(out.data.loadDirectory(ICHOR_GAME_DATA_DIR));
        for (std::uint64_t seed = 1; seed <= 40; ++seed) {
            auto game = forge(out.data, out.setup, seed);
            if (!game->cast || !game->loot || !game->pois) continue;
            std::vector<std::string> problems;
            CarrySystem carry(game->loot, CarryCatalogue::fromData(out.data, problems));
            ParleySystem parley(worldOf(*game), out.data, carry);
            if (parley.binding().bound) {
                out.game = std::move(game);
                out.seed = seed;
                return out;
            }
        }
        FAIL("no seed up to 40 forged a world with a merchant and a dungeon holding loot");
        return out;
    }();
    return world;
}

CarryCatalogue catalogue()
{
    std::vector<std::string> problems;
    return CarryCatalogue::fromData(shared().data, problems);
}

std::filesystem::path scratch(const char* name)
{
    const std::filesystem::path dir = std::filesystem::path(ICHOR_TEST_OUTPUT) / "parley" / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

struct Rig {
    Scene scene{7};
    CarrySystem carry;
    ParleySystem parley;

    explicit Rig(const ParleyWorld& world = worldOf(*shared().game))
        : carry(shared().game->loot, catalogue())
        , parley(world, shared().data, carry)
    {
        carry.install(scene);
        parley.install(scene);
        SpawnCommand player;
        player.id = StableId::of("player");
        player.name.value = "player";
        player.player = Player{};
        scene.enqueue(player);
        tick();
        if (parley.binding().bound) toGiver();
    }

    void tick(std::uint32_t buttons = 0)
    {
        InputFrame frame;
        frame.buttons = buttons;
        scene.tick(kDt, frame);
    }

    void settle()
    {
        tick();
        tick();
        tick();
    }

    void press(std::uint32_t button = InputFrame::Interact)
    {
        tick(button);
        settle();
    }

    void teleport(double x, double z, float y)
    {
        Transform transform;
        transform.position = {x, static_cast<double>(y), z};
        scene.enqueue(SetTransformCommand{StableId::of("player"), transform});
        settle();
    }

    const NpcSpawn& giver() const { return *shared().game->cast->find(parley.binding().giverNpc); }
    const PlacedLoot& item() const { return *shared().game->loot->find(parley.binding().itemId); }

    void toGiver() { teleport(giver().x + 1.0, giver().z, giver().groundY); }
    void toItem() { teleport(item().x, item().z, item().groundY); }

    const Parley& state() const
    {
        const Parley* held = parley.state(scene);
        REQUIRE(held != nullptr);
        return *held;
    }

    std::uint32_t count(const std::string& id) const { return carry.countOf(scene, id); }

    void choose(const std::string& key)
    {
        parley.choose(scene, key);
        settle();
    }

    // Opens the conversation as F would and walks the whole quest up to the point asked for.
    void accept()
    {
        press();
        REQUIRE(parley.open());
        choose("accept");
    }
};

const ItemDef& itemOfClass(const char* itemClass, const char* tier)
{
    static std::map<std::string, ItemDef> found;
    const std::string key = std::string(itemClass) + "/" + tier;
    if (!found.contains(key)) {
        const CarryCatalogue c = catalogue();
        for (const char* tableName : {"notable_items", "catalogue"}) {
            for (const auto& [id, record] : shared().data.table(tableName)->records) {
                const auto text = [&](const char* field) {
                    const auto it = record.fields.find(field);
                    return it != record.fields.end() && std::holds_alternative<std::string>(it->second) ? std::get<std::string>(it->second) : std::string{};
                };
                const ItemDef* def = c.items().find(id);
                if (def != nullptr && text("class") == itemClass && text("tier") == tier && def->weightUnits <= 3000) {
                    found[key] = *def;
                    break;
                }
            }
            if (found.contains(key)) break;
        }
    }
    REQUIRE(found.contains(key));
    return found[key];
}

}

TEST_CASE("the errand binds to the world by search, not chance: a merchant, the nearest dungeon with loot, and an item that lies there", "[parley]")
{
    const World& world = shared();
    Rig rig;
    const QuestBinding& binding = rig.parley.binding();
    REQUIRE(binding.bound);
    REQUIRE(rig.giver().archetype == "merchant");
    const PlacedPoi* dungeon = world.game->pois->find(binding.dungeonPoi);
    REQUIRE(dungeon != nullptr);
    REQUIRE(dungeon->kind == PlacedPoiKind::Dungeon);
    REQUIRE(rig.item().poiId == binding.dungeonPoi);
    REQUIRE(binding.where.starts_with("about "));
    REQUIRE_FALSE(binding.itemName.empty());

    // Every dungeon item is at least as far from the giver as the one chosen.
    const auto distance = [&](const PlacedLoot& loot) { return std::hypot(loot.x - rig.giver().x, loot.z - rig.giver().z); };
    for (const PlacedLoot& loot : world.game->loot->all()) {
        const PlacedPoi* poi = world.game->pois->find(loot.poiId);
        if (poi != nullptr && poi->kind == PlacedPoiKind::Dungeon) {
            REQUIRE(distance(loot) >= distance(rig.item()));
        }
    }

    Rig again;
    REQUIRE(again.parley.binding().itemId == binding.itemId);
    REQUIRE(again.parley.binding().giverNpc == binding.giverNpc);
    REQUIRE(again.parley.binding().where == binding.where);
}

TEST_CASE("a world with no merchant, or no dungeon loot, has no errand and nobody to hand it out", "[parley]")
{
    const ForgedGame& game = *shared().game;
    ParleyWorld nobody = worldOf(game);
    nobody.cast = std::make_shared<const PlacedCastSet>();
    Rig noCast(nobody);
    REQUIRE_FALSE(noCast.parley.binding().bound);

    ParleyWorld noLoot = worldOf(game);
    noLoot.loot = std::make_shared<const PlacedLootSet>();
    ParleySystem parley(noLoot, shared().data, noCast.carry);
    REQUIRE_FALSE(parley.binding().bound);
    REQUIRE(parley.questLog(noCast.scene).empty());

    ParleyWorld nothing;
    ParleySystem empty(nothing, shared().data, noCast.carry);
    REQUIRE_FALSE(empty.binding().bound);
    REQUIRE(empty.npcInReach(noCast.scene) == nullptr);
    REQUIRE(empty.prompt(noCast.scene).empty());
}

TEST_CASE("the shipped parley data loads without a problem", "[parley]")
{
    Rig rig;
    for (const std::string& problem : rig.parley.problems()) {
        INFO(problem);
        FAIL_CHECK("a problem loading the data");
    }
    REQUIRE(rig.parley.quest() != nullptr);
    REQUIRE(rig.parley.offers(rig.scene).size() == 4);
}

TEST_CASE("F beside an NPC opens a conversation with their name and a greeting, and walking away closes it", "[parley]")
{
    Rig rig;
    REQUIRE(rig.parley.prompt(rig.scene) == "F: talk to " + rig.parley.nameOf(rig.giver().npcId));
    REQUIRE_FALSE(rig.parley.open());
    rig.press();
    REQUIRE(rig.parley.open());
    const ParleyView view = rig.parley.view(rig.scene);
    REQUIRE(view.open);
    REQUIRE(view.speaker == rig.parley.nameOf(rig.giver().npcId));
    REQUIRE(view.role == "merchant");
    REQUIRE_FALSE(view.text.empty());
    REQUIRE(rig.state().talks.size() == 1);
    REQUIRE(rig.state().talks.front().count == 1);

    rig.teleport(rig.giver().x + 60.0, rig.giver().z + 60.0, rig.giver().groundY);
    REQUIRE_FALSE(rig.parley.open());
    REQUIRE_FALSE(rig.parley.view(rig.scene).open);
    REQUIRE(rig.parley.prompt(rig.scene).empty());

    rig.press();
    REQUIRE_FALSE(rig.parley.open());
}

TEST_CASE("an NPC that is not a merchant only talks: no trade, no errand", "[parley]")
{
    const ForgedGame& game = *shared().game;
    const NpcSpawn* other = nullptr;
    for (const NpcSpawn& npc : game.cast->all()) {
        if (npc.archetype != "merchant") {
            other = &npc;
            break;
        }
    }
    REQUIRE(other != nullptr);
    Rig rig;
    rig.teleport(other->x + 1.0, other->z, other->groundY);
    rig.press();
    REQUIRE(rig.parley.open());
    const ParleyView view = rig.parley.view(rig.scene);
    REQUIRE(view.role == other->archetype);
    for (const ParleyChoice& choice : view.choices) {
        REQUIRE(choice.key != "trade");
        REQUIRE(choice.key != "accept");
    }
    rig.choose("trade");
    REQUIRE(rig.parley.view(rig.scene).page == ParleyView::Page::Talk);
    rig.choose("buy:barley_loaf");
    REQUIRE(rig.count("barley_loaf") == 0);
    REQUIRE(rig.state().coin == kStartingCoin);
}

TEST_CASE("the same NPC greets the same way in two runs of the same world", "[parley]")
{
    Rig a;
    Rig b;
    a.press();
    b.press();
    REQUIRE(a.parley.view(a.scene).text == b.parley.view(b.scene).text);
}

TEST_CASE("a merchant sells from stock at the economy's price, takes coin, and the stock shrinks", "[parley][trade]")
{
    Rig rig;
    rig.press();
    rig.choose("trade");
    const ParleyView page = rig.parley.view(rig.scene);
    REQUIRE(page.page == ParleyView::Page::Trade);
    REQUIRE(page.buying.size() == 4);
    REQUIRE(page.purse == "30 c");

    const std::vector<MerchantOffer> before = rig.parley.offers(rig.scene);
    const auto offerOf = [&](const std::vector<MerchantOffer>& offers, const char* good) { return *std::ranges::find(offers, good, &MerchantOffer::good); };
    REQUIRE(offerOf(before, "lamp_oil_flask").price == 6);
    REQUIRE(offerOf(before, "lamp_oil_flask").stock == 8);

    rig.choose("buy:lamp_oil_flask");
    REQUIRE(rig.state().coin == kStartingCoin - 6);
    REQUIRE(rig.count("lamp_oil_flask") == 1);
    REQUIRE(offerOf(rig.parley.offers(rig.scene), "lamp_oil_flask").stock == 7);
    REQUIRE(rig.parley.view(rig.scene).notice.starts_with("Bought"));
    REQUIRE(rig.parley.view(rig.scene).purse == "24 c");
}

TEST_CASE("a purchase the player cannot afford, cannot carry, or that is out of stock changes nothing", "[parley][trade]")
{
    Rig rig;
    rig.press();
    for (int i = 0; i < 3; ++i) rig.choose("buy:linen_bandage"); // 27 coin
    REQUIRE(rig.state().coin == 3);
    rig.choose("buy:linen_bandage");
    REQUIRE(rig.state().coin == 3);
    REQUIRE(rig.count("linen_bandage") == 3);
    REQUIRE(rig.parley.view(rig.scene).notice == "You cannot afford Linen Bandage");

    // Stacks of consumables hold five: with coin to spare, the sixth is refused and not paid for.
    Rig rich;
    rich.press();
    rich.carry.requestGrant(rich.scene, "barley_loaf", 4);
    rich.settle();
    rich.choose("buy:barley_loaf");
    REQUIRE(rich.count("barley_loaf") == 5);
    const std::uint64_t coin = rich.state().coin;
    rich.choose("buy:barley_loaf");
    REQUIRE(rich.count("barley_loaf") == 5);
    REQUIRE(rich.state().coin == coin);

    rich.choose("buy:no_such_good");
    rich.choose("buy:" + rich.parley.binding().itemId);
    REQUIRE(rich.state().coin == coin);
}

TEST_CASE("a merchant out of reach does no business", "[parley][trade]")
{
    Rig rig;
    rig.press();
    rig.teleport(rig.giver().x + 40.0, rig.giver().z, rig.giver().groundY);
    rig.choose("buy:barley_loaf");
    REQUIRE(rig.count("barley_loaf") == 0);
    REQUIRE(rig.state().coin == kStartingCoin);
    rig.carry.requestGrant(rig.scene, "barley_loaf", 1);
    rig.settle();
    rig.choose("sell:barley_loaf");
    REQUIRE(rig.count("barley_loaf") == 1);
    REQUIRE(rig.state().coin == kStartingCoin);
}

TEST_CASE("selling pays the buyback share, tier prices stand in for goods with none, and what was sold can be bought back", "[parley][trade]")
{
    Rig rig;
    rig.press();
    rig.choose("buy:barley_loaf");
    REQUIRE(rig.state().coin == kStartingCoin - 4);
    rig.choose("sell:barley_loaf");
    REQUIRE(rig.state().coin == kStartingCoin - 4 + 2);
    REQUIRE(rig.count("barley_loaf") == 0);

    const ItemDef& gear = itemOfClass("gear", "notable");
    rig.carry.requestGrant(rig.scene, gear.id, 1);
    rig.settle();
    const std::uint64_t coin = rig.state().coin;
    rig.choose("sell:" + gear.id);
    REQUIRE(rig.state().coin == coin + 60); // a notable item is worth 120, and the merchant pays half
    REQUIRE(rig.count(gear.id) == 0);
    const std::vector<MerchantOffer> offers = rig.parley.offers(rig.scene);
    const auto resold = std::ranges::find(offers, gear.id, &MerchantOffer::good);
    REQUIRE(resold != offers.end());
    REQUIRE(resold->stock == 1);
    REQUIRE(resold->price == 120);

    rig.choose("buy:" + gear.id);
    REQUIRE(rig.count(gear.id) == 1);
    REQUIRE(rig.state().coin == coin + 60 - 120);
}

TEST_CASE("a merchant will not buy a key, and sells nothing it was never stocked with", "[parley][trade]")
{
    Rig rig;
    const ItemDef& key = itemOfClass("key", "notable");
    rig.carry.requestGrant(rig.scene, key.id, 1);
    rig.settle();
    rig.press();
    rig.choose("sell:" + key.id);
    REQUIRE(rig.count(key.id) == 1);
    REQUIRE(rig.state().coin == kStartingCoin);
    rig.choose("trade");
    const ParleyView page = rig.parley.view(rig.scene);
    for (const ParleyTradeRow& row : page.selling) {
        REQUIRE(row.key != "sell:" + key.id);
    }
}

TEST_CASE("the trade page lists what the player may sell with the buyback price", "[parley][trade]")
{
    Rig rig;
    rig.carry.requestGrant(rig.scene, "pitch_torch", 2);
    rig.settle();
    rig.press();
    rig.choose("trade");
    const ParleyView page = rig.parley.view(rig.scene);
    REQUIRE(page.selling.size() == 1);
    REQUIRE(page.selling.front().key == "sell:pitch_torch");
    REQUIRE(page.selling.front().price == "2 c"); // 5 coin, half of it, rounded down
    REQUIRE(page.selling.front().detail == "you hold 2");
    REQUIRE(page.choices.size() == 1);
    REQUIRE(page.choices.front().key == "back");
    rig.choose("back");
    REQUIRE(rig.parley.view(rig.scene).page == ParleyView::Page::Talk);
}

TEST_CASE("the errand: ask, fetch from the dungeon, return, be paid, and the journal tells each step", "[parley][quest]")
{
    Rig rig;
    const QuestBinding& binding = rig.parley.binding();
    REQUIRE_FALSE(rig.parley.stageOf(rig.scene).has_value());
    REQUIRE(rig.parley.questLog(rig.scene).empty());

    // The giver offers it first.
    rig.press();
    const ParleyView offer = rig.parley.view(rig.scene);
    REQUIRE(offer.choices.front().key == "accept");
    REQUIRE(offer.text.find(binding.itemName) != std::string::npos);
    REQUIRE(offer.text.find(binding.where) != std::string::npos);
    REQUIRE(offer.text.find('{') == std::string::npos);

    rig.choose("accept");
    REQUIRE(rig.parley.stageOf(rig.scene) == QuestStage::Active);
    std::vector<QuestLogEntry> log = rig.parley.questLog(rig.scene);
    REQUIRE(log.size() == 1);
    REQUIRE(log[0].objectives.size() == 3);
    REQUIRE(log[0].objectives[0].done);
    REQUIRE_FALSE(log[0].objectives[1].done);
    REQUIRE_FALSE(log[0].objectives[2].done);
    REQUIRE(log[0].objectives[1].text.find(binding.itemName) != std::string::npos);
    REQUIRE(log[0].outcome.empty());
    REQUIRE(rig.parley.view(rig.scene).choices.front().key != "accept");

    // Handing in without the item does nothing.
    const std::uint64_t coin = rig.state().coin;
    rig.choose("turnin");
    REQUIRE(rig.parley.stageOf(rig.scene) == QuestStage::Active);
    REQUIRE(rig.state().coin == coin);

    // At the dungeon: picking the item up is the second objective.
    rig.toItem();
    rig.press();
    REQUIRE(rig.count(binding.itemId) == 1);
    REQUIRE(rig.parley.stageOf(rig.scene) == QuestStage::Found);
    log = rig.parley.questLog(rig.scene);
    REQUIRE(log[0].objectives[1].done);
    REQUIRE_FALSE(log[0].objectives[2].done);

    // Back with it, the giver takes it and pays.
    rig.toGiver();
    rig.press();
    const ParleyView found = rig.parley.view(rig.scene);
    REQUIRE(found.choices.front().key == "turnin");
    REQUIRE(found.choices.front().label.find(binding.itemName) != std::string::npos);
    rig.choose("turnin");
    REQUIRE(rig.parley.stageOf(rig.scene) == QuestStage::Done);
    REQUIRE(rig.count(binding.itemId) == 0);
    REQUIRE(rig.state().coin == coin + rig.parley.quest()->rewardCoin);
    REQUIRE(rig.count(rig.parley.quest()->rewardItem) == rig.parley.quest()->rewardCount);
    log = rig.parley.questLog(rig.scene);
    REQUIRE(log[0].stage == QuestStage::Done);
    for (const QuestObjective& objective : log[0].objectives) {
        REQUIRE(objective.done);
    }
    REQUIRE_FALSE(log[0].outcome.empty());
    REQUIRE(log[0].outcome.find('{') == std::string::npos);

    // It pays once.
    rig.choose("turnin");
    rig.choose("accept");
    REQUIRE(rig.parley.stageOf(rig.scene) == QuestStage::Done);
    REQUIRE(rig.state().coin == coin + rig.parley.quest()->rewardCoin);
    REQUIRE(rig.state().quests.size() == 1);
}

TEST_CASE("the objectives follow the pack: putting the item down puts the second objective back", "[parley][quest]")
{
    Rig rig;
    rig.accept();
    rig.toItem();
    rig.press();
    REQUIRE(rig.parley.stageOf(rig.scene) == QuestStage::Found);
    rig.carry.requestDrop(rig.scene, rig.parley.binding().itemId);
    rig.settle();
    REQUIRE(rig.parley.stageOf(rig.scene) == QuestStage::Active);
    REQUIRE_FALSE(rig.parley.questLog(rig.scene)[0].objectives[1].done);
    rig.press(); // picks it up again
    REQUIRE(rig.parley.stageOf(rig.scene) == QuestStage::Found);
}

TEST_CASE("an item picked up before the errand was asked counts the moment it is asked", "[parley][quest]")
{
    Rig rig;
    rig.toItem();
    rig.press();
    REQUIRE(rig.count(rig.parley.binding().itemId) == 1);
    rig.toGiver();
    rig.accept();
    REQUIRE(rig.parley.stageOf(rig.scene) == QuestStage::Found);
}

TEST_CASE("the item of an open errand is not for sale to the merchant", "[parley][quest]")
{
    Rig rig;
    rig.accept();
    rig.toItem();
    rig.press();
    rig.toGiver();
    rig.press();
    rig.choose("sell:" + rig.parley.binding().itemId);
    REQUIRE(rig.count(rig.parley.binding().itemId) == 1);
    rig.choose("trade");
    for (const ParleyTradeRow& row : rig.parley.view(rig.scene).selling) {
        REQUIRE(row.key != "sell:" + rig.parley.binding().itemId);
    }
}

TEST_CASE("asking and handing in far from the giver does nothing", "[parley][quest]")
{
    Rig rig;
    rig.teleport(rig.giver().x + 50.0, rig.giver().z, rig.giver().groundY);
    rig.parley.choose(rig.scene, "accept");
    rig.settle();
    REQUIRE_FALSE(rig.parley.stageOf(rig.scene).has_value());
    rig.toGiver();
    rig.accept();
    rig.toItem();
    rig.press();
    rig.teleport(rig.giver().x + 50.0, rig.giver().z, rig.giver().groundY);
    rig.parley.choose(rig.scene, "turnin");
    rig.settle();
    REQUIRE(rig.parley.stageOf(rig.scene) == QuestStage::Found);
    REQUIRE(rig.count(rig.parley.binding().itemId) == 1);
}

TEST_CASE("a scripted playthrough gives the same state hash and journal twice", "[parley][quest][replay]")
{
    const auto play = [](Rig& rig) {
        rig.accept();
        rig.choose("trade");
        rig.choose("buy:barley_loaf");
        rig.choose("back");
        rig.toItem();
        rig.press();
        rig.toGiver();
        rig.press();
        rig.choose("turnin");
    };
    Rig a;
    Rig b;
    play(a);
    play(b);
    REQUIRE(a.scene.stateHash() == b.scene.stateHash());
    REQUIRE(a.parley.stageOf(a.scene) == QuestStage::Done);
    const auto lines = [](const Rig& rig) {
        std::vector<std::string> out;
        for (const QuestLogEntry& entry : rig.parley.questLog(rig.scene)) {
            out.push_back(entry.title);
            for (const QuestObjective& objective : entry.objectives) out.push_back((objective.done ? "x " : "o ") + objective.text);
            out.push_back(entry.outcome);
        }
        return out;
    };
    REQUIRE(lines(a) == lines(b));
}

TEST_CASE("the command log replayed into a fresh scene reproduces the whole errand", "[parley][quest][replay]")
{
    Rig a;
    a.accept();
    a.choose("buy:linen_bandage");
    a.toItem();
    a.press();
    a.toGiver();
    a.choose("turnin");
    a.tick();
    REQUIRE(a.parley.stageOf(a.scene) == QuestStage::Done);

    Scene replay(7);
    CarrySystem carry(shared().game->loot, catalogue());
    ParleySystem parley(worldOf(*shared().game), shared().data, carry);
    carry.install(replay);
    parley.install(replay);
    for (std::uint64_t tick = 0; tick < a.scene.tickCount(); ++tick) {
        for (const CommandLog::Entry& entry : a.scene.log().entries) {
            if (entry.tick == tick) replay.enqueue(entry.command);
        }
        replay.tick(kDt, {});
    }
    REQUIRE(replay.stateHash() == a.scene.stateHash());
    REQUIRE(parley.stageOf(replay) == QuestStage::Done);
    REQUIRE(parley.coin(replay) == a.state().coin);
}

TEST_CASE("a campaign slot saved in the middle of the errand loads with the same objectives, purse and stock", "[parley][quest][save]")
{
    Rig a;
    a.accept();
    a.choose("buy:lamp_oil_flask");
    a.toItem();
    a.press();
    REQUIRE(a.parley.stageOf(a.scene) == QuestStage::Found);
    const std::uint64_t hash = a.scene.stateHash();

    SaveSlots slots(scratch("slot"), SavePolicy{});
    const SaveContext context{7, 1, 42};
    REQUIRE(slots.save("quicksave", a.scene, context));

    Rig b;
    b.scene.tick(kDt, {});
    REQUIRE(slots.load("quicksave", b.scene, context).status == LoadStatus::Loaded);
    REQUIRE(b.scene.stateHash() == hash);
    REQUIRE(b.parley.stageOf(b.scene) == QuestStage::Found);
    REQUIRE(b.state().coin == a.state().coin);
    REQUIRE(b.state().trades.size() == 1);
    REQUIRE(b.parley.offers(b.scene).size() == a.parley.offers(a.scene).size());
    for (std::size_t i = 0; i < a.parley.offers(a.scene).size(); ++i) {
        REQUIRE(b.parley.offers(b.scene)[i].stock == a.parley.offers(a.scene)[i].stock);
    }
    const auto text = [](const Rig& rig) {
        std::string out;
        for (const QuestLogEntry& entry : rig.parley.questLog(rig.scene)) {
            for (const QuestObjective& objective : entry.objectives) out += (objective.done ? "x" : "o") + objective.text;
        }
        return out;
    };
    REQUIRE(text(a) == text(b));

    // The loaded game finishes the errand exactly as the saved one does.
    for (Rig* rig : {&a, &b}) {
        rig->toGiver();
        rig->press();
        rig->choose("turnin");
    }
    REQUIRE(a.parley.stageOf(a.scene) == QuestStage::Done);
    REQUIRE(b.parley.stageOf(b.scene) == QuestStage::Done);
    REQUIRE(a.scene.stateHash() == b.scene.stateHash());
}

TEST_CASE("a save made before the parley component existed still loads, and the player gains the starting purse", "[parley][save]")
{
    Scene old(7);
    SpawnCommand player;
    player.id = StableId::of("player");
    player.name.value = "player";
    player.player = Player{};
    old.enqueue(player);
    old.tick(kDt, {});
    const std::filesystem::path path = scratch("old") / "old.isav";
    REQUIRE(saveScene(path, old));

    Rig now;
    REQUIRE(loadScene(path, now.scene));
    REQUIRE(now.parley.state(now.scene) == nullptr);
    now.tick();
    REQUIRE(now.parley.state(now.scene) != nullptr);
    REQUIRE(now.state().coin == kStartingCoin);
    REQUIRE(now.state().quests.empty());
}

TEST_CASE("hostile parley state is shrugged off: bad stages, duplicates, a huge purse and empty ids", "[parley]")
{
    Rig rig;
    Parley bad;
    bad.coin = std::numeric_limits<std::uint64_t>::max();
    bad.quests = {{"", 2}, {"quest.errand", 9}, {"quest.errand", 0}, {"quest.errand", 2}, {"quest.errand", 3}, {"other", 1}};
    bad.trades = {{"", 1, 1}, {"barley_loaf", 4000000000u, 4000000000u}, {"no_such_good", 1, 1}};
    bad.talks = {{"", 3}, {"npc_x", 4000000000u}};
    for (auto [entity, player] : rig.scene.registry().view<Player>().each()) {
        rig.scene.registry().emplace_or_replace<Parley>(entity, bad);
    }
    rig.tick();
    const Parley& state = rig.state();
    REQUIRE(state.coin <= 999999999);
    REQUIRE(state.quests.size() == 2);
    REQUIRE(state.quests[0].id == "quest.errand");
    REQUIRE(state.quests[0].stage == 1); // no item in the pack: the stage follows the pack, not the file
    for (const TradeTally& tally : state.trades) REQUIRE_FALSE(tally.good.empty());
    for (const TalkTally& tally : state.talks) REQUIRE_FALSE(tally.npcId.empty());

    for (const MerchantOffer& offer : rig.parley.offers(rig.scene)) {
        REQUIRE(offer.stock <= kMaxGoodStock);
    }
    rig.press();
    rig.choose("buy:barley_loaf");
    rig.choose("sell:barley_loaf");
    rig.choose("");
    rig.choose("sell:");
    rig.choose("buy:");
    rig.choose("nonsense");
    rig.parley.view(rig.scene);
    REQUIRE(rig.state().coin <= 999999999);
}

TEST_CASE("the ParleyView of a closed conversation is empty and the quest log is empty before the errand", "[parley]")
{
    Rig rig;
    const ParleyView view = rig.parley.view(rig.scene);
    REQUIRE_FALSE(view.open);
    REQUIRE(view.choices.empty());
    REQUIRE(rig.parley.questLog(rig.scene).empty());
    REQUIRE_FALSE(rig.parley.begin(rig.scene, "no_such_npc"));
    REQUIRE(rig.parley.begin(rig.scene, rig.giver().npcId));
    REQUIRE(rig.parley.view(rig.scene).open);
    rig.parley.choose(rig.scene, "bye");
    REQUIRE_FALSE(rig.parley.view(rig.scene).open);
}
