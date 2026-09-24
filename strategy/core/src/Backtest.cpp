#include <Didrachma/strategy/core/Backtest.h>
#include <algorithm>
#include <cmath>

namespace Didrachma::Strategy::Core {
std::vector<ValidationError> validate(const BacktestRequest& request, IndicatorCatalog catalog) {
    auto errors = validate(request.strategy_snapshot, catalog);
    const auto add = [&](std::string path, std::string message) {
        errors.push_back({ValidationCode::InvalidBacktestRequest, std::move(path), std::move(message)});
    };

    const auto needs_subject = std::ranges::any_of(request.strategy_snapshot.series, [](const auto& series) {
        return series.instrument.kind == InstrumentKind::Subject;
    });
    if (needs_subject && (!request.subject_symbol || request.subject_symbol->empty()))
        add("/subjectSymbol", "Subject symbol is required by the strategy snapshot");
    if (request.subject_symbol && request.subject_symbol->empty())
        add("/subjectSymbol", "Subject symbol cannot be empty");
    if (request.from > request.through)
        add("/through", "Inclusive through time must not precede from time");
    if (request.provider.id.empty())
        add("/provider/id", "Provider id is required");
    if (!std::isfinite(request.execution.quantity) || request.execution.quantity <= 0)
        add("/execution/quantity", "Quantity must be finite and positive");
    if (request.execution.starting_capital &&
        (!std::isfinite(*request.execution.starting_capital) || *request.execution.starting_capital <= 0))
        add("/execution/startingCapital", "Starting capital must be finite and positive");
    if (!std::isfinite(request.execution.fixed_per_fill) || request.execution.fixed_per_fill < 0 ||
        !std::isfinite(request.execution.percentage_per_fill) || request.execution.percentage_per_fill < 0 ||
        !std::isfinite(request.execution.slippage_percentage) || request.execution.slippage_percentage < 0)
        add("/execution", "Costs and slippage must be finite and non-negative");
    if (request.fill_model_version == 0)
        add("/fillModelVersion", "Fill model version must be positive");
    return errors;
}
} // namespace Didrachma::Strategy::Core
