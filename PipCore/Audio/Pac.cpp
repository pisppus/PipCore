#include "Audio/Pac.hpp"

namespace pipcore::audio
{

    void PacVoiceState::resetToLoopStart() noexcept
    {
        const uint32_t targetBlock = loopStart / PAC_BLOCK_FRAMES;
        const uint8_t *p = data;
        const uint8_t *end = dataEnd;
        for (uint32_t b = 0; b < targetBlock && p < end; ++b)
        {
            uint8_t hdrByte = *p++;
            uint8_t mode = hdrByte >> 4;
            switch (mode)
            {
            case PAC_MODE_SILENCE:
                break;
            case PAC_MODE_HOLD:
                p += 2;
                break;
            case PAC_MODE_ADPCM2:
                p += 3 + PacPayload2;
                break;
            case PAC_MODE_ADPCM4:
                p += 3 + PacPayload4;
                break;
            case PAC_MODE_ADPCM6:
                p += 3 + PacPayload6;
                break;
            default:
                p = end;
                break;
            }
        }
        blockPtr = p;
        currentFrame = loopStart;
        sampleInBlock = 0;
        firstSample = true;
        srcPhase = 0;
    }

    void PacVoiceState::loadNextBlockHeader() noexcept
    {
        if (blockPtr >= dataEnd)
        {
            pacMode = PAC_MODE_SILENCE;
            pacPredictor = 0;
            blockPayloadBytes = 0;
            sampleInBlock = 0;
            return;
        }

        uint8_t hdr = *blockPtr++;
        pacMode = hdr >> 4;

        switch (pacMode)
        {
        case PAC_MODE_SILENCE:
            pacPredictor = 0;
            blockPayloadBytes = 0;
            break;
        case PAC_MODE_HOLD:
            if (blockPtr + 2 <= dataEnd)
            {
                pacPredictor = static_cast<int16_t>(blockPtr[0] | (blockPtr[1] << 8));
                blockPtr += 2;
            }
            else
                pacPredictor = 0;
            blockPayloadBytes = 0;
            break;
        case PAC_MODE_ADPCM2:
            if (blockPtr + 3 <= dataEnd)
            {
                pacPredictor = static_cast<int16_t>(blockPtr[0] | (blockPtr[1] << 8));
                pacStepIndex = static_cast<int8_t>(blockPtr[2]);
                blockPtr += 3;
                blockPayloadBytes = PacPayload2;
            }
            else
            {
                pacMode = PAC_MODE_SILENCE;
                pacPredictor = 0;
                blockPayloadBytes = 0;
            }
            break;
        case PAC_MODE_ADPCM4:
            if (blockPtr + 3 <= dataEnd)
            {
                pacPredictor = static_cast<int16_t>(blockPtr[0] | (blockPtr[1] << 8));
                pacStepIndex = static_cast<int8_t>(blockPtr[2]);
                blockPtr += 3;
                blockPayloadBytes = PacPayload4;
            }
            else
            {
                pacMode = PAC_MODE_SILENCE;
                pacPredictor = 0;
                blockPayloadBytes = 0;
            }
            break;
        case PAC_MODE_ADPCM6:
            if (blockPtr + 3 <= dataEnd)
            {
                pacPredictor = static_cast<int16_t>(blockPtr[0] | (blockPtr[1] << 8));
                pacStepIndex = static_cast<int8_t>(blockPtr[2]);
                blockPtr += 3;
                blockPayloadBytes = PacPayload6;
            }
            else
            {
                pacMode = PAC_MODE_SILENCE;
                pacPredictor = 0;
                blockPayloadBytes = 0;
            }
            break;
        default:
            pacMode = PAC_MODE_SILENCE;
            pacPredictor = 0;
            blockPayloadBytes = 0;
            break;
        }

        sampleInBlock = 0;
    }
}
