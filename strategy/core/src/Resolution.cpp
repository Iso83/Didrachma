#include <Didrachma/strategy/core/Evaluation.h>

namespace Didrachma::Strategy::Core {
Resolution resolve(const Definition& definition, std::optional<std::string_view> subject) {
    Resolution result;
    for (const auto& binding : definition.series) {
        std::string instrument;
        if (binding.instrument.kind == InstrumentKind::Subject) {
            if (!subject || subject->empty()) {
                result.errors.push_back({binding.id, "subject symbol is required"});
                continue;
            }

            instrument = *subject;
        } else {
            instrument = binding.instrument.symbol;
        }

        result.series.push_back({binding.id, {binding.provider_id, std::move(instrument), binding.timeframe}});
    }
    return result;
}
} // namespace Didrachma::Strategy::Core
