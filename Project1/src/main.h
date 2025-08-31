#pragma once
// main.cpp  ——  优化后完整单文件
#define _WINSOCKAPI_
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#define WM_LOAD_ERROR (WM_USER + 3)
#include <windows.h>
#include <windowsx.h>   // 加这一行
#include <commctrl.h>
#include <shellapi.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#define _USE_MATH_DEFINES
#include <cmath>
#include <memory>
#include <string>
#include <future>
#include <unordered_map>
#include <algorithm>
#include <miniz/miniz.h>
#include <tinyxml2.h>
#include <lunasvg/lunasvg.h>    
using tinyxml2::XMLDocument;
using tinyxml2::XMLElement;
#include <litehtml.h>
//#include <litehtml/document.h>
//#include <litehtml/element.h>
//#include <litehtml/types.h>
//#include <litehtml/render_item.h>
//
//#include <litehtml/html_tag.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#pragma comment(lib, "freetype.lib")
#pragma comment(lib, "comctl32.lib")
#include <objidl.h>
#include <filesystem>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")
using namespace Gdiplus;
#pragma comment(lib, "comctl32.lib")
#include <shlwapi.h>
#include <regex>
#pragma comment(lib, "shlwapi.lib")

#include <wininet.h>
#include "resource.h"
#include <duktape.h>
#include <gumbo.h>
#include <string>
#include <sstream>
#include <vector>
#include <unordered_set>
#include <regex>
#include <chrono>
#include <thread>
#include <set>
#include <dwrite_3.h>
#include <d2d1.h>
#include <wrl/client.h>

#include <codecvt>
#include <locale>
#pragma comment(lib, "dwrite.lib")
#include <d2d1_3.h>        // ID2D1DeviceContext / ID2D1Bitmap1

#pragma comment(lib, "d2d1.lib")
#include <dwrite_1.h>   // 需要 IDWriteTextFormat1
#include <d2d1_1.h>       // D2D 1.1
#include <d3d11.h>        // D3D11
#include <dxgi1_2.h>  // DXGI 1.2

#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#include <cctype>

#include <queue>
#include <robuffer.h>   // IBufferByteAccess
#include <new>
#include <wrl.h>
#include <wrl/implements.h>   // 关键
#include <iostream>
//#include "js_runtime.h"
#pragma comment(lib, "windowscodecs.lib")
#ifndef HR
#define HR(hr)  do { HRESULT _hr_ = (hr); if(FAILED(_hr_)) return 0; } while(0)
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "3rdParty/stb_truetype.h"
#include <atomic>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <array>
#include <d2d1.h>
#include <d2d1helper.h>   // 保险起见，再带一次
#include <shared_mutex>
#include <cstdint>
#include <cwctype>
#include <locale>
#include <unicode/unistr.h>
#include <unicode/brkiter.h>
#include <unicode/utypes.h>
#include <unicode/uchar.h>
#include <unicode/utf8.h>
#include <string>
#include <functional>
#include <gumbo.h>
#include <cstring>
#include <stack>
#include <sstream>
using Microsoft::WRL::ComPtr;

namespace fs = std::filesystem;

struct ImageFrame
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;          // 每行字节数
    std::vector<uint8_t> rgba;     // 连续像素，8-bit * 4
};
struct FontKey {
    std::wstring family;
    int          weight;
    bool         italic;
    int          size;          // px
    bool operator==(const FontKey& o) const noexcept = default;
};
namespace std {
    template<>
    struct hash<FontKey> {
        size_t operator()(const FontKey& k) const noexcept {
            size_t h = std::hash<std::wstring>()(k.family);
            h ^= (k.weight << 1) | (k.italic ? 1 : 0);
            h ^= k.size;
            return h;
        }
    };
}

//struct FontKey {
//    std::wstring family;   // CSS 中声明的名字
//    int          weight;   // 400 / 700
//    bool         italic;   // true = italic
//    bool operator==(const FontKey& o) const noexcept {
//        return family == o.family && weight == o.weight && italic == o.italic;
//    }
//};
//namespace std {
//    template<> struct hash<FontKey> {
//        size_t operator()(const FontKey& k) const noexcept {
//            return hash<wstring>()(k.family) ^ (hash<int>()(k.weight) << 1) ^ (hash<bool>()(k.italic) << 2);
//        }
//    };
//}

// ---------- 字体缓存 ----------
struct FontPair {
    ComPtr<IDWriteTextFormat> format;
    litehtml::font_description descr;
};

// -------------- 运行时策略 -----------------
enum class Renderer { GDI, D2D, FreeType };
// -------------- 抽象接口 -----------------
class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;

    virtual void draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr hFont, litehtml::web_color color, const litehtml::position& pos) = 0;
    virtual void draw_image(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const std::string& url, const std::string& base_url) = 0;
    virtual void draw_solid_fill(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::web_color& color) = 0 ;
    virtual void draw_linear_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::background_layer::linear_gradient& gradient) = 0;
    virtual void draw_radial_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::background_layer::radial_gradient& gradient) = 0;
    virtual void draw_conic_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::background_layer::conic_gradient& gradient) = 0;
    virtual void draw_borders(litehtml::uint_ptr hdc, const litehtml::borders& borders, const litehtml::position& draw_pos, bool root) = 0;
    virtual void	draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker& marker) = 0;
    virtual litehtml::uint_ptr	create_font(const litehtml::font_description& descr, const litehtml::document* doc, litehtml::font_metrics* fm) = 0;
    virtual void				delete_font(litehtml::uint_ptr hFont) = 0;
    virtual litehtml::pixel_t	text_width(const char* text, litehtml::uint_ptr hFont) = 0;
    virtual	void set_clip(const litehtml::position& pos, const litehtml::border_radiuses& bdr_radius) = 0;
    virtual	void del_clip() = 0;

    //virtual litehtml::uint_ptr create_font(const char* faceName, int size, int weight, litehtml::font_style italic, unsigned int decoration, litehtml::font_metrics* fm) = 0;
    //virtual void draw_background(litehtml::uint_ptr, const std::vector<litehtml::background_paint>&, std::unordered_map<std::string, ImageFrame>&) = 0;
    
    virtual void load_all_fonts(std::vector<std::pair<std::wstring, std::vector<uint8_t>>>& fonts) = 0;
    virtual void resize(int width, int height) = 0;
    virtual std::set<std::wstring> getCurrentFonts() = 0;
    virtual void clear() = 0;
    virtual HBITMAP get_bitmap() = 0;
};

class AppBootstrap {
public:
    AppBootstrap();
    ~AppBootstrap();
    struct script_info
    {
        litehtml::element::ptr el;   // 只需要保留节点指针
    };

    void enableJS();
    void disableJS();
    void bind_host_objects();   // 新增
    //void make_tooltip_backend();
    void run_pending_scripts();


    std::vector<script_info> m_pending_scripts;

    //std::unique_ptr<js_runtime> m_jsrt;   // 替换裸 duk_context*
};





// -------------- GDI 后端 -----------------
class GdiBackend : public IRenderBackend {
public:
    explicit GdiBackend(int width, int height);
    /* 下面 7 个函数在 .cpp 里用 ExtTextOut / Rectangle 等实现 */
    void draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr hFont, litehtml::web_color color, const litehtml::position& pos) override;
    void draw_image(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const std::string& url, const std::string& base_url) override;
    void draw_solid_fill(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::web_color& color) override;
    void draw_linear_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::background_layer::linear_gradient& gradient) override;
    void draw_radial_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::background_layer::radial_gradient& gradient) override;
    void draw_conic_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::background_layer::conic_gradient& gradient) override;
    void draw_borders(litehtml::uint_ptr hdc, const litehtml::borders& borders, const litehtml::position& draw_pos, bool root) override;
    void	draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker& marker) override;
    litehtml::uint_ptr	create_font(const litehtml::font_description& descr, const litehtml::document* doc, litehtml::font_metrics* fm) override;
    void				delete_font(litehtml::uint_ptr hFont) override;
    litehtml::pixel_t	text_width(const char* text, litehtml::uint_ptr hFont) override;
    void	set_clip(const litehtml::position& pos, const litehtml::border_radiuses& bdr_radius) override;
    void	del_clip() override;

    void load_all_fonts(std::vector<std::pair<std::wstring, std::vector<uint8_t>>>& fonts);
    void resize(int width, int height) override;
    ~GdiBackend();
    std::set<std::wstring> getCurrentFonts() override;
    void clear() override;
    HBITMAP  get_bitmap() override { return nullptr; }
private:
    HDC m_hdc;
    HBITMAP m_memDC;
    // 缓存已创建的 HBITMAP，避免重复 GDI 转换
    std::unordered_map<std::string, HBITMAP> m_gdiBitmapCache;
    int m_w;
    int m_h;
    static COLORREF to_cr(const litehtml::web_color& c)
    {
        return RGB(c.red, c.green, c.blue);
    }

    // 把 ImageFrame 转成 32bpp HBITMAP
    HBITMAP create_dib_from_frame(const ImageFrame& frame);
};

// 全局缓存（也可放 D2DBackend 内）
struct LayoutKey {
    std::wstring txt;
    std::string  fontKey;
    float        maxW;
    bool operator==(const LayoutKey& o) const {
        return txt == o.txt && fontKey == o.fontKey && maxW == o.maxW;
    }
};
namespace std {
    template<> struct hash<LayoutKey> {
        size_t operator()(const LayoutKey& k) const noexcept {
            return hash<std::wstring>()(k.txt) ^
                hash<std::string>()(k.fontKey) ^
                hash<float>()(k.maxW);
        }
    };
}
class FontCache {
public:
    FontCache();
    ~FontCache() = default;
    // 主入口：根据 litehtml 描述 + 可选私有集合，返回 TextFormat
    Microsoft::WRL::ComPtr<IDWriteTextFormat>
        get(std::wstring& familyName, const litehtml::font_description& descr,
            IDWriteFontCollection* privateColl = nullptr, IDWriteFontCollection* sysColl = nullptr);
    void clear();
private:


    // 内部：真正创建
    Microsoft::WRL::ComPtr<IDWriteTextFormat>
        create(const FontKey& key, IDWriteFontCollection* privateColl, IDWriteFontCollection* sysColl);

    // 工具：在指定集合里找家族
    bool findFamily(IDWriteFontCollection* coll,
        const std::wstring& name,
        UINT32& index);

    std::unordered_map<FontKey, Microsoft::WRL::ComPtr<IDWriteTextFormat>> m_map;
    mutable std::shared_mutex                                            m_mtx;
    Microsoft::WRL::ComPtr<IDWriteFactory>                             m_dw;
    std::wstring                                                         m_defaultFamily;

};
// -------------- DirectWrite-D2D 后端 -----------------
class D2DBackend : public IRenderBackend {
public:
    D2DBackend(int w, int h, HWND hwnd);
    //D2DBackend(const D2DBackend&) = default;   // 或自己实现深拷贝


    void draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr hFont, litehtml::web_color color, const litehtml::position& pos) override;
  
    void draw_image(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const std::string& url, const std::string& base_url) override;

    void draw_solid_fill(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::web_color& color) override;
    void draw_linear_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::background_layer::linear_gradient& gradient) override;
    void draw_radial_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::background_layer::radial_gradient& gradient) override;
    void draw_conic_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::background_layer::conic_gradient& gradient) override;
    void draw_borders(litehtml::uint_ptr hdc, const litehtml::borders& borders, const litehtml::position& draw_pos, bool root) override;
    void	draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker& marker) override;
    litehtml::uint_ptr	create_font(const litehtml::font_description& descr, const litehtml::document* doc, litehtml::font_metrics* fm) override;
    void				delete_font(litehtml::uint_ptr hFont) override;
    litehtml::pixel_t	text_width(const char* text, litehtml::uint_ptr hFont) override;
    void	set_clip(const litehtml::position& pos, const litehtml::border_radiuses& bdr_radius) override;
    void	del_clip() override;

    void load_all_fonts(std::vector<std::pair<std::wstring, std::vector<uint8_t>>>& fonts);
    void unload_fonts(void);
    void resize(int width, int height) override;
    //std::optional<std::wstring> mapDynamic(const std::wstring& key);
    //std::wstring resolveFace(const std::wstring& raw);
    std::vector<std::wstring> split_font_list(const std::string& src);
    ComPtr<ID2D1HwndRenderTarget> m_rt;
    bool is_all_zero(const litehtml::border_radiuses& r);
    std::set<std::wstring> getCurrentFonts() override;
    void clear() override;
    HBITMAP  get_bitmap() override;
    ComPtr<ID2D1HwndRenderTarget> m_d2dRT = nullptr;   // ← 注意是 HwndRenderTarget 
private:
    // 自动 AddRef/Release

    Microsoft::WRL::ComPtr<IDWriteFontCollection> m_privateFonts;  // 新增
    std::vector<std::wstring> m_tempFontFiles;
    std::unordered_map<std::string, ComPtr<ID2D1Bitmap>> m_d2dBitmapCache;

    static std::wstring toLower(std::wstring s);
    //static std::optional<std::wstring> mapStatic(const std::wstring& key);
    float m_baselineY = 0;
    std::vector<ComPtr<ID2D1Layer>>  m_clipStack;  // 新增
    ComPtr<IDWriteFontCollection> m_sysFontColl;
    int  m_w, m_h;
    ComPtr<ID2D1SolidColorBrush> getBrush(const litehtml::web_color& c);

    ComPtr<IDWriteTextLayout> getLayout(const std::wstring& txt, const FontPair* fp, float maxW);
    void draw_decoration(const FontPair* fp, litehtml::web_color color, const litehtml::position& pos, IDWriteTextLayout* layout);

    std::unordered_map<LayoutKey, ComPtr<IDWriteTextLayout>> m_layoutCache;
    std::unordered_map<uint32_t, ComPtr<ID2D1SolidColorBrush>> m_brushPool;
    FontCache m_fontCache;


    ComPtr<IDWriteFactory>    m_dwrite;
    ComPtr<ID2D1Factory1> m_d2dFactory = nullptr;   // 原来是 ID2D1Factory = nullptr;

    HWND m_hwnd = nullptr;
};

// -------------- FreeType 后端 -----------------
class FreetypeBackend : public IRenderBackend {
public:
    using RasterCB = std::function<void(int, int, const uint8_t*, int, int)>;
    FreetypeBackend(int w, int h, int dpi,
        uint8_t* surface, int stride,
        FT_Library lib);
    void draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr hFont, litehtml::web_color color, const litehtml::position& pos) override;
    void draw_image(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const std::string& url, const std::string& base_url) override;
    void draw_solid_fill(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::web_color& color) override;
    void draw_linear_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::background_layer::linear_gradient& gradient) override;
    void draw_radial_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::background_layer::radial_gradient& gradient) override;
    void draw_conic_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::background_layer::conic_gradient& gradient) override;
    void draw_borders(litehtml::uint_ptr hdc, const litehtml::borders& borders, const litehtml::position& draw_pos, bool root) override;
    void	draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker& marker) override;
    litehtml::uint_ptr	create_font(const litehtml::font_description& descr, const litehtml::document* doc, litehtml::font_metrics* fm) override;
    void				delete_font(litehtml::uint_ptr hFont) override;
    litehtml::pixel_t	text_width(const char* text, litehtml::uint_ptr hFont) override;
    void	set_clip(const litehtml::position& pos, const litehtml::border_radiuses& bdr_radius) override;
    void	del_clip() override;

    void load_all_fonts(std::vector<std::pair<std::wstring, std::vector<uint8_t>>>& fonts);
    void resize(int width, int height) override;
    std::set<std::wstring> getCurrentFonts() override;
    void clear() override;
    HBITMAP  get_bitmap() override { return nullptr; }
    ~FreetypeBackend();
private:
    int m_w, m_h, m_dpi;
    RasterCB m_raster;
    uint8_t* m_surface;
    int m_stride;
    FT_Library m_lib;
    void blit_glyph(int x, int y,
        const FT_Bitmap& bmp,
        litehtml::web_color c);
    void fill_rect(const litehtml::position& rc,
        litehtml::web_color c);
    std::vector<FT_Face>  m_faces;          // 已加载的 FreeType 字体
    std::vector<std::vector<uint8_t>> m_fontBlobs; // 保持内存常驻
    // 缓存已转换的 FT_Bitmap，避免重复解析
    std::unordered_map<std::string, FT_Bitmap> m_ftBitmapCache;
    /* 把 ImageFrame → FT_Bitmap（8-bit 灰度或 32-bit RGBA） */
    static FT_Bitmap make_ft_bitmap(const ImageFrame& frame)
    {
        FT_Bitmap ftBmp = {};
        ftBmp.rows = static_cast<unsigned>(frame.height);
        ftBmp.width = static_cast<unsigned>(frame.width);
        ftBmp.pitch = static_cast<int>(frame.stride);
        ftBmp.buffer = const_cast<unsigned char*>(frame.rgba.data());
        ftBmp.pixel_mode = FT_PIXEL_MODE_BGRA;   // 如果你后端是 RGBA，可改
        ftBmp.num_grays = 256;
        return ftBmp;
    }
    void draw_image(const ImageFrame& frame,
        const litehtml::position& dst);
};

class ICanvas {
public:
    virtual ~ICanvas() = default;

    /* 返回一个 IRenderBackend，用于 litehtml 绘制 */
    virtual IRenderBackend* backend() = 0;

    /* 把画布内容贴到窗口（WM_PAINT 用）*/
    virtual void present(HDC hdc, int x, int y) = 0;

    /* 尺寸 */
    virtual int  width()  const = 0;
    virtual int  height() const = 0;

    virtual litehtml::uint_ptr getContext() = 0;
    virtual void BeginDraw() = 0;
    virtual void EndDraw() = 0;
    virtual void resize(int width, int height) = 0;
    virtual std::set<std::wstring> getCurrentFonts() = 0;
    virtual void clear() = 0;
    virtual HBITMAP  get_bitmap() = 0;
    /* 工厂：根据当前策略创建画布 */
    //static std::unique_ptr<ICanvas> create(int w, int h, Renderer which);
};




class GdiCanvas : public ICanvas {
public:
    GdiCanvas(int w, int h);
    ~GdiCanvas();
    IRenderBackend* backend() override { return m_backend.get(); }
    void present(HDC hdc, int x, int y) override;
    int width()  const override { return m_w; }
    int height() const override { return m_h; }
    litehtml::uint_ptr getContext() override;
    void BeginDraw() override;
    void EndDraw() override;
    void resize(int width, int height) override;
    std::set<std::wstring> getCurrentFonts() override;
    void clear() override;
    HBITMAP  get_bitmap() override { return nullptr; }
private:
    int  m_w, m_h;
    HDC  m_memDC;
    HBITMAP m_bmp, m_old;
    std::unique_ptr<GdiBackend> m_backend;
};

class D2DCanvas : public ICanvas {
public:
    D2DCanvas(int w, int h, HWND hwnd);
    IRenderBackend* backend() override { return m_backend.get(); }
    void present(HDC hdc, int x, int y) override;
    int width()  const override { return m_w; }
    int height() const override { return m_h; }
    litehtml::uint_ptr getContext() override;
    void BeginDraw() override;
    void EndDraw() override;
    void resize(int width, int height) override;
    std::set<std::wstring> getCurrentFonts() override;
    void clear() override;
    HBITMAP  get_bitmap() override;
private:
    int  m_w, m_h;
    ComPtr<ID2D1Bitmap> m_bmp;
    std::unique_ptr<D2DBackend> m_backend;

    HWND m_hwnd = nullptr;

};

class FreetypeCanvas : public ICanvas {
public:
    FreetypeCanvas(int w, int h, int dpi);
    ~FreetypeCanvas();
    IRenderBackend* backend() override { return m_backend.get(); }
    void present(HDC hdc, int x, int y) override;
    int width()  const override { return m_w; }
    int height() const override { return m_h; }
    litehtml::uint_ptr getContext() override;
    void BeginDraw() override;
    void EndDraw() override;
    void resize(int width, int height) override;
    std::set<std::wstring> getCurrentFonts() override;
    void clear() override;
    HBITMAP  get_bitmap() override { return nullptr; }
private:
    int  m_w, m_h, m_dpi;
    std::vector<uint8_t>        m_pixels;   // 4*w*h
    std::unique_ptr<FreetypeBackend> m_backend;
};

class Paginator {
public:
    void load(litehtml::document* doc, int w, int h);
    void render(ICanvas* canvas, int scrollY);   // 关键：不再区分 HDC / RT
    void clear();
private:
    litehtml::document* m_doc = nullptr;
    int m_w = 0, m_h = 0;


};

// -------------- 新增数据结构 --------------
struct OCFItem {
    std::wstring id, href, media_type, properties;
};
struct OCFRef {
    std::wstring idref, href, linear = L"yes";
};
struct OCFNavPoint {
    std::wstring label, href;
    int order = 0;
};
struct OCFPackage {
    std::wstring rootfile;                // OPF 绝对路径
    std::wstring opf_dir;                 // 目录，带 '/'
    std::vector<OCFItem>   manifest;
    std::vector<OCFRef>    spine;
    std::vector<OCFNavPoint> toc;
    std::map<std::wstring, std::wstring> meta;
    std::wstring toc_path;
};

struct TreeNode {
    const OCFNavPoint* nav;   // 只读引用
    std::vector<size_t> childIdx; // 子节点索引
};

class ZipIndexW {
public:
    ZipIndexW() = default;
    explicit ZipIndexW(mz_zip_archive& zip);

    // 输入/输出均为 std::wstring
    std::wstring find(std::wstring href) const;

private:
    /* ---------- 大小写不敏感哈希/比较 ---------- */
    struct StringHashI {
        size_t operator()(std::wstring s) const noexcept {
            size_t h = 0;
            for (wchar_t c : s)
                h = h * 131 + towlower(static_cast<wint_t>(c));
            return h;
        }
    };

    struct StringEqualI {
        bool operator()(std::wstring a, std::wstring b) const noexcept {
            return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                [](wchar_t c1, wchar_t c2) {
                    return towlower(static_cast<wint_t>(c1)) ==
                        towlower(static_cast<wint_t>(c2));
                });
        }
    };


    using Map = std::unordered_map<std::wstring, std::wstring,
        StringHashI, StringEqualI>;

    Map map_;



    /* ---------- 正规化路径 ---------- */
    static std::wstring url_decode(const std::wstring& in);

    static std::wstring normalize_key(std::wstring href);

    /* ---------- 建立索引 ---------- */
    void build(mz_zip_archive& zip);



};

struct MemFile {
    std::vector<uint8_t> data;
    const char* begin() const { return reinterpret_cast<const char*>(data.data()); }
    size_t      size()  const { return data.size(); }
};
// ---------- EPUB 零解压 ----------
class EPUBBook {
    public:
        mz_zip_archive zip = {};
        std::map<std::wstring, MemFile> cache;
        OCFPackage ocf_pkg_;                     // 解析结果
        ZipIndexW m_zipIndex;
        // -------------- EPUBBook 内部新增成员 --------------

        void parse_ocf_(void);                       // 主解析入口
        void parse_opf_(void);   // 解析 OPF
        void parse_toc_(void);                        // 解析 TOC


        void OnTreeSelChanged(const wchar_t* href);
        bool load(const wchar_t* epub_path);
        MemFile read_zip(const wchar_t* file_name) const;
        std::string load_html(const std::wstring& path) const;

        void load_all_fonts(void);
        void init_doc(std::string html);
 

        static std::wstring extract_text(const tinyxml2::XMLElement* a);

        // 递归解析 EPUB3-Nav <ol>
        void parse_nav_list(tinyxml2::XMLElement* ol, int level,
            const std::string& opf_dir,
            std::vector<OCFNavPoint>& out);


        // 递归解析 NCX <navPoint>
        void parse_ncx_points(tinyxml2::XMLElement* navPoint, int level,
            const std::string& opf_dir,
            std::vector<OCFNavPoint>& out);

        void show_tooltip(const std::string html, int x, int y);
        void hide_tooltip();
        static std::string get_anchor_html(litehtml::document* doc, const std::string& anchor);
        void clear();

        HWND                   m_tooltip{ nullptr };   // 你的缩略图窗口
        std::wstring           m_tooltip_url;          // 缓存当前 url
        std::vector<std::pair<std::wstring, std::vector<uint8_t>>> collect_epub_fonts();
        std::wstring get_font_family_name(const std::vector<uint8_t>& data);
        void build_epub_font_index(const OCFPackage& pkg, EPUBBook* book);
        std::unordered_map<FontKey, std::wstring> m_fontBin;

        EPUBBook() noexcept {}
        ~EPUBBook();
    private:
        static HTREEITEM InsertTreeNodeLazy(HWND, const TreeNode&, const std::vector<TreeNode>&, HTREEITEM);
        void BuildTree(const std::vector<OCFNavPoint>&, std::vector<TreeNode>&, std::vector<size_t>&);
        void FreeTreeData(HWND tv);
        void LoadToc();
        std::vector<TreeNode> m_nodes;
        std::vector<size_t>   m_roots;
};


struct AppSettings {
    bool enableCSS = true;   // 默认启用 css
    bool enableJS = false;   // 默认禁用 JS
    bool enableGlobalCSS = false;
    bool enablePreprocessHTML = true;
    bool displayTOC = true;
    bool displayStatusBar = true;
    bool displayMenuBar = true;
    bool displayScrollBar = true;
    Renderer fontRenderer = Renderer::D2D;
    std::string default_font_name = "Cambria";
    int default_font_size = 16;
    float line_height_multiplier = 1.5;
    int tooltip_width = 350;
    int document_width = 600;
    int split_space_height = 300; // 单位:px
    std::wstring default_serif = L"Georgia";
    std::wstring default_sans_serif = L"Verdana";
    std::wstring default_monospace = L"Consolas";
};
struct AppStates {
    // ---- 取消令牌 ----
    std::shared_ptr<std::atomic_bool> cancelToken;

    // ---- 状态机 ----
    std::atomic_bool needRelayout{ true };   // 是否需要重新排版
    std::atomic_bool isCaching{ false };   // 后台是否正在渲染

    bool isLoaded = false;
    // 工具：生成新令牌，旧令牌立即失效
    void newCancelToken() {
        if (cancelToken) cancelToken->store(true);
        cancelToken = std::make_shared<std::atomic_bool>(false);
    }
};
//static  std::unordered_map<std::wstring, std::wstring> g_fontAlias = {
//    /* 英文字体 */
//    {L"charis",               L"Charis SIL"},
//    {L"charis sil",           L"Charis SIL"},
//    {L"times",                L"Times New Roman"},
//    {L"times new roman",      L"Times New Roman"},
//    {L"timesnewroman",        L"Times New Roman"},
//    {L"arial",                L"Arial"},
//    {L"helvetica",            L"Arial"},           // 在 Windows 上 Helvetica 映射到 Arial
//    {L"verdana",              L"Verdana"},
//    {L"tahoma",               L"Tahoma"},
//    {L"georgia",              L"Georgia"},
//    {L"garamond",             L"Garamond"},
//    {L"palatino",             L"Palatino Linotype"},
//    {L"palatino linotype",    L"Palatino Linotype"},
//    {L"courier",              L"Courier New"},
//    {L"courier new",          L"Courier New"},
//    {L"consolas",             L"Consolas"},
//    {L"lucida console",       L"Lucida Console"},
//    {L"lucida sans unicode",  L"Lucida Sans Unicode"},
//    {L"comic sans",           L"Comic Sans MS"},
//    {L"comic sans ms",        L"Comic Sans MS"},
//    {L"impact",               L"Impact"},
//    {L"trebuchet",            L"Trebuchet MS"},
//    {L"trebuchet ms",          L"Trebuchet MS"},
//    {L"franklin gothic",      L"Franklin Gothic Medium"},
//    {L"tradegothicltstd",        L"Trade Gothic LT Std"},
//    {L"bodoniegyptian-regular", L"BodoniEgyptian"},
//    {L"nobel-regular",           L"Nobel"},
//    {L"nsannotations500-mono" , L"NSAnnotations500 Monospace500"},
//    /* 思源 / 开源无衬线 */
//    {L"source sans",           L"Source Sans Pro"},
//    {L"source sans pro",       L"Source Sans Pro"},
//    {L"source serif",          L"Source Serif Pro"},
//    {L"source serif pro",      L"Source Serif Pro"},
//    {L"source code",           L"Source Code Pro"},
//    {L"source code pro",       L"Source Code Pro"},
//
//    /* 等宽 / 编程字体 */
//    {L"fira code",             L"Fira Code"},
//    {L"fira mono",             L"Fira Mono"},
//    {L"jetbrains mono",        L"JetBrains Mono"},
//    {L"cascadia code",         L"Cascadia Code"},
//    {L"cascadia mono",         L"Cascadia Mono"},
//    {L"roboto mono",           L"Roboto Mono"},
//    {L"inconsolata",           L"Inconsolata"},
//
//    /* 中文字体（简体） */
//    {L"simsun",                L"SimSun"},
//    {L"songti",                L"SimSun"},
//    {L"宋体",                  L"SimSun"},
//    {L"simhei",                L"SimHei"},
//    {L"黑体",                  L"SimHei"},
//    {L"microsoft yahei",       L"Microsoft YaHei"},
//    {L"yahei",                 L"Microsoft YaHei"},
//    {L"微软雅黑",               L"Microsoft YaHei"},
//    {L"dengxian",               L"DengXian"},
//    {L"等线",                  L"DengXian"},
//    {L"kaiti",                 L"KaiTi"},
//    {L"kaiti sc",              L"KaiTi"},
//    {L"楷体",                  L"KaiTi"},
//    {L"fangsong",              L"FangSong"},
//    {L"fangsong sc",           L"FangSong"},
//    {L"仿宋",                  L"FangSong"},
//    {L"lisu",                  L"LiSu"},
//    {L"隶书",                  L"LiSu"},
//    {L"hy-xiaolishu",          L"HYXiaoLiShu_GB18030Super"},
//
//
//    /* 中文字体（繁体） */
//    {L"mingliu",               L"MingLiU"},
//    {L"pmingliu",              L"PMingLiU"},
//    {L"mingliuhkscs",          L"MingLiU_HKSCS"},
//    {L"標楷體",                L"DFKai-SB"},
//
//    /* 日文字体 */
//    {L"ms gothic",             L"MS Gothic"},
//    {L"ms mincho",             L"MS Mincho"},
//    {L"yu gothic",             L"Yu Gothic"},
//    {L"yu mincho",             L"Yu Mincho"},
//    {L"meiryo",                L"Meiryo"},
//    {L"メイリオ",               L"Meiryo"},
//
//    /* 韩文字体 */
//    {L"malgun gothic",         L"Malgun Gothic"},
//    {L"malgun",                L"Malgun Gothic"},
//    {L"맑은 고딕",             L"Malgun Gothic"},
//    {L"batang",                L"Batang"},
//    {L"gulim",                 L"Gulim"},
//    {L"dotum",                 L"Dotum"}
//};







// 把整棵树存到 LPARAM 里
struct TVData {
    const TreeNode* node;
    const std::vector<TreeNode>* all;
    bool inserted = false;   // 新增
};

// 全局索引 ----------------------------------------------------------





class IFileProvider {
public:
    virtual ~IFileProvider() = default;
    virtual bool load(const std::wstring& file_path) = 0;
    // 按路径返回原始二进制
    virtual std::vector<uint8_t> get_data(const std::wstring& path) const = 0;
};



class RenderWorker {
public:
    RenderWorker();
    void push(int w, int h);
    void stop();
    ~RenderWorker();
private:

    void loop();
    struct Task { int w, h; };

    std::queue<Task> m_taskQ;
    std::mutex m_qMtx;
    std::condition_variable m_qCV;
    std::thread m_worker;
    bool m_stop = false;
};






// ---------- LiteHtml 容器 ----------
class SimpleContainer : public litehtml::document_container {
public:
    explicit SimpleContainer(HWND hwnd);
    ~SimpleContainer();
    void clear();

    litehtml::pixel_t	get_default_font_size() const override;
    const char* get_default_font_name() const override;

    void	load_image(const char* src, const char* baseurl, bool redraw_on_ready) override;
    void	get_image_size(const char* src, const char* baseurl, litehtml::size& sz) override;
    void	get_viewport(litehtml::position& viewport) const override;
    void	import_css(litehtml::string& text, const litehtml::string& url, litehtml::string& baseurl) override;

    void	set_caption(const char* caption) override;
    void	set_base_url(const char* base_url) override;
    void	link(const std::shared_ptr<litehtml::document>& doc, const litehtml::element::ptr& el) override;

    void	set_cursor(const char* cursor) override;
    void	transform_text(litehtml::string& text, litehtml::text_transform tt) override;

    litehtml::element::ptr	create_element(const char* tag_name, const litehtml::string_map& attributes, const std::shared_ptr<litehtml::document>& doc) override;

    void	get_media_features(litehtml::media_features& media) const override;
    void	get_language(litehtml::string& language, litehtml::string& culture) const override;

    // 事件
    void	on_anchor_click(const char* url, const litehtml::element::ptr& el) override;
    bool	on_element_click(const litehtml::element::ptr& /*el*/) override;
    void	on_mouse_event(const litehtml::element::ptr& el, litehtml::mouse_event event) override;

    // litehtml 新增
    void	split_text(const char* text, const std::function<void(const char*)>& on_word, const std::function<void(const char*)>& on_space) override;
    litehtml::string resolve_color(const litehtml::string& /*color*/) const { return litehtml::string(); }

    litehtml::pixel_t	pt_to_px(float pt) const override;


    // 渲染后端需要实现的
    void draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr hFont, litehtml::web_color color, const litehtml::position& pos) override;
    void draw_image(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const std::string& url, const std::string& base_url) override;
    void draw_solid_fill(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::web_color& color) override;
    void draw_linear_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::background_layer::linear_gradient& gradient) override;
    void draw_radial_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::background_layer::radial_gradient& gradient) override;
    void draw_conic_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::background_layer::conic_gradient& gradient) override;
    void draw_borders(litehtml::uint_ptr hdc, const litehtml::borders& borders, const litehtml::position& draw_pos, bool root) override;
    void	draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker& marker) override;
    litehtml::uint_ptr	create_font(const litehtml::font_description& descr, const litehtml::document* doc, litehtml::font_metrics* fm) override;
    void				delete_font(litehtml::uint_ptr hFont) override;
    litehtml::pixel_t	text_width(const char* text, litehtml::uint_ptr hFont) override;
    void	set_clip(const litehtml::position& pos, const litehtml::border_radiuses& bdr_radius) override;
    void	del_clip() override;


    //自定义函数
    void makeBackend(HWND hwnd);
    void resize(int w, int y);

    std::unordered_map<std::string, ImageFrame> m_img_cache;
    std::unordered_map<std::string, litehtml::element::ptr> m_anchor_map;
    std::shared_ptr<litehtml::document> m_doc;
    std::unique_ptr<ICanvas> m_canvas;
    //std::unique_ptr<RenderWorker> m_render_worker;

private:
    std::wstring m_root;
    // 1. 锚点表（id -> element）

    // 2. 最后一次传入的 HDC，用于 set_clip / del_clip
    HDC m_last_hdc = nullptr;
    HWND m_hwnd = nullptr;
    // 3. 默认字体句柄（FontWrapper 是你自己的字体包装类）
    HFONT m_hDefaultFont = nullptr;



};





struct BodyBlock {
    int spine_id = 0;
    int block_id = 0;
    std::string html;
    int height = 0; // 未渲染前默认 -1
    bool is_render = false;
};

struct HtmlBlock {
    int height = 0;
    std::string head;
    std::vector<BodyBlock> body_blocks;
    void clear() { height = 0; head = ""; body_blocks.clear(); }
};




class VirtualDoc {
public:
    VirtualDoc();
    void load_book(std::shared_ptr<EPUBBook> book, std::shared_ptr<SimpleContainer> container, int render_width);
    void set_render_width(int width);
    litehtml::document::ptr get_doc(int client_h, int& scrollY, int& y_offset);
    void load_html(std::wstring& href);
    void clear();
private:

    void calculate_height(HtmlBlock& block);

    void calculate_block_height(std::string& head, BodyBlock& block);

    HtmlBlock get_html_block(std::string html, int spine_id);
    void merge_block(HtmlBlock& dst, HtmlBlock& src, bool isAddToBottom=true);
    std::string get_head(std::string& html);
    std::vector<BodyBlock> get_body_blocks(std::string& html, int spine_id = 0, size_t max_chunk_bytes = 1024);
    void serialize_node(const GumboNode* node, std::ostream& out);
    bool gumbo_tag_is_void(GumboTag tag);
    void serialize_element(const GumboElement& el, std::ostream& out);
    int get_id_by_href(std::wstring& href);
    std::wstring get_href_by_id(int spine_id);

    void add_top(int& y_offset);
    void add_bottom();
    void remove_top( int& y_offset);
    void remove_bottom();
    void load_by_id(HtmlBlock& dst, int spine_id, bool isAddToBottom);


    litehtml::document::ptr m_doc;
    std::vector<OCFRef> m_spine;
    std::shared_ptr<EPUBBook> m_book;
    std::shared_ptr<SimpleContainer> m_container;
    int m_render_width;

    HtmlBlock m_render_block;
    HtmlBlock m_top_block;
    HtmlBlock m_bottom_block;

};