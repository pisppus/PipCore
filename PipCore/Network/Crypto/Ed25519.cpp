#include "Ed25519.hpp"

extern "C"
{
#include "ed25519/ed25519.h"
}

namespace pipcore::ota
{
    bool ed25519Verify(const uint8_t signature[64], const uint8_t *message, size_t messageLen,
                       const uint8_t publicKey[32]) noexcept
    {
        if (!signature || !message || !publicKey)
            return false;
        return ed25519_verify(signature, message, messageLen, publicKey) == 1;
    }
}
