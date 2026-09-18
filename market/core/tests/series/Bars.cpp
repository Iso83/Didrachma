#include "TestAssert.h"
#include "common/market/Series.h"

#include <Didrachma/market/core/series/Bars.h>

using namespace Didrachma::Testing;

int test_update_rules_and_revisions() {
    Bars series(key());
    CPPTEST_ASSERT(series.apply({key(), BarUpdateKind::Backfill, {bar(20), bar(0), bar(10), bar(10)}}));
    CPPTEST_ASSERT(series.bars().size() == 3 && series.bars()[0].open_time == at(0));
    CPPTEST_ASSERT(series.revision() == 1 && series.dirty_range()->begin == at(0));
    series.clear_dirty_range();

    CPPTEST_ASSERT(!series.apply({key(), BarUpdateKind::AppendClosed, {bar(10, BarState::Closed, 99)}}));
    CPPTEST_ASSERT(series.revision() == 1);
    CPPTEST_ASSERT(series.apply({key(), BarUpdateKind::Backfill, {bar(10, BarState::Forming, 5)}}));
    CPPTEST_ASSERT(series.apply({key(), BarUpdateKind::ReplaceForming, {bar(10, BarState::Closed, 6)}}));
    CPPTEST_ASSERT(series.bars()[1].state == BarState::Closed && series.bars()[1].close == 6);
    CPPTEST_ASSERT(series.revision() == 3);
    series.clear_dirty_range();

    CPPTEST_ASSERT(series.apply({key(), BarUpdateKind::Reset, {bar(100)}}));
    CPPTEST_ASSERT(series.bars().size() == 1 && series.revision() == 4);
    CPPTEST_ASSERT(series.dirty_range()->begin == at(0) && series.dirty_range()->end == at(110));
    return 0;
}

int test_identity_rejections_and_duplicate_precedence() {
    CPPTEST_ASSERT((Frame{10, Unit::Minute} != Frame{1, Unit::Hour}));
    CPPTEST_ASSERT((Frame{1, Unit::Hour} != Frame{1, Unit::Day}));
    CPPTEST_ASSERT((Key{"fake", "TEST", {10, Unit::Minute}} == key()));

    Bars series(key());
    CPPTEST_ASSERT(!series.apply({{"other", "TEST", {10, Unit::Minute}}, BarUpdateKind::Reset, {bar(0)}}));
    CPPTEST_ASSERT(!series.apply({key(), BarUpdateKind::AppendClosed, {bar(0, BarState::Forming)}}));

    CPPTEST_ASSERT(series.apply(
        {key(), BarUpdateKind::Backfill, {bar(10, BarState::Closed, 1), bar(0), bar(10, BarState::Closed, 7)}}));
    CPPTEST_ASSERT(series.bars().size() == 2);
    CPPTEST_ASSERT(series.bars()[1].close == 7);
    return 0;
}

int main() {
    CPPTEST_RUN(test_update_rules_and_revisions);
    CPPTEST_RUN(test_identity_rejections_and_duplicate_precedence);
    return 0;
}
