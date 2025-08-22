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
#include <litehtml/litehtml.h>
#include <litehtml/document.h>
#include <litehtml/element.h>
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

#include <litehtml/el_text.h>


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
#include "js_runtime.h"
#pragma comment(lib, "windowscodecs.lib")
#ifndef HR
#define HR(hr)  do { HRESULT _hr_ = (hr); if(FAILED(_hr_)) return 0; } while(0)
#endif
using Microsoft::WRL::ComPtr;

namespace fs = std::filesystem;
HWND  g_hwndTV = nullptr;    // 侧边栏 TreeView
HIMAGELIST g_hImg = nullptr;   // 图标(可选)
HWND      g_hWnd;
HWND g_hStatus = nullptr;   // 状态栏句柄
HWND g_hView = nullptr;

HDC        g_hMemDC = nullptr;
FT_Library g_ftLib = nullptr;
ComPtr<ID2D1Factory1> g_d2dFactory = nullptr;   // 原来是 ID2D1Factory = nullptr;
ComPtr<ID2D1HwndRenderTarget> g_d2dRT = nullptr;   // ← 注意是 HwndRenderTarget 

enum class FontBackend { GDI = 0, DirectWrite = 1, FreeType = 2 };

static int g_scrollY = 0;   // 当前像素偏移
static int g_maxScroll = 0;   // 总高度 - 客户区高度
std::wstring g_currentHtmlDir = L"";
constexpr UINT WM_EPUB_PARSED = WM_APP + 1;
constexpr UINT WM_EPUB_UPDATE_SCROLLBAR = WM_APP + 2;
constexpr UINT WM_EPUB_CSS_RELOAD = WM_APP + 3;
constexpr UINT WM_EPUB_CACHE_UPDATED = WM_APP + 4;
constexpr UINT WM_EPUB_ANCHOR = WM_APP + 5;
// -------------- 运行时策略 -----------------
enum class Renderer { GDI, D2D, FreeType };
  // 可随时改

struct AppSettings {
    bool disableCSS = false;   // 默认启用 css
    bool disableJS = true;   // 默认禁用 JS
    bool disablePreprocessHTML = false;
    Renderer fontRenderer = Renderer::D2D;
    std::set<std::wstring> font_serif = { L"Source Han Serif SC", 
        L"Noto Serif CJK SC", 
        L"SimSun", 
        L"Times New Roman" };
    std::set<std::wstring> font_sans_serif = { L"Source Han Sans SC", 
        L"Noto Sans CJK SC", 
        L"Microsoft YaHei", 
        L"PingFang SC", 
        L"Arial" };
    std::set<std::wstring> font_monospace = { L"JetBrains Mono", 
        L"Sarasa Gothic SC", 
        L"Consolas", 
        L"Courier New" };
};
struct AppStates {
    // ---- 取消令牌 ----
    std::shared_ptr<std::atomic_bool> cancelToken;

    // ---- 状态机 ----
    std::atomic_bool needRelayout{ true };   // 是否需要重新排版
    std::atomic_bool isCaching{ false };   // 后台是否正在渲染

    // 工具：生成新令牌，旧令牌立即失效
    void newCancelToken() {
        if (cancelToken) cancelToken->store(true);
        cancelToken = std::make_shared<std::atomic_bool>(false);
    }
};
AppStates g_states;
AppSettings g_cfg;
std::wstring g_last_html_path;
enum class ImgFmt { PNG, JPEG, BMP, GIF, TIFF, SVG, UNKNOWN };
static std::vector<std::wstring> g_tempFontFiles;
static std::unordered_map<std::wstring, std::wstring> g_realFontNames;
std::string PreprocessHTML(std::string html);
void UpdateCache(void);
struct GdiplusDeleter { void operator()(Gdiplus::Image* p) const { delete p; } };
using ImagePtr = std::unique_ptr<Gdiplus::Image, GdiplusDeleter>;
static  std::string g_globalCSS = "";
static fs::file_time_type g_lastTime;
std::set<std::wstring> g_activeFonts;

static const std::unordered_map<std::wstring, std::wstring> g_fontAlias = {
    /* 英文字体 */
    {L"charis",               L"Charis SIL"},
    {L"charis sil",           L"Charis SIL"},
    {L"times",                L"Times New Roman"},
    {L"times new roman",      L"Times New Roman"},
    {L"arial",                L"Arial"},
    {L"helvetica",            L"Arial"},           // 在 Windows 上 Helvetica 映射到 Arial
    {L"verdana",              L"Verdana"},
    {L"tahoma",               L"Tahoma"},
    {L"georgia",              L"Georgia"},
    {L"garamond",             L"Garamond"},
    {L"palatino",             L"Palatino Linotype"},
    {L"palatino linotype",    L"Palatino Linotype"},
    {L"courier",              L"Courier New"},
    {L"courier new",          L"Courier New"},
    {L"consolas",             L"Consolas"},
    {L"lucida console",       L"Lucida Console"},
    {L"lucida sans unicode",  L"Lucida Sans Unicode"},
    {L"comic sans",           L"Comic Sans MS"},
    {L"comic sans ms",        L"Comic Sans MS"},
    {L"impact",               L"Impact"},
    {L"trebuchet",            L"Trebuchet MS"},
    {L"trebuchet ms",          L"Trebuchet MS"},
    {L"franklin gothic",      L"Franklin Gothic Medium"},

    /* 思源 / 开源无衬线 */
    {L"source sans",           L"Source Sans Pro"},
    {L"source sans pro",       L"Source Sans Pro"},
    {L"source serif",          L"Source Serif Pro"},
    {L"source serif pro",      L"Source Serif Pro"},
    {L"source code",           L"Source Code Pro"},
    {L"source code pro",       L"Source Code Pro"},

    /* 等宽 / 编程字体 */
    {L"fira code",             L"Fira Code"},
    {L"fira mono",             L"Fira Mono"},
    {L"jetbrains mono",        L"JetBrains Mono"},
    {L"cascadia code",         L"Cascadia Code"},
    {L"cascadia mono",         L"Cascadia Mono"},
    {L"roboto mono",           L"Roboto Mono"},
    {L"inconsolata",           L"Inconsolata"},

    /* 中文字体（简体） */
    {L"simsun",                L"SimSun"},
    {L"songti",                L"SimSun"},
    {L"宋体",                  L"SimSun"},
    {L"simhei",                L"SimHei"},
    {L"黑体",                  L"SimHei"},
    {L"microsoft yahei",       L"Microsoft YaHei"},
    {L"yahei",                 L"Microsoft YaHei"},
    {L"微软雅黑",               L"Microsoft YaHei"},
    {L"dengxian",               L"DengXian"},
    {L"等线",                  L"DengXian"},
    {L"kaiti",                 L"KaiTi"},
    {L"kaiti sc",              L"KaiTi"},
    {L"楷体",                  L"KaiTi"},
    {L"fangsong",              L"FangSong"},
    {L"fangsong sc",           L"FangSong"},
    {L"仿宋",                  L"FangSong"},
    {L"lisu",                  L"LiSu"},
    {L"隶书",                  L"LiSu"},
    {L"hy-xiaolishu",          L"HYXiaoLiShu_GB18030Super"},


    /* 中文字体（繁体） */
    {L"mingliu",               L"MingLiU"},
    {L"pmingliu",              L"PMingLiU"},
    {L"mingliuhkscs",          L"MingLiU_HKSCS"},
    {L"標楷體",                L"DFKai-SB"},

    /* 日文字体 */
    {L"ms gothic",             L"MS Gothic"},
    {L"ms mincho",             L"MS Mincho"},
    {L"yu gothic",             L"Yu Gothic"},
    {L"yu mincho",             L"Yu Mincho"},
    {L"meiryo",                L"Meiryo"},
    {L"メイリオ",               L"Meiryo"},

    /* 韩文字体 */
    {L"malgun gothic",         L"Malgun Gothic"},
    {L"malgun",                L"Malgun Gothic"},
    {L"맑은 고딕",             L"Malgun Gothic"},
    {L"batang",                L"Batang"},
    {L"gulim",                 L"Gulim"},
    {L"dotum",                 L"Dotum"}
};

static std::unordered_map<std::wstring, std::set<std::wstring>>  g_fontAliasDynamic = {
    {L"serif", g_cfg.font_serif},
    {L"sans-serif", g_cfg.font_sans_serif}, 
    {L"monospace", g_cfg.font_monospace}
};
struct ImageFrame
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;          // 每行字节数
    std::vector<uint8_t> rgba;     // 连续像素，8-bit * 4
};

static bool isWin10OrLater()
{
    static bool once = [] {
        OSVERSIONINFOEXW os{ sizeof(os), 10, 0, 0, 0, {0}, 0, 0, 0, 0, 0 };
        DWORDLONG mask = 0;
        VER_SET_CONDITION(mask, VER_MAJORVERSION, VER_GREATER_EQUAL);
        return ::VerifyVersionInfoW(&os, VER_MAJORVERSION, mask) != FALSE;
        }();
    return once;
}


// HTML 转义辅助函数



// HTML 转义辅助函数
std::string _escape_html(const std::string& s) {
    std::ostringstream oss;
    for (char c : s) {
        switch (c) {
        case '&':  oss << "&amp;";  break;
        case '<':  oss << "&lt;";   break;
        case '>':  oss << "&gt;";   break;
        case '"':  oss << "&quot;"; break;
        case '\'': oss << "&apos;"; break;
        default:   oss << c;       break;
        }
    }
    return oss.str();
}

// 自闭合标签集合
static const std::set<std::string> void_tags = {
    "area", "base", "br", "col", "embed", "hr", "img",
    "input", "keygen", "link", "meta", "param", "source",
    "track", "wbr"
};

// 转换为小写函数
std::string to_lower(const std::string& str) {
    std::string lower_str = str;
    std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return lower_str;
}

std::string generate_html(litehtml::element::ptr elem) {
    if (!elem) return "";

    std::ostringstream oss;

    // 处理文本节点
    if (elem->is_text()) {
        std::string text;
        elem->get_text(text);
        if (!text.empty()) { // 只有当有文本内容时才输出
            oss << _escape_html(text);
        }
    }
    // 处理普通元素
    else {
        const char* tag_name = elem->get_tagName();
        if (!tag_name) return "";

        // 输出开始标签
        oss << "<" << tag_name;

        //// 使用 dump_get_attrs 获取所有属性
        //auto attrs = elem->dump_get_attrs();
        //for (const auto& attr : attrs) {
        //    oss << " " << std::get<0>(attr) << "=\""
        //        << _escape_html(std::get<1>(attr)) << "\"";
        //}
        // 修改后的属性处理
        std::vector<const char*> standardAttrs = {
            "id", "class", "name", "value", "type", "src", "href"
        };

        for (const char* name : standardAttrs) {
            const char* value = elem->get_attr(name);
            if (value && value[0] != '\0') {
                oss << " " << name << "=\"" << _escape_html(value) << "\"";
            }
        }

        // 处理自闭合标签
        std::string tag_str = to_lower(tag_name);
        bool is_void = (void_tags.find(tag_str) != void_tags.end());

        if (is_void) {
            oss << "/>";
        }
        else {
            oss << ">";

            // 递归处理子元素
            const auto& children = elem->children();
            for (const auto& child : children) {
                oss << generate_html(child);
            }

            // 输出闭合标签
            oss << "</" << tag_name << ">";
        }
    }

    return oss.str();
}

// 完整的文档导出函数
std::string get_document_html(litehtml::document::ptr doc) {
    if (!doc) return "";

    // 添加文档类型声明
    std::ostringstream oss;
    oss << "<!DOCTYPE html>";

    // 从根元素开始生成
    if (doc->root()) {
        oss << generate_html(doc->root());
    }

    return oss.str();
}

void save_document_html(litehtml::document::ptr doc) {
    std::string modified_html = get_document_html(doc);

    // 输出到文件
    std::ofstream out("output.html");
    if (out.is_open()) {
        out << modified_html;
        out.close();
        std::cout << "HTML 导出成功" << std::endl;
    }
    else {
        std::cerr << "无法创建输出文件" << std::endl;
    }
}

class InMemoryFontFileLoader
    : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    IDWriteInMemoryFontFileLoader>
{
public:
    // IDWriteFontFileLoader
    IFACEMETHODIMP CreateStreamFromKey(
        const void* /*key*/, UINT32 /*size*/, IDWriteFontFileStream** /*stream*/) override
    {
        return E_NOTIMPL;   // 用不到
    }

    // IDWriteInMemoryFontFileLoader
    IFACEMETHODIMP CreateInMemoryFontFileReference(
        IDWriteFactory* factory,
        const void* data,
        UINT32 size,
        IUnknown* owner,
        IDWriteFontFile** fontFile) override
    {
        return factory->CreateCustomFontFileReference(data, size, this, fontFile);
    }

    STDMETHODIMP_(UINT32) GetFileCount() override
    {
        // 简单计数即可；可按需要维护实际数量
        return 1;
    }
};
class MemoryFontLoader : public IDWriteFontCollectionLoader,
    public IDWriteFontFileEnumerator
{
public:
    // 静态工厂：一次性把若干内存字体打包成私有集合
    static HRESULT CreateCollection(
        IDWriteFactory* dwrite,
        const std::vector<std::pair<std::wstring, std::vector<uint8_t>>>& fonts,
        IDWriteFontCollection** out);

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override { return 1; }
    IFACEMETHODIMP_(ULONG) Release() override { return 1; }

    // IDWriteFontCollectionLoader
    IFACEMETHODIMP CreateEnumeratorFromKey(
        IDWriteFactory* factory,
        const void* collectionKey, UINT32 collectionKeySize,
        IDWriteFontFileEnumerator** enumerator) override;

    // IDWriteFontFileEnumerator
    IFACEMETHODIMP MoveNext(BOOL* hasCurrentFile) override;
    IFACEMETHODIMP GetCurrentFontFile(IDWriteFontFile** fontFile) override;

private:
    // 私有构造，只能由 CreateCollection 调用
    MemoryFontLoader(IDWriteFactory* f,
        const std::vector<std::vector<uint8_t>>& blobs)
        : factory_(f), blobs_(blobs), idx_(0) {
    }

    // 内部辅助：把一段内存封装成 IDWriteFontFile
    static HRESULT CreateInMemoryFontFile(IDWriteFactory* factory,
        const void* data,
        UINT32 size,
        IDWriteFontFile** out);

    Microsoft::WRL::ComPtr<IDWriteFactory> factory_;
    std::vector<std::vector<uint8_t>> blobs_;
    size_t idx_;
    Microsoft::WRL::ComPtr<IDWriteFontFile> current_;
};
class TempFileEnumerator
    : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    IDWriteFontFileEnumerator>
{
public:
    TempFileEnumerator() = default;
    TempFileEnumerator(IDWriteFactory* factory,
        const std::vector<std::wstring>& paths)
        : m_factory(factory), m_paths(paths) {
    }
    HRESULT RuntimeClassInitialize(IDWriteFactory* f,
        const std::vector<std::wstring>& paths)
    {
        m_factory = f;
        m_paths = paths;
        return S_OK;
    }
    IFACEMETHODIMP MoveNext(BOOL* hasCurrentFile) override
    {
        *hasCurrentFile = (m_idx < m_paths.size());
        if (*hasCurrentFile) ++m_idx;
        return S_OK;
    }

    IFACEMETHODIMP GetCurrentFontFile(IDWriteFontFile** fontFile) override
    {
        if (m_idx == 0 || m_idx > m_paths.size()) return E_FAIL;
        return m_factory->CreateFontFileReference(m_paths[m_idx - 1].c_str(), nullptr, fontFile);
    }

private:
    Microsoft::WRL::ComPtr<IDWriteFactory> m_factory;
    std::vector<std::wstring> m_paths;
    size_t m_idx = 0;
};
class TempFileFontLoader
    : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    IDWriteFontCollectionLoader>
{
public:
    TempFileFontLoader() = default;
    explicit TempFileFontLoader(IDWriteFactory* f) : m_factory(f) {}
    static HRESULT CreateCollection(
        IDWriteFactory* dwrite,
        const std::vector<std::pair<std::wstring, std::vector<uint8_t>>>& fonts,
        IDWriteFontCollection** out,
        std::vector<std::wstring>& tempPaths)
    {
        if (!dwrite || !out) return E_INVALIDARG;

        // 注册（只一次）
        static bool reg = false;
        static Microsoft::WRL::ComPtr<TempFileFontLoader> g_loader;
        if (!reg)
        {
            Microsoft::WRL::MakeAndInitialize<TempFileFontLoader>(&g_loader, dwrite);
            dwrite->RegisterFontCollectionLoader(g_loader.Get());
            reg = true;
        }
        // 写临时文件
        std::vector<std::wstring> paths;
        for (const auto& [name, blob] : fonts)
        {
            wchar_t tmpPath[MAX_PATH]{};
            GetTempPathW(MAX_PATH, tmpPath);
            wchar_t tmpFile[MAX_PATH]{};
            PathCombineW(tmpFile, tmpPath, PathFindFileNameW(name.c_str()));
            HANDLE h = CreateFileW(tmpFile, GENERIC_WRITE, 0, nullptr,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE) continue;
            DWORD written = 0;
            WriteFile(h, blob.data(), (DWORD)blob.size(), &written, nullptr);
            CloseHandle(h);
            paths.push_back(tmpFile);
        }

        // 把路径数组整体作为 key
        // key = 连续 wchar_t 字符串数组，每个以 '\0' 结尾
        std::vector<wchar_t> key;
        for (const auto& p : paths)
        {
            key.insert(key.end(), p.begin(), p.end());
            key.push_back(L'\0');
        }
        key.push_back(L'\0');   // 双零结束

        HRESULT hr = dwrite->CreateCustomFontCollection(
            g_loader.Get(),
            key.data(),
            static_cast<UINT32>(key.size() * sizeof(wchar_t)),
            out);

        if (SUCCEEDED(hr))
            tempPaths.swap(paths);
        return hr;
    }

    // IDWriteFontCollectionLoader
    IFACEMETHODIMP CreateEnumeratorFromKey(
        IDWriteFactory* factory,
        void const* collectionKey,
        UINT32 collectionKeySize,
        IDWriteFontFileEnumerator** fontFileEnumerator) override
    {
        *fontFileEnumerator = nullptr;

        // 把 key 还原成路径列表
        std::vector<std::wstring> paths;
        const wchar_t* p = static_cast<const wchar_t*>(collectionKey);
        const wchar_t* end = p + collectionKeySize / sizeof(wchar_t);
        while (*p && p < end)
        {
            paths.emplace_back(p);
            p += wcslen(p) + 1;
        }

        return Microsoft::WRL::MakeAndInitialize<TempFileEnumerator>(
            fontFileEnumerator,
            factory,
            paths);
    }

    // 用 RuntimeClassInitialize 接收额外参数
    HRESULT RuntimeClassInitialize(IDWriteFactory* f)
    {
        m_factory = f;
        return S_OK;
    }
private:

    Microsoft::WRL::ComPtr<IDWriteFactory> m_factory;
};


HRESULT CreateCompatibleFontCollection(
    IDWriteFactory* dwrite,
    const std::vector<std::pair<std::wstring, std::vector<uint8_t>>>& fonts,
    IDWriteFontCollection** out,
    std::vector<std::wstring>& tempFilesToDelete)
{
    if (isWin10OrLater())
    {
        // Win10+ 走内存
        return MemoryFontLoader::CreateCollection(dwrite, fonts, out);
    }
    else
    {
        // Win7/8 回落到临时文件
        return TempFileFontLoader::CreateCollection(dwrite, fonts, out, tempFilesToDelete);
    }
}

// -------------- 抽象接口 -----------------
class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;

    /* 必须实现的 7 个纯虚函数 */
    virtual void draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr hFont, litehtml::web_color color, const litehtml::position& pos) = 0;
    virtual void draw_borders(litehtml::uint_ptr, const litehtml::borders&, const litehtml::position&, bool) = 0;
    virtual litehtml::uint_ptr create_font(const char* faceName, int size, int weight, litehtml::font_style italic, unsigned int decoration, litehtml::font_metrics* fm) = 0;
    virtual void delete_font(litehtml::uint_ptr h) = 0;
    virtual void draw_background(litehtml::uint_ptr, const std::vector<litehtml::background_paint>&) = 0;
    virtual int pt_to_px(int pt) const = 0;
    virtual int text_width(const char* text, litehtml::uint_ptr hFont) = 0;
    virtual void load_all_fonts(void) = 0;
    virtual void resize(int width, int height) = 0;
};
std::shared_ptr<IRenderBackend> g_backend = nullptr;
std::unordered_map<std::string, ImageFrame> g_img_cache;
class AppBootstrap {
public:
    AppBootstrap();
    ~AppBootstrap();
    struct script_info
    {
        litehtml::element::ptr el;   // 只需要保留节点指针
    };

 
    void makeBackend(Renderer which, void* ctx);
    void initBackend(Renderer which);
    void enableJS();
    void disableJS();
    void bind_host_objects();   // 新增
    void run_pending_scripts();

    void switchBackend(std::unique_ptr<IRenderBackend> newBackend) {
        g_backend = std::move(newBackend);
    }

    std::vector<script_info> m_pending_scripts;

    std::unique_ptr<js_runtime> m_jsrt;   // 替换裸 duk_context*

};


std::unique_ptr<AppBootstrap> g_bootstrap;
// -------------- GDI 后端 -----------------
class GdiBackend : public IRenderBackend {
public:
    explicit GdiBackend(HDC hdc) : m_hdc(hdc) {}
    /* 下面 7 个函数在 .cpp 里用 ExtTextOut / Rectangle 等实现 */
    void draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr hFont, litehtml::web_color color, const litehtml::position& pos) override;
    void draw_borders(litehtml::uint_ptr, const litehtml::borders&, const litehtml::position&, bool) override;
    litehtml::uint_ptr create_font(const char* faceName, int size, int weight, litehtml::font_style italic, unsigned int decoration, litehtml::font_metrics* fm) override;
    void delete_font(litehtml::uint_ptr h) override;
    void draw_background(litehtml::uint_ptr, const std::vector<litehtml::background_paint>&) override;
    int pt_to_px(int pt) const override;
    int text_width(const char* text, litehtml::uint_ptr hFont) override;
    void load_all_fonts(void);
    void resize(int width, int height) override;
    ~GdiBackend() {
        if (!g_tempFontFiles.empty()) {
            for (const auto& p : g_tempFontFiles)
            {
                RemoveFontResourceExW(p.c_str(), FR_PRIVATE, 0);
                DeleteFileW(p.c_str());
            }
            g_tempFontFiles.clear();
        }
    }
private:
    HDC m_hdc;
    // 缓存已创建的 HBITMAP，避免重复 GDI 转换
    std::unordered_map<std::string, HBITMAP> m_gdiBitmapCache;

    static COLORREF to_cr(const litehtml::web_color& c)
    {
        return RGB(c.red, c.green, c.blue);
    }

    // 把 ImageFrame 转成 32bpp HBITMAP
    HBITMAP create_dib_from_frame(const ImageFrame& frame);
};

// -------------- DirectWrite-D2D 后端 -----------------
class D2DBackend : public IRenderBackend {
public:
    D2DBackend(int w, int h, ComPtr<ID2D1RenderTarget> devCtx);
    //D2DBackend(const D2DBackend&) = default;   // 或自己实现深拷贝
    /* 下面 6 个函数在 .cpp 里用 IDWriteTextLayout / ID2D1SolidColorBrush 实现 */
    void draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr hFont, litehtml::web_color color, const litehtml::position& pos) override;
    void draw_borders(litehtml::uint_ptr, const litehtml::borders&, const litehtml::position&, bool) override;
    std::vector<std::wstring> split_font_list(const std::string& src);
    litehtml::uint_ptr create_font(const char* faceName, int size, int weight, litehtml::font_style italic, unsigned int decoration, litehtml::font_metrics* fm) override;
    void delete_font(litehtml::uint_ptr h) override;
    void draw_background(litehtml::uint_ptr, const std::vector<litehtml::background_paint>&) override;
    int pt_to_px(int pt) const override;
    int text_width(const char* text, litehtml::uint_ptr hFont) override;
    void load_all_fonts(void);
    void unload_fonts(void);
    void resize(int width, int height) override;
    std::optional<std::wstring> mapDynamic(const std::wstring& key);
    std::wstring resolveFace(const std::wstring& raw);
    ComPtr<ID2D1BitmapRenderTarget> m_rt;
private:
       // 自动 AddRef/Release
    ComPtr<IDWriteFactory>    m_dwrite;
    Microsoft::WRL::ComPtr<IDWriteFontCollection> m_privateFonts;  // 新增
    std::vector<std::wstring> m_tempFontFiles;
    std::wstring m_actualFamily;   // 保存命中的字体家族名m_actualFamily
    std::unordered_map<std::string, ComPtr<ID2D1Bitmap>> m_d2dBitmapCache;
    ComPtr<ID2D1RenderTarget> m_devCtx;
    static std::wstring toLower(std::wstring s);
    static std::optional<std::wstring> mapStatic(const std::wstring& key);


    ComPtr<IDWriteFontCollection> m_sysFontColl;
    int  m_w, m_h;
};

// -------------- FreeType 后端 -----------------
class FreetypeBackend : public IRenderBackend {
public:
    using RasterCB = std::function<void(int, int, const uint8_t*, int, int)>;
    FreetypeBackend(int w, int h, int dpi,
        uint8_t* surface, int stride,
        FT_Library lib);
    void draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr hFont, litehtml::web_color color, const litehtml::position& pos) override;
    void draw_borders(litehtml::uint_ptr, const litehtml::borders&, const litehtml::position&, bool) override;
    litehtml::uint_ptr create_font(const char* faceName, int size, int weight, litehtml::font_style italic, unsigned int decoration, litehtml::font_metrics* fm) override;
    void delete_font(litehtml::uint_ptr h) override;
    void draw_background(litehtml::uint_ptr, const std::vector<litehtml::background_paint>&) override;
    int pt_to_px(int pt) const override;
    int text_width(const char* text, litehtml::uint_ptr hFont) override;
    void load_all_fonts(void);
    void resize(int width, int height) override;
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
    /* 工厂：根据当前策略创建画布 */
    static std::unique_ptr<ICanvas> create(int w, int h, Renderer which);
};



class RenderWorker {
public:
    static RenderWorker& instance();
    void push(int w, int h, int sy);
    void stop();
    ~RenderWorker();
private:
    RenderWorker() : worker_(&RenderWorker::loop, this) {}
    void loop();
    struct Task { int w, h, sy; };

    std::mutex              m_;
    std::condition_variable cv_;
    std::optional<Task>     latest_;   // 只保留最新
    std::atomic<bool>       stop_{ false };
    std::thread             worker_;
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
private:
    int  m_w, m_h;
    HDC  m_memDC;
    HBITMAP m_bmp, m_old;
    std::unique_ptr<GdiBackend> m_backend;
};

class D2DCanvas : public ICanvas {
public:
    D2DCanvas(int w, int h, ComPtr<ID2D1RenderTarget> devCtx);
    IRenderBackend* backend() override { return m_backend.get(); }
    void present(HDC hdc, int x, int y) override;
    int width()  const override { return m_w; }
    int height() const override { return m_h; }
    litehtml::uint_ptr getContext() override;
    void BeginDraw() override;
    void EndDraw() override;
    void resize(int width, int height) override;
private:
    int  m_w, m_h;
    ComPtr<ID2D1Bitmap> m_bmp;
    std::unique_ptr<D2DBackend> m_backend;
    ComPtr<ID2D1RenderTarget> m_devCtx;



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
private:
    int  m_w, m_h, m_dpi;
    std::vector<uint8_t>        m_pixels;   // 4*w*h
    std::unique_ptr<FreetypeBackend> m_backend;
};


std::unique_ptr<ICanvas>
ICanvas::create(int w, int h, Renderer which)
{
    switch (which)
    {
    case Renderer::GDI:
        return std::make_unique<GdiCanvas>(w, h);

    case Renderer::D2D:
        return std::make_unique<D2DCanvas>(w, h, g_d2dRT);
    case Renderer::FreeType:
        return std::make_unique<FreetypeCanvas>(w, h, 96);   // 96 dpi 示例
    }
    return nullptr;
}
class Paginator {
public:
    void load(litehtml::document* doc, int w, int h);
    void render(ICanvas* canvas, int scrollY);   // 关键：不再区分 HDC / RT
    void clear();
private:
    litehtml::document* m_doc = nullptr;
    int m_w = 0, m_h = 0;
};
// -------------- 工厂 -----------------


static ImgFmt detect_fmt(const uint8_t* d, size_t n, const wchar_t* ext)
{

    if (n >= 4 && memcmp(d, "\x89PNG", 4) == 0) return ImgFmt::PNG;
    if (n >= 2 && d[0] == 0xFF && d[1] == 0xD8)   return ImgFmt::JPEG;
    if (n >= 2 && d[0] == 'B' && d[1] == 'M')      return ImgFmt::BMP;
    if (n >= 6 && memcmp(d, "GIF87a", 6) == 0)    return ImgFmt::GIF;
    if (n >= 6 && memcmp(d, "GIF89a", 6) == 0)    return ImgFmt::GIF;
    if (n >= 4 && memcmp(d, "MM\x00*", 4) == 0)   return ImgFmt::TIFF;
    if (n >= 4 && memcmp(d, "II*\x00", 4) == 0)   return ImgFmt::TIFF;
    if (ext && _wcsicmp(ext, L"svg") == 0)       return ImgFmt::SVG;

    return ImgFmt::UNKNOWN;
}

std::unique_ptr<ICanvas> g_canvas;


void SavePixelsToPNG(const BYTE* pixels, UINT w, UINT h, UINT stride, const wchar_t* file)
{
    ComPtr<IWICImagingFactory> wic;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));

    ComPtr<IWICBitmap> wicBmp;
    wic->CreateBitmapFromMemory(
        w, h,
        GUID_WICPixelFormat32bppPBGRA,
        stride,
        stride * h,
        const_cast<BYTE*>(pixels),
        &wicBmp);

    ComPtr<IWICBitmapEncoder> enc;
    wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);

    ComPtr<IWICStream> stream;
    wic->CreateStream(&stream);
    stream->InitializeFromFilename(file, GENERIC_WRITE);
    enc->Initialize(stream.Get(), WICBitmapEncoderNoCache);

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    enc->CreateNewFrame(&frame, &props);
    frame->Initialize(props.Get());
    frame->SetSize(w, h);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
    frame->SetPixelFormat(&format);
    frame->WriteSource(wicBmp.Get(), nullptr);
    frame->Commit();
    enc->Commit();
}


void PrintSystemFontFamilies()
{
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // 1. 拿 IDWriteFactory
    Microsoft::WRL::ComPtr<IDWriteFactory> factory;
    DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(factory.GetAddressOf()));

    // 2. 拿系统字体集合
    Microsoft::WRL::ComPtr<IDWriteFontCollection> sysFonts;
    factory->GetSystemFontCollection(&sysFonts);

    UINT32 familyCount = sysFonts->GetFontFamilyCount();
    char buf[512];

    for (UINT32 i = 0; i < familyCount; ++i)
    {
        Microsoft::WRL::ComPtr<IDWriteFontFamily> family;
        sysFonts->GetFontFamily(i, &family);

        Microsoft::WRL::ComPtr<IDWriteLocalizedStrings> names;
        family->GetFamilyNames(&names);

        UINT32 idx = 0;
        UINT32 len = 0;
        BOOL exists = FALSE;
        names->FindLocaleName(L"en-us", &idx, &exists);
        if (!exists) idx = 0;

        names->GetStringLength(idx, &len);
        std::wstring wname(len + 1, 0);
        names->GetString(idx, wname.data(), len + 1);

        // 转成 UTF-8
        std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> conv;
        std::string name = conv.to_bytes(wname.c_str());

        snprintf(buf, sizeof(buf), "[SystemFont] %s\n", name.c_str());
        OutputDebugStringA(buf);
    }

    ::CoUninitialize();
}
// 把 ID2D1Bitmap 保存为 PNG，返回 true 表示成功
bool DumpBitmap(ID2D1Bitmap* bmp, const wchar_t* file)
{
    if (!bmp || !file) return false;

    // 1. 尺寸
    D2D1_SIZE_U sz = bmp->GetPixelSize();
    if (sz.width == 0 || sz.height == 0) return false;

    // 2. 创建 WIC 工厂
    ComPtr<IWICImagingFactory> wic;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    if (FAILED(hr)) return false;

    // 3. 创建 WIC 位图（32bpp PBGRA）
    ComPtr<IWICBitmap> wicBmp;
    hr = wic->CreateBitmap(
        sz.width, sz.height,
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapCacheOnLoad,
        &wicBmp);
    if (FAILED(hr)) return false;

    // 4. 创建临时 D2D WIC RenderTarget，把 bmp 画进去
    ComPtr<ID2D1Factory> d2dFactory;
    bmp->GetFactory(&d2dFactory);

    ComPtr<ID2D1RenderTarget> rt;
    hr = d2dFactory->CreateWicBitmapRenderTarget(
        wicBmp.Get(),
        D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                D2D1_ALPHA_MODE_PREMULTIPLIED)),
        &rt);
    if (FAILED(hr)) return false;



    // 5. 编码 PNG
    ComPtr<IWICStream> stream;
    hr = wic->CreateStream(&stream);
    if (FAILED(hr)) return false;

    hr = stream->InitializeFromFilename(file, GENERIC_WRITE);
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapEncoder> encoder;
    hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (FAILED(hr)) return false;

    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    hr = encoder->CreateNewFrame(&frame, &props);
    if (FAILED(hr)) return false;

    hr = frame->Initialize(props.Get());
    if (FAILED(hr)) return false;

    hr = frame->SetSize(sz.width, sz.height);
    if (FAILED(hr)) return false;

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
    hr = frame->SetPixelFormat(&format);
    if (FAILED(hr)) return false;

    hr = frame->WriteSource(wicBmp.Get(), nullptr);
    if (FAILED(hr)) return false;

    hr = frame->Commit();
    if (FAILED(hr)) return false;

    hr = encoder->Commit();
    return SUCCEEDED(hr);
}

void DumpAllFontNames()
{
    HDC hdc = GetDC(nullptr);
    LOGFONTW lf{ 0 };
    lf.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(hdc, &lf,
        [](const LOGFONTW* lpelfe, const TEXTMETRICW*, DWORD, LPARAM) -> int {
            OutputDebugStringW((L"[Enum] " + std::wstring(lpelfe->lfFaceName) + L"\n").c_str());
            return 1;
        }, 0, 0);
    ReleaseDC(nullptr, hdc);
}

// 真正读文件
static void do_reload()
{
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    fs::path cssPath = fs::path(exe).parent_path() / L"config" / L"global.css";

    try
    {
        auto t = fs::last_write_time(cssPath);
        if (t != g_lastTime)
        {
            std::ifstream f(cssPath, std::ios::binary);
            if (f)
            {
                std::ostringstream oss;
                oss << f.rdbuf();
                g_globalCSS = oss.str();
                g_lastTime = t;
                PostMessage(g_hView, WM_EPUB_CSS_RELOAD, 0, 0);
            }
        }
    }
    catch (...) { /* 文件不存在就忽略 */ }
}

// 后台线程：每秒检查一次
static void css_watcher_thread()
{
    do_reload();   // 启动时先读一次
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        do_reload();
    }
}

// 在 main() 里调用一次即可
inline void start_css_watcher()
{
    static std::once_flag once;
    std::call_once(once, [] {
        std::thread(css_watcher_thread).detach();
        });
}
// ---------- 工具 ----------
static std::string w2a(const std::wstring& s)
{
    if (s.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(len - 1, 0);                 // 去掉末尾 '\0'
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, &out[0], len, nullptr, nullptr);
    return out;
}

static std::wstring a2w(const std::string& s)
{
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(len - 1, 0);                // 去掉末尾 '\0'
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], len);
    return out;
}

static inline std::string trim_any(const std::string& s,
    const char* ws = " \t\"'")
{
    if (s.empty()) return s;
    size_t first = s.find_first_not_of(ws);
    if (first == std::string::npos) return "";
    size_t last = s.find_last_not_of(ws);
    return s.substr(first, last - first + 1);
}



void ShowActiveFontsDialog(HWND hParent)
{
    std::wstring text;
    if (g_activeFonts.empty())
    {
        text = L"当前没有加载任何字体。";
    }
    else
    {
        for (const auto& name : g_activeFonts)
            text += name + L"\r\n";
    }

    MessageBoxW(hParent,
        text.c_str(),
        L"当前正在使用的字体",
        MB_ICONINFORMATION | MB_OK);
}

bool is_xhtml(const std::wstring& file_path)
{
    auto dot = file_path.rfind(L'.');
    if (dot == std::wstring::npos) return false;

    std::wstring ext = file_path.substr(dot + 1);

    // 1. 小写
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);

    // 2. 去掉控制字符
    ext.erase(std::remove_if(ext.begin(), ext.end(),
        [](wchar_t c) { return c < 32 || c > 126; }),
        ext.end());

    // 3. 比较
    return ext == L"xhtml";
}


// 读取 ./config/global.css


static std::string insert_global_css(std::string html) {
    if (!g_globalCSS.empty())
    {
        // 简单暴力：直接插到 </head> 之前
        const std::string styleBlock =
            "<style>\n" + g_globalCSS + "\n</style>\n";
        size_t pos = html.find("</head>");
        if (pos != std::string::npos)
            html.insert(pos, styleBlock);
        else
            html.insert(0, "<head>" + styleBlock + "</head>");
    }
    return html;
}

void EnableClearType()
{
    BOOL ct = FALSE;
    SystemParametersInfoW(SPI_GETCLEARTYPE, 0, &ct, 0);
    if (!ct)
        SystemParametersInfoW(SPI_SETCLEARTYPE, TRUE, 0, SPIF_UPDATEINIFILE);
}
static HFONT CreateFontBetter(const wchar_t* faceW, int size, int weight,
    bool italic, HDC hdcForDpi)
{
    LOGFONTW lf = { 0 };
    lf.lfHeight = -MulDiv(size, GetDeviceCaps(hdcForDpi, LOGPIXELSY), 72);
    lf.lfWeight = weight;
    lf.lfItalic = italic ? TRUE : FALSE;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfQuality = CLEARTYPE_QUALITY;          // ← 关键
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_SWISS;
    const wchar_t* src = (faceW && *faceW) ? faceW : L"Microsoft YaHei";
    wcsncpy_s(lf.lfFaceName, src, _TRUNCATE);   // 超长自动截断

    // 让系统根据 FontLink 自动 fallback（中文、日文、符号都能匹配）
    return CreateFontIndirectW(&lf);
}
//// 创建字体时顺带把度量算好
//struct FontWrapper {
//    HFONT hFont = nullptr;
//    int   height = 0;
//    int   ascent = 0;
//    int   descent = 0;
//
//    explicit FontWrapper(const wchar_t* face, int size, int weight, bool italic)
//    {
//        HDC hdc = GetDC(nullptr);                    // 临时 HDC
//        hFont = CreateFontBetter(face, size, weight, italic, hdc);
//
//        if (hFont) {
//            HGDIOBJ old = SelectObject(hdc, hFont);
//            TEXTMETRICW tm{};
//            GetTextMetricsW(hdc, &tm);
//            height = tm.tmHeight;
//            ascent = tm.tmAscent;
//            descent = tm.tmDescent;
//            SelectObject(hdc, old);
//        }
//        ReleaseDC(nullptr, hdc);
//    }
//    ~FontWrapper() { if (hFont) DeleteObject(hFont); }
//};

inline void SetStatus(const wchar_t* msg) {
    SendMessage(g_hStatus, SB_SETTEXT, 0, (LPARAM)msg);
}


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
    const OCFNavPoint* data;
    std::vector<TreeNode> children;
};

static std::vector<TreeNode> BuildTree(const std::vector<OCFNavPoint>& flat)
{
    std::vector<TreeNode> roots;
    std::vector<TreeNode*> stack;          // 充当“当前父节点栈”
    stack.push_back(nullptr);              // 栈底：根层

    for (const auto& np : flat)
    {
        // 根据 order 决定栈深度
        while (stack.size() > static_cast<size_t>(np.order + 1))
            stack.pop_back();

        TreeNode node{ &np, {} };
        if (stack.back())
            stack.back()->children.push_back(std::move(node));
        else
            roots.push_back(std::move(node));

        stack.push_back(&roots.back());    // 新节点入栈
    }
    return roots;
}



class ZipIndexW {
public:
    ZipIndexW() = default;
    explicit ZipIndexW(mz_zip_archive& zip) { build(zip); }

    // 输入/输出均为 std::wstring
    std::wstring find(std::wstring href) const {
        // 1. 分离锚点
        size_t pos = href.find(L'#');
        std::wstring pure = pos == std::wstring::npos ? href : href.substr(0, pos);
        std::wstring anchor = pos == std::wstring::npos ? L"" : href.substr(pos);

        // 2. 查索引
        std::wstring key = normalize_key(pure);
        auto it = map_.find(key);
        std::wstring result = it == map_.end() ? std::wstring{} : it->second;

        // 3. 把锚点拼回去
        return result.empty() ? result : result + anchor;
    }

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
    static std::wstring url_decode(const std::wstring& in)
    {
        wchar_t out[INTERNET_MAX_URL_LENGTH];
        DWORD len = INTERNET_MAX_URL_LENGTH;
        if (SUCCEEDED(UrlCanonicalizeW(in.c_str(), out, &len, URL_UNESCAPE)))
            return std::wstring(out, len);
        return in;
    }

    static std::wstring normalize_key(std::wstring href)
    {
        // 1. 去掉 ? 和 #
        auto pure = href.substr(0, href.find_first_of(L"?#"));
        // 2. URL 解码
        pure = url_decode(pure);
        // 3. 取文件名并转小写
        auto filename = fs::path(pure).filename().wstring();
        std::transform(filename.begin(), filename.end(), filename.begin(),
            [](wchar_t c) { return towlower(static_cast<wint_t>(c)); });
        return filename;
    }

    /* ---------- 建立索引 ---------- */
    void build(mz_zip_archive& zip) {
        mz_uint idx = 0;
        mz_zip_archive_file_stat st{};
        while (mz_zip_reader_file_stat(&zip, idx++, &st)) {
            // miniz 返回 UTF-8，转成 wstring
            std::wstring wpath = a2w(st.m_filename);
            std::wstring key = normalize_key(wpath);
            map_.emplace(std::move(key), std::move(wpath));
        }
    }



};

ZipIndexW g_zipIndex;
// ---------- EPUB 零解压 ----------
struct EPUBBook {
    struct MemFile {
        std::vector<uint8_t> data;
        const char* begin() const { return reinterpret_cast<const char*>(data.data()); }
        size_t      size()  const { return data.size(); }
    };
    mz_zip_archive zip = {};
    std::map<std::wstring, MemFile> cache;

    // -------------- EPUBBook 内部新增成员 --------------
    OCFPackage ocf_pkg_;                     // 解析结果
    void parse_ocf_(void);                       // 主解析入口
    void parse_opf_(void);   // 解析 OPF
    void parse_toc_(void);                        // 解析 TOC
    void LoadToc(void);
    void OnTreeSelChanged(const wchar_t* href);
    bool load(const wchar_t* epub_path);
    MemFile read_zip(const wchar_t* file_name) const;
    std::string load_html(const std::wstring& path) const;

    void load_all_fonts(void);
    void init_doc(std::string html);
    static HTREEITEM InsertTreeNode(HWND tv, const TreeNode& node, HTREEITEM hParent)
    {
        TVINSERTSTRUCTW tvi{};
        tvi.hParent = hParent;
        tvi.hInsertAfter = TVI_LAST;
        tvi.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
        tvi.item.pszText = const_cast<LPWSTR>(node.data->label.c_str());
        tvi.item.lParam = reinterpret_cast<LPARAM>(_wcsdup(node.data->href.c_str()));
        tvi.item.cChildren = node.children.empty() ? 0 : 1;
        HTREEITEM hItem = TreeView_InsertItem(tv, &tvi);

        for (const auto& child : node.children)
            InsertTreeNode(tv, child, hItem);

        return hItem;
    }
    static std::wstring extract_text(const tinyxml2::XMLElement* a)
    {
        if (!a) return L"";

        // 1. 拿到 <a> 的完整 XML 字符串
        tinyxml2::XMLPrinter printer;
        a->Accept(&printer);
        std::string xml = printer.CStr();   // "<a ...><span ...>I</span>: The Meadow</a>"

        // 2. 去掉最外层 <a ...> 和 </a>
        size_t start = xml.find('>') + 1;
        size_t end = xml.rfind('<');
        if (start == std::string::npos || end == std::string::npos || end <= start)
            return L"";

        std::string inner = xml.substr(start, end - start);   // "<span ...>I</span>: The Meadow"

        // 3. 简单剥掉所有标签（正则或手写）
        std::regex tag_re("<[^>]*>");
        std::string plain = std::regex_replace(inner, tag_re, "");

        return a2w(plain);   // "I: The Meadow"
    }
    // 递归解析 EPUB3-Nav <ol>
    static void parse_nav_list(tinyxml2::XMLElement* ol, int level,
        const std::string& opf_dir,
        std::vector<OCFNavPoint>& out)
    {
        if (!ol) return;
        for (auto* li = ol->FirstChildElement("li"); li; li = li->NextSiblingElement("li"))
        {
            auto* a = li->FirstChildElement("a");
            if (!a) continue;

            OCFNavPoint np;
            np.label = extract_text(a);
            np.href = a2w(a->Attribute("href") ? a->Attribute("href") : "");
            if (!np.href.empty())
                np.href = g_zipIndex.find(np.href);
            np.order = level;               // 层级深度
            out.emplace_back(std::move(np));

            // 递归子 <ol>
            if (auto* sub = li->FirstChildElement("ol"))
                parse_nav_list(sub, level + 1, opf_dir, out);
        }
    }

    // 递归解析 NCX <navPoint>
    static void parse_ncx_points(tinyxml2::XMLElement* navPoint, int level,
        const std::string& opf_dir,
        std::vector<OCFNavPoint>& out)
    {
        if (!navPoint) return;
        for (auto* pt = navPoint; pt; pt = pt->NextSiblingElement("navPoint"))
        {
            auto* lbl = pt->FirstChildElement("navLabel");
            auto* txt = lbl ? lbl->FirstChildElement("text") : nullptr;
            auto* con = pt->FirstChildElement("content");

            OCFNavPoint np;

            np.label = txt ? extract_text(txt) : L"";
            np.href = a2w(con && con->Attribute("src") ? con->Attribute("src") : "");
            if (!np.href.empty())
                np.href = g_zipIndex.find(np.href);
            np.order = level;               // 层级深度
            out.emplace_back(std::move(np));

            // 递归子 <navPoint>
            parse_ncx_points(pt->FirstChildElement("navPoint"), level + 1, opf_dir, out);
        }
    }



    EPUBBook() noexcept {}
    ~EPUBBook() { 
        mz_zip_reader_end(&zip);
        for (const auto& path : g_tempFontFiles)
        {
            RemoveFontResourceExW(path.c_str(), FR_PRIVATE, 0);
            DeleteFileW(path.c_str());
        }
        g_tempFontFiles.clear();
    }
};


EPUBBook::MemFile EPUBBook::read_zip(const wchar_t* file_name) const {
    MemFile mf;

    // 2.1 先按给定宽路径找
    std::string narrow_name = w2a(file_name);
    size_t uncomp_size = 0;
    void* p = mz_zip_reader_extract_file_to_heap(
        const_cast<mz_zip_archive*>(&zip),
        narrow_name.c_str(),
        &uncomp_size, 0);

    // 2.2 没找到 && 是相对路径，再拼一次 opf_dir
    if (!p && file_name[0] != L'/' && !ocf_pkg_.opf_dir.empty()) {
        std::wstring abs = ocf_pkg_.opf_dir + file_name;
        std::string narrow_abs = w2a(abs);
        p = mz_zip_reader_extract_file_to_heap(
            const_cast<mz_zip_archive*>(&zip),
            narrow_abs.c_str(),
            &uncomp_size, 0);
    }

    if (p) {
        mf.data.assign(static_cast<uint8_t*>(p),
            static_cast<uint8_t*>(p) + uncomp_size);
        mz_free(p);
    }
    return mf;
}

std::string EPUBBook::load_html(const std::wstring& path) const {
    namespace fs = std::filesystem;
    g_currentHtmlDir = fs::path(path).parent_path().wstring();
    auto it = cache.find(path);
    if (it != cache.end()) return std::string(it->second.begin(), it->second.size());
    EPUBBook::MemFile mf = read_zip(path.c_str());
    if (mf.data.empty()) return {};
    return std::string(mf.begin(), mf.size());
}

bool EPUBBook::load(const wchar_t* epub_path) {
    namespace fs = std::filesystem;
    if (!fs::exists(epub_path))
        throw std::runtime_error("文件不存在");

    mz_zip_reader_end(&zip);           // 1. 先关闭旧 zip
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_file(&zip, w2a(epub_path).c_str(), 0))
        throw std::runtime_error("zip 打开失败：" +
            std::to_string(mz_zip_get_last_error(&zip)));
    g_zipIndex = ZipIndexW(zip);
    parse_ocf_();
    parse_opf_();
    parse_toc_();
    load_all_fonts();
    //DumpAllFontNames();
    LoadToc();
    return true;
}


// -------------- 实现（直接粘到 EPUBBook 末尾即可） --------------
void EPUBBook::parse_ocf_() {
    ocf_pkg_ = {};  // 清空
    auto container = read_zip(L"META-INF/container.xml");
    if (container.data.empty()) return;

    tinyxml2::XMLDocument doc;
    if (doc.Parse(container.begin(), container.size()) != tinyxml2::XML_SUCCESS) return;

    auto* rootfile = doc.FirstChildElement("container")
        ? doc.FirstChildElement("container")->FirstChildElement("rootfiles")
        : nullptr;
    rootfile = rootfile ? rootfile->FirstChildElement("rootfile") : nullptr;
    if (!rootfile || !rootfile->Attribute("full-path")) return;

    ocf_pkg_.rootfile = a2w(rootfile->Attribute("full-path"));
    ocf_pkg_.opf_dir = ocf_pkg_.rootfile.substr(0, ocf_pkg_.rootfile.find_last_of(L'/') + 1);

}

void EPUBBook::parse_opf_() {
    auto opf = read_zip(ocf_pkg_.rootfile.c_str());
    std::string xml(opf.begin(), opf.begin() + opf.size());
    if (opf.data.empty()) return;
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS) return;

    auto* man = doc.RootElement()
        ? doc.RootElement()->FirstChildElement("manifest")
        : nullptr;

    for (auto* it = man ? man->FirstChildElement("item") : nullptr;
        it; it = it->NextSiblingElement("item"))
    {
        OCFItem item;
        item.id = a2w(it->Attribute("id") ? it->Attribute("id") : "");
        item.href = a2w(it->Attribute("href") ? it->Attribute("href") : "");
        item.media_type = a2w(it->Attribute("media-type") ? it->Attribute("media-type") : "");
        item.properties = a2w(it->Attribute("properties") ? it->Attribute("properties") : "");


        // 只在 href 非空时拼绝对路径
        if (!item.href.empty())
            item.href = g_zipIndex.find(item.href);

        ocf_pkg_.manifest.emplace_back(std::move(item));
    }

    // spine
    auto* spine = doc.RootElement()
        ? doc.RootElement()->FirstChildElement("spine")
        : nullptr;
    // 先把 manifest 做成 id -> href 的映射
    std::unordered_map<std::wstring, std::wstring> id2href;
    for (const auto& m : ocf_pkg_.manifest)
        id2href[m.id] = m.href;

    // 再解析 spine
    for (auto* it = spine ? spine->FirstChildElement("itemref") : nullptr;
        it; it = it->NextSiblingElement("itemref")) {

        OCFRef ref;
        ref.idref = a2w(it->Attribute("idref") ? it->Attribute("idref") : "");
        ref.href = id2href[ref.idref];   // 直接填进去
        ref.linear = a2w(it->Attribute("linear") ? it->Attribute("linear") : "yes");
        ocf_pkg_.spine.emplace_back(std::move(ref));
    }
    // meta
    auto* meta = doc.RootElement()
        ? doc.RootElement()->FirstChildElement("metadata")
        : nullptr;
    for (auto* it = meta ? meta->FirstChildElement() : nullptr;
        it; it = it->NextSiblingElement()) {
        ocf_pkg_.meta[a2w(it->Name())] = a2w(it->GetText() ? it->GetText() : "");
    }
}


void EPUBBook::parse_toc_()
{
    std::wstring toc_path;
    for (const auto& it : ocf_pkg_.manifest)
    {
        if (it.properties.find(L"nav") != std::wstring::npos ||
            it.id.find(L"ncx") != std::wstring::npos)
        {
            toc_path = it.href;
            break;
        }
    }
    if (toc_path.empty()) return;

    ocf_pkg_.toc_path = toc_path;
    auto toc = read_zip(toc_path.c_str());
    if (toc.data.empty()) return;

    tinyxml2::XMLDocument doc;
    if (doc.Parse(toc.begin(), toc.size()) != tinyxml2::XML_SUCCESS) return;

    bool is_nav = is_xhtml(toc_path);
    std::string opf_dir = w2a(ocf_pkg_.opf_dir);

    ocf_pkg_.toc.clear();

    if (is_nav)
    {
        auto* body = doc.FirstChildElement("html")
            ? doc.FirstChildElement("html")->FirstChildElement("body")
            : nullptr;
        if (!body) return;

        for (auto* nav = body->FirstChildElement("nav");
            nav;
            nav = nav->NextSiblingElement("nav"))
        {
            const char* type = nav->Attribute("epub:type");
            if (type && std::string(type) == "toc")
            {
                parse_nav_list(nav->FirstChildElement("ol"), 0, opf_dir, ocf_pkg_.toc);
                break;   // 找到就停
            }
        }
    }
    else // NCX
    {
        auto* navMap = doc.RootElement()
            ? doc.RootElement()->FirstChildElement("navMap")
            : nullptr;
        if (navMap)
            parse_ncx_points(navMap->FirstChildElement("navPoint"), 0, opf_dir, ocf_pkg_.toc);
    }
}

// ---------- LiteHtml 容器 ----------
class SimpleContainer : public litehtml::document_container {
public:
    explicit SimpleContainer(const std::wstring& root = L"")
        : m_root(root){   }
    void clear_images() { g_img_cache.clear(); }
    int text_width(const char* text, litehtml::uint_ptr hFont) override;
    void load_image(const char* src, const char* /*baseurl*/, bool) override;

    void get_image_size(const char* src, const char* baseurl, litehtml::size& sz) override;
    void get_client_rect(litehtml::position& client) const override;
    litehtml::element::ptr create_element(const char*, const litehtml::string_map&, const std::shared_ptr<litehtml::document>&) override;

    int get_default_font_size() const override { return 16; }
    const char* get_default_font_name() const override { return "Segoe UI"; }
    void import_css(litehtml::string&, const litehtml::string&, litehtml::string&) override;

    void set_caption(const char*) override;
    void set_base_url(const char*) override;
    void link(const std::shared_ptr<litehtml::document>&, const litehtml::element::ptr&) override;
    void on_anchor_click(const char*, const litehtml::element::ptr&) override;
    void set_cursor(const char*) override;
    void transform_text(litehtml::string&, litehtml::text_transform) override;

    void set_clip(const litehtml::position&, const litehtml::border_radiuses&) override;
    void del_clip() override;

    void get_media_features(litehtml::media_features&) const override;
    void get_language(litehtml::string&, litehtml::string&) const override;

    void draw_list_marker(litehtml::uint_ptr, const litehtml::list_marker&) override;
  



    // 渲染后端需要实现的
    void draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr hFont, litehtml::web_color color, const litehtml::position& pos) override;
    void draw_borders(litehtml::uint_ptr, const litehtml::borders&, const litehtml::position&, bool) override;
    litehtml::uint_ptr create_font(const char* faceName, int size, int weight, litehtml::font_style italic, unsigned int decoration, litehtml::font_metrics* fm) override;
    void delete_font(litehtml::uint_ptr h) override;
    void draw_background(litehtml::uint_ptr, const std::vector<litehtml::background_paint>&) override;
    int pt_to_px(int pt) const override;

    ~SimpleContainer()
    {
        clear_images();   // 仅触发一次 Image 析构

    }
    std::unordered_map<std::string, litehtml::element::ptr> m_anchor_map;

private:
    std::wstring m_root;
    // 1. 锚点表（id -> element）

    // 2. 最后一次传入的 HDC，用于 set_clip / del_clip
    HDC m_last_hdc = nullptr;

    // 3. 默认字体句柄（FontWrapper 是你自己的字体包装类）
    HFONT m_hDefaultFont = nullptr;


};

static ImageFrame decode_img(const EPUBBook::MemFile& mf, const wchar_t* ext)
{
    ImageFrame frame;
    auto fmt = detect_fmt(mf.data.data(), mf.data.size(), ext);

    switch (fmt)
    {
    case ImgFmt::SVG:
    {
        auto doc = lunasvg::Document::loadFromData(
            reinterpret_cast<const char*>(mf.data.data()), mf.data.size());
        if (!doc) return {};
        lunasvg::Bitmap svgBmp = doc->renderToBitmap();
        if (svgBmp.isNull()) return {};

        frame.width = svgBmp.width();
        frame.height = svgBmp.height();
        frame.stride = frame.width * 4;
        frame.rgba.assign(
            reinterpret_cast<uint8_t*>(svgBmp.data()),
            reinterpret_cast<uint8_t*>(svgBmp.data()) + frame.stride * frame.height);
        break;
    }

    default:   // PNG/JPEG/BMP/GIF/TIFF/…
    {
        IStream* pStream = SHCreateMemStream(mf.data.data(),
            static_cast<UINT>(mf.data.size()));
        if (!pStream) return {};
        Gdiplus::Bitmap bmp(pStream, FALSE);
        pStream->Release();
        if (bmp.GetLastStatus() != Gdiplus::Ok) return {};

        frame.width = bmp.GetWidth();
        frame.height = bmp.GetHeight();
        frame.stride = frame.width * 4;
        frame.rgba.resize(frame.stride * frame.height);

        Gdiplus::BitmapData data{};
        Gdiplus::Rect rc(0, 0, frame.width, frame.height);
        if (bmp.LockBits(&rc, Gdiplus::ImageLockModeRead,
            PixelFormat32bppPARGB, &data) == Gdiplus::Ok)
        {
            for (uint32_t y = 0; y < frame.height; ++y)
                memcpy(frame.rgba.data() + y * frame.stride,
                    reinterpret_cast<uint8_t*>(data.Scan0) + y * data.Stride,
                    frame.stride);
            bmp.UnlockBits(&data);
        }
        break;
    }
    }
    return frame;
}
// ---------- 分页 ----------


void Paginator::load(litehtml::document* doc, int w, int h)
{
    m_doc = doc;
    
    m_w = w;
    m_h = h;
    g_maxScroll = 0;
    if (!m_doc) return;
    /* 1) 需要排版才排 */
    if (g_states.needRelayout.exchange(false)) {
        m_doc->render(m_w, litehtml::render_all);
    }
    g_maxScroll = m_doc->height();
}
void Paginator::render(ICanvas* canvas, int scrollY)
{
    if (!canvas || !m_doc) return;

    canvas->BeginDraw();
    // 让 litehtml 画在左上角，clip 足够大
    litehtml::position clip(0, 0, m_w, m_h);
    m_doc->draw(canvas->getContext(),
        0, -scrollY, &clip);   // scrollY 先给 0
    canvas->EndDraw();
}
void Paginator::clear() {
    m_doc = nullptr;
    m_w = m_h = 0;
    g_maxScroll = 0;
}

// ---------- 全局 ----------
HINSTANCE g_hInst;
std::shared_ptr<SimpleContainer> g_container;
EPUBBook  g_book;
std::shared_ptr<litehtml::document> g_doc;
Paginator g_pg;

std::future<void> g_parse_task;


litehtml::element::ptr element_from_point(litehtml::element::ptr root,
    int x, int y)
{
    if (!root) return nullptr;

    // 倒序遍历（后渲染的在上面，先匹配）
    const auto& ch = root->children();
    for (auto it = ch.rbegin(); it != ch.rend(); ++it)
    {
        if (auto hit = element_from_point(*it, x, y))
            return hit;
    }

    // 检查自身
    litehtml::position pos = root->get_placement();
    if (x >= pos.left() && x < pos.right() &&
        y >= pos.top() && y < pos.bottom())
    {
        return root;
    }
    return nullptr;
}

LRESULT CALLBACK ViewWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_SIZE:
    {
        g_states.needRelayout.store(true);
        if (g_canvas) {
            g_canvas->resize(LOWORD(lp), HIWORD(lp));
        }
        if (g_d2dRT) {
            RECT rc;
            GetClientRect(g_hView, &rc);
            D2D1_SIZE_U size{ rc.right - rc.left, rc.bottom - rc.top };
            g_d2dRT->Resize(size);   // HwndRenderTarget 专用
        }
        return 0;
    }
    case WM_EPUB_CACHE_UPDATED:
    {

        UpdateWindow(g_hView);
  
        return 0;
    }
    case WM_EPUB_ANCHOR:
    {
        wchar_t* sel = reinterpret_cast<wchar_t*>(wp);
        if (sel) {
            std::string cssSel = w2a(sel);
            if (auto el = g_doc->root()->select_one(cssSel.c_str())) {
                g_scrollY = el->get_placement().y;
            }
            free(sel);          // 对应 _wcsdup
        }
        UpdateCache();
        InvalidateRect(hWnd, nullptr, FALSE);
        UpdateWindow(g_hView);

        return 0;
    }
    case WM_EPUB_UPDATE_SCROLLBAR: {
        RECT rc;
        GetClientRect(hWnd, &rc);
        // 垂直滚动条
        SCROLLINFO si{ sizeof(si) };
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin = 0;
        si.nMax = std::max(0, g_maxScroll);
        si.nPage = rc.bottom;               // 每次滚一页
        si.nPos = g_scrollY;
        SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
        // 水平滚动条（如果不需要可删掉）
        si.nMax = 0;
        si.nPage = rc.right;
        SetScrollInfo(hWnd, SB_HORZ, &si, TRUE);
        // 重新排版+缓存
        UpdateCache();
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_VSCROLL:
    {
        RECT rc;
        GetClientRect(hWnd, &rc);
        int code = LOWORD(wp);
        int pos = HIWORD(wp);
        int delta = 0;

        switch (code)
        {
        case SB_LINEUP:   delta = -30; break;      // 3 行
        case SB_LINEDOWN: delta = 30; break;
        case SB_PAGEUP:   delta = -rc.bottom; break;
        case SB_PAGEDOWN: delta = rc.bottom; break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:
            g_scrollY = std::clamp(pos, 0, g_maxScroll);          // 直接定位
            goto _scroll_end;
        default: return 0;
        }

        g_scrollY = std::clamp(g_scrollY + delta, 0, g_maxScroll);

    _scroll_end:
        SetScrollPos(hWnd, SB_VERT, g_scrollY, TRUE);
        UpdateCache();
        InvalidateRect(hWnd, nullptr, FALSE);   // 触发 WM_PAINT
        return 0;
    }
    case WM_MOUSEWHEEL:
    {
        RECT rc;
        GetClientRect(hWnd, &rc);
        int zDelta = GET_WHEEL_DELTA_WPARAM(wp);
        g_scrollY = std::clamp<int>(g_scrollY - zDelta, 0, std::max<int>(g_maxScroll - rc.bottom, 0));
        SetScrollPos(hWnd, SB_VERT, g_scrollY, TRUE);
        UpdateCache();
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        if (g_canvas )
            g_canvas->present(hdc, 0, 0);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProc(hWnd, msg, wp, lp);
}

ATOM RegisterViewClass(HINSTANCE hInst)
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = ViewWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"EPUBView";
    return RegisterClassW(&wc);
}

//// 4. UI 线程：只管发任务
void UpdateCache()
{
    if (!g_doc || !g_canvas) return;

    RECT rc; GetClientRect(g_hView, &rc);
    int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0) return;


    g_pg.load(g_doc.get(), w, h);
    /* 3) 渲染整页 */
    g_pg.render(g_canvas.get(), g_scrollY);
}


// ---------- 窗口 ----------
LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        DragAcceptFiles(h, TRUE);
        SendMessage(g_hWnd, WM_SIZE, 0, 0);

        return 0;
    }
    case WM_DROPFILES: {
        wchar_t file[MAX_PATH]{};
        DragQueryFileW(reinterpret_cast<HDROP>(w), 0, file, MAX_PATH);
        DragFinish(reinterpret_cast<HDROP>(w));


        // 1. 等待上一次任务结束（简单做法：阻塞等待）
        if (g_parse_task.valid()) {
            g_parse_task.wait();
        }

        // 2. 立即释放旧对象，防止野指针
        g_doc.reset();
        g_container.reset();
        g_pg.clear();              // 如果你 Paginator 有 clear() 就调

        // 3. 启动新任务
        SetStatus(L"正在加载...");
        g_parse_task = std::async(std::launch::async, [file] {
            try {
                g_book = EPUBBook{};   // 保险：用新实例
                g_book.load(file);
                PostMessage(g_hWnd, WM_EPUB_PARSED, 0, 0);
            }
            catch (const std::exception& e) {
                // 把异常文本发到主线程
                std::string what = e.what();
                auto* buf = (wchar_t*)CoTaskMemAlloc((what.size() + 1) * sizeof(wchar_t));
                MultiByteToWideChar(CP_UTF8, 0, what.c_str(), -1, buf, (int)what.size() + 1);
                PostMessage(g_hWnd, WM_LOAD_ERROR, 0, (LPARAM)buf);
            }
            catch (...) {
                PostMessage(g_hWnd, WM_LOAD_ERROR, 0,
                    (LPARAM)_wcsdup(L"未知错误"));
            }
            });
        return 0;
    }

    case WM_EPUB_PARSED: {
        g_last_html_path = g_book.ocf_pkg_.spine[0].href;
        std::string html = g_book.load_html(g_last_html_path);
       
        g_book.init_doc(html);

        // 2) 立即把第 0 页画到缓存位图
        UpdateCache();          // 复用前面给出的 UpdateCache()

        // 4) 触发一次轻量 WM_PAINT（只 BitBlt）
        InvalidateRect(g_hView, nullptr, FALSE);
        InvalidateRect(g_hWnd, nullptr, FALSE);
        UpdateWindow(g_hView);
        UpdateWindow(g_hWnd);
        SetStatus(L"加载完成");
        return 0;
    }
    case WM_EPUB_CSS_RELOAD: {
        // 重新解析当前章节即可
        return 0;
    }
    case WM_SIZE:
    {
        SendMessage(g_hStatus, WM_SIZE, 0, 0);

        RECT rcStatus, rcClient;
        GetClientRect(h, &rcClient);
        GetWindowRect(g_hStatus, &rcStatus);
        ScreenToClient(h, (POINT*)&rcStatus);
        ScreenToClient(h, (POINT*)&rcStatus + 1);
        int cyStatus = rcStatus.bottom - rcStatus.top;

        int cx = rcClient.right;
        int cy = rcClient.bottom - cyStatus;

        const int TV_W = 200;
        const int BAR_H = 30;

        MoveWindow(g_hwndTV, 0, 0, TV_W, cy, TRUE);

        MoveWindow(g_hView, TV_W, 0, cx - TV_W, cy, TRUE);
        UpdateCache();
        SendMessage(g_hView, WM_EPUB_UPDATE_SCROLLBAR, 0, 0);

        UpdateWindow(g_hWnd);
        UpdateWindow(g_hView);
        return 0;
    }
    case WM_LOAD_ERROR: {
        wchar_t* msg = (wchar_t*)l;
        SetStatus(msg);
        free(msg);                // 对应 CoTaskMemAlloc / _wcsdup
        return 0;
    }
    case WM_PAINT: {

        return 0;
    }

    case WM_MOUSEWHEEL: {

        //UpdateCache();              // 关键：立即排版并缓存
        //InvalidateRect(g_hView, nullptr, FALSE);
        return 0;
    }


    case WM_DESTROY: {
        for (HTREEITEM h = TreeView_GetRoot(g_hwndTV); h; )
        {
            TVITEMW tvi{ TVIF_PARAM };
            tvi.hItem = h;
            TreeView_GetItem(g_hwndTV, &tvi);
            free((void*)tvi.lParam);
            h = TreeView_GetNextSibling(g_hwndTV, h);
        }
        //if (g_d2dRT) { g_d2dRT->Release();   g_d2dRT = nullptr; }
        //if (g_d2dFactory) { g_d2dFactory->Release(); g_d2dFactory = nullptr; }
        if (g_hMemDC) { DeleteDC(g_hMemDC);  g_hMemDC = nullptr; }
        if (g_ftLib) { FT_Done_FreeType(g_ftLib); g_ftLib = nullptr; }
 
        PostQuitMessage(0);
        return 0;
    }
    case WM_NOTIFY:
    {
        LPNMHDR nm = reinterpret_cast<LPNMHDR>(l);
        if (nm->hwndFrom == g_hwndTV)
        {
            // 1. 让系统完成默认选中/高亮
            LRESULT lr = DefWindowProc(h, m, w, l);

            // 2. 只在 *真正改变后* 做加载
            if (nm->code == TVN_SELCHANGED)
            {
                HTREEITEM hSel = TreeView_GetSelection(g_hwndTV);
                if (hSel)
                {
                    SetFocus(g_hwndTV);

                    TVITEMW tvi{ TVIF_PARAM, hSel };
                    if (TreeView_GetItem(g_hwndTV, &tvi))
                    {
                        const wchar_t* href = reinterpret_cast<const wchar_t*>(tvi.lParam);
                        if (href && *href)
                            g_book.OnTreeSelChanged(href);
                    }
                }
            }
            else if (nm->code == TVN_ITEMEXPANDED)
            {
                UpdateWindow(g_hWnd);
            }

            return lr;   // 必须返回 DefWindowProc 的结果
        }
        break;
    }
    case WM_COMMAND: {
        switch (LOWORD(w)) {
        case IDM_TOGGLE_CSS: {
            g_cfg.disableCSS = !g_cfg.disableCSS;
            CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_CSS,
                MF_BYCOMMAND | (g_cfg.disableCSS ? MF_CHECKED : MF_UNCHECKED));

            break;
        }
        case IDM_TOGGLE_JS: {
            g_cfg.disableJS = !g_cfg.disableJS;          // 切换状态
            CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_JS,
                MF_BYCOMMAND | (g_cfg.disableJS ? MF_CHECKED : MF_UNCHECKED));

            break;
        }
        case IDM_INFO_FONTS:{
            ShowActiveFontsDialog(g_hWnd);
            PrintSystemFontFamilies();
            break;
        }
                          break;

        }
    }
    }
    return DefWindowProc(h, m, w, l);

}



// ---------- 入口 ----------
int WINAPI wWinMain(HINSTANCE h, HINSTANCE, LPWSTR, int n) {
  
    ULONG_PTR gdiplusToken{};
    GdiplusStartupInput gdiplusStartupInput{};
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
    g_hInst = h;
    InitCommonControls();
    WNDCLASSEX w{ sizeof(WNDCLASSEX) };
    w.lpfnWndProc = WndProc;
    w.hInstance = h;
    w.hCursor = LoadCursor(nullptr, IDC_ARROW);
    w.lpszClassName = L"EPUBLite";
    w.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    w.lpszMenuName = nullptr;   // ← 必须为空
    RegisterClassEx(&w);
    RegisterViewClass(g_hInst);
    start_css_watcher();

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_TREEVIEW_CLASSES };
    InitCommonControlsEx(&icc);
    // 在 CreateWindow 之前
    HMENU hMenu = LoadMenu(g_hInst, MAKEINTRESOURCE(IDR_MENU_MAIN));
    
    if (!hMenu) {
        MessageBox(nullptr, L"LoadMenu 失败", L"Error", MB_ICONERROR);
        return 0;
    }



    g_hWnd = CreateWindowW(L"EPUBLite", L"EPUB Lite Reader",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, 800, 600,
        nullptr, nullptr, h, nullptr);
    // 放在主窗口 CreateWindow 之后
    g_hStatus = CreateWindowEx(
        0, STATUSCLASSNAME, L"就绪",
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0,           // 位置和大小由 WM_SIZE 调整
        g_hWnd, nullptr, g_hInst, nullptr);

    g_hwndTV = CreateWindowExW(
        0, WC_TREEVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER |
        TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
        0, 0, 200, 600,
        g_hWnd, (HMENU)100, g_hInst, nullptr);
    g_hView = CreateWindowExW(
        0, L"EPUBView", nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_CLIPSIBLINGS,
        0, 0, 1, 1,
        g_hWnd, (HMENU)101, g_hInst, nullptr);
    g_bootstrap = std::make_unique<AppBootstrap>();
    SetMenu(g_hWnd, hMenu);            // ← 放在 CreateWindow 之后

    CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_CSS,
        MF_BYCOMMAND | (g_cfg.disableCSS ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_JS,
        MF_BYCOMMAND | (g_cfg.disableJS ? MF_CHECKED : MF_UNCHECKED));

    
    EnableClearType();
    ShowWindow(g_hWnd, n);
    UpdateWindow(g_hWnd);
    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    GdiplusShutdown(gdiplusToken);
    return static_cast<int>(msg.wParam);
}

// ---------- 目录解析 ----------
void EPUBBook::LoadToc()
{
    OutputDebugStringA(("toc size = " + std::to_string(ocf_pkg_.toc.size()) + "\n").c_str());
    TreeView_DeleteAllItems(g_hwndTV);

    auto tree = BuildTree(ocf_pkg_.toc);
    for (const auto& root : tree)
        InsertTreeNode(g_hwndTV, root, TVI_ROOT);
}

void EPUBBook::init_doc(std::string html) {
    /* 2. 加载 HTML */


    if (html.empty()) return;
    if (!g_cfg.disableCSS) { html = insert_global_css(html); }
    if (!g_cfg.disablePreprocessHTML) { html = PreprocessHTML(html); }
    g_doc.reset();
    g_container.reset();
    g_container = std::make_shared<SimpleContainer>(L".");
    // 完整兜底 UA 样式表（litehtml 专用）

    g_doc = litehtml::document::createFromString(html.c_str(), g_container.get());
    /* 关键：DOM 刚建好，立即回填内联脚本 */
    if (!g_cfg.disableJS) {
        g_bootstrap->bind_host_objects();

        g_bootstrap->run_pending_scripts(); // 立即执行
        save_document_html(g_doc);
    }


    g_scrollY = 0;
}
// ---------- 点击目录跳转 ----------
void EPUBBook::OnTreeSelChanged(const wchar_t* href)
{
    if (!href || !*href) return;
 


    /* 1. 分离文件路径与锚点 */
    std::wstring whref(href);
    size_t pos = whref.find(L'#');
    std::wstring file_path = (pos == std::wstring::npos) ? whref : whref.substr(0, pos);
    std::string  id = (pos == std::wstring::npos) ? "" :
        litehtml::wchar_to_utf8(whref.substr(pos + 1));

    if (g_last_html_path != file_path){
        std::string html = g_book.load_html(file_path.c_str());
        g_book.init_doc(html);
        g_states.needRelayout.store(true, std::memory_order_release);
        g_last_html_path = file_path;
        UpdateCache();
        SendMessage(g_hView, WM_EPUB_UPDATE_SCROLLBAR, 0, 0);
    }

    /* 3. 跳转到锚点 */
    if (!id.empty())
    {
        std::wstring cssSel = L"#" + a2w(id);   // 转成宽字符
        // WM_APP + 3 约定为“跳转到锚点选择器”
        PostMessageW(g_hView, WM_EPUB_ANCHOR,
            reinterpret_cast<WPARAM>(_wcsdup(cssSel.c_str())), 0);
    }
    UpdateWindow(g_hView);
    UpdateWindow(g_hWnd);
}

// SimpleContainer.cpp
void SimpleContainer::load_image(const char* src, const char* /*baseurl*/, bool)
{
    if (g_img_cache.contains(src)) return;

    std::wstring wpath = g_zipIndex.find(a2w(src));
    EPUBBook::MemFile mf = g_book.read_zip(wpath.c_str());
    if (mf.data.empty())
    {
        OutputDebugStringW((L"EPUB not found: " + wpath + L"\n").c_str());
        return;
    }

    auto dot = wpath.find_last_of(L'.');
    std::wstring ext;
    if (dot != std::wstring::npos && dot + 1 < wpath.size())
    {
        ext = wpath.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    }

    ImageFrame frame = decode_img(mf, ext.empty() ? nullptr : ext.c_str());
    if (!frame.rgba.empty())
    {
        g_img_cache.emplace(src, std::move(frame));
    }
    else
    {
        OutputDebugStringA(("EPUB decode failed: " + std::string(src) + "\n").c_str());
    }
}

int SimpleContainer::text_width(const char* text, litehtml::uint_ptr hFont) 
{
    return g_canvas->backend()->text_width(text, hFont);
}


void SimpleContainer::get_image_size(const char* src, const char* baseurl, litehtml::size& sz) {
    if (!g_img_cache.contains(src)) { sz.width = sz.height = 0; return; }
    auto img = g_img_cache[src];
    sz.width = img.width;
    sz.height = img.height;
}

void SimpleContainer::get_client_rect(litehtml::position& client) const {
    RECT rc{}; GetClientRect(g_hWnd, &rc);
    client = { 0, 0, rc.right, rc.bottom - 30 };
}

litehtml::element::ptr
SimpleContainer::create_element(const char* tag,
    const litehtml::string_map& attrs,
    const std::shared_ptr<litehtml::document>& doc)
{
    if (litehtml::t_strcasecmp(tag, "script") != 0)
        return nullptr;   // 让 litehtml 自己建别的节点
    if (g_cfg.disableJS) { return nullptr; }

    /* 1. 建节点（litehtml 会把内联文本自动收进来） */
    auto el = std::make_shared<litehtml::html_tag>(doc);
    el->set_tagName(tag);

    /* 2. 记录到待执行列表 */
    AppBootstrap::script_info si;
    si.el = el;
    g_bootstrap->m_pending_scripts.emplace_back(std::move(si));
    return el;
}



void SimpleContainer::import_css(litehtml::string& text,
    const litehtml::string& url,
    litehtml::string& baseurl)
{
    if (g_cfg.disableCSS) {
        text.clear();           // 禁用所有外部/内部 CSS
        return;
    }
    // url 可能是相对路径，baseurl 是当前 html 所在目录
    std::wstring w_path = g_zipIndex.find(a2w(url));
    EPUBBook::MemFile mf = g_book.read_zip(w_path.c_str());
    if (!mf.data.empty())
    {
        // 直接填到 text（litehtml 期望 UTF-8）
        text.assign(reinterpret_cast<const char*>(mf.data.data()),
            mf.data.size());
    }
    else
    {
        OutputDebugStringW((L"CSS not found: " + w_path + L"\n").c_str());
    }

    // baseurl 保持原样即可
}





// ---------- 2. 标题 ----------------------------------------------------
void SimpleContainer::set_caption(const char* cap)
{
    if (cap && g_hWnd) {
        SetWindowTextW(g_hWnd, a2w(cap).c_str());
        //OutputDebugStringW((a2w(cap)+L"\n").c_str());
    }
}

// ---------- 3. base url -------------------------------------------------
void SimpleContainer::set_base_url(const char* base)
{
    return ;
}

// ---------- 4. 链接注册 --------------------------------------------------
void SimpleContainer::link(const std::shared_ptr<litehtml::document>& doc,
    const litehtml::element::ptr& el)
{
    // 简单做法：把锚点 id -> 元素 存起来，点击时滚动
    const char* id = el->get_attr("id");
    if (id && *id)
        m_anchor_map[id] = el;
}

// ---------- 5. 点击锚点 -------------------------------------------------
void SimpleContainer::on_anchor_click(const char* url,
    const litehtml::element::ptr& el)
{
    if (!url || !*url) return;

    // 内部 #id
    if (url[0] == '#')
    {
        auto it = m_anchor_map.find(url + 1);
        if (it != m_anchor_map.end())
        {
            // 这里只是示例：把元素 y 坐标发出去，真正滚动自己实现
            int y = it->second->get_placement().y;
            SendMessage(g_hWnd, WM_VSCROLL, MAKEWPARAM(SB_THUMBPOSITION, y), 0);
        }
        return;
    }

    // 外部链接：交给宿主
    ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

// ---------- 6. 鼠标形状 -------------------------------------------------
void SimpleContainer::set_cursor(const char* cursor)
{
    LPCWSTR id = IDC_ARROW;
    if (cursor)
    {
        if (strcmp(cursor, "pointer") == 0) id = IDC_HAND;
        else if (strcmp(cursor, "text") == 0) id = IDC_IBEAM;
    }
    SetCursor(LoadCursor(nullptr, id));
}

// ---------- 7. 文本转换 ----------------------------------------------
void SimpleContainer::transform_text(litehtml::string& text,
    litehtml::text_transform tt)
{
    if (text.empty()) return;
    std::wstring w = a2w(text.c_str());
    switch (tt)
    {
    case litehtml::text_transform_capitalize:
        if (!w.empty()) w[0] = towupper(w[0]);
        for (size_t i = 1; i < w.size(); ++i)
            if (iswspace(w[i - 1])) w[i] = towupper(w[i]);
        break;
    case litehtml::text_transform_uppercase:
        CharUpperBuffW(w.data(), (DWORD)w.size());
        break;
    case litehtml::text_transform_lowercase:
        CharLowerBuffW(w.data(), (DWORD)w.size());
        break;
    default: break;
    }
    text = w2a(w);
}

// ---------- 8. 裁剪 ----------------------------------------------------
void SimpleContainer::set_clip(const litehtml::position& pos,
    const litehtml::border_radiuses& radius)
{
    // 取得当前绘制 HDC
    HDC hdc = reinterpret_cast<HDC>(m_last_hdc);
    if (!hdc) return;

    // 1. 如果四个角半径都为 0，退化为矩形
    if (radius.top_left_x == 0 && radius.top_right_x == 0 &&
        radius.bottom_left_x == 0 && radius.bottom_right_x == 0)
    {
        HRGN rgn = CreateRectRgn(pos.x, pos.y,
            pos.x + pos.width,
            pos.y + pos.height);
        SelectClipRgn(hdc, rgn);
        DeleteObject(rgn);
        return;
    }

    // 2. 否则用圆角矩形
    //    CreateRoundRectRgn 的圆角直径 = 2 * radius
    int rx = std::max({ radius.top_left_x, radius.top_right_x,
                       radius.bottom_left_x, radius.bottom_right_x });
    int ry = rx;   // 简化：保持 1:1 圆角；如需椭圆角可分别传 rx/ry
    HRGN rgn = CreateRoundRectRgn(pos.x, pos.y,
        pos.x + pos.width,
        pos.y + pos.height,
        rx * 2, ry * 2);
    SelectClipRgn(hdc, rgn);
    DeleteObject(rgn);
}
void SimpleContainer::del_clip()
{
    SelectClipRgn(reinterpret_cast<HDC>(m_last_hdc), nullptr);
}

// ---------- 9. 媒体查询 -----------------------------------------------
void SimpleContainer::get_media_features(litehtml::media_features& mf) const
{
    RECT rc; GetClientRect(g_hWnd, &rc);
    mf.width = rc.right - rc.left;
    mf.height = rc.bottom - rc.top;
    mf.device_width = GetSystemMetrics(SM_CXSCREEN);
    mf.device_height = GetSystemMetrics(SM_CYSCREEN);
    mf.color = 8;        // 24 位色
    mf.monochrome = 0;
    mf.type = litehtml::media_type_screen;
}

// ---------- 10. 语言 ---------------------------------------------------
void SimpleContainer::get_language(litehtml::string& language,
    litehtml::string& culture) const
{
    language = "en";
    culture = "US";
    // 真正 EPUB 可从 OPF <dc:language> 读
}

// ---------- 11. 列表标记 ----------------------------------------------
void SimpleContainer::draw_list_marker(litehtml::uint_ptr hdc,
    const litehtml::list_marker& marker)
{
    HDC dc = reinterpret_cast<HDC>(hdc);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));

    std::wstring txt;
    if (marker.marker_type == litehtml::list_style_type_disc)
        txt = L"•";
    else if (marker.marker_type == litehtml::list_style_type_decimal)
        txt = std::to_wstring(marker.index) + L".";

    HFONT hOld = (HFONT)SelectObject(dc, m_hDefaultFont);
    TextOutW(dc, marker.pos.x, marker.pos.y, txt.c_str(), (int)txt.size());
    SelectObject(dc, hOld);
}




// --------------------------------------------------
// 通用 HTML 预处理
// --------------------------------------------------
std::string PreprocessHTML(std::string html)
{
    //-------------------------------------------------
    // 1. Adobe Adept <meta name="..." value="..."/>
    //-------------------------------------------------
// 把 <title/> 或 <title /> 改成成对标签 <title></title>
    html = std::regex_replace(html,
        std::regex(R"(<title\b[^>]*?/\s*>)", std::regex::icase),
        "<title></title>");


    //-------------------------------------------------
    // 2. 自闭合 <script .../> → <script ...>code</script>
    //-------------------------------------------------
  // 2. 自闭合 <script .../> → <script ...>code</script>
    if (g_cfg.disableCSS) {
        // (?s) 不是 std::regex 的标准写法，这里用 [\s\S]* 代替“任意字符含换行”
        static const std::regex killAllScript(
            R"(<script\b[^>]*>(?:[^<]*(?:<(?!/script>)[^<]*)*)?</script>)",
            std::regex::icase | std::regex::optimize);
        html = std::regex_replace(html, killAllScript, "");
    }
    else
    {
        std::regex  scRe(R"(<script\b([^>]*)\bsrc\s*=\s*["']([^"']*)["']([^>]*)/\s*>)",
            std::regex::icase);
        std::string out;
        out.reserve(html.size());

        std::sregex_iterator it(html.begin(), html.end(), scRe);
        std::sregex_iterator end;
        size_t last = 0;

        for (; it != end; ++it)
        {
            const std::smatch& m = *it;

            // 2.1 读文件
            std::string src = m[2].str();
            std::wstring w_path = g_zipIndex.find(a2w(src));
            EPUBBook::MemFile mf = g_book.read_zip(w_path.c_str());
            std::string code;
            if (!mf.data.empty())
                code.assign(reinterpret_cast<const char*>(mf.data.data()),
                    mf.data.size());

            // 2.2 去掉 src 属性
            std::string attrs = m[1].str() + m[3].str();
            attrs = std::regex_replace(attrs,
                std::regex(R"(\s*\bsrc\s*=\s*["'][^"']*["'])", std::regex::icase), "");

            // 2.3 拼成对标签
            out.append(html, last, m.position() - last);
            out += "<script" + attrs + ">" + code + "</script>";
            last = m.position() + m.length();
        }
        out.append(html, last, std::string::npos);
        html.swap(out);
    }
    //-------------------------------------------------
    // 2. EPUB 3 的 <meta property="..." content="..."/>
    //     litehtml 只认识 name/content，不认识 property
    //-------------------------------------------------
    //html = std::regex_replace(html,
    //    std::regex(R"(<meta\b([^>]*)\bproperty\s*=\s*["']([^"']*)["']([^>]*)\bcontent\s*=\s*["']([^"']*)["']([^>]*)/?>)",
    //        std::regex::icase),
    //    "<meta $1name=\"$2\" content=\"$4\"$5>");

    //-------------------------------------------------
    // 3. 自闭合标签缺少空格导致解析错位
    //     例如 <br/> <hr/> <img .../> 写成 <br/ > <img.../>
    //-------------------------------------------------
    //html = std::regex_replace(html,
    //    std::regex(R"(<([a-zA-Z]+)(\s*[^>]*?)\s*/\s*>)"),
    //    "<$1$2 />");

    //-------------------------------------------------
    // 4. 删除 epub 专用命名空间属性
    //-------------------------------------------------
    //html = std::regex_replace(html,
    //    std::regex(R"(\s+xmlns(:\w+)?\s*=\s*["'][^"']*["'])"),
    //    "");
    //html = std::regex_replace(html,
    //    std::regex(R"(\s+\w+:\w+\s*=\s*["'][^"']*["'])"),
    //    "");

    //-------------------------------------------------
    // 5. 可选：压缩连续空白字符
    //-------------------------------------------------
    // html = std::regex_replace(html, std::regex(R"(\s+)"), " ");

    return html;
}





// 转发给后端

void SimpleContainer::draw_text(litehtml::uint_ptr hdc,
    const char* text,
    litehtml::uint_ptr hFont,
    litehtml::web_color color,
    const litehtml::position& pos) {
    g_canvas->backend()->draw_text(hdc, text, hFont, color, pos);
}

void SimpleContainer::draw_borders(litehtml::uint_ptr hdc,
    const litehtml::borders& borders,
    const litehtml::position& pos,
    bool root)
{
    g_canvas->backend()->draw_borders(hdc, borders, pos, root);
}

litehtml::uint_ptr SimpleContainer::create_font(const char* faceName,
    int size,
    int weight,
    litehtml::font_style italic,
    unsigned int decoration,
    litehtml::font_metrics* fm) {
    return g_canvas->backend()->create_font(faceName, size, weight, italic, decoration, fm);
}

void SimpleContainer::delete_font(litehtml::uint_ptr hFont)
{
    g_canvas->backend()->delete_font(hFont);
}
void SimpleContainer::draw_background(litehtml::uint_ptr hdc,
    const std::vector<litehtml::background_paint>& bg)
{
    g_canvas->backend()->draw_background(hdc, bg);
}

int SimpleContainer::pt_to_px(int pt) const {
    return MulDiv(pt, GetDeviceCaps(GetDC(nullptr), LOGPIXELSY), 72);
}




// DirectWrite backend
/* ---------- 构造 ---------- */
D2DCanvas::D2DCanvas(int w, int h, ComPtr<ID2D1RenderTarget> devCtx)
    : m_w(w), m_h(h), m_devCtx(devCtx){
   
    m_backend = std::make_unique<D2DBackend>(w, h, devCtx);

}
/* ---------- 把缓存位图贴到窗口 ---------- */
void D2DCanvas::present(HDC hdc, int x, int y)
{
    if (!m_devCtx) return;

    m_backend->m_rt->GetBitmap(&m_bmp);

    // 2. 开始绘制
    m_devCtx->BeginDraw();
    m_devCtx->Clear(D2D1::ColorF(D2D1::ColorF::White));
    m_devCtx->DrawBitmap(m_bmp.Get());
    m_devCtx->EndDraw();



}


// ---------- 辅助：UTF-8 ↔ UTF-16 ----------


// ---------- 字体缓存 ----------
struct FontPair {
    ComPtr<IDWriteTextFormat> format;
    ComPtr<IDWriteFontFace>   face;
    int size;
};

// ---------- 实现 ----------

D2DBackend::D2DBackend(int w, int h, ComPtr<ID2D1RenderTarget> devCtx)
    : m_w(w), m_h(h), m_devCtx(devCtx){
  
    // 2. 创建 DWrite 工厂
    HRESULT hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(m_dwrite.GetAddressOf()));
    if (FAILED(hr))
    {
        OutputDebugStringA("DWriteCreateFactory failed\n");
        __debugbreak();
    }

    ComPtr<ID2D1BitmapRenderTarget> bmpRT;
    hr = m_devCtx->CreateCompatibleRenderTarget(
        D2D1::SizeF(static_cast<float>(m_w), static_cast<float>(m_h)), // 逻辑尺寸
        &bmpRT);
     if (FAILED(hr))
        throw std::runtime_error("CreateCompatibleRenderTarget failed");

    m_rt = std::move(bmpRT);            // 保存到成员变量（可选）

    /* 提前拿到系统字体集合（只需一次即可） */
 
    m_dwrite->GetSystemFontCollection(&m_sysFontColl, FALSE);
}
void D2DBackend::draw_text(litehtml::uint_ptr hdc,
    const char* text,
    litehtml::uint_ptr hFont,
    litehtml::web_color color,
    const litehtml::position& pos)
{
    assert(rt && SUCCEEDED(rt->GetFactory(nullptr)));
    if (!text || !*text || !hFont) return;

    ComPtr<ID2D1BitmapRenderTarget> rt = m_rt;
    FontPair* fp = reinterpret_cast<FontPair*>(hFont);
    if (!rt) {
        OutputDebugStringA("rt == nullptr\n");
        return;
    }


    ComPtr<ID2D1SolidColorBrush> brush;
    rt->CreateSolidColorBrush(
        D2D1::ColorF(color.red / 255.0f,
            color.green / 255.0f,
            color.blue / 255.0f,
            color.alpha / 255.0f),
        &brush);
    // 3. 文本
    std::wstring wtxt = a2w(text);
    UINT32 textLen = static_cast<UINT32>(wtxt.size());
    if (textLen == 0) return;
    if (wtxt.empty()) return;
    // 4. 用足够大的布局宽度，避免文字被截断
    const float maxWidth = 8192.0f;   // 足够大
    const float maxHeight = 512.0f;



    ComPtr<IDWriteTextLayout> layout;
    m_dwrite->CreateTextLayout(
        wtxt.c_str(), textLen,
        fp->format.Get(),
        maxWidth,
        maxHeight,
        &layout);

    if (!layout) {
        OutputDebugStringA("layout == nullptr\n");
        return;
    }
    //// ====== 插入 baseline 对齐 ======
    //DWRITE_LINE_METRICS lineMetrics{};
    //UINT32 lineCount = 0;
    //layout->GetLineMetrics(&lineMetrics, 1, &lineCount);
    //float baseline = lineMetrics.baseline;   // 相对于 layout 原点的 baseline 距离

    // 4. 直接绘制：litehtml 已把 pos 转为相对于当前片段左上角
    rt->DrawTextLayout(D2D1::Point2F(static_cast<float>(pos.x),
        static_cast<float>(pos.y )),
        layout.Get(),
        brush.Get());


    //std::vector<DWRITE_CLUSTER_METRICS> cms;
    //UINT32 actual = 0;
    //layout->GetClusterMetrics(nullptr, 0, &actual);   // 先拿数量
    //if (actual) {
    //    cms.resize(actual);
    //    layout->GetClusterMetrics(cms.data(), actual, &actual);
    //}

    //// 打印
    //for (size_t i = 0; i < cms.size(); ++i) {
    //    OutputDebugStringW(std::format(L"[{}] width={:.3f}\n",
    //        i, cms[i].width).c_str());
    //}
    //DWRITE_TEXT_METRICS tm;
    //layout->GetMetrics(&tm);
    //for (wchar_t ch : wtxt) {
    //    OutputDebugStringW(std::format(L"U+{:04X} ", (unsigned)ch).c_str());
    //}
    //OutputDebugStringW(std::format(L"text=\"{}\", width={}\n", wtxt.data(), tm.width).c_str());


}



void D2DBackend::draw_borders(litehtml::uint_ptr hdc,
    const litehtml::borders& borders,
    const litehtml::position& draw_pos,
    bool root)
{
    ComPtr<ID2D1BitmapRenderTarget> rt = m_rt;
    ComPtr<ID2D1SolidColorBrush> brush;
    rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &brush);

    D2D1_RECT_F rc = D2D1::RectF(
        (float)draw_pos.left(), (float)draw_pos.top(),
        (float)draw_pos.right(), (float)draw_pos.bottom());

    if (borders.left.width > 0) {
        brush->SetColor(D2D1::ColorF(
            borders.left.color.red / 255.0f,
            borders.left.color.green / 255.0f,
            borders.left.color.blue / 255.0f,
            borders.left.color.alpha / 255.0f));
        rt->FillRectangle(
            D2D1::RectF(rc.left, rc.top,
                rc.left + borders.left.width, rc.bottom),
            brush.Get());
    }
    if (borders.right.width > 0) {
        brush->SetColor(D2D1::ColorF(
            borders.right.color.red / 255.0f,
            borders.right.color.green / 255.0f,
            borders.right.color.blue / 255.0f,
            borders.right.color.alpha / 255.0f));
        rt->FillRectangle(
            D2D1::RectF(rc.right - borders.right.width, rc.top,
                rc.right, rc.bottom),
            brush.Get());
    }
    if (borders.top.width > 0) {
        brush->SetColor(D2D1::ColorF(
            borders.top.color.red / 255.0f,
            borders.top.color.green / 255.0f,
            borders.top.color.blue / 255.0f,
            borders.top.color.alpha / 255.0f));
        rt->FillRectangle(
            D2D1::RectF(rc.left, rc.top,
                rc.right, rc.top + borders.top.width),
            brush.Get());
    }
    if (borders.bottom.width > 0) {
        brush->SetColor(D2D1::ColorF(
            borders.bottom.color.red / 255.0f,
            borders.bottom.color.green / 255.0f,
            borders.bottom.color.blue / 255.0f,
            borders.bottom.color.alpha / 255.0f));
        rt->FillRectangle(
            D2D1::RectF(rc.left, rc.bottom - borders.bottom.width,
                rc.right, rc.bottom),
            brush.Get());
    }

}

// 工具：转小写
std::wstring  D2DBackend::toLower(std::wstring s)
{
    std::transform(s.begin(), s.end(), s.begin(), towlower);
    return s;
}

// 1. 静态表映射
std::optional<std::wstring> D2DBackend::mapStatic(const std::wstring& key)
{
    auto it = g_fontAlias.find(key);
    return (it != g_fontAlias.end()) ? std::optional{ it->second } : std::nullopt;
}

// 2. 动态表映射
// 声明改为静态，并把系统字体集合作为参数
std::optional<std::wstring>
D2DBackend::mapDynamic(const std::wstring& key)
{
   
    auto it = g_fontAliasDynamic.find(key);
    if (it == g_fontAliasDynamic.end())
        return std::nullopt;

    for (const std::wstring& face : it->second)
    {
        UINT32 index = 0;
        BOOL   exists = FALSE;
        m_sysFontColl->FindFamilyName(face.c_str(), &index, &exists);
        if (exists)
            return face;
    }
    return std::nullopt;
}
// 3. 解析单个 token
std::wstring D2DBackend::resolveFace(const std::wstring& raw)
{
    const std::wstring key = toLower(raw);

    if (auto v = mapStatic(key))   return *v;
    if (auto v = mapDynamic(key))  return *v;
    return raw;                    // 原样返回
}

std::vector<std::wstring>
D2DBackend::split_font_list(const std::string& src) {
    std::vector<std::wstring> out;
    std::string token;
    for (size_t i = 0, n = src.size(); i < n; ++i)
    {
        if (src[i] == ',')
        {
            token = trim_any(token);
            if (!token.empty()) {
                std::wstring face = a2w(token);
                out.emplace_back(resolveFace(face));
                token.clear();
            }
        }
        else
        {
            token += src[i];
        }
    }
    token = trim_any(token);
    if (!token.empty())
    {
        std::wstring face = a2w(token);
        out.emplace_back(resolveFace(face));
    }
    return out;
};
litehtml::uint_ptr D2DBackend::create_font(const char* faceName,
    int size,
    int weight,
    litehtml::font_style italic,
    unsigned int decoration,
    litehtml::font_metrics* fm)
{
    if (!m_dwrite || !fm) return 0;

    /*----------------------------------------------------------
      1. 把 font-family 字符串拆成单个字体名
    ----------------------------------------------------------*/

    std::vector<std::wstring> faces = split_font_list(faceName ? faceName : "Segoe UI");

    /*----------------------------------------------------------
      2. 逐个尝试创建 IDWriteTextFormat
    ----------------------------------------------------------*/



    ComPtr<IDWriteTextFormat> fmt;

    for (const auto& f : faces)
    {
        UINT32 index = 0;
        BOOL   exists = FALSE;

        /* 1. 私有集合 */
        if (m_privateFonts)
        {
            m_privateFonts->FindFamilyName(f.c_str(), &index, &exists);
            if (exists &&
                SUCCEEDED(m_dwrite->CreateTextFormat(
                    f.c_str(), m_privateFonts.Get(),
                    static_cast<DWRITE_FONT_WEIGHT>(weight),
                    italic == litehtml::font_style_italic ? DWRITE_FONT_STYLE_ITALIC
                    : DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL,
                    static_cast<float>(size),
                    L"en-us",
                    &fmt)))
            {
                m_actualFamily = f;          // 保存命中的家族名
                //OutputDebugStringW((L"[private] 命中：" + m_actualFamily + L"\n").c_str());
                break;
            }
        }

        /* 2. 系统集合 */
        index = 0;
        exists = FALSE;
        m_sysFontColl->FindFamilyName(f.c_str(), &index, &exists);
        if (exists &&
            SUCCEEDED(m_dwrite->CreateTextFormat(
                f.c_str(), nullptr,
                static_cast<DWRITE_FONT_WEIGHT>(weight),
                italic == litehtml::font_style_italic ? DWRITE_FONT_STYLE_ITALIC
                : DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                static_cast<float>(size),
                L"en-us",
                &fmt)))
        {
            m_actualFamily = f;              // 保存命中的家族名
            //OutputDebugStringW((L"[system] 命中：" + m_actualFamily + L"\n").c_str());
            break;
        }
        OutputDebugStringW((L"[DWrite] 未找到字体：" + f + L"\n").c_str());
    }

    if (!fmt){
        OutputDebugStringW(L"[DWrite] 未找到字体:");
        OutputDebugStringW(a2w(faceName).c_str());
        OutputDebugStringW(L"  使用默认字体\n");
        m_dwrite->CreateTextFormat(
            L"Segoe UI", nullptr,
            static_cast<DWRITE_FONT_WEIGHT>(weight),
            italic == litehtml::font_style_italic ? DWRITE_FONT_STYLE_ITALIC
            : DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            static_cast<float>(size),
            L"en-us",
            &fmt); 
    }

    if (!fmt) {
        OutputDebugStringW(L"[DWrite] 加载默认字体失败\n");
        return 0; }
    /*----------------------------------------------------------
      用命中的字体家族名去拿真正的字体并计算度量
    ----------------------------------------------------------*/
 
    ComPtr<IDWriteFontFamily>     family;
    ComPtr<IDWriteFont>           dwFont;

    /* 根据命中的来源选择集合 */
    ComPtr<IDWriteFontCollection> targetCollection;
    if (!m_actualFamily.empty())
    {
        UINT32 index = 0;
        BOOL   exists = FALSE;
        if (m_privateFonts && m_privateFonts->FindFamilyName(m_actualFamily.c_str(), &index, &exists) && exists)
            targetCollection = m_privateFonts;
        else
            targetCollection = m_sysFontColl;

        targetCollection->FindFamilyName(m_actualFamily.c_str(), &index, &exists);
        if (exists)
        {
            targetCollection->GetFontFamily(index, &family);
            family->GetFirstMatchingFont(
                static_cast<DWRITE_FONT_WEIGHT>(weight),
                DWRITE_FONT_STRETCH_NORMAL,
                italic == litehtml::font_style_italic ? DWRITE_FONT_STYLE_ITALIC
                : DWRITE_FONT_STYLE_NORMAL,
                &dwFont);

            DWRITE_FONT_METRICS dwFm{};
            dwFont->GetMetrics(&dwFm);
            float dip = static_cast<float>(size) / dwFm.designUnitsPerEm;
            fm->ascent = static_cast<int>(dwFm.ascent * dip + 0.5f);
            fm->descent = static_cast<int>(dwFm.descent * dip + 0.5f);
            fm->height = static_cast<int>((dwFm.ascent + dwFm.descent + dwFm.lineGap) * dip + 0.5f);
            fm->x_height = static_cast<int>(dwFm.xHeight * dip + 0.5f);
        }
    }
    /*----------------------------------------------------------
      6. 返回句柄
    ----------------------------------------------------------*/
    FontPair* fp = new FontPair{ fmt, nullptr, size };

    return reinterpret_cast<litehtml::uint_ptr>(fp);
}

void D2DBackend::delete_font(litehtml::uint_ptr h)
{
    if (h) delete reinterpret_cast<FontPair*>(h);
}

void D2DBackend::draw_background(litehtml::uint_ptr hdc,
    const std::vector<litehtml::background_paint>& bg)
{
    assert(m_rt && "render target is null");
    if (bg.empty()) return;

    ComPtr<ID2D1BitmapRenderTarget> rt = m_rt;

    for (const auto& b : bg)
    {
        //--------------------------------------------------
        // 1. 纯色背景
        //--------------------------------------------------
        if (b.image.empty())
        {
            ComPtr<ID2D1SolidColorBrush> brush;
            rt->CreateSolidColorBrush(
                D2D1::ColorF(b.color.red / 255.0f,
                    b.color.green / 255.0f,
                    b.color.blue / 255.0f,
                    b.color.alpha / 255.0f),
                &brush);

            D2D1_RECT_F rc = D2D1::RectF(
                (float)b.border_box.left(), (float)b.border_box.top(),
                (float)b.border_box.right(), (float)b.border_box.bottom());

            rt->FillRectangle(rc, brush.Get());
            continue;
        }

        //--------------------------------------------------
        // 2. 图片背景
        //--------------------------------------------------
        auto it = g_img_cache.find(b.image);
        if (it == g_img_cache.end()) continue;   // 还没加载

        const ImageFrame& frame = it->second;
        if (frame.rgba.empty()) continue;

        // 如果已经缓存过 D2D 位图，直接拿；否则创建一次再缓存
        ComPtr<ID2D1Bitmap> bmp;
        auto d2d_it = m_d2dBitmapCache.find(b.image);
        if (d2d_it != m_d2dBitmapCache.end())
        {
            bmp = d2d_it->second;
        }
        else
        {
            D2D1_BITMAP_PROPERTIES bp =
                D2D1::BitmapProperties(
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                        D2D1_ALPHA_MODE_PREMULTIPLIED));

            HRESULT hr = rt->CreateBitmap(
                D2D1::SizeU(frame.width, frame.height),
                frame.rgba.data(),
                frame.stride,
                bp,
                &bmp);

            if (SUCCEEDED(hr))
                m_d2dBitmapCache.emplace(b.image, bmp);
        }

        if (!bmp) continue;

        // 简单拉伸到 border_box；需要平铺/居中的话再算源/目标矩形
        D2D1_RECT_F dst = D2D1::RectF(
            (float)b.border_box.left(), (float)b.border_box.top(),
            (float)b.border_box.right(), (float)b.border_box.bottom());

        rt->DrawBitmap(bmp.Get(), dst);
    }
}

int D2DBackend::pt_to_px(int pt) const
{
    return MulDiv(pt, 96, 72);   // 96 DPI
}
int D2DBackend::text_width(const char* text, litehtml::uint_ptr hFont)
{
    if (!text || !*text || !hFont) return 0;
    FontPair* fp = reinterpret_cast<FontPair*>(hFont);
    if (!fp || !fp->format) { OutputDebugStringA("fp->format is null\n"); return 0; }

    std::wstring wtxt = a2w(text);
    if (wtxt.empty()) return 0;
    UINT32 textLen = static_cast<UINT32>(wtxt.size());
    if (textLen == 0) return 0;
    ComPtr<IDWriteTextLayout> layout;
    const float maxWidth = 65536.0f;
    const float maxHeight = 0.0f;
    HRESULT hr = m_dwrite->CreateTextLayout(
        wtxt.data(), textLen,
        fp->format.Get(),
        maxWidth, maxHeight,
        &layout);
    if (FAILED(hr)) return 0;

    // 1. 关闭所有可能压缩空白的选项
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    DWRITE_TRIMMING trimming{ DWRITE_TRIMMING_GRANULARITY_NONE, 0, 0 };
    layout->SetTrimming(&trimming, nullptr);

    // 2. 用 cluster 宽度累加，避免“纯空白字符串”返回 0
    std::vector<DWRITE_CLUSTER_METRICS> cms;
    UINT32 count = 0;
    layout->GetClusterMetrics(nullptr, 0, &count);
    if (count == 0) return 0;
    cms.resize(count);
    layout->GetClusterMetrics(cms.data(), count, &count);

    float total = 0.0f;
    for (const auto& cm : cms)
        total += cm.width;

    return static_cast<int>(total + 0.5f);
}

// GDI backend
GdiCanvas::GdiCanvas(int w, int h) : m_w(w), m_h(h)
{
    // 创建内存 DC 与 32-bit 位图
    HDC screenDC = GetDC(nullptr);
    m_memDC = CreateCompatibleDC(screenDC);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;   // top-down DIB
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    m_bmp = CreateDIBSection(screenDC, &bi, DIB_RGB_COLORS, nullptr, nullptr, 0);
    m_old = (HBITMAP)SelectObject(m_memDC, m_bmp);
    ReleaseDC(nullptr, screenDC);

    // 创建后端
    m_backend = std::make_unique<GdiBackend>(m_memDC);
}

GdiCanvas::~GdiCanvas()
{
    if (m_old) SelectObject(m_memDC, m_old);
    if (m_bmp) DeleteObject(m_bmp);
    if (m_memDC) DeleteDC(m_memDC);
}

void GdiCanvas::present(HDC hdc, int x, int y)
{
    BitBlt(hdc, x, y, m_w, m_h, m_memDC, 0, 0, SRCCOPY);
}



struct GdiFont {
    HFONT hFont;
    TEXTMETRIC tm;
};

/* ---------- 工具：RGB 转 COLORREF ---------- */
static COLORREF to_cr(litehtml::web_color c)
{
    return RGB(c.red, c.green, c.blue);
}

/* ---------- 工具：UTF-8 → UTF-16 ---------- */


/* ---------- 7 个接口实现 ---------- */
void GdiBackend::draw_text(litehtml::uint_ptr hdc,
    const char* text,
    litehtml::uint_ptr hFont,
    litehtml::web_color color,
    const litehtml::position& pos)
{
    if (!text || !hFont) return;
    HDC dc = reinterpret_cast<HDC>(hdc);
    GdiFont* f = reinterpret_cast<GdiFont*>(hFont);

    HFONT old = (HFONT)SelectObject(dc, f->hFont);
    SetTextColor(dc, to_cr(color));
    SetBkMode(dc, TRANSPARENT);

    std::wstring w = a2w(text);
    ExtTextOutW(dc, pos.x, pos.y, ETO_CLIPPED, nullptr,
        w.c_str(), (UINT)w.size(), nullptr);

    SelectObject(dc, old);
}

void GdiBackend::draw_borders(litehtml::uint_ptr hdc,
    const litehtml::borders& borders,
    const litehtml::position& draw_pos,
    bool root)
{
    HDC dc = reinterpret_cast<HDC>(hdc);
    HPEN oldPen = (HPEN)SelectObject(dc, GetStockObject(DC_PEN));
    HBRUSH oldBrush = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));

    auto drawEdge = [&](int x, int y, int w, int h, litehtml::border br) {
        if (br.width <= 0) return;
        SetDCPenColor(dc, to_cr(br.color));
        RECT rc{ x, y, x + w, y + h };
        Rectangle(dc, rc.left, rc.top, rc.right, rc.bottom);
        };

    int l = draw_pos.left(), t = draw_pos.top();
    int r = draw_pos.right(), b = draw_pos.bottom();

    // 四条边
    drawEdge(l, t, borders.left.width, b - t, borders.left);
    drawEdge(r - borders.right.width, t, borders.right.width, b - t, borders.right);
    drawEdge(l, t, r - l, borders.top.width, borders.top);
    drawEdge(l, b - borders.bottom.width, r - l, borders.bottom.width, borders.bottom);

    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
}

litehtml::uint_ptr GdiBackend::create_font(const char* faceName,
    int size,
    int weight,
    litehtml::font_style italic,
    unsigned int decoration,
    litehtml::font_metrics* fm)
{
    std::wstring wface = a2w(faceName ? faceName : "Segoe UI");
    HFONT hFont = CreateFontW(
        -size, 0, 0, 0,
        weight,
        italic == litehtml::font_style_italic ? TRUE : FALSE,
        FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        wface.c_str());

    HDC dc = m_hdc;
    HFONT old = (HFONT)SelectObject(dc, hFont);
    GdiFont* f = new GdiFont{ hFont };
    GetTextMetrics(dc, &f->tm);
    SelectObject(dc, old);

    if (fm) {
        fm->ascent = f->tm.tmAscent;
        fm->descent = f->tm.tmDescent;
        fm->height = f->tm.tmHeight;
        fm->x_height = f->tm.tmHeight / 2; // 近似
    }
    return reinterpret_cast<litehtml::uint_ptr>(f);
}

void GdiBackend::delete_font(litehtml::uint_ptr h)
{
    if (h) {
        GdiFont* f = reinterpret_cast<GdiFont*>(h);
        DeleteObject(f->hFont);
        delete f;
    }
}


int GdiBackend::pt_to_px(int pt) const
{
    return MulDiv(pt, GetDeviceCaps(m_hdc, LOGPIXELSY), 72);
}

int GdiBackend::text_width(const char* text, litehtml::uint_ptr hFont)
{
    if (!hFont || !text) return 0;
    HFONT hF = reinterpret_cast<HFONT>(hFont);
    HDC hdc = GetDC(nullptr);
    HGDIOBJ old = SelectObject(hdc, hF);

    SIZE sz{};
    std::wstring wtxt = a2w(text);
    GetTextExtentPoint32W(hdc, wtxt.c_str(), static_cast<int>(wtxt.size()), &sz);

    SelectObject(hdc, old);
    ReleaseDC(nullptr, hdc);
    return sz.cx;
}
// freetype backend
struct FreetypeCtx {
    FT_Library lib = nullptr;
    FreetypeCtx() { FT_Init_FreeType(&lib); }
    ~FreetypeCtx() { if (lib) FT_Done_FreeType(lib); }
};

static FreetypeCtx g_ft;   // 全局单例

FreetypeCanvas::FreetypeCanvas(int w, int h, int dpi)
    : m_w(w), m_h(h), m_dpi(dpi)
{
    m_pixels.resize(static_cast<size_t>(w) * h * 4, 0); // RGBA
    m_backend = std::make_unique<FreetypeBackend>(
        w, h, dpi, m_pixels.data(), w * 4, g_ft.lib);
}

FreetypeCanvas::~FreetypeCanvas() = default;

void FreetypeCanvas::present(HDC hdc, int x, int y)
{
    // 把 RGBA 像素 BitBlt 到 HDC
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = m_w;
    bi.bmiHeader.biHeight = -m_h; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    StretchDIBits(hdc,
        x, y, m_w, m_h,
        0, 0, m_w, m_h,
        m_pixels.data(),
        &bi,
        DIB_RGB_COLORS,
        SRCCOPY);
}

struct FreetypeFont {
    FT_Face face = nullptr;
    int size = 0;
};



FreetypeBackend::FreetypeBackend(int w, int h, int dpi,
    uint8_t* surface, int stride,
    FT_Library lib)
    : m_w(w), m_h(h), m_dpi(dpi),
    m_surface(surface), m_stride(stride), m_lib(lib) {
}

/* ---------- 文本 ---------- */
void FreetypeBackend::draw_text(litehtml::uint_ptr,
    const char* text,
    litehtml::uint_ptr hFont,
    litehtml::web_color color,
    const litehtml::position& pos)
{
    if (!text || !*text || !hFont) return;
    auto* font = reinterpret_cast<FreetypeFont*>(hFont);

    std::wstring wtxt = a2w(text);
    FT_Set_Pixel_Sizes(font->face, 0, font->size);

    int x = pos.x;
    for (wchar_t ch : wtxt) {
        if (FT_Load_Char(font->face, ch, FT_LOAD_RENDER)) continue;
        FT_Bitmap& bmp = font->face->glyph->bitmap;
        int y = pos.y + font->face->glyph->bitmap_top;
        blit_glyph(x, y, bmp, color);
        x += font->face->glyph->advance.x >> 6;
    }
}



/* ---------- 边框 ---------- */
void FreetypeBackend::draw_borders(litehtml::uint_ptr,
    const litehtml::borders& borders,
    const litehtml::position& draw_pos,
    bool)
{
    auto drawEdge = [&](int x, int y, int w, int h, litehtml::border br) {
        if (br.width <= 0) return;
        fill_rect(litehtml::position(x, y, w, h), br.color);
        };
    int l = draw_pos.left(), t = draw_pos.top();
    int r = draw_pos.right(), b = draw_pos.bottom();
    drawEdge(l, t, borders.left.width, b - t, borders.left);
    drawEdge(r - borders.right.width, t, borders.right.width, b - t, borders.right);
    drawEdge(l, t, r - l, borders.top.width, borders.top);
    drawEdge(l, b - borders.bottom.width, r - l, borders.bottom.width, borders.bottom);
}

/* ---------- 字体 ---------- */
litehtml::uint_ptr FreetypeBackend::create_font(const char* faceName,
    int size,
    int weight,
    litehtml::font_style italic,
    unsigned int,
    litehtml::font_metrics* fm)
{
    std::string path = "C:/Windows/Fonts/"; // 可扩展
    path += faceName ? faceName : "segoeui.ttf";

    FT_Face face;
    if (FT_New_Face(m_lib, path.c_str(), 0, &face)) return 0;

    auto* font = new FreetypeFont{ face, size };

    if (fm) {
        FT_Set_Pixel_Sizes(face, 0, size);
        fm->ascent = face->size->metrics.ascender >> 6;
        fm->descent = -(face->size->metrics.descender >> 6);
        fm->height = (face->size->metrics.height) >> 6;
        fm->x_height = fm->height / 2; // 近似
    }
    return reinterpret_cast<litehtml::uint_ptr>(font);
}

void FreetypeBackend::delete_font(litehtml::uint_ptr h)
{
    if (h) {
        auto* f = reinterpret_cast<FreetypeFont*>(h);
        FT_Done_Face(f->face);
        delete f;
    }
}

int FreetypeBackend::pt_to_px(int pt) const
{
    return MulDiv(pt, m_dpi, 72);
}

/* ---------- 内部工具 ---------- */
void FreetypeBackend::fill_rect(const litehtml::position& rc,
    litehtml::web_color c)
{
    uint8_t* dst = m_surface + rc.y * m_stride + rc.x * 4;
    for (int y = 0; y < rc.height; ++y) {
        uint8_t* p = dst;
        for (int x = 0; x < rc.width; ++x) {
            p[0] = c.blue;
            p[1] = c.green;
            p[2] = c.red;
            p[3] = c.alpha;
            p += 4;
        }
        dst += m_stride;
    }
}

void FreetypeBackend::blit_glyph(int x, int y,
    const FT_Bitmap& bmp,
    litehtml::web_color c)
{
    for (int j = 0; j < static_cast<int>(bmp.rows); ++j) {
        for (int i = 0; i < static_cast<int>(bmp.width); ++i) {
            uint8_t a = bmp.buffer[j * bmp.pitch + i];
            if (!a) continue;
            int px = x + i;
            int py = y + j;
            if (px < 0 || py < 0 || px >= m_w || py >= m_h) continue;

            uint8_t* dst = m_surface + py * m_stride + px * 4;
            // 简单 alpha blend
            uint8_t inv = 255 - a;
            dst[0] = (dst[0] * inv + c.blue * a) / 255;
            dst[1] = (dst[1] * inv + c.green * a) / 255;
            dst[2] = (dst[2] * inv + c.red * a) / 255;
            dst[3] = 255;
        }
    }
}

int FreetypeBackend::text_width(const char* text, litehtml::uint_ptr hFont)
{
    if (!text || !*text || !hFont) return 0;

    FT_Face face = reinterpret_cast<FT_Face>(hFont);

    /* ---------- 1. UTF-8 → UTF-32 ---------- */
    std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> conv;
    std::u32string u32 = conv.from_bytes(text);

    int pen_x = 0;          // 26.6 固定小数
    for (char32_t ch : u32)
    {
        /* 2. 加载字形索引 */
        FT_UInt glyph_index = FT_Get_Char_Index(face, ch);
        if (!glyph_index) continue;   // 缺字

        /* 3. 加载字形（默认标志即可） */
        FT_Error err = FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT);
        if (err) continue;

        /* 4. 累加 advance */
        pen_x += face->glyph->advance.x;   // 26.6 格式
    }

    /* 5. 26.6 → 像素 */
    return (pen_x + 32) >> 6;   // 四舍五入
}

void EPUBBook::load_all_fonts() {
    g_canvas->backend()->load_all_fonts();
}

//void EPUBBook::load_all_fonts()
//{
//    for (const auto& item : g_book.ocf_pkg_.manifest)
//    {
//        const std::wstring& mime = item.media_type;
//        if (mime != L"application/x-font-ttf" &&
//            mime != L"application/font-sfnt" &&
//            mime != L"font/otf" &&
//            mime != L"font/ttf" &&
//            mime != L"font/woff" &&
//            mime != L"font/woff2" &&
//            mime != L"application/truetype" &&
//            mime != L"application/opentype")
//        {
//            continue;
//        }
//
//        std::wstring wpath = g_zipIndex.find(item.href);
//        EPUBBook::MemFile mf = g_book.read_zip(wpath.c_str());
//        if (mf.data.empty())
//        {
//            OutputDebugStringW((L"[Font] 字体文件为空: " + wpath + L"\n").c_str());
//            continue;
//        }
//
//        // 1. 取文件名（不含路径）
//        wchar_t fileName[MAX_PATH]{};
//        wcscpy_s(fileName, wpath.c_str());
//        PathStripPathW(fileName);   // ✅ 安全：C 风格数组
//
//        // 2. 拼临时完整路径
//        wchar_t tmpDir[MAX_PATH]{};
//        GetTempPathW(MAX_PATH, tmpDir);
//
//        wchar_t tmpFile[MAX_PATH]{};
//        PathCombineW(tmpFile, tmpDir, fileName);
//
//        // 3. 写临时文件
//        HANDLE hFile = CreateFileW(tmpFile,
//            GENERIC_WRITE,
//            0,
//            nullptr,
//            CREATE_ALWAYS,
//            FILE_ATTRIBUTE_NORMAL,
//            nullptr);
//        if (hFile == INVALID_HANDLE_VALUE)
//        {
//            OutputDebugStringW((L"[Font] 写临时文件失败: " + std::wstring(fileName) + L"\n").c_str());
//            continue;
//        }
//
//        DWORD written = 0;
//        WriteFile(hFile, mf.data.data(), (DWORD)mf.data.size(), &written, nullptr);
//        CloseHandle(hFile);
//
//        // 4. 注册到进程私有字体表
//        int ret = AddFontResourceExW(tmpFile, FR_PRIVATE, 0);
//        if (ret == 0)
//        {
//            OutputDebugStringW((L"[Font] 添加失败: " + std::wstring(tmpFile) + L"\n").c_str());
//            DeleteFileW(tmpFile);
//            continue;
//        }
//
//        OutputDebugStringW((L"[Font] 添加成功: " + std::wstring(tmpFile) + L"\n").c_str());
//
//        g_tempFontFiles.emplace_back(tmpFile);   // 记录路径，退出时删除
//    }
//}

/* ---------- 1. 静态工厂 ---------- */

void AppBootstrap::makeBackend(Renderer which, void* ctx)
{
    switch (which) {
    case Renderer::GDI:{
         std::make_unique<GdiBackend>(ctx ? static_cast<HDC>(ctx) : g_hMemDC);
    }
    case Renderer::D2D: {
        if (!ctx && !g_d2dRT) throw std::runtime_error("D2D render target not ready");
        RECT rc;
        GetClientRect(g_hView, &rc);
        int w = rc.right;
        int h = rc.bottom;
        g_canvas = std::make_unique<D2DCanvas>(w, h, ctx ? static_cast<ID2D1RenderTarget*>(ctx) : g_d2dRT.Get());
    }
    case Renderer::FreeType: {
        // 内部自己准备像素缓冲
        int w = 1024, h = 768, dpi = 96, stride = w * 4;
        std::vector<uint8_t> buf(h * stride);
        std::make_unique<FreetypeBackend>(w, h, dpi, buf.data(), stride, g_ftLib);
    }
    }
}

void AppBootstrap::initBackend(Renderer which) {
    //if (g_canvas) return;
    switch (which) {
    case Renderer::GDI: {
        /* 1) 基础设施 */
        g_hMemDC = CreateCompatibleDC(nullptr);          // GDI

    }
    case Renderer::D2D: {
        /* 1) D2D 工厂 */
        D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED,
            __uuidof(ID2D1Factory1),            // 接口 GUID
            nullptr,                          // 工厂选项（可 nullptr）
            reinterpret_cast<void**>(g_d2dFactory.GetAddressOf()));
        /* 2) DXGI 工厂 & 适配器 */

        ComPtr<IDXGIFactory> dxgiFactory;
        CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&dxgiFactory);
        ComPtr<IDXGIAdapter> adapter;
        dxgiFactory->EnumAdapters(0, &adapter);

        /* 3) D3D11 设备 */
        D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
        ComPtr<ID3D11Device> d3dDevice;
        D3D11CreateDevice(
            adapter.Get(),
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            levels, 1,
            D3D11_SDK_VERSION,
            &d3dDevice,
            nullptr,
            nullptr);

        RECT rc; GetClientRect(g_hView, &rc);
        ComPtr<ID2D1HwndRenderTarget> hwndRT;
        g_d2dFactory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(
                g_hView,
                D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top)),
            hwndRT.GetAddressOf());   // ← 关键：GetAddressOf()
        //ComPtr<ID2D1RenderTarget> g_d2dRT;
        hwndRT.As(&g_d2dRT);
    }
    case Renderer::FreeType: {
        FT_Init_FreeType(&g_ftLib);                      // FreeType

    }
    }
}



//------------------------------------------
// 公共辅助：从 EPUB 提取字体 blob
//------------------------------------------
static std::wstring make_safe_filename(std::wstring_view src, int index)
{
    std::wstring out{ src };
    const std::wstring illegal = L"<>:\"/\\|?*";
    for (wchar_t& c : out)
        if (illegal.find(c) != std::wstring::npos) c = L'_';

    // 去掉目录分隔符，只保留纯文件名
    size_t lastSlash = out.find_last_of(L"/\\");
    if (lastSlash != std::wstring::npos)
        out = out.substr(lastSlash + 1);

    // 加上序号，确保唯一
    return std::to_wstring(index) + L"_" + out;
}

static std::vector<std::pair<std::wstring, std::vector<uint8_t>>>
collect_epub_fonts()
{
    std::vector<std::pair<std::wstring, std::vector<uint8_t>>> fonts;
    int index = 0;   // 全局序号
    for (const auto& item : g_book.ocf_pkg_.manifest)
    {
        const std::wstring& mime = item.media_type;
        if (mime == L"application/x-font-ttf" ||
            mime == L"application/font-sfnt" ||
            mime == L"font/otf" ||
            mime == L"font/ttf" ||
            mime == L"font/woff" ||
            mime == L"font/woff2")
        {
            std::wstring wpath = g_zipIndex.find(item.href);
            EPUBBook::MemFile mf = g_book.read_zip(wpath.c_str());
            if (!mf.data.empty())
                fonts.emplace_back(make_safe_filename(wpath, index++), std::move(mf.data));
        }
    }
    return fonts;
}

//------------------------------------------
// 3.1  GDI 实现
//------------------------------------------
void GdiBackend::load_all_fonts()
{
    auto fonts = collect_epub_fonts();
    for (auto& [path, blob] : fonts)
    {
        // 1. 写临时文件
        wchar_t tmpPath[MAX_PATH]{};
        GetTempPathW(MAX_PATH, tmpPath);
        wchar_t fileName[MAX_PATH]{};
        wcscpy_s(fileName, path.c_str());
        PathStripPathW(fileName);
        PathCombineW(tmpPath, tmpPath, fileName);

        HANDLE h = CreateFileW(tmpPath, GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        DWORD written = 0;
        WriteFile(h, blob.data(), (DWORD)blob.size(), &written, nullptr);
        CloseHandle(h);

        // 2. 注册到进程
        if (AddFontResourceExW(tmpPath, FR_PRIVATE, 0))
        {
            g_tempFontFiles.emplace_back(tmpPath);   // 退出时 RemoveFontResourceEx + DeleteFile
            OutputDebugStringW((L"[GDI] 已加载字体: " + std::wstring(tmpPath) + L"\n").c_str());
        }
        else
        {
            DeleteFileW(tmpPath);
        }
    }
    SendMessage(HWND_BROADCAST, WM_FONTCHANGE, 0, 0);   // 通知 GDI
}

//------------------------------------------
// 3.2  DirectWrite 实现
//------------------------------------------
void D2DBackend::load_all_fonts()
{
    auto fonts = collect_epub_fonts();
    if (fonts.empty()) return;

    // 先清理旧字体
    unload_fonts();
  
    if (SUCCEEDED(CreateCompatibleFontCollection(
        m_dwrite.Get(), fonts, &m_privateFonts, m_tempFontFiles)))
    {
        OutputDebugStringW(L"[DWrite] 字体已加载（兼容模式)\n");
        // 打印已加载的全部字体名
        UINT32 familyCount = 0;
        familyCount = m_privateFonts->GetFontFamilyCount();
       
        OutputDebugStringW(std::format(L"[DWrite] 总数： {}\n", familyCount).c_str());
        for (UINT32 i = 0; i < familyCount; ++i)
        {
            Microsoft::WRL::ComPtr<IDWriteFontFamily> family;
            if (SUCCEEDED(m_privateFonts->GetFontFamily(i, &family)))
            {
                Microsoft::WRL::ComPtr<IDWriteLocalizedStrings> names;
                if (SUCCEEDED(family->GetFamilyNames(&names)))
                {
                    UINT32 idx = 0;
                    BOOL exists = FALSE;
                    names->FindLocaleName(L"en-us", &idx, &exists);
                    if (!exists) idx = 0;   // 回退到第一个

                    UINT32 len = 0;
                    names->GetStringLength(idx, &len);
                    std::wstring name(len + 1, L'\0');
                    names->GetString(idx, name.data(), len + 1);

                    OutputDebugStringW(std::format(L"[DWrite] 加载字体: {} " ,name).c_str());
                    OutputDebugStringW(L"\n");

                    
                }
            }
        }
    }
}

void D2DBackend::unload_fonts()
{
    if (m_privateFonts) m_privateFonts.Reset();
    // Win7/8 需要删除临时文件
    for (const auto& p : m_tempFontFiles)
    {
        m_dwrite->UnregisterFontCollectionLoader(
            static_cast<IDWriteFontCollectionLoader*>(nullptr));
        DeleteFileW(p.c_str());
    }
    m_tempFontFiles.clear();
}

//------------------------------------------
// 3.3  FreeType 实现
//------------------------------------------



void FreetypeBackend::load_all_fonts()
{
    // 1. 收集字体（文件名 → 二进制数据）
    auto fonts = collect_epub_fonts();   // 返回 vector<pair<path, blob>>
    if (fonts.empty()) return;

    // 2. 清理旧字体
    for (FT_Face f : m_faces)
        FT_Done_Face(f);
    m_faces.clear();
    m_fontBlobs.clear();

    // 3. 加载新字体
    for (auto& [path, blob] : fonts)
    {
        // 必须保持 blob 生命周期，FreeType 不会复制
        m_fontBlobs.emplace_back(std::move(blob));

        FT_Face face = nullptr;
        FT_Error err = FT_New_Memory_Face(
            m_lib,                       // 构造函数里传进来的 FT_Library
            m_fontBlobs.back().data(),   // 数据首地址
            (FT_Long)m_fontBlobs.back().size(),
            0,                           // face_index
            &face);

        if (err == 0)
        {
            m_faces.emplace_back(face);
            OutputDebugStringW((L"[FreeType] loaded " + path + L"\n").c_str());
        }
        else
        {
            OutputDebugStringW((L"[FreeType] failed to load " + path + L"\n").c_str());
        }
    }
}



// -------------------------------------------------
// 静态工厂
// -------------------------------------------------
HRESULT MemoryFontLoader::CreateCollection(
    IDWriteFactory* dwrite,
    const std::vector<std::pair<std::wstring, std::vector<uint8_t>>>& fonts,
    IDWriteFontCollection** out)
{
    if (!dwrite || !out) return E_INVALIDARG;

    // 1. 注册 loader（只注册一次）
    static bool registered = false;
    if (!registered)
    {
        Microsoft::WRL::ComPtr<MemoryFontLoader> stub(new MemoryFontLoader(nullptr, {}));
        HRESULT hr = dwrite->RegisterFontCollectionLoader(stub.Get());
        if (FAILED(hr)) return hr;
        registered = true;
    }

    // 2. 把 vector<blob> 打包成一块连续内存
    std::vector<std::vector<uint8_t>> blobs;
    blobs.reserve(fonts.size());
    for (const auto& [name, data] : fonts)
        blobs.emplace_back(data);

    // 3. 创建自定义集合
    return dwrite->CreateCustomFontCollection(
        static_cast<IDWriteFontCollectionLoader*>(nullptr),   // 用 key 区分
        blobs.data(),
        static_cast<UINT32>(blobs.size() * sizeof(blobs[0])),
        out);
}

// -------------------------------------------------
// IUnknown
// -------------------------------------------------
HRESULT MemoryFontLoader::QueryInterface(REFIID riid, void** ppv)
{
    if (riid == __uuidof(IUnknown) ||
        riid == __uuidof(IDWriteFontCollectionLoader))
    {
        *ppv = static_cast<IDWriteFontCollectionLoader*>(this);
    }
    else if (riid == __uuidof(IDWriteFontFileEnumerator))
    {
        *ppv = static_cast<IDWriteFontFileEnumerator*>(this);
    }
    else
    {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    return S_OK;
}

// -------------------------------------------------
// IDWriteFontCollectionLoader
// -------------------------------------------------
HRESULT MemoryFontLoader::CreateEnumeratorFromKey(
    IDWriteFactory* factory,
    const void* collectionKey, UINT32 collectionKeySize,
    IDWriteFontFileEnumerator** enumerator)
{
    if (!factory || !enumerator) return E_INVALIDARG;

    // collectionKey 指向 vector<vector<uint8_t>>
    const auto* blobs = reinterpret_cast<const std::vector<uint8_t>*>(collectionKey);
    size_t count = collectionKeySize / sizeof(std::vector<uint8_t>);
    if (!blobs || count == 0) return E_INVALIDARG;

    Microsoft::WRL::ComPtr<MemoryFontLoader> loader(
        new MemoryFontLoader(factory, std::vector<std::vector<uint8_t>>(blobs, blobs + count)));
    *enumerator = loader.Detach();
    return S_OK;
}

// -------------------------------------------------
// IDWriteFontFileEnumerator
// -------------------------------------------------
HRESULT MemoryFontLoader::MoveNext(BOOL* hasCurrentFile)
{
    if (!hasCurrentFile) return E_INVALIDARG;
    *hasCurrentFile = FALSE;

    if (idx_ < blobs_.size())
    {
        HRESULT hr = CreateInMemoryFontFile(factory_.Get(),
            blobs_[idx_].data(),
            static_cast<UINT32>(blobs_[idx_].size()),
            &current_);
        *hasCurrentFile = SUCCEEDED(hr);
        ++idx_;
        return hr;
    }
    return S_OK;
}

HRESULT MemoryFontLoader::GetCurrentFontFile(IDWriteFontFile** fontFile)
{
    if (!fontFile) return E_INVALIDARG;
    *fontFile = current_.Get();
    if (*fontFile) (*fontFile)->AddRef();
    return S_OK;
}




HRESULT MemoryFontLoader::CreateInMemoryFontFile(
    IDWriteFactory* factory,
    const void* data,
    UINT32 size,
    IDWriteFontFile** out)
{
    using Microsoft::WRL::MakeAndInitialize;

    Microsoft::WRL::ComPtr<InMemoryFontFileLoader> loader;
    HRESULT hr = MakeAndInitialize<InMemoryFontFileLoader>(&loader);
    if (FAILED(hr)) return hr;

    hr = factory->RegisterFontFileLoader(loader.Get());
    if (FAILED(hr) && hr != DWRITE_E_ALREADYREGISTERED) return hr;

    return loader->CreateInMemoryFontFileReference(
        factory,
        data,
        size,
        nullptr,
        out);
}





AppBootstrap::AppBootstrap() {
    initBackend(g_cfg.fontRenderer);
    makeBackend(g_cfg.fontRenderer, nullptr);
    if (!g_cfg.disableJS) { enableJS(); }
}

AppBootstrap::~AppBootstrap() {

}

// GdiCanvas
litehtml::uint_ptr GdiCanvas::getContext()
{
    return reinterpret_cast<litehtml::uint_ptr>(m_memDC);
}

void GdiCanvas::BeginDraw() { /* GDI 无需配对调用，留空 */ }
void GdiCanvas::EndDraw() { /* 留空或在此处 BitBlt 到窗口 DC */ }

litehtml::uint_ptr D2DCanvas::getContext() { return reinterpret_cast<litehtml::uint_ptr>(m_backend->m_rt.Get());}
void D2DCanvas::BeginDraw() { 
    m_backend->m_rt->BeginDraw(); 
    m_backend->m_rt->Clear(D2D1::ColorF(D2D1::ColorF::White));   // 先排除红色干扰
}
void D2DCanvas::EndDraw() { m_backend->m_rt->EndDraw(); }

// FreetypeCanvas
litehtml::uint_ptr FreetypeCanvas::getContext()
{
    return reinterpret_cast<litehtml::uint_ptr>(m_backend.get());
}

void FreetypeCanvas::BeginDraw() { /* 留空或清屏 */ }
void FreetypeCanvas::EndDraw() { /* 留空或刷新显示 */ }


void GdiCanvas::resize(int width, int height){
    m_w = width;
    m_h = height;
}

void D2DCanvas::resize(int width, int height)
{
    if (width <= 0 || height <= 0) return;

    m_w = width;
    m_h = height;
    m_backend->resize(width, height);
}

void FreetypeCanvas::resize(int width, int height) {
    m_w = width;
    m_h = height;
}



HBITMAP GdiBackend::create_dib_from_frame(const ImageFrame& frame)
{
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = static_cast<LONG>(frame.width);
    bmi.bmiHeader.biHeight = -static_cast<LONG>(frame.height); // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hBmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hBmp) return nullptr;

    // 把 RGBA → BGRA（GDI 需要 B,G,R,A）
    const uint8_t* src = frame.rgba.data();
    uint8_t* dst = static_cast<uint8_t*>(bits);
    size_t len = frame.width * frame.height;
    for (size_t i = 0; i < len; ++i)
    {
        dst[i * 4 + 0] = src[i * 4 + 2]; // B
        dst[i * 4 + 1] = src[i * 4 + 1]; // G
        dst[i * 4 + 2] = src[i * 4 + 0]; // R
        dst[i * 4 + 3] = src[i * 4 + 3]; // A
    }
    return hBmp;
}

void GdiBackend::draw_background(litehtml::uint_ptr hdc,
    const std::vector<litehtml::background_paint>& bg)
{
    if (bg.empty()) return;
    HDC dc = reinterpret_cast<HDC>(hdc);

    for (const auto& b : bg)
    {
        RECT rc{ b.border_box.left(), b.border_box.top(),
                 b.border_box.right(), b.border_box.bottom() };

        //--------------------------------------------------
        // 1. 纯色背景
        //--------------------------------------------------
        if (b.image.empty())
        {
            HBRUSH br = CreateSolidBrush(to_cr(b.color));
            FillRect(dc, &rc, br);
            DeleteObject(br);
            continue;
        }

        //--------------------------------------------------
        // 2. 图片背景
        //--------------------------------------------------
        auto it = g_img_cache.find(b.image);
        if (it == g_img_cache.end()) continue;   // 未加载

        const ImageFrame& frame = it->second;
        if (frame.rgba.empty()) continue;

        // 先看缓存
        HBITMAP& hBmp = m_gdiBitmapCache[b.image];
        if (!hBmp)
            hBmp = create_dib_from_frame(frame);
        if (!hBmp) continue;

        // 创建内存 DC
        HDC memDC = CreateCompatibleDC(dc);
        HGDIOBJ oldBmp = SelectObject(memDC, hBmp);

        // 拉伸到目标矩形
        int srcW = static_cast<int>(frame.width);
        int srcH = static_cast<int>(frame.height);
        SetStretchBltMode(dc, HALFTONE);
        StretchBlt(dc,
            rc.left, rc.top,
            rc.right - rc.left,
            rc.bottom - rc.top,
            memDC,
            0, 0, srcW, srcH,
            SRCCOPY);

        SelectObject(memDC, oldBmp);
        DeleteDC(memDC);
    }
}


void FreetypeBackend::draw_image(const ImageFrame& frame,
    const litehtml::position& dst)
{
    // 简单拉伸：逐像素贴图
    // 这里演示把 RGBA 直接写入后端像素缓冲区
    // 假设后端有：void put_pixel(int x,int y,uint32_t color);

    const uint8_t* src = frame.rgba.data();
    int srcW = static_cast<int>(frame.width);
    int srcH = static_cast<int>(frame.height);

    for (int y = 0; y < dst.height; ++y)
    {
        int sy = y * srcH / dst.height;
        for (int x = 0; x < dst.width; ++x)
        {
            int sx = x * srcW / dst.width;
            const uint8_t* p = src + (sy * srcW + sx) * 4;
            uint32_t rgba = (p[3] << 24) | (p[2] << 16) | (p[1] << 8) | p[0];
            // put_pixel(dst.left + x, dst.top + y, rgba);
        }
    }
}

void FreetypeBackend::draw_background(litehtml::uint_ptr,
    const std::vector<litehtml::background_paint>& bg)
{
    for (const auto& b : bg)
    {
        //--------------------------------------------------
        // 1. 纯色背景
        //--------------------------------------------------
        if (b.image.empty())
        {
            fill_rect(b.border_box, b.color);
            continue;
        }

        //--------------------------------------------------
        // 2. 图片背景
        //--------------------------------------------------
        auto it = g_img_cache.find(b.image);
        if (it == g_img_cache.end()) continue;   // 未加载

        const ImageFrame& frame = it->second;
        if (frame.rgba.empty()) continue;

        draw_image(frame, b.border_box);
    }
}

FreetypeBackend::~FreetypeBackend()
{
    for (auto& [_, bmp] : m_ftBitmapCache)
    {
        // 手动释放 buffer（仅当 buffer 是我们自己 malloc 的）
        if (bmp.buffer)
        {
            free(bmp.buffer);   // 对应 malloc
            bmp.buffer = nullptr;
        }
    }
    m_ftBitmapCache.clear();
}

void GdiBackend::resize(int width, int height) {

}
void D2DBackend::resize(int width, int height) {
    m_w = width;
    m_h = height;
    m_rt.Reset();
    ComPtr<ID2D1BitmapRenderTarget> bmpRT;
    HRESULT hr = m_devCtx->CreateCompatibleRenderTarget(
        D2D1::SizeF(static_cast<float>(m_w), static_cast<float>(m_h)), // 逻辑尺寸
        &bmpRT);
    if (FAILED(hr))
        throw std::runtime_error("CreateCompatibleRenderTarget failed");

    m_rt = std::move(bmpRT);            // 保存到成员变量（可选）

}
void FreetypeBackend::resize(int width, int height) {

}



void AppBootstrap::enableJS()
{
    if (!m_jsrt) m_jsrt = std::make_unique<js_runtime>(g_doc.get());
    if (!m_jsrt->switch_engine("duktape"))
        OutputDebugStringA("[Duktape] Duktape init failed\n");
    else {
        OutputDebugStringA("[Duktape] Duktape init OK\n");
        m_jsrt->set_logger(OutputDebugStringA);
        m_jsrt->eval("console.log('hello from duktape\n');");
    }
}

void AppBootstrap::disableJS()
{
    m_jsrt.reset();   // 直接销毁即可，js_runtime 会负责 shutdown
}

void AppBootstrap::run_pending_scripts()
{
    if (!m_jsrt) return;          // 没有 JS 引擎就跳过
    for (const auto& script : m_pending_scripts)
    {
        litehtml::string code;
        script.el->get_text(code);  // 取出 <script> 里的纯文本
        if (!code.empty())
            m_jsrt->eval(code, "<script>");  // 交给 QuickJS / Duktape / V8
    }
    m_pending_scripts.clear();    // 执行完清空
}

void AppBootstrap::bind_host_objects()
{
    if (!m_jsrt) return;
    m_jsrt->bind_document(g_doc.get());   // js_runtime 内部会转发到当前引擎
}





/* ---------- 实现 ---------- */
RenderWorker& RenderWorker::instance() { static RenderWorker w; return w; }

void RenderWorker::push(int w, int h, int sy)
{
    {
        std::lock_guard lg(m_);
        latest_.emplace(Task{ w,h,sy });
    }
    cv_.notify_one();
}

void RenderWorker::loop()
{
    for (;;)
    {
        Task task;
        {
            std::unique_lock ul(m_);
            cv_.wait(ul, [this] { return stop_ || latest_.has_value(); });
            if (stop_) break;
            task = *latest_;
            latest_.reset();
        }

 
     
        g_pg.load(g_doc.get(), task.w, task.h);

        g_states.isCaching.store(true);
        g_pg.render(g_canvas.get(), task.sy);

        g_states.isCaching.store(false);
        PostMessage(g_hView, WM_EPUB_CACHE_UPDATED, 0, 0);
    }
}

void RenderWorker::stop()
{
    { std::lock_guard lg(m_); stop_ = true; }
    cv_.notify_one();
    if (worker_.joinable()) worker_.join();
}

RenderWorker::~RenderWorker() { stop(); }


//void UpdateCache()
//{
//    if (!g_doc || !g_canvas) return;
//    RECT rc; GetClientRect(g_hView, &rc);
//    int w = rc.right, h = rc.bottom;
//    if (w <= 0 || h <= 0) return;
//
//    RenderWorker::instance().push(w, h, g_scrollY);
//}