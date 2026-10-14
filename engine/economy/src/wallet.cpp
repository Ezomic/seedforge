#include "ichor/economy/wallet.h"

#include "ichor/core/binary.h"
#include "ichor/core/hash.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace ichor {

namespace {

constexpr std::uint32_t kWalletVersion = 1;

constexpr std::array<const char*, kCurrencySlotCount> kSlotIds = {"coin", "hard"};

std::string where(const std::string& id, const char* field)
{
    return "currency '" + id + "': \"" + field + "\" ";
}

bool readText(const DataRecord& record, const char* name, std::string& out, std::string& error)
{
    const auto it = record.fields.find(name);
    const auto* value = it == record.fields.end() ? nullptr : std::get_if<std::string>(&it->second);
    if (value == nullptr || value->empty()) {
        error = where(record.id, name) + "must be a non-empty string";
        return false;
    }
    out = *value;
    return true;
}

bool readNames(const DataRecord& record, const char* name, std::vector<std::string>& out, std::string& error)
{
    const auto it = record.fields.find(name);
    const auto* value = it == record.fields.end() ? nullptr : std::get_if<std::vector<std::string>>(&it->second);
    if (value == nullptr) {
        error = where(record.id, name) + "must be a list of strings";
        return false;
    }
    std::set<std::string> seen;
    for (const std::string& entry : *value) {
        if (entry.empty() || !seen.insert(entry).second) {
            error = where(record.id, name) + "must hold distinct non-empty strings";
            return false;
        }
    }
    out = *value;
    return true;
}

bool contains(const std::vector<std::string>& list, std::string_view entry)
{
    return std::find(list.begin(), list.end(), entry) != list.end();
}

}

std::optional<CurrencyTable> CurrencyTable::fromTable(const DataTable& table, std::string& error)
{
    CurrencyTable result;
    for (const auto& [id, record] : table.records) {
        const auto slot = std::find_if(kSlotIds.begin(), kSlotIds.end(), [&](const char* name) { return id == name; });
        if (slot == kSlotIds.end()) {
            error = "currency '" + id + "': the only currencies are \"coin\" and \"hard\"";
            return std::nullopt;
        }
        for (const auto& [name, value] : record.fields) {
            if (name != "name" && name != "symbol" && name != "earn" && name != "spend" && name != "cap") {
                error = "currency '" + id + "': unknown field \"" + name + "\"";
                return std::nullopt;
            }
        }
        CurrencyDef& def = result.defs_[static_cast<std::size_t>(slot - kSlotIds.begin())];
        def.id = id;
        if (!readText(record, "name", def.name, error) || !readText(record, "symbol", def.symbol, error)
            || !readNames(record, "earn", def.earn, error) || !readNames(record, "spend", def.spend, error)) {
            return std::nullopt;
        }
        const auto cap = record.fields.find("cap");
        const auto* value = cap == record.fields.end() ? nullptr : std::get_if<double>(&cap->second);
        if (value == nullptr || *value != std::floor(*value) || *value < 1.0 || *value > static_cast<double>(kMaxCurrencyCap)) {
            error = where(id, "cap") + "must be an integer from 1 to " + std::to_string(kMaxCurrencyCap);
            return std::nullopt;
        }
        def.cap = static_cast<std::uint64_t>(*value);
    }
    for (const CurrencyDef& def : result.defs_) {
        if (def.id.empty()) {
            error = "the currency table needs both \"coin\" and \"hard\"";
            return std::nullopt;
        }
    }
    return result;
}

bool CurrencyTable::canEarnFrom(CurrencySlot slot, std::string_view source) const
{
    return contains(def(slot).earn, source);
}

bool CurrencyTable::canSpendOn(CurrencySlot slot, std::string_view sink) const
{
    return contains(def(slot).spend, sink);
}

EarnResult Wallet::earn(const CurrencyTable& table, CurrencySlot slot, std::uint64_t amount, std::string_view source)
{
    if (!table.canEarnFrom(slot, source)) {
        return {TxStatus::NotAllowed, 0};
    }
    std::uint64_t& held = balances_[static_cast<std::size_t>(slot)];
    const std::uint64_t cap = table.def(slot).cap;
    const std::uint64_t room = cap > held ? cap - held : 0;
    const std::uint64_t credited = std::min(amount, room);
    held += credited;
    return {TxStatus::Ok, credited};
}

TxStatus Wallet::spend(const CurrencyTable& table, CurrencySlot slot, std::uint64_t amount, std::string_view sink)
{
    if (!table.canSpendOn(slot, sink)) {
        return TxStatus::NotAllowed;
    }
    std::uint64_t& held = balances_[static_cast<std::size_t>(slot)];
    if (held < amount) {
        return TxStatus::Insufficient;
    }
    held -= amount;
    return TxStatus::Ok;
}

std::vector<std::uint8_t> Wallet::serialise() const
{
    BinaryWriter writer;
    writer.write(kWalletVersion);
    for (const std::uint64_t held : balances_) {
        writer.write(held);
    }
    return writer.take();
}

std::optional<Wallet> Wallet::deserialise(std::span<const std::uint8_t> bytes, const CurrencyTable& table)
{
    BinaryReader reader(bytes);
    std::uint32_t version = 0;
    if (!reader.read(version) || version != kWalletVersion) {
        return std::nullopt;
    }
    Wallet wallet;
    for (std::size_t i = 0; i < kCurrencySlotCount; ++i) {
        if (!reader.read(wallet.balances_[i]) || wallet.balances_[i] > table.def(static_cast<CurrencySlot>(i)).cap) {
            return std::nullopt;
        }
    }
    if (!reader.atEnd()) {
        return std::nullopt;
    }
    return wallet;
}

std::uint64_t Wallet::digest() const
{
    const std::vector<std::uint8_t> bytes = serialise();
    return hash64(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

}
