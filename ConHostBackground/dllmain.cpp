// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "pch.h"

#include <psapi.h>
#include <MinHook.h>
#include <initializer_list>
#include "stb_image.h"
#include "stb_image_resize.h"
#include <vector>

#define INRANGE(x, a, b) (x >= a && x <= b)
#define GET_BYTE(x) (GET_BITS(x[0]) << 4 | GET_BITS(x[1]))
#define GET_BITS(x) (INRANGE((x & (~0x20)), 'A', 'F') ? ((x & (~0x20)) - 'A' + 0xa) : (INRANGE(x, '0', '9') ? x - '0' : 0))


uintptr_t FindSignatureModule(const char* szModule, const char* szSignature) {
	const char* pattern = szSignature;
	uintptr_t firstMatch = 0;
	static const auto rangeStart = (uintptr_t)GetModuleHandleA(szModule);
	static MODULEINFO miModInfo;
	static bool init = false;
	if (!init) {
		init = true;
		GetModuleInformation(GetCurrentProcess(), (HMODULE)rangeStart, &miModInfo, sizeof(MODULEINFO));
	}
	static const uintptr_t rangeEnd = rangeStart + miModInfo.SizeOfImage;

	BYTE patByte = GET_BYTE(pattern);
	const char* oldPat = pattern;

	for (uintptr_t pCur = rangeStart; pCur < rangeEnd; pCur++) {
		if (!*pattern)
			return firstMatch;

		while (isspace(*(PBYTE)pattern))
			pattern++;

		if (!*pattern)
			return firstMatch;

		if (oldPat != pattern) {
			oldPat = pattern;
			if (*(PBYTE)pattern != '\?')
				patByte = GET_BYTE(pattern);
		}

		if (*(PBYTE)pattern == '\?' || *(BYTE*)pCur == patByte) {
			if (!firstMatch)
				firstMatch = pCur;

			if (!pattern[2] || !pattern[1])
				return firstMatch;

			//if (*(PWORD)pattern == '\?\?' || *(PBYTE)pattern != '\?')
			//pattern += 3;

			//else
			pattern += 2;
		}
		else {
			pattern = szSignature;
			firstMatch = 0;
		}
	}
	return 0u;
}
// 将 PNG 图像加载为 HBITMAP，并进行颜色通道转换
HBITMAP LoadBitmapFromPNG(const char* filepath, int& width, int& height) {
    unsigned char* data = nullptr;
    data = stbi_load(filepath, &width, &height, nullptr, 4); // 4 channels (RGBA)
    if (!data) {
        return nullptr;
    }

    // 创建 BGRA 数据缓冲区
    unsigned char* bgraData = new unsigned char[width * height * 4];

    // 转换 RGBA 到 BGRA
    for (int i = 0; i < width * height; i++) {
        bgraData[i * 4 + 0] = data[i * 4 + 2]; // B
        bgraData[i * 4 + 1] = data[i * 4 + 1]; // G
        bgraData[i * 4 + 2] = data[i * 4 + 0]; // R
        bgraData[i * 4 + 3] = data[i * 4 + 3]; // A
    }

    // 创建 HBITMAP
    HBITMAP hBitmap = CreateBitmap(width, height, 1, 32, bgraData);

    // 清理
    delete[] bgraData;
    stbi_image_free(data);

    return hBitmap;
}
#pragma comment(lib,"Msimg32.lib")
HBITMAP g_hBitmap = nullptr; // 全局变量存储位图
int g_width = 0; // 位图宽度
int g_height = 0; // 位图高度
int (*flushbufferlines)(void* pthis);
// 辅助函数：计算 UniformToFill 的缩放参数
void CalculateUniformToFill(int srcWidth, int srcHeight, int dstWidth, int dstHeight,
    int& outX, int& outY, int& outWidth, int& outHeight) {
    float srcAspect = srcWidth / (float)srcHeight;
    float dstAspect = dstWidth / (float)dstHeight;

    if (srcAspect > dstAspect) {
        // 源图像更宽，以高度为准
        outHeight = dstHeight;
        outWidth = (int)(dstHeight * srcAspect);
        outY = 0;
        outX = (outWidth - dstWidth) / -2;
    }
    else {
        // 源图像更高，以宽度为准
        outWidth = dstWidth;
        outHeight = (int)(dstWidth / srcAspect);
        outX = 0;
        outY = (outHeight - dstHeight) / -2;
    }
}

// 辅助函数：从 HBITMAP 获取像素数据
bool GetBitmapPixels(HBITMAP hBitmap, std::vector<unsigned char>& pixels, int& width, int& height) {
    BITMAP bmp;
    GetObject(hBitmap, sizeof(BITMAP), &bmp);
    width = bmp.bmWidth;
    height = bmp.bmHeight;

    pixels.resize(width * height * 4);

    BITMAPINFO bi = { 0 };
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;  // 负值表示自上而下
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC hdc = CreateCompatibleDC(NULL);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(hdc, hBitmap);

    GetDIBits(hdc, hBitmap, 0, height, pixels.data(), &bi, DIB_RGB_COLORS);

    SelectObject(hdc, oldBitmap);
    DeleteDC(hdc);

    return true;
}

// 辅助函数：创建 HBITMAP
HBITMAP CreateBitmapFromPixels(HDC hdc, const unsigned char* pixels, int width, int height) {
    BITMAPINFO bi = { 0 };
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;  // 负值表示自上而下
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (hBitmap && bits) {
        memcpy(bits, pixels, width * height * 4);
    }

    return hBitmap;
}
unsigned char* scaledPixels{};
int scaledHeight{};
int scaledWidth{};
int Microsoft_Console_Render_GdiEngine_EndPaint(void* pthis) {
    if (*((BYTE*)pthis + 56)) {
        HDC thdc = *((HDC*)pthis + 10);
        HDC sdc = *((HDC*)pthis + 19);

        int dstX = *((DWORD*)pthis + 23);
        int dstY = *((DWORD*)pthis + 24);
        int dstWidth = *((DWORD*)pthis + 25) - dstX;
        int dstHeight = *((DWORD*)pthis + 26) - dstY;

        // 创建双缓冲
        HDC memDC = CreateCompatibleDC(thdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(thdc, dstWidth, dstHeight);
        HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

        flushbufferlines(pthis);

        if (!g_hBitmap) {
            g_hBitmap = LoadBitmapFromPNG("bg.png", g_width, g_height);
        }

        if (g_hBitmap) {
            // 获取背景图像的像素数据
            std::vector<unsigned char> srcPixels;
            int srcWidth, srcHeight;
            if (GetBitmapPixels(g_hBitmap, srcPixels, srcWidth, srcHeight)) {
                // 计算 UniformToFill 的参数
                int scaledX, scaledY, scaledWidth, scaledHeight;
                CalculateUniformToFill(srcWidth, srcHeight, dstWidth, dstHeight,
                    scaledX, scaledY, scaledWidth, scaledHeight);

                if (!::scaledPixels || (scaledHeight != ::scaledHeight || scaledWidth != ::scaledWidth)) {
                    scaledPixels = new unsigned char[scaledWidth * scaledHeight * 4];
                    // 使用 stb_image_resize 进行缩放
                    stbir_resize_uint8(srcPixels.data(), srcWidth, srcHeight, srcWidth * 4,
                        scaledPixels, scaledWidth, scaledHeight, scaledWidth * 4,
                        4);  // 4 表示 RGBA 四个通道
                    ::scaledHeight = scaledHeight;
                    ::scaledWidth = scaledWidth;
                }

                // 创建缩放后的位图
                HBITMAP scaledBitmap = CreateBitmapFromPixels(memDC, scaledPixels, scaledWidth, scaledHeight);

                if (scaledBitmap) {
                    // 绘制缩放后的背景
                    HDC scaledDC = CreateCompatibleDC(memDC);
                    HBITMAP oldScaledBitmap = (HBITMAP)SelectObject(scaledDC, scaledBitmap);

                    BitBlt(memDC, 0, 0, dstWidth, dstHeight,
                        scaledDC, -scaledX, -scaledY, SRCCOPY);

                    SelectObject(scaledDC, oldScaledBitmap);
                    DeleteDC(scaledDC);
                    DeleteObject(scaledBitmap);
                }
            }
        }

        // 使用 AlphaBlend 将原内容绘制到内存 DC
        BLENDFUNCTION blf{};
        blf.SourceConstantAlpha = 200;
        blf.BlendOp = AC_SRC_OVER;
        AlphaBlend(memDC, dstX, dstY, dstWidth, dstHeight,
            sdc, dstX, dstY, dstWidth, dstHeight, blf);

        // 将内存 DC 的内容复制到目标 DC
        BitBlt(thdc, dstX, dstY, dstWidth, dstHeight, memDC, dstX, dstY, SRCCOPY);

        // 清理资源
        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);

        *((HDC*)pthis + 10) = 0;
        *((BYTE*)pthis + 56) = 0;
    }
    return 0;
}
BOOL APIENTRY DllMain(HMODULE hModule,
    DWORD  ul_reason_for_call,
    LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: {
        // 加载位图并缓存
        //g_hBitmap = LoadBitmapFromPNG("bg.png", g_width, g_height);
        //if (!g_hBitmap) {
        //    MessageBoxA(NULL, "Failed to load bg.png", "Error", MB_OK | MB_ICONERROR);
        //    break;
        //}

        auto func = FindSignatureModule("conhost.exe", R"a(
            40 53 48 83 EC 50 80 79  38 00 48 8B D9 0F 84 9D
            30 02 00 E8 B8 00 00 00  85 C0 0F 88 B5 30 02 00
            44 8B 43 60 8B 4B 68 8B  53 5C 41 2B C8 48 8B 83
            98 00 00 00 44 8B 4B 64  C7 44 24 40 20 00 CC 00
            44 2B CA 44 89 44 24 38  89 54 24 30 48 89 44 24
            28 89 4C 24 20 48 8B 4B  50 48 FF 15 78 B2 0E 00
            0F 1F 44 00 00 85 C0 0F  84 87 30 02 00 48 83 A3
            20 13 00 00 00 0F 57 C0  F3 0F 7F 83 28 13 00 00
            C6 83 38 13 00 00 00 48  FF 15 3A B2 0E 00 0F 1F
            44 00 00 85 C0 0F 84 7B  30 02 00 48 8B 53 50 48
            8B 4B 30 48 FF 15 6E B6  0E 00 0F 1F 44 00 00 85
            C0 0F 84 81 30 02 00 48  83 63 50 00 33 C0 C6 43
            )a");
        flushbufferlines = (decltype(flushbufferlines))FindSignatureModule("conhost.exe", R"a(
48 89 5C 24 18 55 56 57  41 54 41 55 41 56 41 57
48 83 EC 70 48 8B D9 45  33 ED 41 8B FD 4C 39 A9
B8 12 00 00 0F 86 89 02  00 00 41 8B ED 48 8D B1
40 01 00 00 4C 8D A1 98  00 00 00 4D 8B F4 4C 8D
7E 08 44 38 AB 45 13 00  00 74 76 41 B8 01 00 00
00 8B 16 49 8B 0F 48 FF  15 6B B2 0E 00 0F 1F 44
00 00 83 F8 01 75 57 48  8D 4E 14 44 8B 4E 10 41
0F BA E9 0C 48 8B 46 28  48 89 44 24 38 8B 06 89
44 24 30 49 8B 07 48 89  44 24 28 48 89 4C 24 20
44 8B 46 FC 8B 56 F8 48  8B 8B 98 00 00 00 48 FF
15 DB B1 0E 00 0F 1F 44  00 00 85 C0 0F 84 E4 00
00 00 4C 8D B3 98 00 00  00 E9 C3 00 00 00 4D 8B
F4 B8 20 00 00 00 66 89  84 24 B0 00 00 00 48 8D
84 24 B8 00 00 00 48 89  44 24 60 4C 89 6C 24 58
4C 89 6C 24 50 48 8B 46  28 48 89 44 24 48 48 8D
84 24 B0 00 00 00 48 89  44 24 40 4C 89 6C 24 38
)a");
        MH_Initialize();
        MH_CreateHook((LPVOID)func, Microsoft_Console_Render_GdiEngine_EndPaint, 0);
        MH_EnableHook((LPVOID)func);
    }
                           break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        // 清理全局位图
        if (g_hBitmap) {
            DeleteObject(g_hBitmap);
            g_hBitmap = nullptr;
        }
        break;
    }
    return TRUE;
}