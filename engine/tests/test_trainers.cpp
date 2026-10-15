#include "ichor/data/data_set.h"
#include "ichor/economy/wallet.h"
#include "ichor/factions/standing.h"
#include "ichor/progression/trainers.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace ichor;

namespace {

std::string readData(const char* name)
{
    std::ifstream file(std::filesystem::path(ICHOR_GAME_DATA_DIR) / "progression" / name, std::ios::binary);
    REQUIRE(file.good());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::vector<Capability> shippedCapabilities()
{
    std::vector<Capability> table;
    std::vector<std::string> problems;
    REQUIRE(parseCapabilities("capabilities.json", readData("capabilities.json"), table, problems));
    return table;
}

CurrencyTable shippedCurrencies()
{
    DataSet data;
    REQUIRE(data.loadDirectory(ICHOR_GAME_DATA_DIR));
    const DataTable* table = data.table("currency");
    REQUIRE(table != nullptr);
    std::string error;
    auto result = CurrencyTable::fromTable(*table, error);
    REQUIRE(result.has_value());
    return *result;
}

std::vector<Trainer> shippedTrainers(const std::vector<Capability>& table)
{
    std::vector<Trainer> trainers;
    std::vector<std::string> problems;
    INFO((problems.empty() ? std::string() : problems.front()));
    REQUIRE(parseTrainers("trainers.json", readData("trainers.json"), table, trainers, problems));
    return trainers;
}

struct World {
    std::vector<Capability> table = shippedCapabilities();
    std::vector<Trainer> trainers = shippedTrainers(table);
    CurrencyTable currencies = shippedCurrencies();
    CapabilityUnlocks unlocks{table};
    Wallet wallet;
    FoundTrainers found{"ferryman_aldric", "cliff_warden", "locksmith_nera"};
    bool open = true;
    TrainerGate gate = [this](std::string_view) { return open; };

    const Trainer& trainer(const std::string& id) const
    {
        for (const Trainer& t : trainers) {
            if (t.id == id) {
                return t;
            }
        }
        FAIL("no trainer " << id);
        return trainers.front();
    }

    void fund(std::uint64_t amount) { REQUIRE(wallet.earn(currencies, CurrencySlot::Coin, amount, "loot").status == TxStatus::Ok); }
    TrainQuote learn(const std::string& who, const std::string& what) { return train(trainer(who), what, found, gate, unlocks, wallet, currencies); }
};

std::vector<std::string> parseProblems(const std::string& text)
{
    World w;
    std::vector<Trainer> out;
    std::vector<std::string> problems;
    const bool ok = parseTrainers("t.json", text, w.table, out, problems);
    REQUIRE(ok == problems.empty());
    return problems;
}

std::string record(const std::string& id = "t_swim", const std::string& capability = "swim", const std::string& trainer = "t")
{
    return R"({"id": ")" + id + R"(", "trainer": ")" + trainer + R"(", "name": "T", "gate": "g", "currency": "coin", "sink": "trade", "capability": ")" + capability
        + R"(", "cost": 1, "days": 1})";
}

std::string withField(const std::string& key, const std::string& value)
{
    std::string text = record();
    const std::size_t at = text.find("\"" + key + "\":");
    REQUIRE(at != std::string::npos);
    const std::size_t end = text.find_first_of(",}", text.find(':', at));
    return "[" + text.replace(at, end - at, "\"" + key + "\": " + value) + "]";
}

}

TEST_CASE("the shipped trainers parse, offer real capabilities and use allowed sinks", "[trainers]")
{
    World w;
    REQUIRE(w.trainers.size() >= 3);
    for (const Trainer& t : w.trainers) {
        INFO(t.id);
        CHECK(w.currencies.canSpendOn(t.currency, t.sink));
        CHECK_FALSE(t.offers.empty());
    }
}

TEST_CASE("training charges the price, unlocks the capability and reports the days", "[trainers]")
{
    World w;
    w.fund(1000);
    const TrainQuote quote = w.learn("ferryman_aldric", "swim");
    REQUIRE(quote.status == TrainStatus::Done);
    CHECK(quote.cost == 300);
    CHECK(quote.days == 2);
    CHECK(w.wallet.balance(CurrencySlot::Coin) == 700);
    CHECK(w.unlocks.can("swim"));
}

TEST_CASE("a trainer who has not been found teaches nothing", "[trainers]")
{
    World w;
    w.fund(1000);
    w.found.erase("ferryman_aldric");
    const auto walletDigest = w.wallet.digest();
    const auto unlockDigest = w.unlocks.digest();
    CHECK(w.learn("ferryman_aldric", "swim").status == TrainStatus::NotFound);
    CHECK(w.wallet.digest() == walletDigest);
    CHECK(w.unlocks.digest() == unlockDigest);
    w.found.insert("ferryman_aldric");
    CHECK(w.learn("ferryman_aldric", "swim").status == TrainStatus::Done);
}

TEST_CASE("a closed standing gate refuses, and the gate asked about is the trainer's own", "[trainers]")
{
    World w;
    w.fund(1000);
    std::vector<std::string> asked;
    w.gate = [&](std::string_view id) {
        asked.emplace_back(id);
        return false;
    };
    const auto walletDigest = w.wallet.digest();
    CHECK(w.learn("cliff_warden", "climb").status == TrainStatus::GateClosed);
    CHECK(asked == std::vector<std::string>{"wardens_friendly"});
    CHECK(w.wallet.digest() == walletDigest);
    CHECK_FALSE(w.unlocks.can("climb"));

    w.gate = nullptr;
    CHECK(w.learn("cliff_warden", "climb").status == TrainStatus::GateClosed);
}

TEST_CASE("training follows real faction standing and loses access when standing drops", "[trainers]")
{
    World w;
    w.fund(5000);
    std::vector<Faction> factions;
    factions.push_back(Faction{});
    factions.back().id = "wardens";
    std::vector<StandingTier> tiers{{"hostile", "Hostile", kStandingMin}, {"friendly", "Friendly", 200}};
    std::vector<StandingGate> gates{{"wardens_friendly", "wardens", "friendly"}};
    FactionStanding standing(factions, tiers, gates);
    w.gate = [&](std::string_view id) { return standing.allowed(id); };

    CHECK(w.learn("cliff_warden", "climb").status == TrainStatus::GateClosed);
    REQUIRE(standing.change("wardens", 250));
    CHECK(w.learn("cliff_warden", "climb").status == TrainStatus::Done);
    REQUIRE(standing.change("wardens", -500));
    CHECK(w.learn("cliff_warden", "descend").status == TrainStatus::GateClosed);
    CHECK_FALSE(w.unlocks.can("descend"));
}

TEST_CASE("only what the trainer offers can be learned there", "[trainers]")
{
    World w;
    w.fund(5000);
    CHECK(w.learn("ferryman_aldric", "climb").status == TrainStatus::NotOffered);
    CHECK(w.learn("ferryman_aldric", "nope").status == TrainStatus::NotOffered);
    CHECK(w.learn("ferryman_aldric", "").status == TrainStatus::NotOffered);
    CHECK(w.unlocks.unlocked().empty());
}

TEST_CASE("prerequisites and repeats are refused without charging", "[trainers]")
{
    World w;
    w.fund(5000);
    const auto funded = w.wallet.digest();
    CHECK(w.learn("ferryman_aldric", "dive").status == TrainStatus::PrerequisiteUnmet);
    CHECK(w.wallet.digest() == funded);
    REQUIRE(w.learn("ferryman_aldric", "swim").status == TrainStatus::Done);
    const auto afterSwim = w.wallet.digest();
    CHECK(w.learn("ferryman_aldric", "swim").status == TrainStatus::AlreadyUnlocked);
    CHECK(w.wallet.digest() == afterSwim);
    CHECK(w.learn("ferryman_aldric", "dive").status == TrainStatus::Done);
    CHECK(w.unlocks.can("dive"));
}

TEST_CASE("price is exact: one coin short refuses, the exact amount buys", "[trainers]")
{
    World w;
    w.fund(299);
    const auto funded = w.wallet.digest();
    const auto unlockDigest = w.unlocks.digest();
    CHECK(w.learn("ferryman_aldric", "swim").status == TrainStatus::Insufficient);
    CHECK(w.wallet.digest() == funded);
    CHECK(w.unlocks.digest() == unlockDigest);
    w.fund(1);
    CHECK(w.learn("ferryman_aldric", "swim").status == TrainStatus::Done);
    CHECK(w.wallet.balance(CurrencySlot::Coin) == 0);
}

TEST_CASE("a sink the currency does not allow is refused", "[trainers]")
{
    World w;
    w.fund(5000);
    Trainer bad = w.trainer("ferryman_aldric");
    bad.sink = "power";
    const auto funded = w.wallet.digest();
    CHECK(train(bad, "swim", w.found, w.gate, w.unlocks, w.wallet, w.currencies).status == TrainStatus::SinkNotAllowed);
    CHECK(w.wallet.digest() == funded);
    CHECK_FALSE(w.unlocks.can("swim"));
}

TEST_CASE("quoting never changes anything", "[trainers]")
{
    World w;
    w.fund(1000);
    const auto walletDigest = w.wallet.digest();
    const auto unlockDigest = w.unlocks.digest();
    const TrainQuote quote = quoteTraining(w.trainer("ferryman_aldric"), "swim", w.found, w.gate, w.unlocks, w.wallet, w.currencies);
    CHECK(quote.status == TrainStatus::Done);
    CHECK(quote.cost == 300);
    CHECK(w.wallet.digest() == walletDigest);
    CHECK(w.unlocks.digest() == unlockDigest);
}

TEST_CASE("a free lesson costs nothing but still needs access", "[trainers]")
{
    World w;
    Trainer free = w.trainer("ferryman_aldric");
    free.offers = {{"swim", 0, 0}};
    CHECK(train(free, "swim", w.found, w.gate, w.unlocks, w.wallet, w.currencies).status == TrainStatus::Done);
    CHECK(w.unlocks.can("swim"));
}

TEST_CASE("trainer parsing rejects hostile and malformed input without crashing", "[trainers]")
{
    CHECK(parseProblems("[" + record() + "]").empty());
    CHECK(parseProblems("[]").empty());
    CHECK_FALSE(parseProblems("").empty());
    CHECK_FALSE(parseProblems("{}").empty());
    CHECK_FALSE(parseProblems("[1]").empty());
    CHECK_FALSE(parseProblems(std::string("[{\"id\": \"ÿþ\"}]")).empty());
    CHECK_FALSE(parseProblems(std::string(300 * 1024, ' ')).empty());
    CHECK_FALSE(parseProblems(std::string(5000, '[') + std::string(5000, ']')).empty());
    CHECK_FALSE(parseProblems(R"([{"id": "t"}])").empty());

    CHECK_FALSE(parseProblems(withField("trainer", "\"Bad Id\"")).empty());
    CHECK_FALSE(parseProblems(withField("id", "\"Bad Id\"")).empty());
    CHECK_FALSE(parseProblems(withField("currency", "\"gold\"")).empty());
    CHECK_FALSE(parseProblems(withField("name", "\"T\u2014x\"")).empty());
    CHECK_FALSE(parseProblems(withField("gate", "\"\"")).empty());
    CHECK_FALSE(parseProblems(withField("capability", "\"nope\"")).empty());
    CHECK_FALSE(parseProblems(withField("cost", "-1")).empty());
    CHECK_FALSE(parseProblems(withField("cost", "1.5")).empty());
    CHECK_FALSE(parseProblems(withField("days", "1e300")).empty());
    CHECK_FALSE(parseProblems(withField("cost", "18446744073709551615")).empty());
    CHECK_FALSE(parseProblems(withField("cost", "\"1\"")).empty());
    CHECK_FALSE(parseProblems(withField("days", "null")).empty());
}

TEST_CASE("records of one trainer must agree and not repeat a capability or id", "[trainers]")
{
    CHECK_FALSE(parseProblems("[" + record() + "," + record() + "]").empty());
    CHECK_FALSE(parseProblems("[" + record("a") + "," + record("b") + "]").empty());
    CHECK_FALSE(parseProblems("[" + record("a", "swim") + "," + record("b", "dive").replace(record("b", "dive").find("\"T\""), 3, "\"U\"") + "]").empty());
    CHECK(parseProblems("[" + record("a", "swim") + "," + record("b", "dive") + "," + record("c", "swim", "u") + "]").empty());
}

TEST_CASE("parsed trainers group their offers and come out sorted by id", "[trainers]")
{
    World w;
    std::vector<Trainer> out;
    std::vector<std::string> problems;
    REQUIRE(parseTrainers("t.json", "[" + record("a", "swim", "zed") + "," + record("b", "dive", "zed") + "," + record("c", "climb", "abe") + "]", w.table, out, problems));
    REQUIRE(out.size() == 2);
    CHECK(out[0].id == "abe");
    CHECK(out[1].id == "zed");
    REQUIRE(out[1].offers.size() == 2);
    CHECK(out[1].offers[0].capability == "swim");
    CHECK(out[1].offers[1].capability == "dive");
}

TEST_CASE("a failed parse leaves the output untouched", "[trainers]")
{
    World w;
    std::vector<Trainer> out = w.trainers;
    std::vector<std::string> problems;
    CHECK_FALSE(parseTrainers("t.json", "[{}]", w.table, out, problems));
    CHECK(out.size() == w.trainers.size());
    CHECK_FALSE(problems.empty());
}
