#include "ichor/data/ue_export.h"

#include <catch2/catch_test_macros.hpp>

#include <climits>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

using namespace ichor;

namespace {

class Fixture {
public:
    Fixture()
    {
        static int counter = 0;
        static const unsigned process = std::random_device{}();
        root_ = std::filesystem::temp_directory_path() / ("ichor_ue_export_" + std::to_string(process) + "_" + std::to_string(counter++));
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);
    }
    ~Fixture() { std::filesystem::remove_all(root_); }

    void write(const std::string& relative, const std::string& text) const
    {
        const std::filesystem::path path = root_ / relative;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << text;
    }
    DataSet load() const
    {
        DataSet set;
        REQUIRE(set.loadDirectory(root_));
        return set;
    }

private:
    std::filesystem::path root_;
};

const char* kSchema = R"({"tables":[{"table":"currency","assetClass":"UCurrencyData","fields":[
  {"name":"name","type":"string","required":true},
  {"name":"cap","type":"int32","required":true},
  {"name":"rate","type":"float"},
  {"name":"tag","type":"name"},
  {"name":"hidden","type":"bool"},
  {"name":"earn","type":"string_array"}]}]})";

UeSchema schema(const char* text = kSchema)
{
    UeSchema out;
    std::string error;
    REQUIRE(parseUeSchema(text, out, error));
    return out;
}

bool hasError(const UeConversion& c, const std::string& part)
{
    for (const std::string& e : c.errors) {
        if (e.find(part) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}

TEST_CASE("ue export converts records to typed flat assets", "[ue_export]")
{
    Fixture fx;
    fx.write("currency/c.json", R"([{"id":"coin","name":"Coin","cap":999999999,"rate":0.5,"tag":"a.b-c","hidden":true,"earn":["loot","quest"]}])");
    const UeConversion out = convertToUeAssets(fx.load(), schema());
    REQUIRE(out.ok());
    REQUIRE(out.assets.size() == 1);
    const UeAsset& a = out.assets[0];
    CHECK(a.assetName == "DA_currency_coin");
    CHECK(a.assetClass == "UCurrencyData");
    REQUIRE(a.fields.size() == 6);
    CHECK(std::get<std::string>(a.fields[0].value) == "Coin");
    CHECK(std::get<std::int32_t>(a.fields[1].value) == 999999999);
    CHECK(std::get<float>(a.fields[2].value) == 0.5f);
    CHECK(std::get<bool>(a.fields[4].value));
    CHECK(std::get<std::vector<std::string>>(a.fields[5].value).size() == 2);
}

TEST_CASE("ue export omits optional fields that are absent", "[ue_export]")
{
    Fixture fx;
    fx.write("currency/c.json", R"([{"id":"coin","name":"Coin","cap":1}])");
    const UeConversion out = convertToUeAssets(fx.load(), schema());
    REQUIRE(out.ok());
    CHECK(out.assets[0].fields.size() == 2);
}

TEST_CASE("ue export reports every problem and emits no partial asset", "[ue_export]")
{
    Fixture fx;
    fx.write("currency/c.json", R"([
      {"id":"a","cap":1},
      {"id":"b","name":"B","cap":1.5},
      {"id":"c","name":"C","cap":3000000000},
      {"id":"d","name":"D","cap":1,"surprise":1},
      {"id":"e","name":"E","cap":1,"tag":"bad tag"},
      {"id":"f","name":5,"cap":1},
      {"id":"ok","name":"Ok","cap":1}])");
    const UeConversion out = convertToUeAssets(fx.load(), schema());
    CHECK(hasError(out, "currency/a: missing required field 'name'"));
    CHECK(hasError(out, "currency/b: field 'cap'"));
    CHECK(hasError(out, "currency/c: field 'cap'"));
    CHECK(hasError(out, "currency/d: field 'surprise' is not in the schema"));
    CHECK(hasError(out, "currency/e: field 'tag'"));
    CHECK(hasError(out, "currency/f: field 'name' must be a string"));
    REQUIRE(out.assets.size() == 1);
    CHECK(out.assets[0].sourceId == "ok");
}

TEST_CASE("ue export rejects numbers outside the target type", "[ue_export]")
{
    Fixture fx;
    fx.write("currency/c.json", R"([{"id":"a","name":"A","cap":-2147483649},{"id":"b","name":"B","cap":2147483647,"rate":1e300},{"id":"c","name":"C","cap":-2147483648,"rate":-3.4e38}])");
    const UeConversion out = convertToUeAssets(fx.load(), schema());
    CHECK(hasError(out, "currency/a: field 'cap'"));
    CHECK(hasError(out, "currency/b: field 'rate'"));
    REQUIRE(out.assets.size() == 1);
    CHECK(std::get<std::int32_t>(out.assets[0].fields[1].value) == INT32_MIN);
}

TEST_CASE("ue export flags a schema table missing from the data", "[ue_export]")
{
    Fixture fx;
    fx.write("other/o.json", R"([{"id":"x"}])");
    const UeConversion out = convertToUeAssets(fx.load(), schema());
    CHECK(hasError(out, "table 'currency' is not in the data"));
    CHECK(out.assets.empty());
}

TEST_CASE("ue export rejects ids that collapse to the same asset name", "[ue_export]")
{
    Fixture fx;
    fx.write("currency/c.json", R"([{"id":"a.b","name":"1","cap":1},{"id":"a_b","name":"2","cap":1}])");
    const UeConversion out = convertToUeAssets(fx.load(), schema());
    CHECK(hasError(out, "collides"));
    CHECK(out.assets.size() == 1);
}

TEST_CASE("ue export rejects case-only collisions and over-long asset names", "[ue_export]")
{
    Fixture fx;
    fx.write("currency/c.json", R"([{"id":"Coin","name":"1","cap":1},{"id":"coin","name":"2","cap":1},{"id":")" + std::string(300, 'x') + R"(","name":"3","cap":1}])");
    const UeConversion out = convertToUeAssets(fx.load(), schema());
    CHECK(hasError(out, "collides"));
    CHECK(hasError(out, "longer than"));
    CHECK(out.assets.size() == 1);
}

TEST_CASE("ue intermediate json matches a golden text", "[ue_export]")
{
    Fixture fx;
    fx.write("currency/c.json", R"([{"id":"a","name":"A\u00e9\u0000","cap":-5,"rate":0.1}])");
    const UeConversion out = convertToUeAssets(fx.load(), schema());
    REQUIRE(out.ok());
    const std::string golden =
        "{\n  \"version\": 1,\n  \"assets\": [\n    {\n      \"assetName\": \"DA_currency_a\",\n      \"assetClass\": \"UCurrencyData\",\n"
        "      \"table\": \"currency\",\n      \"sourceId\": \"a\",\n      \"fields\": [\n"
        "        {\n          \"name\": \"name\",\n          \"type\": \"string\",\n          \"value\": \"A\xc3\xa9\\u0000\"\n        },\n"
        "        {\n          \"name\": \"cap\",\n          \"type\": \"int32\",\n          \"value\": -5\n        },\n"
        "        {\n          \"name\": \"rate\",\n          \"type\": \"float\",\n          \"value\": 0.10000000149011612\n        }\n"
        "      ]\n    }\n  ]\n}\n";
    CHECK(toUeIntermediateJson(out) == golden);
}

TEST_CASE("ue export hostile ids produce safe asset names", "[ue_export]")
{
    Fixture fx;
    fx.write("currency/c.json", R"([{"id":"../x y\u0000z/\u00e9","name":"A","cap":1}])");
    const UeConversion out = convertToUeAssets(fx.load(), schema());
    REQUIRE(out.ok());
    for (const char c : out.assets[0].assetName) {
        CHECK(((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'));
    }
}

TEST_CASE("ue schema parser rejects malformed schemas", "[ue_export]")
{
    UeSchema out;
    std::string error;
    CHECK_FALSE(parseUeSchema("", out, error));
    CHECK_FALSE(parseUeSchema("[]", out, error));
    CHECK_FALSE(parseUeSchema(R"({"tables":[{"table":"t","assetClass":"C","fields":[{"name":"a","type":"quaternion"}]}]})", out, error));
    CHECK(error.find("unknown type") != std::string::npos);
    CHECK_FALSE(parseUeSchema(R"({"tables":[{"table":"t","assetClass":"C","fields":[{"name":"a","type":"bool"},{"name":"a","type":"bool"}]}]})", out, error));
    CHECK_FALSE(parseUeSchema(R"({"tables":[{"table":"t","assetClass":"C","fields":[{"name":"id","type":"bool"}]}]})", out, error));
    CHECK_FALSE(parseUeSchema(R"({"tables":[{"table":"../t","assetClass":"C","fields":[]}]})", out, error));
    CHECK_FALSE(parseUeSchema(R"({"tables":[{"table":"t","assetClass":"C","fields":[]},{"table":"t","assetClass":"C","fields":[]}]})", out, error));
    CHECK_FALSE(parseUeSchema(R"({"tables":[{"table":"t","assetClass":"C","fields":[{"name":"a","type":"bool","required":"yes"}]}]})", out, error));
}

TEST_CASE("ue intermediate json is deterministic and typed", "[ue_export]")
{
    Fixture fx;
    fx.write("currency/c.json", R"([{"id":"b","name":"B","cap":2,"rate":0.25},{"id":"a","name":"A","cap":1,"earn":["x"]}])");
    const UeConversion out = convertToUeAssets(fx.load(), schema());
    REQUIRE(out.ok());
    const std::string text = toUeIntermediateJson(out);
    CHECK(text == toUeIntermediateJson(out));
    CHECK(text.find("\"assetName\": \"DA_currency_a\"") < text.find("\"assetName\": \"DA_currency_b\""));
    CHECK(text.find("\"type\": \"int32\"") != std::string::npos);
    CHECK(text.find("\"value\": 0.25") != std::string::npos);
    CHECK(text.find("\"version\": 1") != std::string::npos);
}

TEST_CASE("ue export converts the shipped currency table", "[ue_export]")
{
    DataSet data;
    REQUIRE(data.loadDirectory(ICHOR_REPO_DATA));
    const UeSchema s = schema(R"({"tables":[{"table":"currency","assetClass":"UCurrencyData","fields":[
      {"name":"name","type":"string","required":true},{"name":"symbol","type":"string","required":true},
      {"name":"earn","type":"string_array"},{"name":"spend","type":"string_array"},{"name":"cap","type":"int32","required":true}]}]})");
    const UeConversion out = convertToUeAssets(data, s);
    REQUIRE(out.ok());
    CHECK(out.assets.size() == data.table("currency")->records.size());
}
