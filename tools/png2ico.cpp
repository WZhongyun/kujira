// Build-time helper: converts the model's icon.png into a multi-size .ico for the exe.
// Usage: png2ico <input.png> <output.ico>
#define _CRT_SECURE_NO_WARNINGS
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace
{
// Area-averaging downscale (premultiplied, so transparent edges don't darken).
std::vector<uint8_t> Resize(const uint8_t* src, int sw, int sh, int size)
{
    std::vector<uint8_t> out(static_cast<size_t>(size) * size * 4);
    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            const double x0 = double(x) * sw / size, x1 = double(x + 1) * sw / size;
            const double y0 = double(y) * sh / size, y1 = double(y + 1) * sh / size;
            double acc[4] = {}, area = 0;
            for (int sy = int(y0); sy < sh && sy < y1; ++sy)
            {
                const double wy = std::min<double>(sy + 1, y1) - std::max<double>(sy, y0);
                for (int sx = int(x0); sx < sw && sx < x1; ++sx)
                {
                    const double w = wy * (std::min<double>(sx + 1, x1) - std::max<double>(sx, x0));
                    const uint8_t* p = src + (static_cast<size_t>(sy) * sw + sx) * 4;
                    const double a = p[3] / 255.0;
                    acc[0] += p[0] * a * w;
                    acc[1] += p[1] * a * w;
                    acc[2] += p[2] * a * w;
                    acc[3] += p[3] * w;
                    area += w;
                }
            }
            uint8_t* d = &out[(static_cast<size_t>(y) * size + x) * 4];
            const double alpha = area > 0 ? acc[3] / area : 0;
            for (int c = 0; c < 3; ++c)
            {
                const double v = alpha > 0 ? acc[c] / area / (alpha / 255.0) : 0;
                d[c] = static_cast<uint8_t>(std::min(255.0, v + 0.5));
            }
            d[3] = static_cast<uint8_t>(std::min(255.0, alpha + 0.5));
        }
    }
    return out;
}

// argv[i] as a FILE*, UTF-16 aware on Windows so non-ASCII folders work.
FILE* OpenArg(int argc, char** argv, int i, const char* mode)
{
#ifdef _WIN32
    int wargc = 0;
    wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    FILE* f = nullptr;
    if (wargv && i < wargc)
    {
        wchar_t wmode[8] = {};
        for (int k = 0; mode[k] && k < 7; ++k) wmode[k] = static_cast<wchar_t>(mode[k]);
        f = _wfopen(wargv[i], wmode);
    }
    LocalFree(wargv);
    return f;
#else
    (void)argc;
    return std::fopen(argv[i], mode);
#endif
}

void Put16(std::vector<uint8_t>& b, uint32_t v) { b.push_back(v & 0xFF); b.push_back((v >> 8) & 0xFF); }
void Put32(std::vector<uint8_t>& b, uint32_t v) { Put16(b, v & 0xFFFF); Put16(b, v >> 16); }

// One icon image as a 32-bit DIB (BITMAPINFOHEADER + bottom-up BGRA + AND mask).
std::vector<uint8_t> Dib(const std::vector<uint8_t>& rgba, int size)
{
    std::vector<uint8_t> b;
    const uint32_t maskStride = ((size + 31) / 32) * 4;
    Put32(b, 40);
    Put32(b, size);
    Put32(b, size * 2);  // colour + mask
    Put16(b, 1);
    Put16(b, 32);
    Put32(b, 0);
    Put32(b, size * size * 4 + maskStride * size);
    Put32(b, 0); Put32(b, 0); Put32(b, 0); Put32(b, 0);
    for (int y = size - 1; y >= 0; --y)
    {
        for (int x = 0; x < size; ++x)
        {
            const uint8_t* p = &rgba[(static_cast<size_t>(y) * size + x) * 4];
            b.push_back(p[2]); b.push_back(p[1]); b.push_back(p[0]); b.push_back(p[3]);
        }
    }
    b.insert(b.end(), maskStride * size, 0);  // alpha channel is used instead
    return b;
}
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: png2ico <input.png> <output.ico>\n");
        return 2;
    }
    int w = 0, h = 0, n = 0;
    uint8_t* src = nullptr;
    if (FILE* in = OpenArg(argc, argv, 1, "rb"))
    {
        src = stbi_load_from_file(in, &w, &h, &n, 4);
        std::fclose(in);
    }
    if (!src)
    {
        std::fprintf(stderr, "png2ico: cannot read %s\n", argv[1]);
        return 1;
    }

    const int sizes[] = { 16, 20, 24, 32, 40, 48, 64, 256 };
    std::vector<std::vector<uint8_t>> images;
    for (int size : sizes) images.push_back(Dib(Resize(src, w, h, size), size));
    stbi_image_free(src);

    std::vector<uint8_t> ico;
    Put16(ico, 0);
    Put16(ico, 1);  // icon
    Put16(ico, static_cast<uint32_t>(images.size()));
    uint32_t offset = 6 + 16 * static_cast<uint32_t>(images.size());
    for (size_t i = 0; i < images.size(); ++i)
    {
        const int size = sizes[i];
        ico.push_back(size >= 256 ? 0 : static_cast<uint8_t>(size));
        ico.push_back(size >= 256 ? 0 : static_cast<uint8_t>(size));
        ico.push_back(0);
        ico.push_back(0);
        Put16(ico, 1);
        Put16(ico, 32);
        Put32(ico, static_cast<uint32_t>(images[i].size()));
        Put32(ico, offset);
        offset += static_cast<uint32_t>(images[i].size());
    }
    for (const auto& img : images) ico.insert(ico.end(), img.begin(), img.end());

    FILE* f = OpenArg(argc, argv, 2, "wb");
    if (!f || std::fwrite(ico.data(), 1, ico.size(), f) != ico.size())
    {
        std::fprintf(stderr, "png2ico: cannot write %s\n", argv[2]);
        if (f) std::fclose(f);
        return 1;
    }
    std::fclose(f);
    return 0;
}
