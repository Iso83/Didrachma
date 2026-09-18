#pragma once

#include <Didrachma/market/core/provider/Data.h>
#include <algorithm>
#include <map>

namespace Didrachma::Testing {
using namespace Market::Core::Provider;
using namespace Market::Core::Series;

class FakeProvider final : public Data {
    class FakeSubscription final : public Subscription {
        FakeProvider* m_owner;
        std::size_t m_id;

    public:
        FakeSubscription(FakeProvider* owner, std::size_t id) : m_owner(owner), m_id(id) {}
        ~FakeSubscription() override {
            m_owner->m_handlers.erase(m_id);
        }
    };

    std::vector<Bar> m_history;
    std::map<std::size_t, std::pair<Key, UpdateHandler>> m_handlers;
    std::size_t m_next_id{};

public:
    explicit FakeProvider(std::vector<Bar> history = {}) : m_history(std::move(history)) {}

    [[nodiscard]] CapabilitySet capabilities() const override {
        return Capability::History | Capability::Streaming;
    }

    HistoryResult load_history(const HistoryRequest& request) override {
        std::vector<Bar> result;
        std::ranges::copy_if(m_history, std::back_inserter(result), [&](const Bar& bar) {
            return bar.open_time >= request.range.begin && bar.open_time < request.range.end;
        });

        return result;
    }

    std::unique_ptr<Subscription> subscribe(const Key& key, UpdateHandler handler) override {
        const auto id = m_next_id++;
        m_handlers.emplace(id, std::pair{key, std::move(handler)});
        return std::make_unique<FakeSubscription>(this, id);
    }

    void emit(BarUpdate update) {
        for (const auto& [id, listener] : m_handlers)
            if (listener.first == update.key)
                listener.second(update);
    }

    [[nodiscard]] std::size_t subscriber_count() const {
        return m_handlers.size();
    }
};
} // namespace Didrachma::Testing
