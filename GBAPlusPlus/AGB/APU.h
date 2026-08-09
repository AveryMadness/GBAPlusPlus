#pragma once
#include <array>
#include <cstdint>

class APU
{
public:
    APU(std::array<uint8_t, 1024>& ioRegisters);

    void Reset();

    //sounds fifos
    static constexpr uint32_t FIFO_A_OFFSET = 0x0A0;
    static constexpr uint32_t FIFO_B_OFFSET = 0x0A4;
    static constexpr uint32_t FIFO_DEPTH = 32;

    static bool IsFifoOffset(uint32_t offset);

    void WriteFifo(uint32_t offset, uint8_t value);

    static constexpr uint32_t OUTPUT_SAMPLE_RATE = 32768;
    static constexpr uint32_t CYCLES_PER_OUTPUT_SAMPLE = 512;

    struct RefillRequest
    {
        bool fifoA = false;
        bool fifoB = false;
    };
    
    RefillRequest Tick(uint8_t overflowMask);

    size_t ReadSamples(int16_t* destination, size_t maxFrames);
    size_t GetQueuedFrames() const { return frameCount; }

    void OnSoundCntHWrite();
    void OnSoundCntXWrite();

    int8_t GetLatchedSampleA() const { return latchedSampleA; }
    int8_t GetLatchedSampleB() const { return latchedSampleB; }

    uint32_t GetFifoLevelA() const { return fifoA.count; }
    uint32_t GetFifoLevelB() const { return fifoB.count; }

private:
    struct Fifo
    {
        std::array<int8_t, FIFO_DEPTH> data{};
        uint32_t readIndex = 0;
        uint32_t writeIndex = 0;
        uint32_t count = 0;

        void Clear();
        void Push(int8_t sample);
        bool Pop(int8_t& outSample);
    };

    Fifo fifoA;
    Fifo fifoB;

    int8_t latchedSampleA = 0;
    int8_t latchedSampleB = 0;

    bool ServiceFifo(Fifo& fifo, int8_t& latchedSample, uint8_t overflowMask, bool useTimer1);

    static constexpr size_t FRAME_RING_CAPACITY = 8192;
    std::array<int16_t, FRAME_RING_CAPACITY * 2> frameRing{};
    size_t frameReadIndex = 0;
    size_t frameWriteIndex = 0;
    size_t frameCount = 0;

    uint32_t sampleClock = 0;
    void GenerateFrame();

    uint16_t ReadSoundCntH() const;
    bool MasterEnabled() const;

    std::array<uint8_t, 1024>& ioRegisters;
};
