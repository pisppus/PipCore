#pragma once

#include <cstdint>
#include <cstddef>

namespace pipcore::audio
{

    enum PACFlags : uint16_t
    {
        PAC_FLAG_LOOP = (1u << 0),
    };

    enum PACBlockMode : uint8_t
    {
        PAC_MODE_SILENCE = 0,
        PAC_MODE_ADPCM2 = 1,
        PAC_MODE_ADPCM4 = 2,
        PAC_MODE_ADPCM6 = 3,
        PAC_MODE_HOLD = 4,
    };

    struct alignas(4) PACHeader
    {
        char magic[4];
        uint16_t version;
        uint16_t flags;
        uint32_t sourceRate;
        uint32_t nativeRate;
        uint32_t frameCount;
        uint32_t loopStart;
        uint32_t loopEnd;
        uint32_t blockCount;
        uint32_t dataOffset;
        uint32_t dataSize;
        uint32_t reserved1;
        uint32_t reserved2;

        [[nodiscard]] inline bool isValid() const noexcept
        {
            return magic[0] == 'P' && magic[1] == 'A' && magic[2] == 'C' && magic[3] == '!';
        }
        [[nodiscard]] inline bool isLoop() const noexcept { return (flags & PAC_FLAG_LOOP) != 0; }
    };
    static_assert(sizeof(PACHeader) == 48, "PACHeader must be exactly 48 bytes");

    static constexpr uint32_t PAC_BLOCK_FRAMES = 256;
    inline constexpr uint16_t PacPayload2 = PAC_BLOCK_FRAMES / 4;
    inline constexpr uint16_t PacPayload4 = PAC_BLOCK_FRAMES / 2;
    inline constexpr uint16_t PacPayload6 = (PAC_BLOCK_FRAMES * 6U) / 8U;

    inline constexpr int8_t kIndexTable[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

    inline constexpr int8_t kIndexTable2[4] = {-1, 1, -1, 2};

    inline constexpr int16_t kStepTable[89] = {
        7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28,
        31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
        130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494,
        544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
        2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
        9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

    [[nodiscard]] inline constexpr int16_t clampSat16(int32_t v) noexcept
    {
        if (v > 32767)
            return 32767;
        if (v < -32768)
            return -32768;
        return static_cast<int16_t>(v);
    }

    [[nodiscard]] inline int16_t decodeAdpcm4(uint8_t code, int16_t &predictor, int8_t &stepIndex) noexcept
    {
        int16_t step = kStepTable[stepIndex];
        int32_t diff = step >> 3;
        if (code & 4)
            diff += step;
        if (code & 2)
            diff += (step >> 1);
        if (code & 1)
            diff += (step >> 2);
        if (code & 8)
            predictor = clampSat16(static_cast<int32_t>(predictor) - diff);
        else
            predictor = clampSat16(static_cast<int32_t>(predictor) + diff);
        stepIndex += kIndexTable[code & 7];
        if (stepIndex < 0)
            stepIndex = 0;
        else if (stepIndex > 88)
            stepIndex = 88;
        return predictor;
    }

    [[nodiscard]] inline int16_t decodeAdpcm6(uint8_t code, int16_t &predictor, int8_t &stepIndex) noexcept
    {
        int16_t step = kStepTable[stepIndex];
        int32_t diff = step >> 5;
        if (code & 16)
            diff += step;
        if (code & 8)
            diff += (step >> 1);
        if (code & 4)
            diff += (step >> 2);
        if (code & 2)
            diff += (step >> 3);
        if (code & 1)
            diff += (step >> 4);
        if (code & 32)
            predictor = clampSat16(static_cast<int32_t>(predictor) - diff);
        else
            predictor = clampSat16(static_cast<int32_t>(predictor) + diff);
        uint8_t adaptIdx = (code >> 1) & 0x0F;
        stepIndex += kIndexTable[adaptIdx];
        if (stepIndex < 0)
            stepIndex = 0;
        else if (stepIndex > 88)
            stepIndex = 88;
        return predictor;
    }

    [[nodiscard]] inline int16_t decodeAdpcm2(uint8_t code, int16_t &predictor, int8_t &stepIndex) noexcept
    {
        int16_t step = kStepTable[stepIndex];
        int32_t delta = (code & 1) ? step : (step >> 2);
        if (code & 2)
            predictor = clampSat16(static_cast<int32_t>(predictor) - delta);
        else
            predictor = clampSat16(static_cast<int32_t>(predictor) + delta);
        stepIndex += kIndexTable2[code & 0x03];
        if (stepIndex < 0)
            stepIndex = 0;
        else if (stepIndex > 88)
            stepIndex = 88;
        return predictor;
    }

    struct PacVoiceState
    {

        const uint8_t *data = nullptr;
        const uint8_t *dataEnd = nullptr;
        uint32_t frameCount = 0;
        uint32_t loopStart = 0;
        uint32_t loopEnd = 0;
        bool isLoop = false;

        const uint8_t *blockPtr = nullptr;
        uint32_t currentFrame = 0;
        uint16_t sampleInBlock = 0;

        uint8_t pacMode = PAC_MODE_SILENCE;
        uint16_t blockPayloadBytes = 0;
        int16_t pacPredictor = 0;
        int8_t pacStepIndex = 0;

        uint32_t srcStep = 65536;
        uint32_t srcPhase = 0;
        int16_t prevSrcSample = 0;
        int16_t curSrcSample = 0;
        bool firstSample = true;
        bool isPassthrough = true;
        bool active = false;

        void resetToLoopStart() noexcept;
        void loadNextBlockHeader() noexcept;

        [[nodiscard]] inline int16_t decodeSourceSample() noexcept
        {

            const bool looping = isLoop && loopEnd > loopStart;
            const uint32_t endFrame = looping ? loopEnd : frameCount;
            if (currentFrame >= endFrame)
            {
                if (looping)
                    resetToLoopStart();
                else
                {
                    active = false;
                    return 0;
                }
            }

            if (sampleInBlock == 0)
                loadNextBlockHeader();

            int16_t out = 0;
            switch (pacMode)
            {
            case PAC_MODE_SILENCE:
                out = 0;
                break;
            case PAC_MODE_HOLD:
                out = pacPredictor;
                break;
            case PAC_MODE_ADPCM4:
                if (sampleInBlock == 0)
                {
                    out = pacPredictor;
                }
                else
                {
                    uint16_t codeIdx = sampleInBlock - 1;
                    uint16_t byteIdx = codeIdx >> 1;
                    if (blockPtr + byteIdx < dataEnd)
                    {
                        uint8_t b = blockPtr[byteIdx];
                        uint8_t code = (codeIdx & 1) ? (b >> 4) : (b & 0x0F);
                        out = decodeAdpcm4(code, pacPredictor, pacStepIndex);
                    }
                    else
                        out = pacPredictor;
                }
                break;
            case PAC_MODE_ADPCM2:
                if (sampleInBlock == 0)
                {
                    out = pacPredictor;
                }
                else
                {
                    uint16_t codeIdx = sampleInBlock - 1;
                    uint16_t byteIdx = codeIdx >> 2;
                    if (blockPtr + byteIdx < dataEnd)
                    {
                        uint8_t b = blockPtr[byteIdx];
                        uint8_t shift = (codeIdx & 3) << 1;
                        uint8_t code = (b >> shift) & 0x03;
                        out = decodeAdpcm2(code, pacPredictor, pacStepIndex);
                    }
                    else
                        out = pacPredictor;
                }
                break;
            case PAC_MODE_ADPCM6:
                if (sampleInBlock == 0)
                {
                    out = pacPredictor;
                }
                else
                {
                    uint16_t codeIdx = sampleInBlock - 1;
                    uint16_t groupIdx = codeIdx >> 2;
                    uint16_t inGroup = codeIdx & 3;
                    uint16_t byteBase = groupIdx * 3;
                    if (blockPtr + byteBase + 2 < dataEnd)
                    {
                        uint32_t chunk = blockPtr[byteBase] | (static_cast<uint32_t>(blockPtr[byteBase + 1]) << 8) |
                                         (static_cast<uint32_t>(blockPtr[byteBase + 2]) << 16);
                        uint8_t code = (chunk >> (inGroup * 6)) & 0x3F;
                        out = decodeAdpcm6(code, pacPredictor, pacStepIndex);
                    }
                    else
                        out = pacPredictor;
                }
                break;
            default:
                out = 0;
                break;
            }

            sampleInBlock++;
            currentFrame++;

            if (sampleInBlock >= PAC_BLOCK_FRAMES)
            {
                sampleInBlock = 0;
                blockPtr += blockPayloadBytes;
            }
            return out;
        }

        inline void decodeMixFrame(int16_t &outL, int16_t &outR) noexcept
        {
            if (isPassthrough)
            {
                int16_t s = decodeSourceSample();
                outL = outR = active ? s : 0;
                return;
            }

            if (firstSample)
            {
                prevSrcSample = decodeSourceSample();
                curSrcSample = prevSrcSample;
                firstSample = false;
                srcPhase = 0;
            }

            while (srcPhase >= 65536u)
            {
                srcPhase -= 65536u;
                prevSrcSample = curSrcSample;
                curSrcSample = decodeSourceSample();
                if (!active)
                {
                    outL = outR = 0;
                    return;
                }
            }

            int32_t frac = static_cast<int32_t>(srcPhase);
            int32_t diff = static_cast<int32_t>(curSrcSample) - static_cast<int32_t>(prevSrcSample);
            int32_t out = static_cast<int32_t>(prevSrcSample) + ((diff * frac) >> 16);
            outL = outR = clampSat16(out);
            srcPhase += srcStep;
        }
    };
}
