#include "APU.h"

void APU::Fifo::Clear()
{
    data.fill(0);
    readIndex = 0;
    writeIndex = 0;
    count = 0;
}

void APU::Fifo::Push(int8_t sample)
{
    //full fifo drops write
    if (count >= FIFO_DEPTH)
        return;

    data[writeIndex] = sample;
    writeIndex = (writeIndex + 1) % FIFO_DEPTH;
    count++;
}

bool APU::Fifo::Pop(int8_t& outSample)
{
    if (count == 0)
        return false;

    outSample = data[readIndex];
    readIndex = (readIndex + 1) % FIFO_DEPTH;
    count--;
    return true;
}

APU::APU(std::array<uint8_t, 1024>& ioRegisters)
    : ioRegisters(ioRegisters)
{
    Reset();
}

void APU::Reset()
{
    fifoA.Clear();
    fifoB.Clear();
    latchedSampleA = 0;
    latchedSampleB = 0;

    frameRing.fill(0);
    frameReadIndex = 0;
    frameWriteIndex = 0;
    frameCount = 0;
    sampleClock = 0;
}

bool APU::IsFifoOffset(uint32_t offset)
{
    return offset >= FIFO_A_OFFSET && offset < FIFO_B_OFFSET + 4;
}

uint16_t APU::ReadSoundCntH() const
{
    return static_cast<uint16_t>(ioRegisters[0x082] | (ioRegisters[0x083] << 8));
}

bool APU::MasterEnabled() const
{
    return (ioRegisters[0x084] & 0x80) != 0;
}

void APU::WriteFifo(uint32_t offset, uint8_t value)
{
    int8_t sample = static_cast<int8_t>(value);

    if (offset < FIFO_B_OFFSET)
        fifoA.Push(sample);
    else
        fifoB.Push(sample);
}

void APU::OnSoundCntHWrite()
{
    uint16_t control = ReadSoundCntH();

    if (control & 0x0800)
    {
        fifoA.Clear();
        latchedSampleA = 0;
    }

    if (control & 0x8000)
    {
        fifoB.Clear();
        latchedSampleB = 0;
    }

    //two reset bits cant be set on next write
    ioRegisters[0x083] &= static_cast<uint8_t>(~0x88);
}

void APU::OnSoundCntXWrite()
{
    if (MasterEnabled())
        return;

    //clear channels but let them still play to avoid artifacting
    fifoA.Clear();
    fifoB.Clear();
    latchedSampleA = 0;
    latchedSampleB = 0;
}

bool APU::ServiceFifo(Fifo& fifo, int8_t& latchedSample, uint8_t overflowMask, bool useTimer1)
{
    uint8_t clockBit = useTimer1 ? 0x2 : 0x1;
    if (!(overflowMask & clockBit))
        return false;

    //empty fifo keeps the last sample so it doesnt do bs
    int8_t sample;
    if (fifo.Pop(sample))
        latchedSample = sample;

    //if half or less than half make the dma read more
    return fifo.count <= FIFO_DEPTH / 2;
}

static int32_t Clamp16(int32_t value)
{
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return value;
}

void APU::GenerateFrame()
{
    int32_t left = 0;
    int32_t right = 0;

    if (MasterEnabled())
    {
        uint16_t control = ReadSoundCntH();

        int32_t a = latchedSampleA;
        int32_t b = latchedSampleB;

        //bits 2 and 3 pick full volume, otherwise the channel plays at half
        if (!(control & 0x0004)) a >>= 1;
        if (!(control & 0x0008)) b >>= 1;

        if (control & 0x0200) left += a;
        if (control & 0x0100) right += a;
        if (control & 0x2000) left += b;
        if (control & 0x1000) right += b;
    }

    //clamp to +-256
    left = Clamp16(left * 128);
    right = Clamp16(right * 128);

    if (frameCount >= FRAME_RING_CAPACITY)
    {
        return;
    }

    frameRing[frameWriteIndex * 2] = static_cast<int16_t>(left);
    frameRing[frameWriteIndex * 2 + 1] = static_cast<int16_t>(right);
    frameWriteIndex = (frameWriteIndex + 1) % FRAME_RING_CAPACITY;
    frameCount++;
}

size_t APU::ReadSamples(int16_t* destination, size_t maxFrames)
{
    size_t taken = 0;

    while (taken < maxFrames && frameCount > 0)
    {
        destination[taken * 2] = frameRing[frameReadIndex * 2];
        destination[taken * 2 + 1] = frameRing[frameReadIndex * 2 + 1];
        frameReadIndex = (frameReadIndex + 1) % FRAME_RING_CAPACITY;
        frameCount--;
        taken++;
    }

    return taken;
}

APU::RefillRequest APU::Tick(uint8_t overflowMask)
{
    RefillRequest request;

    if (overflowMask != 0 && MasterEnabled())
    {
        uint16_t control = ReadSoundCntH();

        request.fifoA = ServiceFifo(fifoA, latchedSampleA, overflowMask, (control & 0x0400) != 0);
        request.fifoB = ServiceFifo(fifoB, latchedSampleB, overflowMask, (control & 0x4000) != 0);
    }

    if (++sampleClock >= CYCLES_PER_OUTPUT_SAMPLE)
    {
        sampleClock = 0;
        GenerateFrame();
    }

    return request;
}
