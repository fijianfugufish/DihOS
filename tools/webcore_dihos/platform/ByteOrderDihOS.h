#pragma once
#include <wtf/ByteOrder.h>
#include <cstdint>

#if OS(DIHOS)
// Pure integer conversions, not socket/OS operations. Keep these local to
// the native WebCore consumers; no host networking headers are imported.
inline uint16_t ntohs(uint16_t value)
{
    if constexpr (std::endian::native == std::endian::little)
        return std::byteswap(value);
    return value;
}
inline uint32_t ntohl(uint32_t value)
{
    if constexpr (std::endian::native == std::endian::little)
        return std::byteswap(value);
    return value;
}
inline uint16_t htons(uint16_t value) { return ntohs(value); }
inline uint32_t htonl(uint32_t value) { return ntohl(value); }
#endif
