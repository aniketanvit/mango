/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2018 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#include "jpeg.hpp"

// ----------------------------------------------------------------------------------------------------
// Generic C++ implementation
// ----------------------------------------------------------------------------------------------------

namespace
{

    using namespace mango;

    struct IDCT
    {
        int x0, x1, x2, x3;
        int y0, y1, y2, y3;
        
        void compute(int s0, int s1, int s2, int s3, int s4, int s5, int s6, int s7)
        {
            const int n0 = (s2 + s6) * 2217;
            const int t2 = n0 + s6 * -7567;
            const int t3 = n0 + s2 * 3135;
            const int t0 = (s0 + s4) << 12;
            const int t1 = (s0 - s4) << 12;
            x0 = t0 + t3;
            x3 = t0 - t3;
            x1 = t1 + t2;
            x2 = t1 - t2;
            
            int p1 = s7 + s1;
            int p2 = s5 + s3;
            int p3 = s7 + s3;
            int p4 = s5 + s1;
            int p5 = (p3 + p4) * 4816;
            p1 = p1 * -3685 + p5;
            p2 = p2 * -10497 + p5;
            p3 = p3 * -8034;
            p4 = p4 * -1597;
            y0 = p1 + p3 + s7 * 1223;
            y1 = p2 + p4 + s5 * 8410;
            y2 = p2 + p3 + s3 * 12586;
            y3 = p1 + p4 + s1 * 6149;
        }
    };
    
} // namespace

namespace jpeg
{

    void idct(uint8* dest, int stride, const BlockType* data, const uint16* qt)
    {
        int temp[64];
        int* v;

        const int16_t *s = data;

        v = temp;

        for (int i = 0; i < 8; ++i)
        {
            if (s[1] || s[2] || s[3] || s[4] || s[5] || s[6] || s[7])
            {
                // dequantize
                const int s0 = s[0] * qt[0];
                const int s1 = s[1] * qt[1];
                const int s2 = s[2] * qt[2];
                const int s3 = s[3] * qt[3];
                const int s4 = s[4] * qt[4];
                const int s5 = s[5] * qt[5];
                const int s6 = s[6] * qt[6];
                const int s7 = s[7] * qt[7];

                IDCT idct;
                idct.compute(s0, s1, s2, s3, s4, s5, s6, s7);
                const int bias = 0x200;
                idct.x0 += bias;
                idct.x1 += bias;
                idct.x2 += bias;
                idct.x3 += bias;
                v[0] = (idct.x0 + idct.y3) >> 10;
                v[1] = (idct.x1 + idct.y2) >> 10;
                v[2] = (idct.x2 + idct.y1) >> 10;
                v[3] = (idct.x3 + idct.y0) >> 10;
                v[4] = (idct.x3 - idct.y0) >> 10;
                v[5] = (idct.x2 - idct.y1) >> 10;
                v[6] = (idct.x1 - idct.y2) >> 10;
                v[7] = (idct.x0 - idct.y3) >> 10;
            }
            else
            {
                int dc = (s[0] * qt[0]) << 2;
                v[0] = dc;
                v[1] = dc;
                v[2] = dc;
                v[3] = dc;
                v[4] = dc;
                v[5] = dc;
                v[6] = dc;
                v[7] = dc;
            }

            v += 8;
            s += 8;
            qt += 8;
        }

        v = temp;

        for (int i = 0; i < 8; ++i)
        {
            IDCT idct;
            idct.compute(v[0], v[8], v[16], v[24], v[32], v[40], v[48], v[56]);
            ++v;
            const int bias = 0x10000 + (128 << 17);
            idct.x0 += bias;
            idct.x1 += bias;
            idct.x2 += bias;
            idct.x3 += bias;
            dest[0] = byteclamp((idct.x0 + idct.y3) >> 17);
            dest[1] = byteclamp((idct.x1 + idct.y2) >> 17);
            dest[2] = byteclamp((idct.x2 + idct.y1) >> 17);
            dest[3] = byteclamp((idct.x3 + idct.y0) >> 17);
            dest[4] = byteclamp((idct.x3 - idct.y0) >> 17);
            dest[5] = byteclamp((idct.x2 - idct.y1) >> 17);
            dest[6] = byteclamp((idct.x1 - idct.y2) >> 17);
            dest[7] = byteclamp((idct.x0 - idct.y3) >> 17);
            dest += stride;
        }
    }

} // namespace jpeg

// ----------------------------------------------------------------------------------------------------
// SIMD implementation
// ----------------------------------------------------------------------------------------------------

#if defined(JPEG_ENABLE_SIMD)

namespace jpeg
{

    static inline uint8x16 packRow(float32x4 x, float32x4 y)
    {
        float32x4 a = x - y;
        float32x4 b = x + y;
        b = b.wzyx;
        int16x8 c0 = simd::narrow(convert<int32x4>(a).m, convert<int32x4>(b).m);
        uint16x8 c1 = reinterpret<uint16x8>(c0);
        return simd::narrow(c1.m, c1.m);
    }

    void simd_idct(uint8* dest, int stride, const BlockType* data, const uint16* qt)
    {
        float32x4 temp[16];
        float* v = reinterpret_cast<float*>(temp);

        const float32x4 f0(-0.3535533905f, 0.3535533905f, 128.0f,        128.0f);
        const float32x4 f1( 0.4619397662f, 0.1913417161f,-0.4619397662f,-0.1913417161f);
        const float32x4 c0 = f0.yyyy;
        const float32x4 c1 = f1.xywz;
        const float32x4 c2 = f0.yxxy;
        const float32x4 c3 = f1.yzxw;
        const float32x4 c4(-0.4903926402f,-0.4157348061f,-0.2777851165f,-0.0975451610f);
        const float32x4 c5(-0.4157348061f, 0.0975451610f, 0.4903926402f, 0.2777851165f);
        const float32x4 c6(-0.2777851165f, 0.4903926402f,-0.0975451610f,-0.4157348061f);
        const float32x4 c7(-0.0975451610f, 0.2777851165f,-0.4157348061f, 0.4903926402f);
        const float32x4 c8 = f0.wwww;

        const float32x8 c04(c0, c4);
        const float32x8 c15(c1, c5);
        const float32x8 c26(c2, c6);
        const float32x8 c37(c3, c7);

        for (int i = 0; i < 8; ++i)
        {
            int16x8 d = *reinterpret_cast<const int16x8 *>(data);
            int16x8 q = *reinterpret_cast<const int16x8 *>(qt);
            int16x8 dq = simd::mullo(d, q);
            float32x8 s = convert<float32x8>(int32x8(simd::extend32x8(dq)));

            float32x4 s0 = s.low;
            float32x4 s1 = s.high;
            float32x8 v0(s0.xxxx, s0.yyyy);
            float32x8 v1(s0.zzzz, s0.wwww);
            float32x8 v2(s1.xxxx, s1.yyyy);
            float32x8 v3(s1.zzzz, s1.wwww);
            float32x8 xy = madd(madd(madd(v0 * c04, v1, c15), v2, c26), v3, c37);
            float32x4 x = xy.low;
            float32x4 y = xy.high;
            float32x4 a = x + y;
            float32x4 b = x - y;
            b = b.wzyx;

            simd::float32x4_ustore(v + 0, a);
            simd::float32x4_ustore(v + 4, b);

            data += 8;
            qt += 8;
            v += 8;
        }

        v = reinterpret_cast<float *>(temp) + 4;

        for (int i = 0; i < 2; ++i)
        {
#if 0
            float32x4 v0 = simd::float32x4_uload(v + 0 * 8);
            float32x4 v2 = simd::float32x4_uload(v + 2 * 8);
            float32x4 v4 = simd::float32x4_uload(v + 4 * 8);
            float32x4 v6 = simd::float32x4_uload(v + 6 * 8);
            float32x4 x0 = madd(madd(madd(v0.xxxx * c0, v2.xxxx, c1), v4.xxxx, c2), v6.xxxx, c3);
            float32x4 x1 = madd(madd(madd(v0.yyyy * c0, v2.yyyy, c1), v4.yyyy, c2), v6.yyyy, c3);
            float32x4 x2 = madd(madd(madd(v0.zzzz * c0, v2.zzzz, c1), v4.zzzz, c2), v6.zzzz, c3);
            float32x4 x3 = madd(madd(madd(v0.wwww * c0, v2.wwww, c1), v4.wwww, c2), v6.wwww, c3);
            x0 = x0 + c8;
            x1 = x1 + c8;
            x2 = x2 + c8;
            x3 = x3 + c8;

            float32x4 v1 = simd::float32x4_uload(v + 1 * 8);
            float32x4 v3 = simd::float32x4_uload(v + 3 * 8);
            float32x4 v5 = simd::float32x4_uload(v + 5 * 8);
            float32x4 v7 = simd::float32x4_uload(v + 7 * 8);
            float32x4 y0 = madd(madd(madd(v1.xxxx * c4, v3.xxxx, c5), v5.xxxx, c6), v7.xxxx, c7);
            float32x4 y1 = madd(madd(madd(v1.yyyy * c4, v3.yyyy, c5), v5.yyyy, c6), v7.yyyy, c7);
            float32x4 y2 = madd(madd(madd(v1.zzzz * c4, v3.zzzz, c5), v5.zzzz, c6), v7.zzzz, c7);
            float32x4 y3 = madd(madd(madd(v1.wwww * c4, v3.wwww, c5), v5.wwww, c6), v7.wwww, c7);
#else
            // experimental 8-way transformation
            // TODO: optimize memory layout
            float32x4 v0 = simd::float32x4_uload(v + 0 * 8);
            float32x4 v1 = simd::float32x4_uload(v + 1 * 8);
            float32x4 v2 = simd::float32x4_uload(v + 2 * 8);
            float32x4 v3 = simd::float32x4_uload(v + 3 * 8);
            float32x4 v4 = simd::float32x4_uload(v + 4 * 8);
            float32x4 v5 = simd::float32x4_uload(v + 5 * 8);
            float32x4 v6 = simd::float32x4_uload(v + 6 * 8);
            float32x4 v7 = simd::float32x4_uload(v + 7 * 8);

            float32x8 v01x(v0.xxxx, v1.xxxx);
            float32x8 v01y(v0.yyyy, v1.yyyy);
            float32x8 v01z(v0.zzzz, v1.zzzz);
            float32x8 v01w(v0.wwww, v1.wwww);

            float32x8 v23x(v2.xxxx, v3.xxxx);
            float32x8 v23y(v2.yyyy, v3.yyyy);
            float32x8 v23z(v2.zzzz, v3.zzzz);
            float32x8 v23w(v2.wwww, v3.wwww);

            float32x8 v45x(v4.xxxx, v5.xxxx);
            float32x8 v45y(v4.yyyy, v5.yyyy);
            float32x8 v45z(v4.zzzz, v5.zzzz);
            float32x8 v45w(v4.wwww, v5.wwww);

            float32x8 v67x(v6.xxxx, v7.xxxx);
            float32x8 v67y(v6.yyyy, v7.yyyy);
            float32x8 v67z(v6.zzzz, v7.zzzz);
            float32x8 v67w(v6.wwww, v7.wwww);

            float32x8 xy0 = madd(madd(madd(v01x * c04, v23x, c15), v45x, c26), v67x, c37);
            float32x8 xy1 = madd(madd(madd(v01y * c04, v23y, c15), v45y, c26), v67y, c37);
            float32x8 xy2 = madd(madd(madd(v01z * c04, v23z, c15), v45z, c26), v67z, c37);
            float32x8 xy3 = madd(madd(madd(v01w * c04, v23w, c15), v45w, c26), v67w, c37);

            float32x4 x0 = xy0.low + c8;
            float32x4 x1 = xy1.low + c8;
            float32x4 x2 = xy2.low + c8;
            float32x4 x3 = xy3.low + c8;
            float32x4 y0 = xy0.high;
            float32x4 y1 = xy1.high;
            float32x4 y2 = xy2.high;
            float32x4 y3 = xy3.high;
#endif
            
            store_low(dest + stride * 0, packRow(x3, y3));
            store_low(dest + stride * 1, packRow(x2, y2));
            store_low(dest + stride * 2, packRow(x1, y1));
            store_low(dest + stride * 3, packRow(x0, y0));

            dest += stride * 4;
            v -= 4;
        }
    }

} // namespace jpeg

#endif
