#pragma once

#include <string>

class Stream {

    public:

    Stream(const void* stream)
        : stream_(stream)
    {
    }

    Stream(const Stream& stream)
        : stream_(stream.get())
    {
    }

    //

    template <typename T> static T GetField(const void* pDataPtr);
    template <typename T> T GetField();
    template <typename T> void SkipField();

    void Skip(size_t pByteCount)
    {
        stream_ = (uint8_t*)stream_ + pByteCount;
    }

    const void* get() const { return stream_; }

    //

    uint8_t operator*()
    { // Read one byte.
        return this->GetField<uint8_t>();
    }

    private:
    const void* stream_;
};

// Byte-swap utility template functions.

template <typename T>
T SwapBytes16(const T* data)
{
    T bytes = *(const T*)data;

    T Val0 = (bytes & 0xff) << 8;
    T Val1 = (bytes & 0xff00) >> 8;

    return (Val0 | Val1);
}

template <typename T>
T SwapBytes32(const T* data)
{
    T bytes = *(const T*)data;

    T Val0 = (bytes & 0xff) << 24;
    T Val1 = (bytes & 0xff00) << 8;
    T Val2 = (bytes & 0xff0000) >> 8;
    T Val3 = (bytes & 0xff000000) >> 24;

    return (Val0 | Val1 | Val2 | Val3);
}

template <typename T>
T SwapBytes64(const T* data)
{
    T bytes = *(const T*)data;

    T Val0 = (bytes & 0xff) << 56;
    T Val1 = (bytes & 0xff00) << 40;
    T Val2 = (bytes & 0xff0000) >> 24;
    T Val3 = (bytes & 0xff000000) << 8;
    T Val4 = (bytes & 0xff00000000) >> 8;
    T Val5 = (bytes & 0xff0000000000) >> 24;
    T Val6 = (bytes & 0xff000000000000) >> 40;
    T Val7 = (bytes & 0xff00000000000000) >> 56;

    return (Val0 | Val1 | Val2 | Val3 | Val4 | Val5 | Val6 | Val7);
}

//

template <typename T>
void
Stream::SkipField()
{
    stream_ = (uint8_t*)stream_ + sizeof(T);
}

template <typename T>
static T
Stream::GetField(const void* pDataPtr)
{
    Stream s(pDataPtr);
    return s.GetField<T>();
}


template <typename T>
T Stream::GetField()
{
    // TrueType files are big-endian encoded, so if the 
    // target cpu is little-endian, we must byte-swap fields
    // that have a width of 2 bytes or more.

    const T* castedStream = (const T*)stream_;
    T fieldData = (T)NULL;

#ifndef BIGENDIAN
    const size_t fieldSize = sizeof(T);

    if constexpr (fieldSize == 1) {
        fieldData = *castedStream;
    }
    else if (fieldSize == 2) {
        fieldData = SwapBytes16<T>(castedStream);
    }
    else if (fieldSize == 4) {
        fieldData = SwapBytes32<T>(castedStream);
    }
    else if (fieldSize == 8) {
        fieldData = SwapBytes64<T>(castedStream);
    }
    else {
        static_assert("Invalid field size");
    }
#else
    fieldData = *castedStream;
#endif

    SkipField<T>();

    return fieldData;
}
