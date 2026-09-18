#include "Parser.h"
#include "Polling.h"
#include "Request.h"

#include <Didrachma/market/providers/yahoo/Interval.h>
#include <Didrachma/market/providers/yahoo/Provider.h>
#include <array>
#include <condition_variable>
#include <curl/curl.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>

using namespace Didrachma::Market::Core::Provider;
using namespace Didrachma::Market::Core::Time;
using namespace Didrachma::Market::Core::Series;
namespace fs = std::filesystem;

namespace Didrachma::Market::Providers::Yahoo {
namespace {
using enum Unit;
constexpr std::array supported_intervals{
    Interval{{1, Minute}, {1, Minute}, "1m", "1 Minute", std::chrono::days{8}},
    Interval{{2, Minute}, {2, Minute}, "2m", "2 Minutes", std::chrono::days{60}},
    Interval{{5, Minute}, {5, Minute}, "5m", "5 Minutes", std::chrono::days{60}},
    Interval{{10, Minute}, {5, Minute}, "5m", "10 Minutes", std::chrono::days{60}},
    Interval{{15, Minute}, {15, Minute}, "15m", "15 Minutes", std::chrono::days{60}},
    Interval{{30, Minute}, {30, Minute}, "30m", "30 Minutes", std::chrono::days{60}},
    Interval{{1, Hour}, {1, Hour}, "1h", "1 Hour", std::chrono::days{730}},
    Interval{{90, Minute}, {90, Minute}, "90m", "90 Minutes", std::chrono::days{60}},
    Interval{{1, Day}, {1, Day}, "1d", "1 Day", {}},
    Interval{{5, Day}, {5, Day}, "5d", "5 Days", {}},
    Interval{{7, Day}, {7, Day}, "1wk", "1 Week", {}},
    Interval{{30, Day}, {30, Day}, "1mo", "1 Month", {}},
    Interval{{90, Day}, {90, Day}, "3mo", "3 Months", {}},
};
} // namespace

std::span<const Interval> intervals() {
    return supported_intervals;
}

const Interval* find_interval(Frame frame) {
    const auto match = std::ranges::find(supported_intervals, frame, &Interval::frame);
    return match == supported_intervals.end() ? nullptr : &*match;
}

HistoryValidation validate_history(Frame frame, Range range, UtcTimestamp now) {
    const auto* interval = find_interval(frame);
    if (!interval)
        return {false, "Yahoo Finance does not support this timeframe", {}};

    if (range.begin >= range.end)
        return {false, "History start must be before its exclusive end", {}};

    if (range.begin >= now)
        return {false, "Yahoo Finance history cannot start in the future", {}};

    if (interval->history_reach) {
        const auto earliest = now - *interval->history_reach;
        if (range.begin < earliest)
            return {false,
                    "Requested history starts before Yahoo's " + std::to_string(interval->history_reach->count()) +
                        "-day limit for the source interval " + std::string{interval->value},
                    earliest};
    }

    return {true, {}, {}};
}

Range suggested_history_range(Frame frame, UtcTimestamp now) {
    constexpr auto default_history = std::chrono::days{365 * 5};
    const auto end = std::chrono::floor<std::chrono::days>(now) + std::chrono::days{1};
    const auto* interval = find_interval(frame);
    if (interval && interval->history_reach)
        return {std::chrono::ceil<std::chrono::days>(now - *interval->history_reach), end};

    return {end - default_history, end};
}

std::time_t unix_time(UtcTimestamp timestamp) {
    return std::chrono::system_clock::to_time_t(timestamp);
}

namespace Intern {
std::string cache_filename(const HistoryRequest& request) {
    const auto* interval = find_interval(request.key.timeframe);
    if (!interval)
        return {};
    return request.key.instrument + "_" + std::to_string(request.key.timeframe.quantity) + "_" +
           std::to_string(static_cast<int>(request.key.timeframe.unit)) + "_from_" + std::string{interval->value} +
           "_" + std::to_string(unix_time(request.range.begin)) + "_" + std::to_string(unix_time(request.range.end)) +
           ".json";
}
} // namespace Intern

class Provider::Impl {
public:
    CURL* curl{};
    std::string error;
    std::mutex transport_mutex;

    Impl() {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK || !(curl = curl_easy_init()))
            throw std::runtime_error("Unable to initialize libcurl");
    }
    ~Impl() {
        curl_easy_cleanup(curl);
        curl_global_cleanup();
    }

    std::string build_url(const HistoryRequest& request);
    bool download(const std::string& url, const fs::path& filename);
};

std::string Provider::Impl::build_url(const HistoryRequest& request) {
    const auto* interval = find_interval(request.key.timeframe);
    if (!interval)
        return {};

    char* encoded =
        curl_easy_escape(curl, request.key.instrument.c_str(), static_cast<int>(request.key.instrument.size()));
    if (!encoded)
        return {};

    const auto url =
        std::format("https://query1.finance.yahoo.com/v8/finance/chart/"
                    "{}?period1={}&period2={}&interval={}&events=history&includeAdjustedClose=true",
                    encoded, unix_time(request.range.begin), unix_time(request.range.end), interval->value);
    curl_free(encoded);

    return url;
}

std::size_t write_file(char* data, std::size_t size, std::size_t count, void* userdata) {
    auto& output = *static_cast<std::ofstream*>(userdata);

    const auto bytes = size * count;
    output.write(data, static_cast<std::streamsize>(bytes));

    return output ? bytes : 0;
}

bool Provider::Impl::download(const std::string& url, const fs::path& filename) {
    auto temporary = filename;
    temporary += ".part";

    std::ofstream output(temporary, std::ios::binary);
    if (!output) {
        error = "Could not create cache file";
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 Didrachma/0.1");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 15000L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &output);

    const auto result = curl_easy_perform(curl);

    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    output.close();

    if (result != CURLE_OK || status != 200) {
        fs::remove(temporary);
        error = "Yahoo Finance download failed";
        return false;
    }

    std::error_code ec;
    fs::rename(temporary, filename, ec);
    if (ec) {
        error = ec.message();
        return false;
    }

    return true;
}

Provider::Provider() : m_impl(std::make_unique<Impl>()) {}
Provider::~Provider() = default;
Provider::Provider(Provider&&) noexcept = default;

Provider& Provider::operator=(Provider&&) noexcept = default;

HistoryResult Provider::load_history(const HistoryRequest& request) {
    std::scoped_lock transport_lock(m_impl->transport_mutex);
    const auto now = std::chrono::time_point_cast<UtcTimestamp::duration>(std::chrono::system_clock::now());
    const auto validation = validate_history(request.key.timeframe, request.range, now);
    if (!validation.valid)
        return Error{validation.message};

    const auto url = m_impl->build_url(request);
    if (url.empty())
        return Error{"Yahoo Finance does not support this timeframe"};

    const auto filename = Intern::cache_filename(request);

    if (!fs::exists(filename) && !m_impl->download(url, filename))
        return Error{m_impl->error};
    try {
        std::ifstream input(filename);
        const auto& json = nlohmann::json::parse(input);
        return Intern::parse_history(json, request.key.timeframe);
    } catch (const Intern::ResponseError& e) {
        fs::remove(filename);
        return Error{e.what(), e.code()};
    } catch (const std::exception& e) {
        fs::remove(filename);
        return Error{e.what()};
    }
}

namespace Intern {
class PollingSubscription final : public Subscription {
    std::condition_variable_any m_wait;
    std::jthread m_worker;

public:
    PollingSubscription(Provider& provider, Key key, UpdateHandler handler)
        : m_worker([&provider, key = std::move(key), handler = std::move(handler), this](std::stop_token stop) mutable {
              std::optional<Bar> previous;
              while (!stop.stop_requested()) {
                  const auto now =
                      std::chrono::time_point_cast<UtcTimestamp::duration>(std::chrono::system_clock::now());
                  auto result = provider.load_history({key, polling_range(key.timeframe, now)});
                  if (const auto* bars = std::get_if<std::vector<Bar>>(&result))
                      for (auto& update : polling_updates(key, previous, *bars, now))
                          handler(std::move(update));

                  std::mutex mutex;
                  std::unique_lock lock(mutex);
                  m_wait.wait_for(lock, stop, std::chrono::seconds{30}, [] { return false; });
              }
          }) {}

    ~PollingSubscription() override {
        m_worker.request_stop();
        m_wait.notify_all();
    }
};
} // namespace Intern

std::unique_ptr<Subscription> Provider::subscribe(const Key& key, UpdateHandler handler) {
    if (!find_interval(key.timeframe) || !handler)
        return {};

    return std::make_unique<Intern::PollingSubscription>(*this, key, std::move(handler));
}
} // namespace Didrachma::Market::Providers::Yahoo
