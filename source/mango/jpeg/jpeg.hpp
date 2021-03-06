/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2018 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <vector>
#include <string>
#include <mango/core/core.hpp>
#include <mango/image/image.hpp>
#include <mango/math/math.hpp>

//#define JPEG_ENABLE_PRINT
#define JPEG_ENABLE_THREAD
#define JPEG_ENABLE_SIMD
#define JPEG_ENABLE_MODERN_HUFFMAN

#define JPEG_MAX_BLOCKS_IN_MCU   10  // Maximum # of blocks per MCU in the JPEG specification
#define JPEG_MAX_COMPS_IN_SCAN   4   // JPEG limit on # of components in one scan
#define JPEG_NUM_ARITH_TBLS      16  // Arith-coding tables are numbered 0..15
#define JPEG_DC_STAT_BINS        64  // ...
#define JPEG_AC_STAT_BINS        256 // ...
#define JPEG_HUFF_LOOKUP_BITS    8   // Huffman look-ahead table log2 size
#define JPEG_HUFF_LOOKUP_SIZE    (1 << JPEG_HUFF_LOOKUP_BITS)

#ifdef JPEG_ENABLE_SIMD

    #if defined(MANGO_ENABLE_SSE2)
        #define JPEG_ENABLE_SSE2
    #endif

    #if defined(MANGO_ENABLE_AVX2)
        #define JPEG_ENABLE_AVX2
    #endif

    #if defined(MANGO_ENABLE_NEON)
        #define JPEG_ENABLE_NEON
    #endif

#endif

namespace jpeg
{

    // ----------------------------------------------------------------------------
    // typedefs
    // ----------------------------------------------------------------------------

    using mango::uint8;
    using mango::uint16;
    using mango::uint32;
    using mango::uint64;
    using mango::Memory;
    using mango::Format;
    using mango::Surface;
	using mango::Stream;
    using mango::ThreadPool;

    using BlockType = mango::int16;

#ifdef MANGO_CPU_64BIT

    using DataType = uint64;
    #define JPEG_REGISTER_SIZE 64

#else

    using DataType = uint32;
    #define JPEG_REGISTER_SIZE 32

#endif

    template <typename T>
    using AlignedVector = std::vector<T, mango::AlignedAllocator<T>>;

    struct Header
    {
        int width;
        int height;
        int xblock;
        int yblock;
        Format format;
    };

    struct Status
    {
        bool success;
        bool enableDirectDecode;
        std::string info;
    };

    struct QuantTable
    {
        uint16* table;  // Quantization table
        int     bits;   // Quantization table precision (8 or 16 bits)
    };

#ifndef JPEG_ENABLE_MODERN_HUFFMAN

    struct HuffTable
    {
        uint8   size[17];
        uint8   value[256];

        int     maxcode[18];
        int     valoffset[18+1];
        int     lookup[JPEG_HUFF_LOOKUP_SIZE];

        void configure();
    };

#else

    struct HuffTable
    {
        uint8       size[17];
        uint8       value[256];

        // acceleration tables
        DataType    maxcode[18];
        uint8*      valueAddress[19];
        uint8       lookupSize[JPEG_HUFF_LOOKUP_SIZE];
        uint8       lookupValue[JPEG_HUFF_LOOKUP_SIZE];

        void configure();
    };

#endif

    struct jpegBuffer
    {
        uint8* ptr;
        uint8* end;
        uint8* nextFF;

        DataType data;
        int remain;

        void restart();
        void bytes(int n);

#ifdef MANGO_CPU_64BIT

        // 64 bit register
        void ensure16()
        {
            if (remain < 16)
            {
                remain += 48;
                if (ptr + 8 < nextFF)
                {
                    data = (data << 48) | (uload64be(ptr) >> 16);
                    ptr += 6;
                }
                else
                {
                    bytes(6);
                }
            }
        }

#else

        // 32 bit register
        void ensure16()
        {
            if (remain < 16)
            {
                remain += 16;
                if (ptr + 2 < nextFF)
                {
                    data = (data << 16) | uload16be(ptr);
                    ptr += 2;
                }
                else
                {
                    bytes(2);
                }
            }
        }

#endif
    };

    struct Huffman
    {
        int last_dc_value[JPEG_MAX_COMPS_IN_SCAN];
        int eob_run;

        void restart();
    };

    struct Arithmetic
    {
        uint32 c;
        uint32 a;
        int ct;

        int last_dc_value[JPEG_MAX_COMPS_IN_SCAN]; // Last DC coef for each component
        int dc_context[JPEG_MAX_COMPS_IN_SCAN]; // Context index for DC conditioning

        uint8 dc_L[JPEG_NUM_ARITH_TBLS]; // L values for DC arith-coding tables
        uint8 dc_U[JPEG_NUM_ARITH_TBLS]; // U values for DC arith-coding tables
        uint8 ac_K[JPEG_NUM_ARITH_TBLS]; // K values for AC arith-coding tables

        uint8 dc_stats[JPEG_NUM_ARITH_TBLS][JPEG_DC_STAT_BINS];
        uint8 ac_stats[JPEG_NUM_ARITH_TBLS][JPEG_AC_STAT_BINS];
        uint8 fixed_bin[4]; // Statistics bin for coding with fixed probability 0.5

        Arithmetic();
        ~Arithmetic();

        void restart(jpegBuffer& buffer);
    };

    struct Frame
    {
        int compid; // Component identifier
        int Hsf;    // Horizontal sampling factor
        int Vsf;    // Vertical sampling factor
        int Tq;     // Quantization table destination selector
        int offset;
    };

    struct DecodeBlock
    {
        int offset;
        int pred;
        union
        {
            struct
            {
                int dc;
                int ac;
            } index;
            struct
            {
                HuffTable* dc;
                HuffTable* ac;
            } table;
        };
    };

    struct DecodeState
    {
        jpegBuffer buffer;
        Huffman huffman;
        Arithmetic arithmetic;

        DecodeBlock block[JPEG_MAX_BLOCKS_IN_MCU];
        int blocks;
        int comps_in_scan;

        const int* zigzagTable;

        int spectralStart;
        int spectralEnd;
        int successiveHigh;
        int successiveLow;

        void (*decode)(BlockType* output, DecodeState* state);
    };

    struct Block
    {
        QuantTable* qt;
        int offset;
        int stride;
    };

    struct ProcessState
    {
        Block block[JPEG_MAX_BLOCKS_IN_MCU];
        int blocks;

        Frame frame[JPEG_MAX_COMPS_IN_SCAN];
        int frames;

	    void (*idct)(uint8* dest, int stride, const BlockType* data, const uint16* qt);
        void (*process)(uint8* dest, int stride, const BlockType* data, ProcessState* state, int width, int height);
        void (*clipped)(uint8* dest, int stride, const BlockType* data, ProcessState* state, int width, int height);

        void (*process_Y          )(uint8* dest, int stride, const BlockType* data, ProcessState* state, int width, int height);
        void (*process_YCbCr      )(uint8* dest, int stride, const BlockType* data, ProcessState* state, int width, int height);
        void (*process_CMYK       )(uint8* dest, int stride, const BlockType* data, ProcessState* state, int width, int height);
        void (*process_YCbCr_8x8  )(uint8* dest, int stride, const BlockType* data, ProcessState* state, int width, int height);
        void (*process_YCbCr_8x16 )(uint8* dest, int stride, const BlockType* data, ProcessState* state, int width, int height);
        void (*process_YCbCr_16x8 )(uint8* dest, int stride, const BlockType* data, ProcessState* state, int width, int height);
        void (*process_YCbCr_16x16)(uint8* dest, int stride, const BlockType* data, ProcessState* state, int width, int height);
    };

    // ----------------------------------------------------------------------------
    // Parser
    // ----------------------------------------------------------------------------

    class Parser
    {
    protected:
        QuantTable quantTable[JPEG_MAX_COMPS_IN_SCAN];
        HuffTable huffTable[2][JPEG_MAX_COMPS_IN_SCAN];

        AlignedVector<uint16> quantTableVector;
        BlockType* blockVector;

        std::vector< Frame > frames;
        Frame* scanFrame; // current Progressive AC scan frame

        DecodeState decodeState;
        ProcessState processState;

        int restartInterval;
        int restartCounter;

        std::string m_info;
        Surface* m_surface;

        int width;  // Image width, does include alignment
        int height; // Image height, does include alignment
        int xsize;  // Image width, does not include alignment
        int ysize;  // Image height, does not include alignment
        int xclip;
        int yclip;
        int precision; // 8 or 16 bits
        bool is_progressive;
        bool is_arithmetic;
        bool is_lossless;
        int Hmax;
        int Vmax;
        int blocks_in_mcu;
        int xblock;
        int yblock;
        int xmcu;
        int ymcu;
        int mcus;

        bool isJPEG(Memory memory) const;

        uint8* stepMarker(uint8* p);
        uint8* seekMarker(uint8* p, uint8* end);

        void processSOI();
        void processEOI();
        void processCOM(uint8* p);
        void processTEM(uint8* p);
        void processRES(uint8* p);
        void processJPG(uint8* p);
        void processJPG(uint8* p, uint16 marker);
        void processAPP(uint8* p, uint16 marker);
        void processSOF(uint8* p, uint16 marker);
        uint8* processSOS(uint8* p, uint8* end);
        void processDQT(uint8* p);
        void processDNL(uint8* p);
        void processDRI(uint8* p);
        void processDHT(uint8* p);
        void processDAC(uint8* p);
        void processDHP(uint8* p);
        void processEXP(uint8* p);

        void parse(Memory memory, bool decode);

        void restart();
        bool handleRestart();

        void decodeSequential();
        void decodeSequentialST();
        void decodeSequentialMT();
        void decodeProgressive();
        void finishProgressive();
        void finishProgressiveST();
        void finishProgressiveMT();

    public:

        Header header;
        Memory exif_memory; // Exif block, if one is present
        Memory icc_memory; // ICC color profile block, if one is present
        Memory scan_memory; // Scan block

        Parser(Memory memory);
        ~Parser();

        Status decode(Surface& target);
    };

    // ----------------------------------------------------------------------------
    // jpegPrint
    // ----------------------------------------------------------------------------

#ifdef JPEG_ENABLE_PRINT

    #define jpegPrint(...) printf(__VA_ARGS__)
#else

    #define jpegPrint(...)
#endif

    // ----------------------------------------------------------------------------
    // functions
    // ----------------------------------------------------------------------------

    typedef void (*ProcessFunc)(uint8* dest, int stride, const BlockType* data, ProcessState* state, int width, int height);

    void huff_decode_mcu           (BlockType* output, DecodeState* state);
    void huff_decode_dc_first      (BlockType* output, DecodeState* state);
    void huff_decode_dc_refine     (BlockType* output, DecodeState* state);
    void huff_decode_ac_first      (BlockType* output, DecodeState* state);
    void huff_decode_ac_refine     (BlockType* output, DecodeState* state);

#ifdef MANGO_ENABLE_LICENSE_BSD
    void arith_decode_mcu          (BlockType* output, DecodeState* state);
    void arith_decode_dc_first     (BlockType* output, DecodeState* state);
    void arith_decode_dc_refine    (BlockType* output, DecodeState* state);
    void arith_decode_ac_first     (BlockType* output, DecodeState* state);
    void arith_decode_ac_refine    (BlockType* output, DecodeState* state);
#endif

    void idct                      (uint8* dest, int stride, const BlockType* data, const uint16* qt);
    void process_Y                 (uint8* dest, int stride, const BlockType* data, ProcessState* state, int width, int height);
    void process_YCbCr             (uint8* dest, int stride, const BlockType* data, ProcessState* state, int width, int height);
    void process_CMYK              (uint8* dest, int stride, const BlockType* data, ProcessState* state, int width, int height);
    void process_YCbCr_8x8         (uint8* dest, int stride, const BlockType* data, ProcessState* state, int width, int height);
    void process_YCbCr_8x16        (uint8* dest, int stride, const BlockType* data, ProcessState* state, int width, int height);
    void process_YCbCr_16x8        (uint8* dest, int stride, const BlockType* data, ProcessState* state, int width, int height);
    void process_YCbCr_16x16       (uint8* dest, int stride, const BlockType* data, ProcessState* state, int width, int height);

#if defined(JPEG_ENABLE_SIMD)
    void idct_simd                 (uint8* dest, int stride, const BlockType* data, const uint16* qt);
#endif

#if defined(JPEG_ENABLE_SSE2)
    void idct_sse2                 (uint8* dest, int stride, const BlockType* data, const uint16* qt);
    void process_YCbCr_8x8_sse2    (uint8* dest, int stride, const BlockType* data, ProcessState* state, int width, int height);
    void process_YCbCr_8x16_sse2   (uint8* dest, int stride, const BlockType* data, ProcessState* state, int width, int height);
    void process_YCbCr_16x8_sse2   (uint8* dest, int stride, const BlockType* data, ProcessState* state, int width, int height);
    void process_YCbCr_16x16_sse2  (uint8* dest, int stride, const BlockType* data, ProcessState* state, int width, int height);
#endif

#if defined(JPEG_ENABLE_AVX2)
    void process_YCbCr_8x8_avx2    (uint8* dest, int stride, const BlockType* data, ProcessState* state, int width, int height);
    void process_YCbCr_8x16_avx2   (uint8* dest, int stride, const BlockType* data, ProcessState* state, int width, int height);
    void process_YCbCr_16x8_avx2   (uint8* dest, int stride, const BlockType* data, ProcessState* state, int width, int height);
    void process_YCbCr_16x16_avx2  (uint8* dest, int stride, const BlockType* data, ProcessState* state, int width, int height);
#endif

	void EncodeImage(Stream& stream, const Surface& surface, float quality);

} // namespace jpeg
