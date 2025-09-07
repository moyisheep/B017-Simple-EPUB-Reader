#pragma once
// main.cpp  ——  优化后完整单文件
#define _WINSOCKAPI_
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <windowsx.h>   // 加这一行
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
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
#include <litehtml\render_item.h>


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
#include <sqlite3.h>
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
#include <vector>
#include <algorithm>
#include <unordered_set>
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
#include <unicode/rbbi.h>
#include <unicode/ubrk.h>
#include <unicode/ustring.h>
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

// 一个字符在窗口坐标系中的包围盒
struct CharBox
{
    wchar_t  ch;
    D2D1_RECT_F rect;   // 左上角 (x,y) 右下角 (x+width,y+height)
    size_t   offset; // 在整篇纯文本中的偏移
};

// 一行文本的所有字符
using LineBoxes = std::vector<CharBox>;

// ---------- 字体缓存 ----------
struct FontPair {
    ComPtr<IDWriteTextFormat> format;
    litehtml::font_description descr;
};

// -------------- 运行时策略 -----------------
enum class Renderer { GDI, D2D};
// -------------- 抽象接口 -----------------
class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;

    virtual void draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr hFont, litehtml::web_color color, const litehtml::position& pos) = 0;
    virtual void draw_image(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const std::string& url, const std::string& base_url) = 0;
    virtual void draw_solid_fill(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::web_color& color) = 0;
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

 
    virtual void load_all_fonts(std::vector<std::pair<std::wstring, std::vector<uint8_t>>>& fonts) = 0;


    virtual void clear() = 0;
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

    ~GdiBackend();

    void clear() override;
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

class FileCollectionLoader : public IDWriteFontCollectionLoader
{
    LONG ref_ = 1;
public:
    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteFontCollectionLoader))
        {
            *ppv = this; AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref_); }
    IFACEMETHODIMP_(ULONG) Release() override
    {
        ULONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }

    // IDWriteFontCollectionLoader
    IFACEMETHODIMP CreateEnumeratorFromKey(
        IDWriteFactory* factory,
        void const* key, UINT32 keySize,
        IDWriteFontFileEnumerator** ppEnumerator) override
    {
        *ppEnumerator = new FileEnumerator(
            factory,
            reinterpret_cast<IDWriteFontFile* const*>(key),
            keySize / sizeof(IDWriteFontFile*));
        return *ppEnumerator ? S_OK : E_OUTOFMEMORY;
    }

private:
    class FileEnumerator : public IDWriteFontFileEnumerator
    {
        IDWriteFactory* fac_;
        std::vector<ComPtr<IDWriteFontFile>> files_;
        UINT32 idx_ = 0;
        LONG ref_ = 1;
    public:
        FileEnumerator(IDWriteFactory* f, IDWriteFontFile* const* files, UINT32 n)
            : fac_(f), files_(files, files + n) {
        }

        IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override
        {
            if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteFontFileEnumerator))
            {
                *ppv = this; AddRef(); return S_OK;
            }
            *ppv = nullptr; return E_NOINTERFACE;
        }
        IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref_); }
        IFACEMETHODIMP_(ULONG) Release() override
        {
            ULONG r = InterlockedDecrement(&ref_);
            if (r == 0) delete this;
            return r;
        }

        IFACEMETHODIMP MoveNext(BOOL* hasCurrent) override
        {
            *hasCurrent = idx_ < files_.size();
            return S_OK;
        }
        IFACEMETHODIMP GetCurrentFontFile(IDWriteFontFile** file) override
        {
            *file = idx_ < files_.size() ? files_[idx_++].Get() : nullptr;
            if (*file) (*file)->AddRef();
            return S_OK;
        }
    };
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
struct FontCachePair 
{
    ComPtr<IDWriteTextFormat> fmt;
    litehtml::font_metrics fm ;
};
class FontCache {
public:
    FontCache();
    ~FontCache() = default;
    // 主入口：根据 litehtml 描述 + 可选私有集合，返回 TextFormat
    FontCachePair
        get(std::wstring& familyName, const litehtml::font_description& descr, IDWriteFontCollection* sysColl = nullptr);
    ComPtr<IDWriteFontCollection> CreatePrivateCollectionFromFile(IDWriteFactory* dw, const wchar_t* path);

    void clear();
private:


    // 内部：真正创建
    FontCachePair
        create(const FontKey& key, IDWriteFontCollection* sysColl);

    // 工具：在指定集合里找家族
    bool findFamily(IDWriteFontCollection* coll,
        const std::wstring& name,
        Microsoft::WRL::ComPtr<IDWriteFontFamily>& family,
        UINT32& index);

    std::unordered_map<FontKey, FontCachePair> m_map;
    mutable std::shared_mutex              m_mtx;
    Microsoft::WRL::ComPtr<IDWriteFactory>   m_dw;
    std::unordered_map<std::wstring_view, ComPtr<IDWriteFontCollection>> collCache;
    FileCollectionLoader* m_loader;

};
// -------------- DirectWrite-D2D 后端 -----------------
class D2DBackend : public IRenderBackend {
public:
    D2DBackend();
  
    //D2DBackend(const D2DBackend&) = default;   // 或自己实现深拷贝




    void record_char_boxes(ID2D1RenderTarget* rt, IDWriteTextLayout* layout, const std::wstring& wtxt, const litehtml::position& pos);



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
    static void build_rounded_rect_path(ComPtr<ID2D1GeometrySink>& sink, const litehtml::position& pos, const litehtml::border_radiuses& bdr);
    void	set_clip(const litehtml::position& pos, const litehtml::border_radiuses& bdr_radius) override;
    void	del_clip() override;


    void load_all_fonts(std::vector<std::pair<std::wstring, std::vector<uint8_t>>>& fonts);
    void unload_fonts(void);

    //std::optional<std::wstring> mapDynamic(const std::wstring& key);
    //std::wstring resolveFace(const std::wstring& raw);
    std::vector<std::wstring> split_font_list(const std::string& src);
    //ComPtr<ID2D1HwndRenderTarget> m_rt;
    bool is_all_zero(const litehtml::border_radiuses& r);

    void clear() override;
    void make_font_metrics(const ComPtr<IDWriteFont>& dwFont, const litehtml::font_description& descr, litehtml::font_metrics* fm);
 
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
    ComPtr<ID2D1SolidColorBrush> getBrush(litehtml::uint_ptr hdc, const litehtml::web_color& c);

    ComPtr<IDWriteTextLayout> getLayout(const std::wstring& txt, const FontPair* fp, float maxW);

    void draw_decoration(litehtml::uint_ptr hdc, const FontPair* fp, litehtml::web_color color, const litehtml::position& pos, IDWriteTextLayout* layout);

    std::unordered_map<LayoutKey, ComPtr<IDWriteTextLayout>> m_layoutCache;
    std::unordered_map<uint32_t, ComPtr<ID2D1SolidColorBrush>> m_brushPool;
    FontCache m_fontCache;


    ComPtr<IDWriteFactory>    m_dwrite;

    ComPtr<IDWriteTextAnalyzer> m_analyzer;

};


class ICanvas {
public:
    virtual ~ICanvas() = default;

    /* 返回一个 IRenderBackend，用于 litehtml 绘制 */

    /* 把画布内容贴到窗口（WM_PAINT 用）*/
    virtual void present(int x, int y, litehtml::position* clip) = 0;

    /* 尺寸 */
    virtual int  width()  const = 0;
    virtual int  height() const = 0;

    virtual litehtml::uint_ptr getContext() = 0;
    virtual void BeginDraw() = 0;
    virtual void EndDraw() = 0;
    virtual void resize(int width, int height) = 0;

    virtual void clear() = 0;

    /* 工厂：根据当前策略创建画布 */
    //static std::unique_ptr<ICanvas> create(int w, int h, Renderer which);
};




class GdiCanvas : public ICanvas {
public:
    GdiCanvas(int w, int h);
    ~GdiCanvas();

    void present(int x, int y, litehtml::position* clip) override {};
    int width()  const override { return m_w; }
    int height() const override { return m_h; }
    litehtml::uint_ptr getContext() override;
    void BeginDraw() override;
    void EndDraw() override;
    void resize(int width, int height) override;

    void clear() override;


private:
    int  m_w, m_h;
    HDC  m_memDC;
    HBITMAP m_bmp, m_old;
    std::unique_ptr<GdiBackend> m_backend;
};

class D2DCanvas : public ICanvas {
public:
    D2DCanvas(int w, int h, HWND hwnd);

    std::vector<RECT> get_selection_rows() const;

    void present(int x, int y, litehtml::position* clip) override;
    int width()  const override { return m_w; }
    int height() const override { return m_h; }
    litehtml::uint_ptr getContext() override;
    void BeginDraw() override;
    void EndDraw() override;
    void resize(int width, int height) override;

    void clear_selection();

    void on_lbutton_dblclk(int x, int y);
    void on_lbutton_up();
    void on_lbutton_down(int x, int y);
    void on_mouse_move(int x, int y);
    void copy_to_clipboard();
  


    void clear() override;
    ComPtr<ID2D1HwndRenderTarget> m_rt;
    litehtml::document::ptr m_doc;
    float m_zoom_factor = 1.0f;

private:
    int  m_w, m_h;
    ComPtr<ID2D1Bitmap> m_bmp;
    D2D1_MATRIX_3X2_F m_oldMatrix{};
    HWND m_hwnd = nullptr;
    ComPtr<ID2D1Factory1> m_d2dFactory = nullptr;   // 原来是 ID2D1Factory = nullptr;

    int64_t hit_test(float x, float y);

    bool   m_selecting = false;



    // 当前选区
    int64_t m_selStart = -1;   // 字符级偏移
    int64_t m_selEnd = -1;   // 同上


    ComPtr<ID2D1SolidColorBrush> m_selBrush;
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
    const OCFNavPoint* nav = nullptr;
    std::vector<size_t> childIdx;
    // 仅用于自绘面板
    bool expanded = false;   // 当前是否展开
    int  yLine = 0;      // 当前在面板中的行号（缓存）
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
    std::wstring m_file_path = L"";
    // -------------- EPUBBook 内部新增成员 --------------

    void parse_ocf_(void);                       // 主解析入口
    void parse_opf_(void);   // 解析 OPF
    void parse_toc_(void);                        // 解析 TOC

    std::wstring get_chapter_name_by_id(int spine_id);
    void OnTreeSelChanged(const wchar_t* href);
    bool load(const wchar_t* epub_path);
    MemFile read_zip(const wchar_t* file_name) const;
    std::string load_html(const std::wstring& path) const;

    void load_all_fonts(void);



    static std::wstring extract_text(const tinyxml2::XMLElement* a);

    // 递归解析 EPUB3-Nav <ol>
    void parse_nav_list(tinyxml2::XMLElement* ol, int level,
        const std::string& opf_dir,
        std::vector<OCFNavPoint>& out);


    // 递归解析 NCX <navPoint>
    void parse_ncx_points(tinyxml2::XMLElement* navPoint, int level,
        const std::string& opf_dir,
        std::vector<OCFNavPoint>& out);

    std::string extract_anchor(const char* href);

    litehtml::element::ptr find_link_in_chain(litehtml::element::ptr start);

    static bool skip_attr(const std::string& val);

    static std::string get_html(litehtml::element::ptr el);

    std::string html_of_anchor_paragraph(litehtml::document* doc, const std::string& anchorId);

    std::string get_html_of_image(litehtml::element::ptr start);

    void show_imageview(const litehtml::element::ptr& el);

    void show_tooltip(const std::string html);
    void hide_imageview();
    void hide_tooltip();
    static std::string get_anchor_html(litehtml::document* doc, const std::string& anchor);
    void clear();
    void LoadToc();
    HWND                   m_tooltip{ nullptr };   // 你的缩略图窗口
    std::wstring           m_tooltip_url;          // 缓存当前 url
    std::vector<std::pair<std::wstring, std::vector<uint8_t>>> collect_epub_fonts();

    void build_epub_font_index(const OCFPackage& pkg, EPUBBook* book);
    std::unordered_map<FontKey, std::vector<std::wstring>> m_fontBin;

    EPUBBook() noexcept {}
    ~EPUBBook();

};


struct AppSettings {
    bool enableCSS = true;   // 默认启用 css
    bool enableJS = false;   // 默认禁用 JS
    bool enableGlobalCSS = false;
    bool enablePreprocessHTML = true;
    bool enableHoverPreview = true;
    bool enableImagePreview = true;

    bool displayTOC = true;
    bool displayStatusBar = true;
    bool displayMenuBar = true;
    bool displayScrollBar = true;
    bool displayToolbar = true;

    int record_update_interval_ms = 1000;
    int record_flush_interval_ms = 10 * 1000;
    int tooltip_delay_ms = 300;

    int font_size = 16;
    float line_height = 1.5f; //倍数
    int document_width = 600;

    int default_font_size = 16;
    float default_line_height = 1.5;
    int default_document_width = 800;

    float zoom_factor = 1.0f;
    Renderer fontRenderer = Renderer::D2D;
    std::string default_font_name = "Microsoft YaHei";


    int tooltip_width = 500;

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
    std::atomic_bool isUpdate{ false };   // 后台是否正在渲染
    std::atomic_bool isTooltipUpdate{ false };   // 后台是否正在渲染
    std::atomic_bool isImageviewUpdate{ false };   // 后台是否正在渲染
    bool isLoaded = false;
    // 工具：生成新令牌，旧令牌立即失效
    void newCancelToken() {
        if (cancelToken) cancelToken->store(true);
        cancelToken = std::make_shared<std::atomic_bool>(false);
    }
};


// 把整棵树存到 LPARAM 里
struct TVData {
    const TreeNode* node;
    const std::vector<TreeNode>* all;
    bool inserted = false;   // 新增
};

// 全局索引 ----------------------------------------------------------











// ---------- LiteHtml 容器 ----------
class SimpleContainer : public litehtml::document_container {
public:
    explicit SimpleContainer();
    ~SimpleContainer();
    void clear();

    litehtml::pixel_t	get_default_font_size() const override;
    const char* get_default_font_name() const override;


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
    void	load_image(const char* src, const char* baseurl, bool redraw_on_ready) override;
    void	get_image_size(const char* src, const char* baseurl, litehtml::size& sz) override;


    //自定义函数
    void makeBackend();


    std::unordered_map<std::string, ImageFrame> m_img_cache;
    std::unordered_map<std::string, litehtml::element::ptr> m_anchor_map;
    litehtml::document::ptr m_doc;
    void init_dpi();
    std::unique_ptr<IRenderBackend> m_backend;
private:

    float m_px_per_pt{ 96.0f / 72.0f };   // 默认 96 DPI

};



struct ScrollPosition
{
    int spine_id = 0;
    int offset = 0;
    float height = 0.0f;
};


struct BodyBlock {
    int spine_id = 0;
    int block_id = 0;
    std::string html;
    float height = 0.0f; // 未渲染前默认 -1

};

struct HtmlBlock {
    int spine_id;
    float height = 0.0f;
    std::string head;
    std::vector<BodyBlock> body_blocks;
};




class VirtualDoc {
public:
    VirtualDoc();
    ~VirtualDoc();
    void load_book(std::shared_ptr<EPUBBook> book, std::shared_ptr<SimpleContainer> container, int render_width);

    litehtml::document::ptr get_doc(int client_h, int& scrollY, int& y_offset);
    void load_html(std::wstring& href);
    void clear();
    ScrollPosition get_scroll_position();
    std::vector<HtmlBlock> m_blocks;
    float get_height_by_id(int spine_id);
    void reload();
    bool exists(int spine_id);
private:
    HtmlBlock get_html_block(std::string html, int spine_id);
    void merge_block(HtmlBlock& dst, HtmlBlock& src, bool isAddToBottom = true);
    int get_id_by_href(std::wstring& href);
    std::wstring get_href_by_id(int spine_id);
    std::string get_head(std::string& html);
    std::vector<BodyBlock> get_body_blocks(std::string& html, int spine_id = 0, size_t max_chunk_bytes = 4*1024);
    void serialize_node(const GumboNode* node, std::ostream& out);
    bool gumbo_tag_is_void(GumboTag tag);
    void serialize_element(const GumboElement& el, std::ostream& out);

    float  m_height = 0.0f;


    bool insert_next_chapter();

    float get_height();
    bool insert_prev_chapter();


    bool load_by_id(int spine_id, bool isPushBack);


    litehtml::document::ptr m_doc;
    std::vector<OCFRef> m_spine;
    std::shared_ptr<EPUBBook> m_book;
    std::shared_ptr<SimpleContainer> m_container;

    int m_current_id = 0;


};

//struct BookRecord
//{
//    int book_id;
//    std::wstring book_path;
//    std::string title;
//    
//    int last_spine_id;
//    int last_scrollY;
//    int doc_width;
//    int font_size;
//    float line_height_multiplier;
//
//    int open_count = 0;
//    int total_time = 0; // 单位：秒
//    float last_open;
//    bool enableCSS = true;   // 默认启用 css
//    bool enableJS = false;   // 默认禁用 JS
//    bool enableGlobalCSS = false;
//    bool enablePreprocessHTML = true;
//    bool displayTOC = true;
//    bool displayStatusBar = true;
//    bool displayMenuBar = true;
//    bool displayScrollBar = true;
//
//
//
//};
//class ReadingRecorder {
//public:
//    ReadingRecorder();
//    ~ReadingRecorder();
//    void open();                     // 初始化数据库、建表、开 WAL
//    void close(); // 关闭数据库，
//
//   //会在wm_mousemove, wm_mousewheel等的消息中调用 
//    void tik(); 
//    // 书籍状态
//
//
//    // 统计示例
//
//private:
//    /*记录的内容：
//    * 
//    *int id, std::wstring book_path, std::wstring title, std::wstring authors, std::wstring current_chapter, float start_time(时间戳）, float end_time（时间戳）, int duration(单位：s)
//    * 
//    */
//
//    sqlite3* m_dbRecords = nullptr;      // Records.db
//    /*记录的内容：
//    * int id, std::wstring book_path, std::string title, std::string author, int open_count, 
//    * int total_words, int last_spine_id, int last_scrollY, int font_size, float line_height_multiplier, int total_time(总阅读时长s), 
//    * float last_open_time(时间戳),     bool enableCSS = true; 
//    *bool enableJS = false;
//    *bool enableGlobalCSS = false;
//    *bool enablePreprocessHTML = true;
//    *bool displayTOC = true;
//    *bool displayStatusBar = true;
//    *bool displayMenuBar = true;
//    *bool displayScrollBar = true;
//    *
//    *    打开新书时读取配置，关闭书时更新数据
//    * 调用tik时更新总阅读时长
//    */
//    sqlite3* m_dbBooks = nullptr;      // Books.db
//    
//};



struct BookRecord {
    int64_t id = -1;                       // 数据库主键；-1 表示未找到
    std::string path;
    std::string title;
    std::string author;
    int         openCount = 0;
    int         totalWords = 0;
    int         lastSpineId = 0;
    int         lastOffset = 0;
    int         fontSize = 0;
    float       lineHeightMul = 0.0f;
    int         docWidth = 0;
    int         totalTime = 0;        // 累计阅读秒数
    int64_t     lastOpenTimestamp = 0;        // 微秒
    bool        enableCSS = true;
    bool        enableJS = false;
    bool        enableGlobalCSS = false;
    bool        enablePreHTML = true;
    bool        displayTOC = true;
    bool        displayStatus = true;
    bool        displayMenu = true;
    bool        displayScroll = true;
};

struct timeFragment
{
    std::string path;
    std::string title;
    std::string author;
    int       spine_id;
    std::string chapter;
    int64_t timestamp;
};

class ReadingRecorder {
public:
    ReadingRecorder();
    ~ReadingRecorder();

    void openBook(const std::string absolutePath); // 返回记录（读或建）
    void flush();            // 一次性写回
    void flushBookRecord();
    void flushTimeRecord();
    void updateRecord();
    int64_t getTotalTime();

    BookRecord m_book_record;
private:
    void initDB();

    sqlite3* m_dbBook = nullptr;
    sqlite3* m_dbTime = nullptr;

    std::vector<timeFragment> m_time_frag;



};


class TocPanel
{
public:
    using OnNavigate = std::function<void(const std::wstring& href)>;

    TocPanel();
    ~TocPanel();
    void clear();
    void GetWindow(HWND hwnd);

    void Load(const std::vector<OCFNavPoint>& flatToc);
    void SetOnNavigate(OnNavigate cb) { onNavigate_ = std::move(cb); }
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void SetHighlightByHref(const std::wstring& href);
private:
    struct Node : TreeNode{};

    // 消息泵

    LRESULT HandleMsg(UINT, WPARAM, LPARAM);

    // 内部工具
    void RebuildVisible();
    int  HitTest(int y) const;
    void Toggle(int line);
    void EnsureVisible(int line);

    // 绘制
    void OnPaint(HDC);
    void OnVScroll(int code, int pos);
    void OnMouseWheel(int delta);
    void OnLButtonDown(int x, int y);

    // 数据
    std::vector<Node>          nodes_;
    std::vector<size_t>        roots_;
    std::vector<size_t>        vis_;      // 可见行索引
    int                        lineH_ = 20;
    int                        scrollY_ = 0;
    int                        totalH_ = 0;
    int                        selLine_ = -1;
    OnNavigate                 onNavigate_;
    HFONT hFont_ = nullptr;
    HWND hwnd_ = nullptr;
    int marginTop = 4;   // 顶部留白
    int marginLeft = 10;  // 左侧留白
};

//  file system
class IFileProvider {
public:
    virtual ~IFileProvider() = default;
    virtual bool load(const std::wstring& file_path) = 0;
    // 按路径返回原始二进制
    virtual MemFile get(const std::wstring& path) const = 0;
    virtual std::wstring find(const std::wstring& path) = 0;
};

class ZipProvider : public IFileProvider 
{
public:
    bool load(const std::wstring& file_path) override;
    MemFile get(const std::wstring& path) const override;
    std::wstring find(const std::wstring& path);
private:
    mz_zip_archive m_zip = {};
    ZipIndexW m_zipIndex;
};

class LocalFileProvider : public IFileProvider
{
    bool load(const std::wstring& file_path) override { return true; };
    // 按路径返回原始二进制
    MemFile get(const std::wstring& path) const override;
    std::wstring find(const std::wstring& path) { return L""; }
};


class EPUBParser
{
public:
    bool load(std::shared_ptr<IFileProvider> fp);
private:
    bool parse_ocf();
    bool parse_opf();
    bool parse_toc();
    static std::wstring extract_text(const tinyxml2::XMLElement* a);

    // 递归解析 EPUB3-Nav <ol>
    void parse_nav_list(tinyxml2::XMLElement* ol, int level,
        const std::string& opf_dir,
        std::vector<OCFNavPoint>& out);


    // 递归解析 NCX <navPoint>
    void parse_ncx_points(tinyxml2::XMLElement* navPoint, int level,
        const std::string& opf_dir,
        std::vector<OCFNavPoint>& out);
    std::shared_ptr<IFileProvider> m_fp;
    OCFPackage m_ocf_pkg;
};

struct GetDocParam
{
    int        client_h;
    int        scrollY;
    int        offsetY;
    HWND       notify_hwnd;   // 通知窗口
};


class AccelManager {
public:
    explicit AccelManager(HWND h) : m_hwnd(h) {}

    // 添加/更新一条快捷键
    void set(WORD cmd, BYTE fVirt, WORD key) {
        // 先删除同命令的旧项
        erase(cmd);
        m_entries.push_back({ fVirt, key, cmd });
        rebuild();
    }
    void add(WORD cmd, BYTE fVirt, WORD key) {
        m_entries.push_back({ fVirt, key, cmd });
        rebuild();
    }
    // 删除某命令
    void erase(WORD cmd) {
        m_entries.erase(
            std::remove_if(m_entries.begin(), m_entries.end(),
                [=](const ACCEL& a) { return a.cmd == cmd; }),
            m_entries.end());
        rebuild();
    }

    // 在消息循环里调用
    bool translate(MSG* m) {
        return m_hAccel && TranslateAccelerator(m_hwnd, m_hAccel, m);
    }

private:
    void rebuild() {
        if (m_hAccel) DestroyAcceleratorTable(m_hAccel);
        m_hAccel = m_entries.empty()
            ? nullptr
            : CreateAcceleratorTable(m_entries.data(),
                static_cast<int>(m_entries.size()));
    }

    std::vector<ACCEL> m_entries;
    HACCEL m_hAccel = nullptr;
    HWND m_hwnd;
};


class ScrollBarEx
{
public:

    void GetWindow(HWND hwnd);
    // API
    void SetSpineCount(int n);

    void SetPosition(int spineId, int totalHeightPx, int offsetPx);
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
private:


    void OnPaint();
    bool HitThumb(const POINT& pt) const;
    void OnLButtonDown(int x, int y);
    void OnMouseLeave(int x, int y);
    void OnMouseMove(int x, int y);
    void OnLButtonUp();

    void OnRButtonUp();

    int m_count = 0;
    ScrollPosition m_pos;

    bool m_dragging = false;
    int  m_dragAnchor = 0;
    int dot_r;      // 普通圆点半径
    int ACTIVE_R = 6;      // 当前圆点半径
    int thumbH = 24;     // 滑块高度
    int LINE_W = 2;      // 竖线宽
    int GUTTER_W = 14;     // 整个滚动条宽

    bool m_mouseIn = false;
    struct ThumbState
    {
        bool hot = false;
        bool drag = false;
        int  dragY = 0;     // 鼠标按下时相对滑块顶部的偏移
    };
    ThumbState m_thumb;
    HWND m_hwnd = nullptr;
};