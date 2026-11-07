// modify this value
#include <vector>
constexpr float dB = 2.0f;
constexpr bool kClampDb = false;
constexpr float dB_clamped = kClampDb ? ((dB > 1.0f) ? 1.0f : dB) : dB;
//

typedef signed char        int8_t;
typedef short              int16_t;
typedef int                int32_t;
typedef long long          int64_t;
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

template <typename value_type>
class Tuple
{
public:
    uint64_t return_value = 0;
    __forceinline constexpr value_type get_tuple_return(uint8_t slot) const
    {
        int size = sizeof(value_type);
        int segment = sizeof(return_value) / size;
        return (value_type)(return_value >> (slot * (size * 8)));
    }
    __forceinline constexpr void set_tuple_return(uint64_t slot0)
    {
        return_value = slot0;
    }
    __forceinline constexpr void set_tuple_return(uint32_t slot0, uint32_t slot1)
    {
        return_value = (uint64_t)slot0 | ((uint64_t)slot1 << 32);
    }
    __forceinline constexpr void set_tuple_return(uint16_t slot0, uint16_t slot1, uint16_t slot2, uint16_t slot3)
    {
        return_value = (uint64_t)slot0 | ((uint64_t)slot1 << 16) | ((uint64_t)slot2 << 32) | ((uint64_t)slot3 << 48);
    }
    __forceinline constexpr void set_tuple_return(uint8_t slot0, uint8_t slot1, uint8_t slot2, uint8_t slot3, uint8_t slot4, uint8_t slot5, uint8_t slot6, uint8_t slot7)
    {
        return_value = (uint64_t)slot0 | ((uint64_t)slot1 << 8) | ((uint64_t)slot2 << 16) | ((uint64_t)slot3 << 24) | ((uint64_t)slot4 << 32) | ((uint64_t)slot5 << 40) | ((uint64_t)slot6 << 48) | ((uint64_t)slot7 << 56);
    }
};

template <typename value_type, uint64_t value>
__forceinline constexpr value_type noclass_get_tuple_return(uint8_t slot)
{
    int size = sizeof(value_type);
    int segment = sizeof(value) / size;
    return (value_type)(value >> (slot * (size * 8)));
}

constexpr Tuple<uint32_t> getfraction()
{
    for (int denominator = 1; denominator <= 10; denominator++) // if you are someone who for some reason needs a higher accuracy then 10/x, you can change these values
    {
        int numerator = static_cast<int>(dB_clamped * denominator + 0.5f); // round to nearest
        float approx = static_cast<float>(numerator) / static_cast<float>(denominator);
        float diff = (approx > dB_clamped) ? (approx - dB_clamped) : (dB_clamped - approx);
        if (diff <= 0.1f)
        {
            Tuple<uint32_t> TupleReturn;
            TupleReturn.set_tuple_return(static_cast<uint32_t>(numerator), static_cast<uint32_t>(denominator));
            return TupleReturn;
        }
    }
    Tuple<uint32_t> Default = {};
    Default.set_tuple_return(100, 10);
    return Default;
}

constexpr auto TupleReturn = getfraction();
constexpr int numerator_deviated = static_cast<int>(TupleReturn.get_tuple_return(0)) - 2;
constexpr int denominator_deviated = static_cast<int>(TupleReturn.get_tuple_return(1)) - 2;

extern "C" void __cdecl hp_cutoff(const float* in, int cutoff_Hz, float* out, int* hp_mem, int len, int channels, int Fs, int arch)
{
    int* st = (hp_mem - 3553);
    for (unsigned long i = 0; i < channels * len; i++) out[i] = (in[i] * channels);
}

// if you are looking for modifying the amplification, scroll up to the very top
extern "C" void __cdecl dc_reject(const float* in, float* out, int* hp_mem, int len, int channels, int Fs)
{
    int* st = (hp_mem - 3553);

    *(int*)((char*)st + 160) = -1; // bitrate
    *(int*)((char*)st + 164) = -1; // user bitrate
    *(int*)((char*)st + 184) = 0; // disable dtx

    constexpr float dB_scaled = 1.0f;

    for (int i = 0; i < channels * len; i++) {
        out[i] = (in[i] * (channels + numerator_deviated)) / (channels + denominator_deviated) * dB_scaled;
    }
}
