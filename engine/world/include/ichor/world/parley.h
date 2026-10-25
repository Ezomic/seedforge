#pragma once

#include "ichor/core/reflect.h"
#include "ichor/data/data_set.h"
#include "ichor/economy/merchant.h"
#include "ichor/economy/wallet.h"
#include "ichor/npc/dialogue_select.h"
#include "ichor/sim/scene.h"
#include "ichor/world/carry.h"
#include "ichor/world/dungeon_content.h"
#include "ichor/world/placed_cast.h"
#include "ichor/world/placed_loot.h"
#include "ichor/world/placed_pois.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Talk, trade and one hand-written quest in the forged world (docs 04, 06, 10, 18). Everything here rides on
// the cast, the points of interest and the loot the bake already placed:
//
//  - Talk: F beside a settlement NPC (when no item lies within reach) opens a grey-box conversation. The NPC's
//    name comes from the data's name list by a hash of its id, and its greeting from the shipped dialogue lines
//    (data/dialogue), picked with the same selector the rest of the game uses, so a greeting is a pure function
//    of the NPC, the world seed and how often the player has spoken to them.
//  - Trade: every cast member of the merchant archetype buys and sells with the inventory. Prices, buyback and
//    stock are the economy module's (stockMerchant, buyFromMerchant, sellToMerchant) and the currency is coin
//    (doc 06: coin buys bread; the Law's hard currency is not involved, and there is no barter in this slice).
//    The wares are the provisions table (data/provisions), which the world never places; what the player sells is
//    priced by its tier. What the merchant has bought, and what has been bought from it, is kept as tallies so
//    its stock changes and survives a save.
//  - One quest, written by hand in data/parley: the merchant nearest the spawn asks the player to bring an item
//    back from the nearest dungeon that has loot lying at it. The giver, the dungeon and the item are chosen from
//    the world by a pure function (never by chance), so a seed always gives the same errand. Where the world can say
//    where an item ends up (ParleyWorld::siteOf), an item with a spot of its own in an unguarded room the player can walk to is preferred,
//    then one in a guarded or crowded room, and only then one left outdoors at the mouth of a dungeon that cannot be entered. A
//    world with no merchant or no loot at a dungeon has no errand, and binding().reason says why. Progress is Active
//    (accepted), Found (the item is in the pack) and Done (handed in, paid).
//
// All state the player has earned is one reflected component on the player, `Parley`, registered with the scene,
// so it is hashed with the scene and saved and loaded inside every slot with nothing else to keep in step. Every
// change is a GameCommand (parley.talk, accept, turnin, buy, sell): logged, applied at the start of a tick,
// deterministic. Which NPC the player is talking to and which page is open are the screen's business, not state.
//
// Deferred: prices do not follow the region's scarcity (the market is the economy module's neutral one); only the one quest.

constexpr const char* kParleyComponent = "world.parley";
constexpr double kTalkReachM = 3.0;  // F opens a conversation within this of an NPC
constexpr double kTradeReachM = 5.0; // and it stays open, and trades and hand-ins work, within this
constexpr std::uint64_t kStartingCoin = 30;
constexpr std::size_t kMaxTradeTallies = 256;
constexpr std::size_t kMaxQuestRecords = 16;
constexpr std::size_t kMaxTalkTallies = 1024;
constexpr std::uint32_t kMaxTradeCount = 100;
constexpr double kParleyNoticeSeconds = 5.0;

enum class QuestStage : std::uint8_t { Active = 1, Found = 2, Done = 3 };

struct TradeTally {
    std::string good;
    std::uint32_t bought = 0; // units the player has bought from merchants
    std::uint32_t sold = 0;   // units the player has sold to them
};

struct QuestProgress {
    std::string id;
    std::uint8_t stage = static_cast<std::uint8_t>(QuestStage::Active);
};

struct TalkTally {
    std::string npcId;
    std::uint32_t count = 0;
};

struct Parley {
    std::uint64_t coin = kStartingCoin;
    std::vector<TradeTally> trades;
    std::vector<QuestProgress> quests;
    std::vector<TalkTally> talks;
};

template <>
struct Reflect<TradeTally> {
    static constexpr auto fields()
    {
        return std::make_tuple(field("good", &TradeTally::good), field("bought", &TradeTally::bought), field("sold", &TradeTally::sold));
    }
};
template <>
struct Reflect<QuestProgress> {
    static constexpr auto fields() { return std::make_tuple(field("id", &QuestProgress::id), field("stage", &QuestProgress::stage)); }
};
template <>
struct Reflect<TalkTally> {
    static constexpr auto fields() { return std::make_tuple(field("npcId", &TalkTally::npcId), field("count", &TalkTally::count)); }
};
template <>
struct Reflect<Parley> {
    static constexpr auto fields()
    {
        return std::make_tuple(field("coin", &Parley::coin), field("trades", &Parley::trades), field("quests", &Parley::quests), field("talks", &Parley::talks));
    }
};

// The quest as written in data/parley (the record "quest.errand"); the {placeholders} are filled when it is shown.
struct HandQuestDef {
    std::string id;
    std::string title;
    std::string giverArchetype;
    std::string offer, reminder, found, thanks;
    std::string objSpeak, objFetch, objReturn, done;
    std::uint64_t rewardCoin = 0;
    std::string rewardItem;
    std::uint32_t rewardCount = 0;
};

// What the world gives the quest to bind to. The reference point picks the giver: the merchant nearest it.
struct ParleyWorld {
    std::shared_ptr<const PlacedCastSet> cast;
    std::shared_ptr<const PlacedPoiSet> pois;
    std::shared_ptr<const PlacedLootSet> loot;
    double referenceX = 0.0;
    double referenceZ = 0.0;
    std::uint64_t seed = 0;
    // Where a dungeon item ends up (dungeon_content.h itemSite); null when the world cannot say, then every item counts alike.
    std::function<ItemSite(const PlacedLoot&)> siteOf;
};

struct QuestBinding {
    bool bound = false;
    std::string reason; // when not bound: why there is no errand in this world
    std::string giverNpc;
    std::string giverName;
    std::string dungeonPoi;
    std::string dungeonKind; // the dungeon's archetype, "crypt"
    std::string itemId;
    std::string itemName;
    bool inside = false;     // the item lies in a room of the dungeon, not outdoors at its mouth
    std::string roomType;    // that room's mission role, "reward"; empty outdoors
    std::string where; // "about 640 m to the north-east", from the giver
};

struct QuestObjective {
    std::string text;
    bool done = false;
};

struct QuestLogEntry {
    std::string id;
    std::string title;
    QuestStage stage = QuestStage::Active;
    std::vector<QuestObjective> objectives; // in the order they are met
    std::string outcome;                    // the closing line once done, else empty
};

struct ParleyChoice {
    std::string key; // what choose() takes
    std::string label;
    bool operator==(const ParleyChoice&) const = default;
};

struct ParleyTradeRow {
    std::string key; // "buy:<id>" or "sell:<id>"
    std::string name;
    std::string detail; // stock or held, as text
    std::string price;
    bool enabled = true;
    bool operator==(const ParleyTradeRow&) const = default;
};

// What the conversation box shows. Pure data: the UI draws it and reads nothing back but choice keys.
struct ParleyView {
    enum class Page : std::uint8_t { Talk, Trade };
    bool open = false;
    Page page = Page::Talk;
    std::string speaker;
    std::string role;
    std::string text;
    std::vector<ParleyChoice> choices;
    std::vector<ParleyTradeRow> buying;  // the merchant's wares
    std::vector<ParleyTradeRow> selling; // what the player may sell
    std::string purse;                   // "30 coin"
    std::string notice;
    bool operator==(const ParleyView&) const = default;
};

class ParleySystem {
public:
    // Reads the tables parley, provisions, dialogue, npcs, notable_items, catalogue and currency. What cannot be loaded
    // is named in problems() and the feature it feeds is left out (no currency: nothing is bought; no quest record: no quest).
    // `carry` must outlive this and be installed on the same scene.
    ParleySystem(ParleyWorld world, const DataSet& data, CarrySystem& carry);
    ParleySystem(const ParleySystem&) = delete;
    ParleySystem& operator=(const ParleySystem&) = delete;

    // Registers the component, the command kinds and the "parley" system. Call before any snapshot is loaded.
    // The scene must not outlive this.
    void install(Scene& scene);

    const std::vector<std::string>& problems() const { return problems_; }
    const QuestBinding& binding() const { return binding_; }
    const HandQuestDef* quest() const { return quest_ ? &*quest_ : nullptr; }

    const Parley* state(const Scene& scene) const;
    std::uint64_t coin(const Scene& scene) const;
    std::optional<QuestStage> stageOf(const Scene& scene) const;

    // The display name of a cast member: a hash of its id picks from the name list.
    std::string nameOf(std::string_view npcId) const;
    // The NPC F would talk to from here, null when none stands within kTalkReachM.
    const NpcSpawn* npcInReach(const Scene& scene, double reachM = kTalkReachM) const;
    // "F: talk to Marta" when someone is in reach, else empty.
    std::string prompt(const Scene& scene) const;

    bool open() const { return !speaking_.empty(); }
    // Opens the conversation with a cast member now, as F would (the player is not moved). False for an unknown id.
    bool begin(Scene& scene, const std::string& npcId);
    void close() { speaking_.clear(); }
    // The UI's request. "accept", "turnin", "trade", "back", "bye", "buy:<id>", "sell:<id>"; anything else is ignored.
    void choose(Scene& scene, const std::string& key);

    ParleyView view(const Scene& scene) const;
    // The accepted quests with their objectives, for the journal. Nothing before the quest is accepted.
    std::vector<QuestLogEntry> questLog(const Scene& scene) const;

    // The merchant's offers given what has been traded, by good id; for tests and the view.
    std::vector<MerchantOffer> offers(const Scene& scene) const;

private:
    struct Good {
        GoodDef def;
        bool ware = false; // sold by merchants from the start
    };

    void step(Scene& scene, const InputFrame& input);
    void talk(Scene& scene, const GameCommand& command);
    void accept(Scene& scene, const GameCommand& command);
    void turnIn(Scene& scene, const GameCommand& command);
    void buy(Scene& scene, const GameCommand& command);
    void sell(Scene& scene, const GameCommand& command);

    void loadGoods(const DataSet& data);
    void loadQuest(const DataSet& data);
    void loadDialogue(const DataSet& data);
    void bindQuest(const ParleyWorld& world);
    void tidy(Parley& parley) const;
    void syncQuest(Scene& scene, Parley& parley) const;
    void say(const Scene& scene, std::string text);
    std::string noticeText(const Scene& scene) const;

    const NpcSpawn* find(const std::string& npcId) const;
    const NpcSpawn* merchantInReach(const Scene& scene) const;
    bool isMerchant(const NpcSpawn& npc) const;
    std::vector<MerchantOffer> offersFor(const Parley& parley, std::string_view extra) const;
    std::string greeting(const Scene& scene, const NpcSpawn& npc) const;
    std::string fill(const std::string& text, const std::string& reward = {}) const;
    std::string priceText(std::uint64_t price) const;
    std::uint32_t talkCount(const Parley* parley, const std::string& npcId) const;
    const QuestProgress* progress(const Parley* parley) const;

    ParleyWorld world_;
    CarrySystem& carry_;
    std::vector<std::string> problems_;
    std::vector<std::string> names_;
    std::vector<std::string> traits_;
    std::map<std::string, Good, std::less<>> goods_;
    std::vector<std::string> wares_;
    std::string merchantArchetype_ = "merchant";
    std::string tradePrompt_;
    std::string farewell_;
    std::optional<CurrencyTable> currency_;
    std::optional<DialoguePool> dialogue_;
    std::optional<HandQuestDef> quest_;
    QuestBinding binding_;

    bool interactHeld_ = false;
    std::string speaking_;
    ParleyView::Page page_ = ParleyView::Page::Talk;
    std::string notice_;
    double noticeAt_ = -1.0e9;
};

}
