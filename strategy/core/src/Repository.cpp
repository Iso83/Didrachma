#include <Didrachma/strategy/core/Repository.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace Didrachma::Strategy::Core {
using json = nlohmann::json;

template <class Enum>
Enum enum_value(const json& value, std::initializer_list<std::pair<std::string_view, Enum>> values) {
    const auto text = value.get<std::string>();
    for (const auto& [name, result] : values)
        if (name == text)
            return result;

    throw std::invalid_argument("unsupported enum value: " + text);
}

template <class Enum>
std::string enum_name(Enum value, std::initializer_list<std::pair<std::string_view, Enum>> values) {
    for (const auto& [name, candidate] : values)
        if (candidate == value)
            return std::string(name);

    throw std::invalid_argument("unsupported enum value");
}

#define ENUM_IO(Type, ...)                                                                                             \
    void to_json(json& j, const Type& value) {                                                                         \
        j = enum_name(value, {__VA_ARGS__});                                                                           \
    }                                                                                                                  \
    void from_json(const json& j, Type& value) {                                                                       \
        value = enum_value<Type>(j, {__VA_ARGS__});                                                                    \
    }

ENUM_IO(Direction, {"long", Direction::Long}, {"short", Direction::Short})
ENUM_IO(InstrumentKind, {"subject", InstrumentKind::Subject}, {"fixed", InstrumentKind::Fixed})
ENUM_IO(Market::Core::Time::Unit, {"minute", Market::Core::Time::Unit::Minute},
        {"hour", Market::Core::Time::Unit::Hour}, {"day", Market::Core::Time::Unit::Day})
ENUM_IO(Comparison, {"less", Comparison::Less}, {"lessOrEqual", Comparison::LessOrEqual}, {"equal", Comparison::Equal},
        {"greaterOrEqual", Comparison::GreaterOrEqual}, {"greater", Comparison::Greater})
ENUM_IO(MarketField, {"open", MarketField::Open}, {"high", MarketField::High}, {"low", MarketField::Low},
        {"close", MarketField::Close}, {"volume", MarketField::Volume})
ENUM_IO(CrossDirection, {"above", CrossDirection::Above}, {"below", CrossDirection::Below},
        {"either", CrossDirection::Either})
ENUM_IO(ReturnKind, {"gain", ReturnKind::Gain}, {"loss", ReturnKind::Loss})
ENUM_IO(ConditionKind, {"marketComparison", ConditionKind::MarketComparison},
        {"indicatorComparison", ConditionKind::IndicatorComparison}, {"indicatorCross", ConditionKind::IndicatorCross},
        {"patternOccurrence", ConditionKind::PatternOccurrence}, {"elapsedTime", ConditionKind::ElapsedTime},
        {"closedBarCount", ConditionKind::ClosedBarCount}, {"unrealizedReturn", ConditionKind::UnrealizedReturn},
        {"all", ConditionKind::All}, {"any", ConditionKind::Any}, {"not", ConditionKind::Not},
        {"sequence", ConditionKind::Sequence})
ENUM_IO(PricePolicyKind, {"absolute", PricePolicyKind::Absolute},
        {"percentageFromEntry", PricePolicyKind::PercentageFromEntry},
        {"indicatorValue", PricePolicyKind::IndicatorValue})
ENUM_IO(EntryOrderKind, {"nextBarOpen", EntryOrderKind::NextBarOpen}, {"limit", EntryOrderKind::Limit})
ENUM_IO(ActionKind, {"adjustStop", ActionKind::AdjustStop}, {"adjustTarget", ActionKind::AdjustTarget},
        {"exit", ActionKind::Exit})
#undef ENUM_IO

void to_json(json& j, const Analysis::Core::Indicator::ParameterValue& value) {
    std::visit([&](const auto& item) { j = item; }, value);
}

Analysis::Core::Indicator::ParameterValue parameter(const json& j) {
    if (j.is_boolean())
        return j.get<bool>();

    if (j.is_number_integer())
        return j.get<std::int64_t>();

    if (j.is_number())
        return j.get<double>();

    if (j.is_string())
        return j.get<std::string>();

    throw std::invalid_argument("indicator parameter must be scalar");
}

void frame_to(json& j, const Market::Core::Time::Frame& value) {
    j = {{"quantity", value.quantity}};
    to_json(j["unit"], value.unit);
}

void frame_from(const json& j, Market::Core::Time::Frame& value) {
    j.at("quantity").get_to(value.quantity);
    from_json(j.at("unit"), value.unit);
}

void to_json(json& j, const InstrumentSelector& value) {
    j = {{"kind", value.kind}};
    if (value.kind == InstrumentKind::Fixed)
        j["symbol"] = value.symbol;
}

void from_json(const json& j, InstrumentSelector& value) {
    j.at("kind").get_to(value.kind);
    value.symbol = j.value("symbol", "");
}

void to_json(json& j, const SeriesBinding& value) {
    j = {{"id", value.id}, {"providerId", value.provider_id}, {"instrument", value.instrument}};
    frame_to(j["timeframe"], value.timeframe);
    if (value.maximum_data_age)
        j["maximumDataAgeSeconds"] = value.maximum_data_age->count();
}

void from_json(const json& j, SeriesBinding& value) {
    j.at("id").get_to(value.id);
    j.at("providerId").get_to(value.provider_id);
    j.at("instrument").get_to(value.instrument);
    frame_from(j.at("timeframe"), value.timeframe);
    if (j.contains("maximumDataAgeSeconds"))
        value.maximum_data_age = Duration{j.at("maximumDataAgeSeconds").get<std::int64_t>()};
}

void to_json(json& j, const IndicatorBinding& value) {
    j = {{"id", value.id},
         {"seriesId", value.series_id},
         {"definitionId", value.definition_id},
         {"parameters", json::object()}};
    for (const auto& [key, item] : value.parameters)
        to_json(j["parameters"][key], item);
}

void from_json(const json& j, IndicatorBinding& value) {
    j.at("id").get_to(value.id);
    j.at("seriesId").get_to(value.series_id);
    j.at("definitionId").get_to(value.definition_id);
    for (const auto& [key, item] : j.at("parameters").items())
        value.parameters.emplace(key, parameter(item));
}

template <class T> void optional_to(json& j, const char* key, const std::optional<T>& value) {
    if (value)
        j[key] = *value;
}
template <class T> void optional_from(const json& j, const char* key, std::optional<T>& value) {
    if (j.contains(key))
        value = j.at(key).get<T>();
}

void to_json(json& j, const MarketComparison& v) {
    j = {{"seriesId", v.series_id}, {"field", v.field}, {"comparison", v.comparison}, {"value", v.value}};
}

void from_json(const json& j, MarketComparison& v) {
    j.at("seriesId").get_to(v.series_id);
    j.at("field").get_to(v.field);
    j.at("comparison").get_to(v.comparison);
    j.at("value").get_to(v.value);
}

void to_json(json& j, const IndicatorComparison& v) {
    j = {{"indicatorId", v.indicator_id}, {"outputId", v.output_id}, {"comparison", v.comparison}, {"value", v.value}};
}

void from_json(const json& j, IndicatorComparison& v) {
    j.at("indicatorId").get_to(v.indicator_id);
    j.at("outputId").get_to(v.output_id);
    j.at("comparison").get_to(v.comparison);
    j.at("value").get_to(v.value);
}

void to_json(json& j, const IndicatorCross& v) {
    j = {{"leftIndicatorId", v.left_indicator_id},
         {"leftOutputId", v.left_output_id},
         {"rightOutputId", v.right_output_id},
         {"direction", v.direction}};
    optional_to(j, "rightIndicatorId", v.right_indicator_id);
    optional_to(j, "constant", v.constant);
}

void from_json(const json& j, IndicatorCross& v) {
    j.at("leftIndicatorId").get_to(v.left_indicator_id);
    j.at("leftOutputId").get_to(v.left_output_id);
    j.at("rightOutputId").get_to(v.right_output_id);
    j.at("direction").get_to(v.direction);
    optional_from(j, "rightIndicatorId", v.right_indicator_id);
    optional_from(j, "constant", v.constant);
}

void to_json(json& j, const PatternOccurrence& v) {
    j = {{"indicatorId", v.indicator_id}};
    optional_to(j, "direction", v.direction);
}

void from_json(const json& j, PatternOccurrence& v) {
    j.at("indicatorId").get_to(v.indicator_id);
    optional_from(j, "direction", v.direction);
}

void to_json(json& j, const ElapsedTimeCondition& v) {
    j = {{"comparison", v.comparison}, {"durationSeconds", v.duration.count()}};
}

void from_json(const json& j, ElapsedTimeCondition& v) {
    j.at("comparison").get_to(v.comparison);
    v.duration = Duration{j.at("durationSeconds").get<std::int64_t>()};
}

void to_json(json& j, const ClosedBarCountCondition& v) {
    j = {{"comparison", v.comparison}, {"count", v.count}};
}

void from_json(const json& j, ClosedBarCountCondition& v) {
    j.at("comparison").get_to(v.comparison);
    j.at("count").get_to(v.count);
}

void to_json(json& j, const UnrealizedReturnCondition& v) {
    j = {{"kind", v.kind}, {"comparison", v.comparison}, {"percentage", v.percentage}};
}

void from_json(const json& j, UnrealizedReturnCondition& v) {
    j.at("kind").get_to(v.kind);
    j.at("comparison").get_to(v.comparison);
    j.at("percentage").get_to(v.percentage);
}

void condition_to(json& j, const std::shared_ptr<ConditionExpression>& value) {
    if (!value) {
        j = nullptr;
        return;
    }

    j = {{"id", value->id}, {"kind", value->kind}};
    std::visit(
        [&](const auto& predicate) {
            using T = std::decay_t<decltype(predicate)>;
            if constexpr (!std::is_same_v<T, std::monostate>)
                j["predicate"] = predicate;
        },
        value->predicate);
    if (!value->children.empty()) {
        j["children"] = json::array();
        for (const auto& child : value->children) {
            json encoded;
            condition_to(encoded, child);
            j["children"].push_back(std::move(encoded));
        }
    }
    if (value->kind == ConditionKind::Sequence) {
        j["sequence"] = json::object();
        if (value->sequence.maximum_elapsed)
            j["sequence"]["maximumElapsedSeconds"] = value->sequence.maximum_elapsed->count();
        optional_to(j["sequence"], "maximumClosedBars", value->sequence.maximum_closed_bars);
    }
}

std::shared_ptr<ConditionExpression> condition_from(const json& j) {
    if (j.is_null())
        return {};

    auto value = std::make_shared<ConditionExpression>();
    j.at("id").get_to(value->id);
    j.at("kind").get_to(value->kind);
    if (j.contains("children"))
        for (const auto& child : j.at("children"))
            value->children.push_back(condition_from(child));

    if (j.contains("sequence")) {
        const auto& s = j.at("sequence");
        if (s.contains("maximumElapsedSeconds"))
            value->sequence.maximum_elapsed = Duration{s.at("maximumElapsedSeconds").get<std::int64_t>()};
        optional_from(s, "maximumClosedBars", value->sequence.maximum_closed_bars);
    }

    if (j.contains("predicate")) {
        const auto& p = j.at("predicate");
        switch (value->kind) {
            case ConditionKind::MarketComparison:
                value->predicate = p.get<MarketComparison>();
                break;
            case ConditionKind::IndicatorComparison:
                value->predicate = p.get<IndicatorComparison>();
                break;
            case ConditionKind::IndicatorCross:
                value->predicate = p.get<IndicatorCross>();
                break;
            case ConditionKind::PatternOccurrence:
                value->predicate = p.get<PatternOccurrence>();
                break;
            case ConditionKind::ElapsedTime:
                value->predicate = p.get<ElapsedTimeCondition>();
                break;
            case ConditionKind::ClosedBarCount:
                value->predicate = p.get<ClosedBarCountCondition>();
                break;
            case ConditionKind::UnrealizedReturn:
                value->predicate = p.get<UnrealizedReturnCondition>();
                break;
            default:
                break;
        }
    }

    return value;
}

void to_json(json& j, const PricePolicy& v) {
    j = {{"kind", v.kind},
         {"value", v.value},
         {"indicatorId", v.indicator_id},
         {"outputId", v.output_id},
         {"offset", v.offset}};
}

void from_json(const json& j, PricePolicy& v) {
    j.at("kind").get_to(v.kind);
    v.value = j.value("value", 0.0);
    v.indicator_id = j.value("indicatorId", "");
    v.output_id = j.value("outputId", "");
    v.offset = j.value("offset", 0.0);
}

void to_json(json& j, const EntryOrder& v) {
    j = {{"kind", v.kind}};
    if (v.kind == EntryOrderKind::Limit) {
        j["limitPrice"] = v.limit_price;
        j["validityPrimaryBars"] = v.validity_primary_bars;
    }
}

void from_json(const json& j, EntryOrder& v) {
    j.at("kind").get_to(v.kind);
    v.limit_price = j.value("limitPrice", 0.0);
    v.validity_primary_bars = j.value("validityPrimaryBars", std::uint64_t{});
}

void to_json(json& j, const RuntimeAction& v) {
    j = {{"kind", v.kind}, {"exitReason", v.exit_reason}};
    optional_to(j, "price", v.price);
}

void from_json(const json& j, RuntimeAction& v) {
    j.at("kind").get_to(v.kind);
    v.exit_reason = j.value("exitReason", "");
    optional_from(j, "price", v.price);
}

json encode(const Definition& v) {
    json j = {{"formatVersion", strategy_format_version},
              {"id", v.id},
              {"displayName", v.display_name},
              {"version", v.version},
              {"description", v.description},
              {"direction", v.direction},
              {"primarySeriesId", v.primary_series_id},
              {"series", v.series},
              {"indicators", v.indicators},
              {"stopLoss", v.stop_loss},
              {"target", v.target}};
    j["entry"] = {{"order", v.entry.order}};
    condition_to(j["entry"]["condition"], v.entry.condition);
    j["exits"] = json::array();
    for (const auto& exit : v.exits) {
        json item = {{"id", exit.id}};
        condition_to(item["condition"], exit.condition);
        j["exits"].push_back(std::move(item));
    }
    j["runtimeRules"] = json::array();
    for (const auto& rule : v.runtime_rules) {
        json item = {{"id", rule.id}, {"priority", rule.priority}, {"actions", rule.actions}};
        condition_to(item["condition"], rule.condition);
        j["runtimeRules"].push_back(std::move(item));
    }

    return j;
}

Definition decode(const json& j) {
    Definition v;
    j.at("id").get_to(v.id);
    j.at("displayName").get_to(v.display_name);
    j.at("version").get_to(v.version);
    j.at("description").get_to(v.description);
    j.at("direction").get_to(v.direction);
    j.at("primarySeriesId").get_to(v.primary_series_id);
    j.at("series").get_to(v.series);
    j.at("indicators").get_to(v.indicators);
    v.entry.order = j.at("entry").at("order").get<EntryOrder>();
    v.entry.condition = condition_from(j.at("entry").at("condition"));
    v.stop_loss = j.at("stopLoss").get<PricePolicy>();
    v.target = j.at("target").get<PricePolicy>();
    for (const auto& item : j.at("exits")) {
        ExitCondition exit;
        item.at("id").get_to(exit.id);
        exit.condition = condition_from(item.at("condition"));
        v.exits.push_back(std::move(exit));
    }
    for (const auto& item : j.at("runtimeRules")) {
        RuntimeRule rule;
        item.at("id").get_to(rule.id);
        item.at("priority").get_to(rule.priority);
        item.at("actions").get_to(rule.actions);
        rule.condition = condition_from(item.at("condition"));
        v.runtime_rules.push_back(std::move(rule));
    }

    return v;
}
} // namespace Didrachma::Strategy::Core

namespace Didrachma::Strategy::Core {
JsonFileRepository::JsonFileRepository(std::filesystem::path path,
                                       std::vector<Analysis::Core::Indicator::Definition> catalog)
    : m_path(std::move(path)), m_catalog(std::move(catalog)) {}
std::vector<RepositoryError> JsonFileRepository::save(const Definition& definition) {
    std::vector<RepositoryError> result;
    for (const auto& item : validate(definition, m_catalog))
        result.push_back({item.path, item.message});
    if (!result.empty())
        return result;

    std::ofstream output(m_path);
    if (!output)
        return {{m_path.string(), "Could not open strategy file for writing"}};

    output << serialize(definition);
    if (!output)
        return {{m_path.string(), "Could not write strategy file"}};

    return {};
}

LoadResult JsonFileRepository::load() const {
    std::ifstream input(m_path);
    if (!input)
        return std::vector<RepositoryError>{{m_path.string(), "Could not open strategy file"}};

    std::ostringstream text;
    text << input.rdbuf();
    return deserialize(text.str(), m_catalog);
}

std::string serialize(const Definition& definition) {
    return encode(definition).dump(2) + "\n";
}

LoadResult deserialize(std::string_view text, IndicatorCatalog catalog) {
    try {
        const auto json = json::parse(text);
        const auto format = json.at("formatVersion").get<std::uint32_t>();
        if (format > strategy_format_version || format == 0)
            return std::vector<RepositoryError>{
                {"/formatVersion", "Unsupported strategy format version: " + std::to_string(format)}};

        auto document = json;
        std::vector<RepositoryWarning> warnings;
        if (format == 1) {
            document["formatVersion"] = strategy_format_version;
            document["entry"].erase("price");
            document["entry"]["order"] = {{"kind", "nextBarOpen"}};
            warnings.push_back({"/entry/price",
                                "Format 1 entry price was ignored by the legacy engine and was migrated explicitly "
                                "to a next-bar-open order"});
        }

        auto definition = decode(document);
        const auto invalid = validate(definition, catalog);
        if (!invalid.empty()) {
            std::vector<RepositoryError> errors;
            for (const auto& item : invalid)
                errors.push_back({item.path, item.message});
            return errors;
        }

        if (!warnings.empty())
            return MigratedDefinition{std::move(definition), std::move(warnings)};
        return definition;
    } catch (const nlohmann::json::exception& e) {
        return std::vector<RepositoryError>{{"/", e.what()}};
    } catch (const std::exception& e) {
        return std::vector<RepositoryError>{{"/", e.what()}};
    }
}

std::string serialize(const BacktestRequest& request) {
    json j = {{"requestFormatVersion", 1},
              {"strategy", encode(request.strategy_snapshot)},
              {"fromUtcSeconds", request.from.time_since_epoch().count()},
              {"throughUtcSeconds", request.through.time_since_epoch().count()},
              {"provider", {{"id", request.provider.id}, {"configuration", request.provider.values}}},
              {"execution",
               {{"quantity", request.execution.quantity},
                {"fixedPerFill", request.execution.fixed_per_fill},
                {"percentagePerFill", request.execution.percentage_per_fill},
                {"slippagePercentage", request.execution.slippage_percentage}}},
              {"fillModelVersion", request.fill_model_version}};
    optional_to(j, "subjectSymbol", request.subject_symbol);
    optional_to(j["execution"], "startingCapital", request.execution.starting_capital);
    return j.dump(2) + "\n";
}

BacktestRequestLoadResult deserialize_backtest_request(std::string_view text, IndicatorCatalog catalog) {
    try {
        const auto j = json::parse(text);
        if (j.at("requestFormatVersion").get<std::uint32_t>() != 1)
            return std::vector<RepositoryError>{{"/requestFormatVersion", "Unsupported backtest request version"}};
        BacktestRequest request;
        request.strategy_snapshot = decode(j.at("strategy"));
        optional_from(j, "subjectSymbol", request.subject_symbol);
        request.from = Market::Core::Time::UtcTimestamp{Duration{j.at("fromUtcSeconds").get<std::int64_t>()}};
        request.through = Market::Core::Time::UtcTimestamp{Duration{j.at("throughUtcSeconds").get<std::int64_t>()}};
        request.provider.id = j.at("provider").at("id").get<std::string>();
        request.provider.values = j.at("provider").at("configuration").get<std::map<std::string, std::string>>();
        const auto& execution = j.at("execution");
        execution.at("quantity").get_to(request.execution.quantity);
        optional_from(execution, "startingCapital", request.execution.starting_capital);
        execution.at("fixedPerFill").get_to(request.execution.fixed_per_fill);
        execution.at("percentagePerFill").get_to(request.execution.percentage_per_fill);
        execution.at("slippagePercentage").get_to(request.execution.slippage_percentage);
        j.at("fillModelVersion").get_to(request.fill_model_version);
        const auto invalid = validate(request, catalog);
        if (!invalid.empty()) {
            std::vector<RepositoryError> errors;
            for (const auto& item : invalid)
                errors.push_back({item.path, item.message});
            return errors;
        }
        return request;
    } catch (const std::exception& e) {
        return std::vector<RepositoryError>{{"/", e.what()}};
    }
}
} // namespace Didrachma::Strategy::Core
