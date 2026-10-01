#pragma once

#include <cstddef>
#include <cstdint>

namespace pipcore::ota
{
    [[nodiscard]] bool ed25519Verify(const uint8_t signature[64], const uint8_t *message, size_t messageLen,
                                     const uint8_t publicKey[32]) noexcept;
}
