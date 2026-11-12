#include "Core/RegistryCatalog.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include "Util/LogUtils.hpp"

namespace kc {

namespace {
    std::vector<RegistryCatalog::Entry>& storage() {
        static std::vector<RegistryCatalog::Entry> entries;
        return entries;
    }
}

void RegistryCatalog::insert(Entry entry) {
    auto& entries = storage();
    for (const auto& existing : entries) {
        if (existing.registry == entry.registry) return;
    }
    entries.push_back(std::move(entry));
}

void RegistryCatalog::buildAll(int& pending) {
    auto& entries = storage();

    std::unordered_map<int, std::vector<std::string>> namesByOrder;
    for (const auto& entry : entries) {
        namesByOrder[entry.order].push_back(entry.name);
    }

    for (const auto& [order, names] : namesByOrder) {
        if (names.size() < 2) continue;

        std::string joined;
        for (const auto& name : names) {
            if (!joined.empty()) joined += ", ";
            joined += "'" + name + "'";
        }

        LogUtils::warn(ThreadName::Worker,
            "Registry", "build order " + std::to_string(order) +
            " is shared by " + joined + "; relative build order is unspecified.");
    }

    std::stable_sort(entries.begin(), entries.end(),
        [](const Entry& a, const Entry& b) { return a.order < b.order; });

    for (const auto& entry : entries) {
        if (entry.build) entry.build(pending);
    }
}

void RegistryCatalog::clearAll() {
    for (const auto& entry : storage()) {
        if (entry.registry) entry.registry->clear();
    }
}

const std::vector<RegistryCatalog::Entry>& RegistryCatalog::entries() {
    return storage();
}

} // namespace kc
