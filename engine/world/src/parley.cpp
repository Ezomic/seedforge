#include "ichor/world/parley.h"

#include "ichor/core/hash.h"
#include "ichor/economy/merchant_service.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <set>

namespace ichor {

namespace {

constexpr const char* kTalk = "parley.talk";
constexpr const char* kAccept = "parley.accept";
constexpr const char* kTurnIn = "parley.turnin";
constexpr const char* kBuy = "parley.buy";
constexpr const char* kSell = "parley.sell";

constexpr std::uint64_t kCoinCap = 999'999'999;
constexpr std::int64_t kMaxStock = 1000;

std::string textOf(const DataRecord& record, const char* key)
{
    const auto it = record.fields.find(key);
    return it != record.fields.end() && std::holds_alternative<std::string>(it->second) ? std::get<std::string>(it->second) : std::string{};
}

double numberOf(const DataRecord& record, const char* key, double fallback = 0.0)
{
    const auto it = record.fields.find(key);
    if (it == record.fields.end() || !std::holds_alternative<double>(it->second)) {
        return fallback;
    }
    const double value = std::get<double>(it->second);
    return std::isfinite(value) ? value : fallback;
}

std::vector<std::string> listOf(const DataRecord& record, const char* key)
{
    const auto it = record.fields.find(key);
    return it != record.fields.end() && std::holds_alternative<std::vector<std::string>>(it->second) ? std::get<std::vector<std::string>>(it->second) : std::vector<std::string>{};
}

const DataRecord* recordOf(const DataSet& data, const char* table, const char* id)
{
    const DataTable* t = data.table(table);
    if (t == nullptr) {
        return nullptr;
    }
    const auto it = t->records.find(id);
    return it == t->records.end() ? nullptr : &it->second;
}

std::uint64_t clampUnsigned(double value, std::uint64_t low, std::uint64_t high)
{
    if (!(value >= static_cast<double>(low))) {
        return low;
    }
    if (value >= static_cast<double>(high)) {
        return high;
    }
    return static_cast<std::uint64_t>(value);
}

// "{key}" replaced from `values` in one pass, so a value is never expanded again; an unknown key stays as written.
std::string fillPlaceholders(const std::string& text, const std::map<std::string, std::string, std::less<>>& values)
{
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t close = text[i] == '{' ? text.find('}', i) : std::string::npos;
        if (close != std::string::npos) {
            const auto it = values.find(std::string_view(text).substr(i + 1, close - i - 1));
            if (it != values.end()) {
                out += it->second;
                i = close + 1;
                continue;
            }
        }
        out.push_back(text[i++]);
    }
    return out;
}

double squaredDistance(double ax, double az, double bx, double bz)
{
    return (ax - bx) * (ax - bx) + (az - bz) * (az - bz);
}

// Eight ways from the north (-z) without any trigonometry: the longer axis decides unless the shorter is over half of it.
std::string compass(double dx, double dz)
{
    const double ax = std::abs(dx);
    const double az = std::abs(dz);
    const std::string north = dz < 0.0 ? "north" : "south";
    const std::string east = dx >= 0.0 ? "east" : "west";
    if (az * 2.0 < ax) {
        return east;
    }
    if (ax * 2.0 < az) {
        return north;
    }
    return north + "-" + east;
}

Wallet walletOf(const CurrencyTable& table, std::uint64_t coin)
{
    Wallet wallet;
    (void)wallet.earn(table, CurrencySlot::Coin, coin, "loot");
    return wallet;
}

const Transform* transformOf(const Scene& scene, StableId actor)
{
    const entt::entity entity = scene.find(actor);
    return entity == entt::null ? nullptr : scene.registry().try_get<Transform>(entity);
}

}

ParleySystem::ParleySystem(ParleyWorld world, const DataSet& data, CarrySystem& carry)
    : world_(std::move(world))
    , carry_(carry)
{
    if (const DataRecord* names = recordOf(data, "parley", "names")) {
        names_ = listOf(*names, "list");
    } else {
        problems_.push_back("parley: there is no names record, so NPCs go by their ids");
    }
    if (const DataRecord* merchant = recordOf(data, "parley", "merchant")) {
        const std::string archetype = textOf(*merchant, "archetype");
        if (!archetype.empty()) {
            merchantArchetype_ = archetype;
        }
        tradePrompt_ = textOf(*merchant, "trade_prompt");
        farewell_ = textOf(*merchant, "farewell");
    } else {
        problems_.push_back("parley: there is no merchant record");
    }
    if (const DataTable* table = data.table("currency")) {
        std::string error;
        currency_ = CurrencyTable::fromTable(*table, error);
        if (!currency_) {
            problems_.push_back("parley: the currency table is unusable, so nothing can be bought: " + error);
        }
    } else {
        problems_.push_back("parley: there is no currency table, so nothing can be bought");
    }
    if (const DataTable* traits = data.table("npcs")) {
        for (const auto& [id, record] : traits->records) {
            traits_.push_back(id);
        }
    }
    loadGoods(data);
    loadQuest(data);
    loadDialogue(data);
    bindQuest(world_);
}

void ParleySystem::loadGoods(const DataSet& data)
{
    const DataRecord* prices = recordOf(data, "parley", "prices");
    if (prices == nullptr) {
        problems_.push_back("parley: there is no prices record, so items sell at a default price");
    }
    const auto tierPrice = [&](const std::string& tier) {
        const double fallback = prices != nullptr ? numberOf(*prices, "fallback", 10.0) : 10.0;
        return prices != nullptr ? numberOf(*prices, tier.c_str(), fallback) : fallback;
    };
    for (const char* tableName : {"notable_items", "catalogue", "provisions"}) {
        const DataTable* table = data.table(tableName);
        if (table == nullptr) {
            continue;
        }
        for (const auto& [id, record] : table->records) {
            if (goods_.contains(id)) {
                continue;
            }
            const double own = numberOf(record, "price");
            const double price = own > 0.0 ? own : tierPrice(textOf(record, "tier"));
            Good good;
            good.def.id = id;
            good.def.basePrice = clampUnsigned(price, 1, kMaxGoodPrice);
            good.def.baseStock = clampUnsigned(numberOf(record, "stock"), 0, static_cast<std::uint64_t>(kMaxStock));
            good.ware = good.def.baseStock > 0 && carry_.catalogue().items().find(id) != nullptr;
            goods_.emplace(id, good);
        }
    }
    for (const auto& [id, good] : goods_) {
        if (good.ware) {
            wares_.push_back(id);
        }
    }
    if (wares_.empty()) {
        problems_.push_back("parley: there are no wares, so merchants only buy");
    }
}

void ParleySystem::loadQuest(const DataSet& data)
{
    const DataRecord* record = recordOf(data, "parley", "quest.errand");
    if (record == nullptr) {
        problems_.push_back("parley: there is no quest.errand record, so there is no quest");
        return;
    }
    HandQuestDef def;
    def.id = record->id;
    def.title = textOf(*record, "title");
    def.giverArchetype = textOf(*record, "giver_archetype");
    def.offer = textOf(*record, "offer");
    def.reminder = textOf(*record, "reminder");
    def.found = textOf(*record, "found");
    def.thanks = textOf(*record, "thanks");
    def.objSpeak = textOf(*record, "obj_speak");
    def.objFetch = textOf(*record, "obj_fetch");
    def.objReturn = textOf(*record, "obj_return");
    def.done = textOf(*record, "done");
    def.rewardCoin = clampUnsigned(numberOf(*record, "reward_coin"), 0, kCoinCap);
    def.rewardItem = textOf(*record, "reward_item");
    def.rewardCount = static_cast<std::uint32_t>(clampUnsigned(numberOf(*record, "reward_count"), 0, 100));
    for (const std::string* required : {&def.title, &def.giverArchetype, &def.offer, &def.reminder, &def.found, &def.thanks, &def.objSpeak, &def.objFetch, &def.objReturn}) {
        if (required->empty()) {
            problems_.push_back("parley: the quest record is missing a text, so there is no quest");
            return;
        }
    }
    if (def.rewardCount > 0 && carry_.catalogue().items().find(def.rewardItem) == nullptr) {
        problems_.push_back("parley: the quest reward item " + def.rewardItem + " cannot be carried, so only coin is paid");
        def.rewardItem.clear();
        def.rewardCount = 0;
    }
    quest_ = std::move(def);
}

void ParleySystem::loadDialogue(const DataSet& data)
{
    const DataTable* table = data.table("dialogue");
    if (table == nullptr) {
        problems_.push_back("parley: there is no dialogue table, so greetings are a fixed line");
        return;
    }
    std::vector<DialogueLine> lines;
    for (const auto& [id, record] : table->records) {
        DialogueLine line;
        line.id = id;
        line.text = textOf(record, "text");
        line.weight = static_cast<std::int64_t>(clampUnsigned(numberOf(record, "weight", 1.0), 0, static_cast<std::uint64_t>(kMaxDialogueWeight)));
        line.cooldown = clampUnsigned(numberOf(record, "cooldown"), 0, 1'000'000);
        bool usable = !line.text.empty();
        for (const std::string& text : listOf(record, "conditions")) {
            DialogueCondition condition;
            std::string error;
            if (!parseCondition(text, condition, error)) {
                usable = false;
                break;
            }
            line.conditions.push_back(std::move(condition));
        }
        if (usable) {
            lines.push_back(std::move(line));
        }
    }
    std::string error;
    dialogue_ = DialoguePool::fromLines(std::move(lines), error);
    if (!dialogue_) {
        problems_.push_back("parley: the dialogue lines are unusable: " + error);
    }
}

void ParleySystem::bindQuest(const ParleyWorld& world)
{
    if (!quest_ || !world.cast || !world.pois || !world.loot) {
        return;
    }
    const NpcSpawn* giver = nullptr;
    double giverDistance = 0.0;
    for (const NpcSpawn& npc : world.cast->all()) {
        if (npc.archetype != quest_->giverArchetype) {
            continue;
        }
        const double d = squaredDistance(npc.x, npc.z, world.referenceX, world.referenceZ);
        if (giver == nullptr || d < giverDistance) {
            giver = &npc;
            giverDistance = d;
        }
    }
    if (giver == nullptr) {
        return;
    }
    const PlacedLoot* item = nullptr;
    const PlacedPoi* dungeon = nullptr;
    double itemDistance = 0.0;
    for (const PlacedLoot& loot : world.loot->all()) {
        const PlacedPoi* poi = world.pois->find(loot.poiId);
        if (poi == nullptr || poi->kind != PlacedPoiKind::Dungeon || carry_.catalogue().info(loot.itemId) == nullptr) {
            continue;
        }
        const double d = squaredDistance(loot.x, loot.z, giver->x, giver->z);
        if (item == nullptr || d < itemDistance) {
            item = &loot;
            dungeon = poi;
            itemDistance = d;
        }
    }
    if (item == nullptr) {
        return;
    }
    const double metres = std::sqrt(itemDistance);
    const std::uint64_t rounded = (static_cast<std::uint64_t>(std::min(metres, 1.0e9)) + 5) / 10 * 10;
    binding_.bound = true;
    binding_.giverNpc = giver->npcId;
    binding_.giverName = nameOf(giver->npcId);
    binding_.dungeonPoi = dungeon->id;
    binding_.dungeonKind = dungeon->archetype.empty() ? std::string("ruin") : dungeon->archetype;
    binding_.itemId = item->itemId;
    binding_.itemName = carry_.catalogue().info(item->itemId)->name;
    binding_.where = std::format("about {} m to the {}", rounded, compass(item->x - giver->x, item->z - giver->z));
}

void ParleySystem::install(Scene& scene)
{
    scene.registerComponent<Parley>(kParleyComponent);
    scene.registerGameCommand(kTalk, [this](Scene& s, const GameCommand& c) { talk(s, c); });
    scene.registerGameCommand(kAccept, [this](Scene& s, const GameCommand& c) { accept(s, c); });
    scene.registerGameCommand(kTurnIn, [this](Scene& s, const GameCommand& c) { turnIn(s, c); });
    scene.registerGameCommand(kBuy, [this](Scene& s, const GameCommand& c) { buy(s, c); });
    scene.registerGameCommand(kSell, [this](Scene& s, const GameCommand& c) { sell(s, c); });
    scene.addSystem("parley", [this](Scene& s, double, const InputFrame& input) { step(s, input); });
}

const Parley* ParleySystem::state(const Scene& scene) const
{
    for (auto [entity, player, parley] : scene.registry().view<const Player, const Parley>().each()) {
        return &parley;
    }
    return nullptr;
}

std::uint64_t ParleySystem::coin(const Scene& scene) const
{
    const Parley* parley = state(scene);
    return parley != nullptr ? parley->coin : kStartingCoin;
}

const QuestProgress* ParleySystem::progress(const Parley* parley) const
{
    if (parley == nullptr || !quest_) {
        return nullptr;
    }
    const auto it = std::ranges::find(parley->quests, quest_->id, &QuestProgress::id);
    return it == parley->quests.end() ? nullptr : &*it;
}

std::optional<QuestStage> ParleySystem::stageOf(const Scene& scene) const
{
    const QuestProgress* found = progress(state(scene));
    if (found == nullptr) {
        return std::nullopt;
    }
    return static_cast<QuestStage>(found->stage);
}

std::uint32_t ParleySystem::talkCount(const Parley* parley, const std::string& npcId) const
{
    if (parley == nullptr) {
        return 0;
    }
    const auto it = std::ranges::find(parley->talks, npcId, &TalkTally::npcId);
    return it == parley->talks.end() ? 0 : it->count;
}

std::string ParleySystem::nameOf(std::string_view npcId) const
{
    return names_.empty() ? std::string(npcId) : names_[hash64(npcId) % names_.size()];
}

const NpcSpawn* ParleySystem::find(const std::string& npcId) const
{
    if (!world_.cast) {
        return nullptr;
    }
    const auto& all = world_.cast->all();
    const auto it = std::ranges::lower_bound(all, npcId, std::less<>{}, &NpcSpawn::npcId);
    return it != all.end() && it->npcId == npcId ? &*it : nullptr;
}

const NpcSpawn* ParleySystem::npcInReach(const Scene& scene, double reachM) const
{
    if (!world_.cast) {
        return nullptr;
    }
    for (auto [entity, player, transform] : scene.registry().view<const Player, const Transform>().each()) {
        const NpcSpawn* best = nullptr;
        double bestSquared = reachM * reachM;
        for (const NpcSpawn& npc : world_.cast->all()) {
            const double d = squaredDistance(npc.x, npc.z, transform.position.x, transform.position.z);
            if (d <= bestSquared && (best == nullptr || d < bestSquared)) {
                best = &npc;
                bestSquared = d;
            }
        }
        return best;
    }
    return nullptr;
}

bool ParleySystem::isMerchant(const NpcSpawn& npc) const
{
    return npc.archetype == merchantArchetype_;
}

const NpcSpawn* ParleySystem::merchantInReach(const Scene& scene) const
{
    const NpcSpawn* npc = npcInReach(scene, kTradeReachM);
    return npc != nullptr && isMerchant(*npc) ? npc : nullptr;
}

bool ParleySystem::atGiver(const Scene& scene) const
{
    const NpcSpawn* npc = npcInReach(scene, kTradeReachM);
    return binding_.bound && npc != nullptr && npc->npcId == binding_.giverNpc;
}

std::string ParleySystem::prompt(const Scene& scene) const
{
    if (carry_.itemWithinReach(scene)) {
        return {};
    }
    const NpcSpawn* npc = npcInReach(scene);
    return npc == nullptr ? std::string() : std::format("F: talk to {}", nameOf(npc->npcId));
}

void ParleySystem::say(const Scene& scene, std::string text)
{
    notice_ = std::move(text);
    noticeAt_ = scene.time();
}

std::string ParleySystem::noticeText(const Scene& scene) const
{
    return scene.time() - noticeAt_ < kParleyNoticeSeconds ? notice_ : std::string();
}

void ParleySystem::tidy(Parley& parley) const
{
    parley.coin = std::min(parley.coin, kCoinCap);
    std::vector<QuestProgress> kept;
    for (QuestProgress& q : parley.quests) {
        const bool known = std::ranges::any_of(kept, [&](const QuestProgress& other) { return other.id == q.id; });
        if (!q.id.empty() && q.stage >= 1 && q.stage <= 3 && !known && kept.size() < kMaxQuestRecords) {
            kept.push_back(std::move(q));
        }
    }
    parley.quests = std::move(kept);
    std::erase_if(parley.trades, [](const TradeTally& t) { return t.good.empty(); });
    if (parley.trades.size() > kMaxTradeTallies) {
        parley.trades.resize(kMaxTradeTallies);
    }
    std::erase_if(parley.talks, [](const TalkTally& t) { return t.npcId.empty(); });
    if (parley.talks.size() > kMaxTalkTallies) {
        parley.talks.resize(kMaxTalkTallies);
    }
}

void ParleySystem::syncQuest(Scene& scene, Parley& parley) const
{
    if (!binding_.bound || !quest_) {
        return;
    }
    for (QuestProgress& q : parley.quests) {
        if (q.id != quest_->id || q.stage == static_cast<std::uint8_t>(QuestStage::Done)) {
            continue;
        }
        q.stage = static_cast<std::uint8_t>(carry_.countOf(scene, binding_.itemId) > 0 ? QuestStage::Found : QuestStage::Active);
    }
}

void ParleySystem::step(Scene& scene, const InputFrame& input)
{
    const bool interact = (input.buttons & InputFrame::Interact) != 0;
    const bool pressed = interact && !interactHeld_;
    interactHeld_ = interact;
    for (auto [entity, id, player, transform] : scene.registry().view<const StableId, const Player, const Transform>().each()) {
        Parley* held = scene.registry().try_get<Parley>(entity);
        if (held == nullptr) {
            held = &scene.registry().emplace<Parley>(entity);
        }
        tidy(*held);
        syncQuest(scene, *held);
        if (pressed && !carry_.itemWithinReach(scene)) {
            if (const NpcSpawn* npc = npcInReach(scene)) {
                speaking_ = npc->npcId;
                page_ = ParleyView::Page::Talk;
                scene.enqueue(GameCommand{id, kTalk, npc->npcId, 0});
            }
        }
        if (!speaking_.empty()) {
            const NpcSpawn* npc = find(speaking_);
            if (npc == nullptr || squaredDistance(npc->x, npc->z, transform.position.x, transform.position.z) > kTradeReachM * kTradeReachM) {
                speaking_.clear();
            }
        }
        break;
    }
}

bool ParleySystem::begin(Scene& scene, const std::string& npcId)
{
    const NpcSpawn* npc = find(npcId);
    if (npc == nullptr) {
        return false;
    }
    speaking_ = npc->npcId;
    page_ = ParleyView::Page::Talk;
    for (auto [entity, id, player] : scene.registry().view<const StableId, const Player>().each()) {
        scene.enqueue(GameCommand{id, kTalk, npc->npcId, 0});
        break;
    }
    return true;
}

void ParleySystem::choose(Scene& scene, const std::string& key)
{
    StableId player = StableId::of("player");
    for (auto [entity, id, marker] : scene.registry().view<const StableId, const Player>().each()) {
        player = id;
        break;
    }
    if (key == "bye") {
        close();
    } else if (key == "trade") {
        const NpcSpawn* npc = find(speaking_);
        if (npc != nullptr && isMerchant(*npc)) {
            page_ = ParleyView::Page::Trade;
        }
    } else if (key == "back") {
        page_ = ParleyView::Page::Talk;
    } else if (key == "accept" && quest_) {
        scene.enqueue(GameCommand{player, kAccept, quest_->id, 0});
    } else if (key == "turnin" && quest_) {
        scene.enqueue(GameCommand{player, kTurnIn, quest_->id, 0});
    } else if (key.starts_with("buy:") && key.size() > 4) {
        scene.enqueue(GameCommand{player, kBuy, key.substr(4), 1});
    } else if (key.starts_with("sell:") && key.size() > 5) {
        scene.enqueue(GameCommand{player, kSell, key.substr(5), 1});
    }
}

void ParleySystem::talk(Scene& scene, const GameCommand& command)
{
    const entt::entity entity = scene.find(command.actor);
    const NpcSpawn* npc = find(command.subject);
    const Transform* transform = transformOf(scene, command.actor);
    if (entity == entt::null || npc == nullptr || transform == nullptr ||
        squaredDistance(npc->x, npc->z, transform->position.x, transform->position.z) > kTradeReachM * kTradeReachM) {
        return;
    }
    Parley& parley = scene.registry().get_or_emplace<Parley>(entity);
    tidy(parley);
    auto it = std::ranges::find(parley.talks, command.subject, &TalkTally::npcId);
    if (it == parley.talks.end()) {
        if (parley.talks.size() >= kMaxTalkTallies) {
            return;
        }
        parley.talks.push_back({command.subject, 0});
        it = parley.talks.end() - 1;
    }
    it->count = it->count < 1'000'000 ? it->count + 1 : it->count;
}

void ParleySystem::accept(Scene& scene, const GameCommand& command)
{
    const entt::entity entity = scene.find(command.actor);
    const Transform* transform = transformOf(scene, command.actor);
    if (entity == entt::null || transform == nullptr || !quest_ || !binding_.bound || command.subject != quest_->id) {
        return;
    }
    const NpcSpawn* giver = find(binding_.giverNpc);
    if (giver == nullptr || squaredDistance(giver->x, giver->z, transform->position.x, transform->position.z) > kTradeReachM * kTradeReachM) {
        return;
    }
    Parley& parley = scene.registry().get_or_emplace<Parley>(entity);
    tidy(parley);
    if (progress(&parley) != nullptr || parley.quests.size() >= kMaxQuestRecords) {
        return;
    }
    parley.quests.push_back({quest_->id, static_cast<std::uint8_t>(QuestStage::Active)});
    say(scene, std::format("Quest taken: {}", quest_->title));
}

void ParleySystem::turnIn(Scene& scene, const GameCommand& command)
{
    const entt::entity entity = scene.find(command.actor);
    const Transform* transform = transformOf(scene, command.actor);
    if (entity == entt::null || transform == nullptr || !quest_ || !binding_.bound || command.subject != quest_->id) {
        return;
    }
    const NpcSpawn* giver = find(binding_.giverNpc);
    if (giver == nullptr || squaredDistance(giver->x, giver->z, transform->position.x, transform->position.z) > kTradeReachM * kTradeReachM) {
        return;
    }
    Parley& parley = scene.registry().get_or_emplace<Parley>(entity);
    tidy(parley);
    const auto it = std::ranges::find(parley.quests, quest_->id, &QuestProgress::id);
    if (it == parley.quests.end() || it->stage == static_cast<std::uint8_t>(QuestStage::Done)) {
        return;
    }
    if (carry_.countOf(scene, binding_.itemId) == 0) {
        say(scene, std::format("You do not have {}", binding_.itemName));
        return;
    }
    if (carry_.removeHeld(scene, binding_.itemId, 1) != InventoryStatus::Ok) {
        return;
    }
    if (currency_) {
        Wallet wallet = walletOf(*currency_, parley.coin);
        (void)wallet.earn(*currency_, CurrencySlot::Coin, quest_->rewardCoin, "quest");
        parley.coin = wallet.balance(CurrencySlot::Coin);
    }
    if (quest_->rewardCount > 0) {
        (void)carry_.addHeld(scene, quest_->rewardItem, quest_->rewardCount, true);
    }
    it->stage = static_cast<std::uint8_t>(QuestStage::Done);
    say(scene, std::format("Quest done: {}", quest_->title));
}

std::vector<MerchantOffer> ParleySystem::offersFor(const Parley& parley, std::string_view extra) const
{
    std::set<std::string, std::less<>> ids(wares_.begin(), wares_.end());
    for (const TradeTally& tally : parley.trades) {
        if (tally.sold > 0 && goods_.contains(tally.good)) {
            ids.insert(tally.good);
        }
    }
    if (!extra.empty() && goods_.contains(extra)) {
        ids.insert(std::string(extra));
    }
    std::vector<GoodDef> defs;
    for (const std::string& id : ids) {
        defs.push_back(goods_.find(id)->second.def);
    }
    std::string error;
    std::optional<std::vector<MerchantOffer>> stocked = stockMerchant(defs, RegionMarket{}, PricingRules{}, error);
    if (!stocked) {
        return {};
    }
    for (MerchantOffer& offer : *stocked) {
        const auto it = std::ranges::find(parley.trades, offer.good, &TradeTally::good);
        if (it != parley.trades.end()) {
            const std::int64_t stock = static_cast<std::int64_t>(offer.stock) + it->sold - it->bought;
            offer.stock = static_cast<std::uint64_t>(std::clamp<std::int64_t>(stock, 0, static_cast<std::int64_t>(kMaxGoodStock)));
        }
    }
    return *stocked;
}

std::vector<MerchantOffer> ParleySystem::offers(const Scene& scene) const
{
    const Parley* parley = state(scene);
    return offersFor(parley != nullptr ? *parley : Parley{}, {});
}

void ParleySystem::buy(Scene& scene, const GameCommand& command)
{
    const entt::entity entity = scene.find(command.actor);
    if (entity == entt::null || !currency_ || merchantInReach(scene) == nullptr) {
        return;
    }
    Parley& parley = scene.registry().get_or_emplace<Parley>(entity);
    tidy(parley);
    const std::uint32_t count = std::clamp<std::uint32_t>(command.count, 1, kMaxTradeCount);
    const auto good = goods_.find(command.subject);
    const std::string name = carry_.catalogue().info(command.subject) != nullptr ? carry_.catalogue().info(command.subject)->name : command.subject;
    auto tally = std::ranges::find(parley.trades, command.subject, &TradeTally::good);
    const bool resold = tally != parley.trades.end() && tally->sold > 0;
    if (good == goods_.end() || !(good->second.ware || resold)) {
        say(scene, std::format("There is no {} for sale", name));
        return;
    }
    if (tally == parley.trades.end() && parley.trades.size() >= kMaxTradeTallies) {
        say(scene, "The merchant will not deal any more today");
        return;
    }
    std::vector<MerchantOffer> offers = offersFor(parley, {});
    Wallet wallet = walletOf(*currency_, parley.coin);
    const TradeResult result = buyFromMerchant(offers, command.subject, count, *currency_, wallet);
    switch (result.status) {
    case ServiceStatus::Ok:
        break;
    case ServiceStatus::OutOfStock:
        say(scene, std::format("There is not that much {} left", name));
        return;
    case ServiceStatus::Insufficient:
        say(scene, std::format("You cannot afford {}", name));
        return;
    default:
        say(scene, std::format("{} cannot be bought", name));
        return;
    }
    if (carry_.addHeld(scene, command.subject, count, false) != InventoryStatus::Ok) {
        say(scene, std::format("You cannot carry {} more", name));
        return;
    }
    parley.coin = wallet.balance(CurrencySlot::Coin);
    if (tally == parley.trades.end()) {
        parley.trades.push_back({command.subject, 0, 0});
        tally = parley.trades.end() - 1;
    }
    tally->bought = static_cast<std::uint32_t>(std::min<std::uint64_t>(std::uint64_t{tally->bought} + count, 1'000'000));
    say(scene, std::format("Bought {} for {}", name, priceText(result.total)));
}

void ParleySystem::sell(Scene& scene, const GameCommand& command)
{
    const entt::entity entity = scene.find(command.actor);
    if (entity == entt::null || !currency_ || merchantInReach(scene) == nullptr) {
        return;
    }
    Parley& parley = scene.registry().get_or_emplace<Parley>(entity);
    tidy(parley);
    const std::uint32_t count = std::clamp<std::uint32_t>(command.count, 1, kMaxTradeCount);
    const CarryItem* info = carry_.catalogue().info(command.subject);
    const std::string name = info != nullptr ? info->name : command.subject;
    if (info == nullptr || carry_.countOf(scene, command.subject) < count) {
        say(scene, std::format("You do not have {}", name));
        return;
    }
    const QuestProgress* quest = progress(&parley);
    const bool questItem = binding_.bound && quest != nullptr && quest->stage != static_cast<std::uint8_t>(QuestStage::Done) && command.subject == binding_.itemId;
    if (info->itemClass == "key" || questItem) {
        say(scene, std::format("The merchant will not buy {}", name));
        return;
    }
    auto tally = std::ranges::find(parley.trades, command.subject, &TradeTally::good);
    if (tally == parley.trades.end() && parley.trades.size() >= kMaxTradeTallies) {
        say(scene, "The merchant will not deal any more today");
        return;
    }
    std::vector<MerchantOffer> offers = offersFor(parley, command.subject);
    Wallet wallet = walletOf(*currency_, parley.coin);
    const TradeResult result = sellToMerchant(offers, command.subject, count, *currency_, wallet);
    if (result.status != ServiceStatus::Ok) {
        say(scene, std::format("{} cannot be sold", name));
        return;
    }
    if (carry_.removeHeld(scene, command.subject, count) != InventoryStatus::Ok) {
        return;
    }
    parley.coin = wallet.balance(CurrencySlot::Coin);
    if (tally == parley.trades.end()) {
        parley.trades.push_back({command.subject, 0, 0});
        tally = parley.trades.end() - 1;
    }
    tally->sold = static_cast<std::uint32_t>(std::min<std::uint64_t>(std::uint64_t{tally->sold} + count, 1'000'000));
    say(scene, std::format("Sold {} for {}", name, priceText(result.total)));
}

std::string ParleySystem::priceText(std::uint64_t price) const
{
    return std::format("{} {}", price, currency_ ? currency_->def(CurrencySlot::Coin).symbol : std::string("c"));
}

std::string ParleySystem::fill(const std::string& text, const std::string& reward) const
{
    std::map<std::string, std::string, std::less<>> values;
    values["giver"] = binding_.giverName;
    values["item"] = binding_.itemName;
    values["dungeon"] = binding_.dungeonKind;
    values["where"] = binding_.where;
    values["reward"] = reward;
    return fillPlaceholders(text, values);
}

std::string ParleySystem::greeting(const Scene& scene, const NpcSpawn& npc) const
{
    if (dialogue_) {
        DialogueContext context;
        context.npcId = hash64(npc.npcId);
        context.tags.emplace_back("topic", "greeting");
        if (!traits_.empty()) {
            context.tags.emplace_back("trait", traits_[hash64(npc.npcId, 7) % traits_.size()]);
        }
        DialogueHistory history;
        history.now = scene.tickCount();
        history.counter = talkCount(state(scene), npc.npcId);
        if (const DialogueLine* line = selectLine(*dialogue_, context, history, world_.seed)) {
            return line->text;
        }
    }
    return "Well met.";
}

ParleyView ParleySystem::view(const Scene& scene) const
{
    ParleyView out;
    const NpcSpawn* npc = speaking_.empty() ? nullptr : find(speaking_);
    if (npc == nullptr) {
        return out;
    }
    const Parley* parley = state(scene);
    out.open = true;
    out.page = page_;
    out.speaker = nameOf(npc->npcId);
    out.role = npc->archetype;
    out.notice = noticeText(scene);
    out.purse = priceText(coin(scene));
    const bool merchant = isMerchant(*npc);
    if (page_ == ParleyView::Page::Trade && merchant) {
        out.text = tradePrompt_.empty() ? "What will it be?" : tradePrompt_;
        std::vector<std::string> held;
        for (const CarryViewRow& row : carry_.view(scene).rows) {
            if (row.count > 0) {
                held.push_back(row.id);
            }
        }
        const Parley empty;
        const Parley& source = parley != nullptr ? *parley : empty;
        const std::vector<MerchantOffer> stock = offersFor(source, {});
        for (const MerchantOffer& offer : stock) {
            if (offer.stock == 0) {
                continue;
            }
            const CarryItem* info = carry_.catalogue().info(offer.good);
            out.buying.push_back({"buy:" + offer.good, info != nullptr ? info->name : offer.good, std::format("{} left", offer.stock), priceText(offer.price),
                coin(scene) >= offer.price});
        }
        const QuestProgress* quest = progress(parley);
        for (const CarryViewRow& row : carry_.view(scene).rows) {
            if (row.count == 0) {
                continue;
            }
            const std::vector<MerchantOffer> priced = offersFor(source, row.id);
            const auto offer = std::ranges::find(priced, row.id, &MerchantOffer::good);
            const bool questItem = binding_.bound && quest != nullptr && quest->stage != static_cast<std::uint8_t>(QuestStage::Done) && row.id == binding_.itemId;
            if (offer == priced.end() || row.itemClass == "key" || questItem) {
                continue;
            }
            out.selling.push_back({"sell:" + row.id, row.name, std::format("you hold {}", row.count), priceText(offer->buyback), true});
        }
        out.choices.push_back({"back", "Back"});
        return out;
    }

    const QuestProgress* quest = progress(parley);
    const bool giver = binding_.bound && quest_ && npc->npcId == binding_.giverNpc;
    if (giver && quest == nullptr) {
        out.text = fill(quest_->offer);
        out.choices.push_back({"accept", "I will bring it back"});
    } else if (giver && quest->stage == static_cast<std::uint8_t>(QuestStage::Active)) {
        out.text = fill(quest_->reminder);
    } else if (giver && quest->stage == static_cast<std::uint8_t>(QuestStage::Found)) {
        out.text = fill(quest_->found);
        out.choices.push_back({"turnin", "Hand over " + binding_.itemName});
    } else if (giver) {
        out.text = fill(quest_->thanks);
    } else {
        out.text = greeting(scene, *npc);
    }
    if (merchant) {
        out.choices.push_back({"trade", "Trade"});
    }
    out.choices.push_back({"bye", farewell_.empty() ? "Farewell" : "Farewell. " + farewell_});
    return out;
}

std::vector<QuestLogEntry> ParleySystem::questLog(const Scene& scene) const
{
    std::vector<QuestLogEntry> log;
    const QuestProgress* quest = progress(state(scene));
    if (quest == nullptr || !quest_ || !binding_.bound) {
        return log;
    }
    const std::uint8_t stage = quest->stage;
    QuestLogEntry entry;
    entry.id = quest_->id;
    entry.title = quest_->title;
    entry.stage = static_cast<QuestStage>(stage);
    entry.objectives.push_back({fill(quest_->objSpeak), true});
    entry.objectives.push_back({fill(quest_->objFetch), stage >= static_cast<std::uint8_t>(QuestStage::Found)});
    entry.objectives.push_back({fill(quest_->objReturn), stage == static_cast<std::uint8_t>(QuestStage::Done)});
    if (stage == static_cast<std::uint8_t>(QuestStage::Done) && !quest_->done.empty()) {
        std::string reward = priceText(quest_->rewardCoin);
        if (quest_->rewardCount > 0) {
            const CarryItem* item = carry_.catalogue().info(quest_->rewardItem);
            reward += std::format(" and {} {}", quest_->rewardCount, item != nullptr ? item->name : quest_->rewardItem);
        }
        entry.outcome = fill(quest_->done, reward);
    }
    log.push_back(std::move(entry));
    return log;
}

}
