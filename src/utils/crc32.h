#ifndef _DCACHE_CRC32_H_
#define _DCACHE_CRC32_H_

#include <string>
#include <cstdint>


namespace dcache
{

    // CRC32 IEEE 哈希函数，兼容 Go 的 crc32.ChecksumIEEE。
    uint32_t crc32IEEE(const std::string &data);

} // namespace dcache

#endif
