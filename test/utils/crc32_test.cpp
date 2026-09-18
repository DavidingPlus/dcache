#include <gtest/gtest.h>

#include "crc32.h"

#include <cstdint>
#include <string>


namespace
{

    using dcache::crc32IEEE;

} // namespace


TEST(Crc32IEEE, MatchesStandardCheckValue)
{
    // CRC32/IEEE 的标准校验字符串。
    EXPECT_EQ(0xCBF43926u, crc32IEEE("123456789"));
}

TEST(Crc32IEEE, HandlesEmptyInput)
{
    EXPECT_EQ(0x00000000u, crc32IEEE(""));
}

TEST(Crc32IEEE, MatchesKnownTextVectors)
{
    EXPECT_EQ(0x3610A686u, crc32IEEE("hello"));
    EXPECT_EQ(0x414FA339u, crc32IEEE("The quick brown fox jumps over the lazy dog"));
}

TEST(Crc32IEEE, PreservesEmbeddedNullBytes)
{
    const std::string data{'a', '\0', 'b'};

    // 该值对应字节序列 [0x61, 0x00, 0x62]。
    EXPECT_EQ(0x15E87871u, crc32IEEE(data));
}

TEST(Crc32IEEE, IsDeterministic)
{
    const std::string data = "node-a#0";

    EXPECT_EQ(crc32IEEE(data), crc32IEEE(data));
}
