#ifndef _CONSISTENT_HASH_H_
#define _CONSISTENT_HASH_H_

#include <string>
#include <cstdint>


namespace kcache
{

    // CRC32 IEEE 哈希函数，兼容 Go 的 crc32.ChecksumIEEE。
    uint32_t crc32IEEE(const std::string &data);

} // namespace kcache


#endif
