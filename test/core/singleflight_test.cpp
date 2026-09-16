#include <gtest/gtest.h>

#include "singleflight.h"

#include <stdexcept>
#include <string>


TEST(SingleFlightTests, FailedCallIsRemovedSoTheKeyCanBeRetried)
{
    SingleFlight singleFlight;
    int callCount = 0;

    EXPECT_THROW(
        singleFlight.Do("key", [&]() -> ByteViewOptional
                        {
                            ++callCount;
                            throw std::runtime_error("load failed"); //
                        }),
        std::runtime_error);

    const auto result = singleFlight.Do("key", [&]() -> ByteViewOptional
                                        {
                                            ++callCount;
                                            return ByteView("value"); //
                                        });

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ("value", result->toString());
    EXPECT_EQ(2, callCount);
}
