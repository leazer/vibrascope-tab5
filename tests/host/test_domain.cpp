#include "test_support.hpp"

#include "vhm/domain/sample_batch.hpp"
#include "vhm/domain/sample_source.hpp"

using vhm::Quality;
using vhm::SampleBatch;
using vhm::StreamId;

void run_domain_tests()
{
    SampleBatch batch{};
    batch.stream = StreamId::Adxl;

    EXPECT_FALSE(batch.usable_for_primary_analysis());

    batch.count = 1;
    EXPECT_TRUE(batch.usable_for_primary_analysis());

    batch.count = static_cast<std::uint16_t>(vhm::kMaxSamplesPerBatch + 1);
    EXPECT_FALSE(batch.usable_for_primary_analysis());
    batch.count = 1;

    batch.quality = Quality::CrcError;
    EXPECT_FALSE(batch.usable_for_primary_analysis());

    batch.quality = Quality::SequenceGap;
    EXPECT_FALSE(batch.usable_for_primary_analysis());

    batch.quality = Quality::TimeUncertain | Quality::AuxiliaryDegraded;
    EXPECT_FALSE(batch.usable_for_primary_analysis());

    batch.quality = Quality::AuxiliaryDegraded;
    EXPECT_TRUE(batch.usable_for_primary_analysis());

    batch.stream = StreamId::Icm;
    batch.quality = Quality::None;
    EXPECT_FALSE(batch.usable_for_primary_analysis());

    EXPECT_TRUE(vhm::kMaxSamplesPerBatch == 108);
}
