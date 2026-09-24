#pragma once

#include <Didrachma/analysis/core/indicator/Analyzer.h>
#include <Didrachma/market/core/provider/Data.h>
#include <Didrachma/market/core/series/BarUpdate.h>
#include <Didrachma/market/core/series/Key.h>
#include <Didrachma/strategy/core/Model.h>
#include <map>
#include <optional>
#include <span>

namespace Didrachma::Strategy::Core {
enum class Truth { False, True, Unknown };
enum class Readiness { Loading, Ready, Stale, InsufficientHistory, ProviderError, CalculationError };

struct Evidence {
    std::string condition_id;
    Truth truth{Truth::Unknown};
    std::optional<double> value;
    std::optional<Market::Core::Time::UtcTimestamp> source_time;
    std::string detail;
};

struct Evaluation {
    Market::Core::Time::UtcTimestamp time;
    Truth truth{Truth::Unknown};
    std::vector<Evidence> evidence;
};

struct RunValues {
    Duration elapsed{};
    std::uint64_t closed_bars{};
    double unrealized_return_percentage{};
};

struct BindingState {
    Readiness readiness{Readiness::Loading};
    std::string detail;
    std::size_t available_history{};
    std::size_t required_history{};
};

struct ResolvedSeries {
    std::string binding_id;
    Market::Core::Series::Key key;
    bool derived{};
    std::optional<std::string> source_binding_id;
};

struct ResolutionError {
    std::string binding_id;
    std::string message;
};

struct Resolution {
    std::vector<ResolvedSeries> series;
    std::vector<ResolutionError> errors;
};

struct DerivedRefresh {
    std::size_t refresh_count{};
    std::size_t source_begin{};
    std::size_t source_count{};
    std::size_t reused_prefix{};
    std::optional<Market::Core::Time::Range> dirty_range;
};

[[nodiscard]] Resolution resolve(const Definition&, std::optional<std::string_view> subject);

class DataGraph {
    struct Implementation;
    std::shared_ptr<Implementation> m_implementation;

public:
    DataGraph(Snapshot, Analysis::Core::Indicator::Analyzer&, std::optional<std::string> subject);

    [[nodiscard]] const Resolution& resolution() const;
    [[nodiscard]] std::size_t required_history(std::string_view binding_id) const;
    [[nodiscard]] BindingState state(std::string_view binding_id) const;
    [[nodiscard]] std::size_t unique_series_count() const;
    [[nodiscard]] std::vector<std::string> dependency_order() const;
    [[nodiscard]] DerivedRefresh derived_refresh(std::string_view binding_id) const;

    void set_series(std::string_view binding_id, std::span<const Market::Core::Series::Bar> bars,
                    std::uint64_t revision = 1);
    void set_provider_error(std::string_view binding_id, std::string message);
    void load(Market::Core::Provider::Data& provider, std::string_view binding_id, Market::Core::Time::Range range);
    void apply(const Market::Core::Series::BarUpdate& update);
    void derive_series(std::string_view binding_id, std::string_view source_binding_id);
    [[nodiscard]] std::vector<Evaluation> evaluate_entry();
    [[nodiscard]] Evaluation evaluate(const std::shared_ptr<ConditionExpression>&, Market::Core::Time::UtcTimestamp,
                                      std::optional<RunValues> run = {});
    [[nodiscard]] std::span<const Market::Core::Series::Bar> primary_bars() const;
    [[nodiscard]] std::optional<double> indicator_value(std::string_view indicator_id, std::string_view output_id,
                                                        Market::Core::Time::UtcTimestamp) const;
};
} // namespace Didrachma::Strategy::Core
