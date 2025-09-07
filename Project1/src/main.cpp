#include "main.h"

HWND  g_hwndToc = nullptr;    // 侧边栏 TreeView
HIMAGELIST g_hImg = nullptr;   // 图标(可选)
HWND      g_hWnd;
HWND g_hStatus = nullptr;   // 状态栏句柄
HWND g_hView = nullptr;
HWND g_hTooltip = nullptr;
HWND g_hImageview = nullptr;
HWND g_hViewScroll = nullptr;
// ---------- 全局 ----------
HINSTANCE g_hInst;
std::shared_ptr<SimpleContainer> g_container;
std::unique_ptr<D2DCanvas> g_canvas;
std::unique_ptr<D2DCanvas> g_tooltip_canvas;
std::unique_ptr<D2DCanvas> g_imageview_canvas;

std::shared_ptr<EPUBBook>  g_book;

Gdiplus::Image* g_pSplashImg = nullptr;
std::future<void> g_parse_task;
enum class StatusBar { INFO = 0, FONT = 1 };
std::unique_ptr<VirtualDoc> g_vd;
static int g_scrollY = 0;   // 当前像素偏移
static int g_offsetY = 0;
static int g_maxScroll = 0;   // 总高度 - 客户区高度
static int g_line_height = 1;
std::wstring g_currentHtmlDir = L"";
std::wstring g_currentHtmlPath = L"";
constexpr UINT WM_EPUB_PARSED = WM_APP + 1;
constexpr UINT WM_EPUB_UPDATE_SCROLLBAR = WM_APP + 2;
constexpr UINT WM_EPUB_CSS_RELOAD = WM_APP + 3;
constexpr UINT WM_EPUB_CACHE_UPDATED = WM_APP + 4;
constexpr UINT WM_EPUB_ANCHOR = WM_APP + 5;
constexpr UINT WM_EPUB_TOOLTIP = WM_APP + 6;
constexpr UINT WM_EPUB_NAVIGATE = WM_APP + 7;

constexpr UINT TB_SETBUTTONTEXT(WM_USER + 8);
constexpr UINT WM_LOAD_ERROR(WM_USER + 9);
constexpr UINT WM_USER_SCROLL(WM_USER + 10);
constexpr UINT SBM_SETSPINECOUNT(WM_USER + 11);
constexpr UINT SBM_SETPOSITION(WM_USER + 12);
// 可随时改


AppStates g_states;
AppSettings g_cfg;
std::wstring g_last_html_path;
enum class ImgFmt { PNG, JPEG, BMP, GIF, TIFF, SVG, UNKNOWN };
static std::vector<std::wstring> g_tempFontFiles;
std::string PreprocessHTML(std::string html);
void UpdateCache(void);
static  std::string g_globalCSS = "";
static fs::file_time_type g_lastTime;
std::set<std::wstring> g_activeFonts;

std::unique_ptr<AppBootstrap> g_bootstrap;
std::unique_ptr<ReadingRecorder> g_recorder;

static MMRESULT g_tickTimer = 0;   // 0 表示当前没有定时器
static MMRESULT g_flushTimer = 0;
static MMRESULT g_tooltipTimer = 0;

int g_center_offset = 0;

std::string g_tootip_css = "<style>img{display:block;width:100%;height:auto;max-height:300px;}</style>";
static HWND  g_hwndSplit = nullptr;   // 分隔条
static int   g_splitX = 200;       // 当前 TOC 宽度（初始值）
static bool  g_dragging = false;     // 是否正在拖动
static bool  g_imageview_dragging = false;
static POINT g_imageview_drag_pos{ 0,0 };
static bool g_mouse_tracked = false;

static int g_tooltipRenderW = 0;   // 当前 tooltip 渲染宽度（客户区）
// 全局
std::unique_ptr<TocPanel> g_toc;
std::unique_ptr<ScrollBarEx> g_scrollbar;
// 1. 在全局或合适位置声明
    // 整篇文档的所有行
std::vector<LineBoxes> g_lines;
std::wstring           g_plainText;       // 整篇纯文本

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

void DumpHex(const wchar_t* tag, const std::wstring& s)
{
    std::wostringstream oss;
    oss << tag << L"(" << s.size() << L"): ";
    for (wchar_t ch : s)
        oss << std::hex << std::setw(4) << std::setfill(L'0') << static_cast<unsigned>(ch) << L" ";
    oss << L"\n";
    OutputDebugStringW(oss.str().c_str());
}

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
static bool ends_with(const std::string& str, const std::string& suffix)
{
    return str.size() >= suffix.size() &&
        str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static bool is_image_url(const char* url)
{
    if (!url) return false;
    std::string u = url;
    return ends_with(u, ".jpg") ||
        ends_with(u, ".png") ||
        ends_with(u, ".jpeg") ||
        ends_with(u, ".gif");
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

void PrintFontFamilies(ComPtr<IDWriteFontCollection> fontCollection)
{
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    UINT32 familyCount = fontCollection->GetFontFamilyCount();
    char buf[512];

    for (UINT32 i = 0; i < familyCount; ++i)
    {
        Microsoft::WRL::ComPtr<IDWriteFontFamily> family;
        fontCollection->GetFontFamily(i, &family);

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
        DumpHex(L"[actual]   ", wname);
        snprintf(buf, sizeof(buf), "[Font] %s\n", name.c_str());
        OutputDebugStringA(buf);
    }

    ::CoUninitialize();
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
static std::string inject_css(std::string html)
{
    std::ostringstream style;
    style << "<style>\n"
        << ":root{font-size:" << g_cfg.font_size << "px;}\n"
        << ":root,body,p,li,div,h1,h2,h3,h4,h5,h6,span, ul{line-height:" << g_cfg.line_height << ";}\n"
        << "</style>\n";

    const std::string& block = style.str();
    size_t pos = html.find("</head>");
    if (pos != std::string::npos)
        html.insert(pos, block);
    else
        html.insert(0, "<head>" + block + "</head>");

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

// 3. 写文字
inline void SetStatus(int pane, const wchar_t* msg)
{
    SendMessage(g_hStatus, SB_SETTEXT,
        pane | SBT_NOBORDERS,   // 高16位=栏索引
        (LPARAM)msg);
}


MemFile EPUBBook::read_zip(const wchar_t* file_name) const {
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
    g_currentHtmlPath = path;
    auto it = cache.find(path);
    if (it != cache.end()) return std::string(it->second.begin(), it->second.size());
    MemFile mf = read_zip(path.c_str());
    if (mf.data.empty()) return {};
    return std::string(mf.begin(), mf.size());
}

bool EPUBBook::load(const wchar_t* epub_path) {
    namespace fs = std::filesystem;
    if (!fs::exists(epub_path))
        throw std::runtime_error("文件不存在");
    m_file_path = epub_path;
    mz_zip_reader_end(&zip);           // 1. 先关闭旧 zip
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_file(&zip, w2a(epub_path).c_str(), 0))
        throw std::runtime_error("zip 打开失败：" +
            std::to_string(mz_zip_get_last_error(&zip)));
    m_zipIndex = ZipIndexW(zip);
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
            item.href = m_zipIndex.find(item.href);

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


static ImageFrame decode_img(const MemFile& mf, const wchar_t* ext)
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










void convert_coordinate(POINT& pt)
{
    pt.x = pt.x - g_center_offset;
    pt.y = pt.y + g_offsetY;
}



void CALLBACK Tick(UINT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR)
{
    // 直接在工作线程/回调里刷新
    if (g_recorder && !g_vd->m_blocks.empty()) { g_recorder->updateRecord(); }
    OutputDebugStringA("定时器触发\n");
    g_tickTimer = 0;
}
LRESULT CALLBACK ViewWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_SIZE:
    {
        if ( g_canvas)
        {
            RECT rcClient;
            GetClientRect(hwnd, &rcClient);   // ← 这才是客户区
            g_canvas->resize(rcClient.right, rcClient.bottom);
            UpdateCache();
        }
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        if (!g_canvas || !g_canvas->m_doc) { break; }

        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        g_canvas->on_lbutton_down(pt.x, pt.y);
        convert_coordinate(pt);
        litehtml::position::vector redraw_boxes;
        g_canvas->m_doc->on_lbutton_down(pt.x, pt.y, 0, 0, redraw_boxes);

        break;
    }
    case WM_LBUTTONUP:
    {
        // 更新阅读记录
        if (!g_tickTimer)
        {
            g_tickTimer = timeSetEvent(g_cfg.record_update_interval_ms, 0, Tick, 0, TIME_ONESHOT);
        }

        //g_book->hide_tooltip();
        if (!g_canvas->m_doc) { return 0; }
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        convert_coordinate(pt);

        litehtml::position::vector redraw_boxes;
        g_canvas->m_doc->on_lbutton_up(pt.x, pt.y, 0, 0, redraw_boxes);
        g_canvas->on_lbutton_up();
        break;
    }
    case WM_LBUTTONDBLCLK:
    {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        g_canvas->on_lbutton_dblclk(pt.x, pt.y);
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        // 更新阅读记录
        if (!g_tickTimer)
        {
            g_tickTimer = timeSetEvent(g_cfg.record_update_interval_ms, 0, Tick, 0, TIME_ONESHOT);
        }
        if (!g_canvas->m_doc) break;
   
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        g_canvas->on_mouse_move(pt.x, pt.y);
        convert_coordinate(pt);
  
        
        
        litehtml::position::vector redraw_boxes;
        g_canvas->m_doc->on_mouse_over(pt.x, pt.y, 0, 0, redraw_boxes);
        if (!redraw_boxes.empty()) {
            for(auto r :redraw_boxes)
            {
                RECT rc{ r.left(), r.top(), r.right(), r.bottom()};
                UpdateCache();
                InvalidateRect(hwnd, &rc, true);

            }
            OutputDebugStringA("redraw_boxes not empty!\n"); 
        }

  
        break;
    }

    case WM_USER_SCROLL:
    {
        int offset = (int)wp;
        int spine = (int)lp;
        //scroll_to(offset);   // 你自己的函数
        return 0;
    }
    case WM_EPUB_ANCHOR:
    {
        // 更新阅读记录
        if (!g_tickTimer) 
        { 
            g_tickTimer = timeSetEvent(g_cfg.record_update_interval_ms, 0, Tick, 0, TIME_ONESHOT);  
        }
 
        if (!g_canvas->m_doc) { return 0; }
        wchar_t* sel = reinterpret_cast<wchar_t*>(wp);
        if (sel) {
            std::string cssSel = "[id=\"" + w2a(sel) + "\"]";
            if (auto el = g_canvas->m_doc->root()->select_one(cssSel.c_str())) {
                g_offsetY = el->get_placement().y;
            }
            free(sel);          // 对应 _wcsdup
        }
        UpdateCache();
        InvalidateRect(hwnd, nullptr, FALSE);
        UpdateWindow(g_hView);

        return 0;
    }
    case WM_EPUB_NAVIGATE:
    {
        // 更新阅读记录
        if (!g_tickTimer)
        {
            g_tickTimer = timeSetEvent(g_cfg.record_update_interval_ms, 0, Tick, 0, TIME_ONESHOT);
        }

        wchar_t* url = reinterpret_cast<wchar_t*>(wp);
        g_book->OnTreeSelChanged(url);  // 现在安全地在主线程执行
        free(url);


        return 0;
    }
    case WM_EPUB_UPDATE_SCROLLBAR: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        // 垂直滚动条
        SCROLLINFO si{ sizeof(si) };
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin = 0;
        si.nMax = std::max(0, g_maxScroll);
        si.nPage = rc.bottom;               // 每次滚一页
        si.nPos = g_scrollY;
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
        // 水平滚动条（如果不需要可删掉）
        si.nMax = 0;
        si.nPage = rc.right;
        SetScrollInfo(hwnd, SB_HORZ, &si, TRUE);
        // 重新排版+缓存
        UpdateCache();
        InvalidateRect(hwnd, nullptr, FALSE);
        //UpdateWindow(hwnd);
        return 0;
    }
    case WM_MOUSELEAVE:
    {

        litehtml::position::vector redraw_box;
        g_canvas->m_doc->on_mouse_leave(redraw_box);

  

        return 0;
    }

    case WM_VSCROLL:
    {
        if (g_canvas) { g_canvas->clear_selection(); }
        RECT rc;
        GetClientRect(hwnd, &rc);

        int code = LOWORD(wp);
        int pos = HIWORD(wp);
        int delta = 0;

        // 启动一次性计时器（用于阅读记录）
        if (!g_tickTimer)
        {
            g_tickTimer = timeSetEvent(g_cfg.record_update_interval_ms,
                0, Tick, 0, TIME_ONESHOT);
        }

        switch (code)
        {
        case SB_LINEUP:   delta = -30;          break;   // 3 行
        case SB_LINEDOWN: delta = 30;          break;
        case SB_PAGEUP:   delta = -rc.bottom;   break;
        case SB_PAGEDOWN: delta = rc.bottom;   break;

        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:
            g_scrollY = std::clamp(pos, 0, g_maxScroll);
            break;                                   // 直接定位后跳出 switch

        default:
            return 0;                                // 不处理
        }

        // 普通滚动（非 THUMB*）才累加 delta
        if (code != SB_THUMBPOSITION && code != SB_THUMBTRACK)
            g_scrollY = std::clamp(g_scrollY + delta, 0, g_maxScroll);

        // 统一更新
        SetScrollPos(hwnd, SB_VERT, g_scrollY, TRUE);
        UpdateCache();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEWHEEL:
    {
        if (g_canvas) { g_canvas->clear_selection(); }
        if (GetKeyState(VK_CONTROL) & 0x8000)
        {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);   // ±120
            float factor = (delta > 0) ? 1.1f : 0.9f;     // 放大 / 缩小系数

            // 2. 更新全局缩放
            g_canvas->m_zoom_factor = std::clamp(g_canvas->m_zoom_factor * factor, 0.25f, 5.0f);
            UpdateCache();
            // 3. 重绘
            InvalidateRect(hwnd, NULL, FALSE);
        
            return 0;   // 已处理，不再传递
        }
 
        RECT rc;
        GetClientRect(hwnd, &rc);
        int zDelta = GET_WHEEL_DELTA_WPARAM(wp);
        //g_scrollY = std::clamp<int>(g_scrollY - zDelta, 0, std::max<int>(g_maxScroll - rc.bottom, 0));
        int factor = std::abs(zDelta / 120);
        if (zDelta >= 0) { g_scrollY -= g_line_height * factor; }
        else { g_scrollY += g_line_height * factor; }
        SetScrollPos(hwnd, SB_VERT, g_scrollY, TRUE);
        // 更新阅读记录
        if (!g_tickTimer)
        {
            g_tickTimer = timeSetEvent(g_cfg.record_update_interval_ms, 0, Tick, 0, TIME_ONESHOT);
        }

        UpdateCache();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_PAINT:

        if (g_canvas && g_canvas->m_doc  && g_states.isUpdate.exchange(false))
        {

            RECT rc;
            GetClientRect(g_hView, &rc);
            int x = g_center_offset;
            int y = -g_offsetY;
            int w = g_cfg.document_width;
            int h = rc.bottom - rc.top;
            litehtml::position clip(x, 0, w, h);
            g_canvas->present(x, y, &clip);

        }
   
        return 0;

    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

ATOM RegisterViewClass(HINSTANCE hInst)
{
    WNDCLASSW wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;   // 关键
    wc.lpfnWndProc = ViewWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"EPUBView";
    return RegisterClassW(&wc);
}




void UpdateCache()
{
    if (!g_canvas || !g_vd || !g_book) return;

    RECT rc;
    GetClientRect(g_hView, &rc);
    int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0) return;
    
    //request_doc_async(h, g_scrollY, g_offsetY);
    auto doc = g_vd->get_doc(h, g_scrollY, g_offsetY);
    g_canvas->m_doc = doc;

    g_states.isUpdate.store(true);

    g_center_offset = std::max((w - g_cfg.document_width ) * 0.5, 0.0);
}

inline void DumpBookRecord()
{
    if (!g_recorder || g_recorder->m_book_record.id < 0) { return; }
    auto& r = g_recorder->m_book_record;
    std::wostringstream oss;            // ← 宽字符流
    oss << std::boolalpha;
    oss << L"===== BookRecord Dump =====\n";
    oss << L"id                 = " << r.id << L'\n';
    oss << L"path               = " << a2w(r.path) << L'\n';   // path 已经是 std::wstring 就 OK
    oss << L"title              = " << a2w(r.title) << L'\n';
    oss << L"author             = " << a2w(r.author) << L'\n';
    oss << L"openCount          = " << r.openCount << L'\n';
    oss << L"totalWords         = " << r.totalWords << L'\n';
    oss << L"lastSpineId        = " << r.lastSpineId << L'\n';
    oss << L"lastOffset         = " << r.lastOffset << L'\n';
    oss << L"fontSize           = " << r.fontSize << L'\n';
    oss << L"lineHeightMul      = " << std::fixed << std::setprecision(2) << r.lineHeightMul << L'\n';
    oss << L"docWidth           = " << r.docWidth << L'\n';
    oss << L"totalTime          = " << r.totalTime << L" (s)\n";
    oss << L"lastOpenTimestamp  = " << r.lastOpenTimestamp << L" (us)\n";
    oss << L"enableCSS          = " << r.enableCSS << L'\n';
    oss << L"enableJS           = " << r.enableJS << L'\n';
    oss << L"enableGlobalCSS    = " << r.enableGlobalCSS << L'\n';
    oss << L"enablePreHTML      = " << r.enablePreHTML << L'\n';
    oss << L"displayTOC         = " << r.displayTOC << L'\n';
    oss << L"displayStatus      = " << r.displayStatus << L'\n';
    oss << L"displayMenu        = " << r.displayMenu << L'\n';
    oss << L"displayScroll      = " << r.displayScroll << L'\n';
    oss << L"============================\n";

    OutputDebugStringW(oss.str().c_str());   // ← 宽字符版本
    OutputDebugStringW((L"\nTotal Read Time (s): " + std::to_wstring(g_recorder->getTotalTime()) + L"\n").c_str());
}
// ---------- 窗口 ----------
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        DragAcceptFiles(hwnd, TRUE);
        SendMessage(g_hWnd, WM_SIZE, 0, 0);

        return 0;
    }
    case WM_DROPFILES: {
        wchar_t file[MAX_PATH]{};
        DragQueryFileW(reinterpret_cast<HDROP>(wp), 0, file, MAX_PATH);
        DragFinish(reinterpret_cast<HDROP>(wp));

        // 只接受 .epub / .EPUB
        const wchar_t* ext = wcsrchr(file, L'.');
        if (!ext || _wcsicmp(ext, L".epub") != 0) {
            MessageBoxW(g_hWnd, L"请拖入 EPUB 文件", L"格式错误", MB_ICONWARNING);
            return 0;
        }
        // 1. 等待上一次任务结束（简单做法：阻塞等待）
        if (g_parse_task.valid()) {
            g_parse_task.wait();
        }

  

        g_container->clear();
        g_canvas->clear();
        g_book->clear();
        g_toc->clear();
        InvalidateRect(g_hView, nullptr, true);
        InvalidateRect(g_hwndToc, nullptr, true);


        // 2. 立即释放旧对象，防止野指针
        //g_container.reset();

        // 3. 启动新任务
        SetStatus(0, L"正在加载...");

        g_parse_task = std::async(std::launch::async, [file] {
            try {
                g_book->load(file);

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
    case WM_MOUSELEAVE:
    {
        litehtml::position::vector redraw_box;
        g_canvas->m_doc->on_mouse_leave(redraw_box);
        return 0;
    }
    case WM_EPUB_PARSED: {

        if (!g_states.isLoaded) {
            g_states.isLoaded = true;
            PostMessage(g_hWnd, WM_COMMAND, MAKEWPARAM(IDM_TOGGLE_STATUS_WINDOW, 0), 0);
            PostMessage(g_hWnd, WM_COMMAND, MAKEWPARAM(IDM_TOGGLE_TOC_WINDOW, 0), 0);

            ShowWindow(g_hView, SW_SHOW);
     
        }
        g_vd->clear();
        g_recorder->flush();
        g_recorder->openBook(w2a(g_book->m_file_path));
        auto& record = g_recorder->m_book_record;
        DumpBookRecord();
        auto spine_id = record.lastSpineId;
        g_offsetY = record.lastOffset;
        // 设置 5 章
        SendMessage(g_hViewScroll, SBM_SETSPINECOUNT, g_book->ocf_pkg_.spine.size(), 0);

        // 设置第 2 章，高 800 px，当前偏移 120 px
 
        g_last_html_path = g_book->ocf_pkg_.spine[spine_id].href;
        g_states.needRelayout.store(true);
        g_vd->load_book(g_book, g_container, g_cfg.document_width);
        g_vd->load_html(g_last_html_path);

        UpdateCache();          // 复用前面给出的 UpdateCache()

        // 4) 触发一次轻量 WM_PAINT（只 BitBlt）
        InvalidateRect(g_hView, nullptr, true);
        InvalidateRect(g_hWnd, nullptr, FALSE);
        UpdateWindow(g_hView);
        UpdateWindow(g_hWnd);
        SetStatus(0, L"加载完成");
        return 0;
    }
    case WM_EPUB_CSS_RELOAD: {
        // 重新解析当前章节即可
        return 0;
    }

    case WM_SIZE:
    {
        RECT rcClient;
        GetClientRect(hwnd, &rcClient);
        const int cx = rcClient.right;
        int cyClient = rcClient.bottom;

        /* 1. 工具栏高度 */
        int cyTB = 0;

        /* 2. 状态栏高度 */
        int cySB = 0;
        if (g_cfg.displayStatusBar && g_hStatus)
        {
            ShowWindow(g_hStatus, SW_SHOW);
            SendMessage(g_hStatus, WM_SIZE, 0, 0);
            int parts[2] = { 120, -1 };
            SendMessage(g_hStatus, SB_SETPARTS, 2, (LPARAM)parts);
            RECT rcSB{};
            GetWindowRect(g_hStatus, &rcSB);
            cySB = rcSB.bottom - rcSB.top;
        }
        else if (g_hStatus)
        {
            ShowWindow(g_hStatus, SW_HIDE);
        }

        /* 3. 剩余可用高度 */
        const int cy = cyClient - cyTB - cySB;

        /* 4. 目录宽度 & 竖线 */
        const int tocW = g_cfg.displayTOC ? g_splitX : 0;
        ShowWindow(g_hwndToc, g_cfg.displayTOC ? SW_SHOW : SW_HIDE);
        ShowWindow(g_hwndSplit, g_cfg.displayTOC ? SW_SHOW : SW_HIDE);

        /* 5. 滚动条宽度 */
        const int sbW = g_cfg.displayScrollBar ? 20 : 0;
        ShowWindow(g_hViewScroll, g_cfg.displayScrollBar ? SW_SHOW : SW_HIDE);

        /* 6. 摆放子窗口（Y 起点统一为 cyTB） */
        MoveWindow(g_hwndToc, 0, cyTB, tocW, cy, TRUE);
        MoveWindow(g_hwndSplit, tocW, cyTB, 2, cy, TRUE);
        MoveWindow(g_hView, tocW + 2, cyTB,
            cx - tocW - 2 - sbW, cy, TRUE);      // 正文让出滚动条
        MoveWindow(g_hViewScroll,
            cx - sbW, cyTB, sbW, cy, TRUE);      // 滚动条贴最右

        UpdateCache();
        SendMessage(g_hView, WM_EPUB_UPDATE_SCROLLBAR, 0, 0);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_LOAD_ERROR: {
        wchar_t* msg = (wchar_t*)lp;
        SetStatus(0, msg);
        free(msg);                // 对应 CoTaskMemAlloc / _wcsdup
        return 0;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(g_hWnd, &ps);
        RECT rc;
        GetClientRect(g_hWnd, &rc);
        // 1. 强制整屏刷白
    // ① 先整屏刷成红色——肉眼可见


        // 如果还没加载 EPUB，就画起始图
        if (!g_states.isLoaded && g_pSplashImg)
        {
            Gdiplus::Graphics g(hdc);
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

            RECT rc;
            GetClientRect(g_hWnd, &rc);
            Gdiplus::RectF win(0.0f, 0.0f,
                static_cast<float>(rc.right),
                static_cast<float>(rc.bottom));

            // 计算保持比例的缩放因子
            float imgW = static_cast<float>(g_pSplashImg->GetWidth());
            float imgH = static_cast<float>(g_pSplashImg->GetHeight());
            float scale = std::max(win.Width / imgW, win.Height / imgH);

            float drawW = imgW * scale;
            float drawH = imgH * scale;

            // 居中
            float x = (win.Width - drawW) / 2.0f;
            float y = (win.Height - drawH) / 2.0f;

            g.DrawImage(g_pSplashImg,
                Gdiplus::RectF(x, y, drawW, drawH),
                0, 0, imgW, imgH,
                Gdiplus::UnitPixel);
        }
        else
        {
            // 原有绘制逻辑（交给子控件或默认处理）
        }
        EndPaint(g_hWnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        if (LOWORD(lp) >= g_splitX && LOWORD(lp) <= g_splitX + 2)
        {
            SetCapture(hwnd); g_dragging = true;
        }
        break;
    }
    case WM_MOUSEWHEEL: 
    {


        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (g_dragging)
        {
            int x = LOWORD(lp);
            if (x < 50) x = 50; if (x > 400) x = 400;
            g_splitX = x;
            PostMessage(hwnd, WM_SIZE, 0, 0);
        }
        break;
    }
    case WM_LBUTTONUP:
    {
        if (g_dragging) { ReleaseCapture(); g_dragging = false; }
        break;
    }
    case WM_SETCURSOR:
    {
        if (LOWORD(lp) == HTCLIENT)
        {
            POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
            if (pt.x >= g_splitX && pt.x <= g_splitX + 2)
            {
                SetCursor(LoadCursor(nullptr, IDC_SIZEWE)); return TRUE;
            }
        }
        break;
    }
    case WM_DESTROY: {
        g_recorder->flush();
        if (g_tooltipTimer)
        {
            timeKillEvent(g_tooltipTimer);
            g_tooltipTimer = 0;
        }
        PostQuitMessage(0);
        return 0;
    }
    case WM_VSCROLL: { return 0; }
    case WM_HSCROLL: { return 0; }

    case WM_COMMAND: {
        switch (LOWORD(wp))
        {
        case IDM_TOGGLE_CSS:
            g_cfg.enableCSS = !g_cfg.enableCSS;
            CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_CSS,
                MF_BYCOMMAND | (g_cfg.enableCSS ? MF_CHECKED : MF_UNCHECKED));
            if (g_vd) { g_vd->reload(); }
            break;
     
        case IDM_TOGGLE_JS:
            g_cfg.enableJS = !g_cfg.enableJS;          // 切换状态
            CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_JS,
                MF_BYCOMMAND | (g_cfg.enableJS ? MF_CHECKED : MF_UNCHECKED));
            if (g_vd) { g_vd->reload(); }
            break;
      
        case IDM_TOGGLE_GLOBAL_CSS:
            g_cfg.enableGlobalCSS = !g_cfg.enableGlobalCSS;          // 切换状态
            CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_GLOBAL_CSS,
                MF_BYCOMMAND | (g_cfg.enableGlobalCSS ? MF_CHECKED : MF_UNCHECKED));
            if (g_vd) { g_vd->reload(); }
            break;
   
        case IDM_TOGGLE_PREPROCESS_HTML:
            g_cfg.enablePreprocessHTML = !g_cfg.enablePreprocessHTML;          // 切换状态
            CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_PREPROCESS_HTML,
                MF_BYCOMMAND | (g_cfg.enablePreprocessHTML ? MF_CHECKED : MF_UNCHECKED));
            if (g_vd) { g_vd->reload(); }
            break;
       

        case IDM_TOGGLE_HOVER_PREVIEW:
            g_cfg.enableHoverPreview = !g_cfg.enableHoverPreview;          // 切换状态
            CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_HOVER_PREVIEW,
                MF_BYCOMMAND | (g_cfg.enableHoverPreview ? MF_CHECKED : MF_UNCHECKED));
            break;
    
        case IDM_TOGGLE_TOC_WINDOW:
   
            g_cfg.displayTOC = !g_cfg.displayTOC;          // 切换状态
            CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_TOC_WINDOW,
                MF_BYCOMMAND | (g_cfg.displayTOC ? MF_CHECKED : MF_UNCHECKED));
            if (g_cfg.displayTOC) { ShowWindow(g_hwndToc, SW_SHOW); }
            else { ShowWindow(g_hwndToc, SW_HIDE); }
            // 让主窗口重新布局
            PostMessage(g_hWnd, WM_SIZE, 0, 0);
            break;
   
        case IDM_TOGGLE_STATUS_WINDOW:

            g_cfg.displayStatusBar = !g_cfg.displayStatusBar;          // 切换状态
            CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_STATUS_WINDOW,
                MF_BYCOMMAND | (g_cfg.displayStatusBar ? MF_CHECKED : MF_UNCHECKED));
            if (g_cfg.displayStatusBar) { ShowWindow(g_hStatus, SW_SHOW); }
            else { ShowWindow(g_hStatus, SW_HIDE); }
            // 让主窗口重新布局
            PostMessage(g_hWnd, WM_SIZE, 0, 0);
            break;
 
        case ID_EPUB_RELOAD:
  
            if (g_vd) { g_vd->reload(); }
            break;
   
        case ID_FONT_BIGGER:        // Ctrl + '+'
            g_cfg.font_size = std::min(g_cfg.font_size + 1.0f, 72.0f);   // 上限 72
            if (g_vd) { g_vd->reload(); }   // 重新加载并排版
            break;

        case ID_FONT_SMALLER:       // Ctrl + '-'
            g_cfg.font_size = std::max(g_cfg.font_size - 1.0f, 8.0f);    // 下限 8
            if (g_vd) { g_vd->reload(); }
            break;

        case ID_FONT_RESET:         // Ctrl + '0'
            g_cfg.font_size = g_cfg.default_font_size;   // 默认字号
            if (g_vd) { g_vd->reload(); }
            break;

        case ID_LINE_HEIGHT_UP:     // Ctrl + Shift + '+'
            g_cfg.line_height = std::min(g_cfg.line_height + 0.1f, 3.0f);
            if (g_vd) { g_vd->reload(); }
            break;

        case ID_LINE_HEIGHT_DOWN:   // Ctrl + Shift + '-'
            g_cfg.line_height = std::max(g_cfg.line_height - 0.1f, 1.0f);
            if (g_vd) { g_vd->reload(); }
            break;

        case ID_LINE_HEIGHT_RESET:  // Ctrl + Shift + '0'
            g_cfg.line_height = g_cfg.default_line_height;  // 默认行高
            if (g_vd) { g_vd->reload(); }
            break;

        case ID_WIDTH_BIGGER:       // Alt + '→'
            g_cfg.document_width = std::min(g_cfg.document_width + 50.0f, 2000.0f);
            if (g_vd) { g_vd->reload(); }   // 仅重新排版即可
            break;

        case ID_WIDTH_SMALLER:      // Alt + '←'
            g_cfg.document_width = std::max(g_cfg.document_width - 50.0f, 300.0f);
            if (g_vd) { g_vd->reload(); }
            break;

        case ID_WIDTH_RESET:        // Alt + '0'
            g_cfg.document_width = g_cfg.default_document_width;   // 默认宽度
            if (g_vd) { g_vd->reload(); }
            break;
        case ID_EDIT_COPY:
            g_canvas->copy_to_clipboard();
            break;
        break;
        }
    }
    }
    return DefWindowProc(hwnd, msg, wp, lp);

}
// 从资源加载 PNG → Gdiplus::Image*
Gdiplus::Image* LoadPngFromResource(HINSTANCE hInst, UINT resId)
{
    // 1. 找到资源
    HRSRC hRes = FindResource(hInst, MAKEINTRESOURCE(resId), L"PNG");
    if (!hRes) return nullptr;

    DWORD resSize = SizeofResource(hInst, hRes);
    HGLOBAL hResData = LoadResource(hInst, hRes);
    if (!hResData) return nullptr;

    const void* pResData = LockResource(hResData);   // 无需解锁，进程结束自动释放

    // 2. 把内存块包装成 IStream
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, resSize);
    if (!hMem) return nullptr;

    void* pMem = GlobalLock(hMem);
    memcpy(pMem, pResData, resSize);
    GlobalUnlock(hMem);

    IStream* pStream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(hMem, TRUE, &pStream)))
    {
        GlobalFree(hMem);
        return nullptr;
    }

    // 3. 用 GDI+ 解码
    Gdiplus::Image* img = new Gdiplus::Image(pStream);
    pStream->Release();   // CreateStreamOnHGlobal 第二个参数 TRUE → hMem 会被自动释放

    if (img->GetLastStatus() != Gdiplus::Ok)
    {
        delete img;
        return nullptr;
    }
    return img;
}
bool SaveHDCAsBmp(HDC hdc, int width, int height, const wchar_t* name)
{
    // 1. 创建兼容的内存 DC 和位图
    HDC     memDC = CreateCompatibleDC(hdc);
    BITMAPINFO bi = { 0 };
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = height;   // 正数 = 底-上 DIB
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;       // 24-bit RGB
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hBmp = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hBmp) { DeleteDC(memDC); return false; }

    // 2. 把 hdc 内容拷到 DIB
    HGDIOBJ oldBmp = SelectObject(memDC, hBmp);
    BitBlt(memDC, 0, 0, width, height, hdc, 0, 0, SRCCOPY);

    // 3. 构造 BITMAPFILEHEADER + DIB 数据
    BITMAPFILEHEADER bfh = { 0 };
    bfh.bfType = 0x4D42; // 'BM'
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    DWORD dibSize = ((width * 3 + 3) & ~3) * height; // 每行 4 字节对齐
    bfh.bfSize = bfh.bfOffBits + dibSize;

    // 4. 写文件
    HANDLE hFile = CreateFileW(name,
        GENERIC_WRITE,
        0, nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        SelectObject(memDC, oldBmp);
        DeleteObject(hBmp);
        DeleteDC(memDC);
        return false;
    }

    DWORD written = 0;
    WriteFile(hFile, &bfh, sizeof(bfh), &written, nullptr);
    WriteFile(hFile, &bi.bmiHeader, sizeof(bi.bmiHeader), &written, nullptr);
    WriteFile(hFile, bits, dibSize, &written, nullptr);
    CloseHandle(hFile);

    // 5. 清理
    SelectObject(memDC, oldBmp);
    DeleteObject(hBmp);
    DeleteDC(memDC);
    return true;
}
LRESULT CALLBACK ImageviewProc(HWND hwnd, UINT m, WPARAM w, LPARAM l)
{
    switch(m)
    {
    case WM_DESTROY: 
        return 0;
    case WM_PAINT:
    {
        if (!IsWindowVisible(g_hImageview)) { return 0; }
        if (!g_imageview_canvas)
        {
            OutputDebugStringA("[ImageviewProc] self or doc null\n");
            break;
        }

        if (g_states.isImageviewUpdate.exchange(false))
        {
            RECT rc;
            GetClientRect(g_hImageview, &rc);
            litehtml::position clip(0, 0, rc.right - rc.left, rc.bottom - rc.top);
            g_imageview_canvas->present(0, 0, &clip);


        }
        return 0;
    }

    case WM_MOUSEWHEEL: {

        static int currentW = [] {
            RECT rc; GetClientRect(g_hImageview, &rc);
            return rc.right - rc.left;   // 第一次用真实客户区宽度
            }();
        int delta = GET_WHEEL_DELTA_WPARAM(w);
        float factor = (delta > 0) ? 1.1f : 0.9f; /* 1. 鼠标指针在屏幕上的位置 */
        POINT pt;
        GetCursorPos(&pt); /* 2. 当前窗口矩形（屏幕坐标） */
        RECT wr; GetWindowRect(hwnd, &wr); /* 3. 获取屏幕工作区大小（不含任务栏） */

        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfo(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
        int scrW = mi.rcWork.right - mi.rcWork.left;
        int scrH = mi.rcWork.bottom - mi.rcWork.top;
        /* 3.5 提前判断：若窗口外框已顶满屏幕，放大就忽略 */
        DWORD style = GetWindowLong(hwnd, GWL_STYLE);
        DWORD exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
        UINT dpi = GetDpiForWindow(hwnd);
        RECT rNow{ 0, 0, currentW, 1 }; // 高度先随便填，只要算宽度 
        AdjustWindowRectExForDpi(&rNow, style, FALSE, exStyle, dpi);
        int winW_now = rNow.right - rNow.left;
        g_imageview_canvas->m_doc->render(currentW);
        int docH_now = g_imageview_canvas->m_doc->height(); RECT rH{ 0, 0, 1, docH_now };
        AdjustWindowRectExForDpi(&rH, style, FALSE, exStyle, dpi);
        int winH_now = rH.bottom - rH.top; // 放大且已顶满 → 直接 return 
        if (factor > 1.0f && (winW_now >= scrW || winH_now >= scrH)) { return 0; } /* 4. 计算新的渲染尺寸，并立即限制在屏幕内 */
        int renderW = std::max(32, static_cast<int>(currentW * factor + 0.5f));
        int renderH = 0; // 先限制宽度 
        renderW = std::min(renderW, scrW); // 重新渲染得到高度 
        g_imageview_canvas->m_doc->render(renderW);
        renderH = g_imageview_canvas->m_doc->height(); // 再限制高度
        renderH = std::min(renderH, scrH);
        renderW = std::max(renderW, 32); // 防止极端情况宽度过小 
        /* 5. 计算窗口外框尺寸（含标题栏/边框） */
        RECT r{ 0, 0, renderW, renderH };
        AdjustWindowRectExForDpi(&r, style, FALSE, exStyle, dpi);
        int winW = r.right - r.left; int winH = r.bottom - r.top;
        /* 6. 以鼠标位置为缩放原点，计算新左上角 */
        int newX = pt.x - (pt.x - wr.left) * winW / (wr.right - wr.left);
        int newY = pt.y - (pt.y - wr.top) * winH / (wr.bottom - wr.top);
        /* 7. 最终再保证左上角也在屏幕内（简单 clamp） */
        newX = std::max((long)newX, mi.rcWork.left);
        newY = std::max((long)newY, mi.rcWork.top);
        newX = std::min((long)newX, mi.rcWork.right - winW);
        newY = std::min((long)newY, mi.rcWork.bottom - winH);
        /* 8. 更新窗口 & 画布 */
        g_imageview_canvas->resize(renderW, renderH);
        SetWindowPos(hwnd, HWND_TOPMOST, newX, newY, winW, winH, SWP_NOACTIVATE | SWP_NOZORDER);

        currentW = renderW;
        g_states.isImageviewUpdate.store(true);
        InvalidateRect(hwnd, nullptr, false);

        return 0;
    }
                      /* 2. 左键拖动 */
    case WM_LBUTTONDOWN:
        g_imageview_dragging = true;
        SetCapture(hwnd);                     // 锁定鼠标
        g_imageview_drag_pos = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        return 0;

    case WM_LBUTTONUP:
        if (g_imageview_dragging)
        {
            g_imageview_dragging = false;
            ReleaseCapture();
        }

        return 0;

    case WM_MOUSEMOVE:
        if (g_imageview_dragging)
        {
            int dx = GET_X_LPARAM(l) - g_imageview_drag_pos.x;
            int dy = GET_Y_LPARAM(l) - g_imageview_drag_pos.y;

            RECT wr;
            GetWindowRect(hwnd, &wr);
            SetWindowPos(hwnd, nullptr,
                wr.left + dx, wr.top + dy,
                0, 0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            g_states.isImageviewUpdate.store(true);
        }
        return 0;
    case WM_RBUTTONUP:
    {
        g_book->hide_imageview();
        return 0;
    }
    case WM_ERASEBKGND: {
        return 1;
    }
    }
    return DefWindowProc(hwnd, m, w, l);
}
LRESULT CALLBACK TooltipProc(HWND hwnd, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {

    case WM_PAINT:
    {
        if (!IsWindowVisible(g_hTooltip)) { return 0; }
        if (!g_tooltip_canvas)
        {
            OutputDebugStringA("[TooltipProc] self or doc null\n");
            break;
        }

        if (g_states.isTooltipUpdate.exchange(false))
        {
            RECT rc;
            GetClientRect(g_hTooltip, &rc);
            litehtml::position clip(0, 0, rc.right - rc.left, rc.bottom - rc.top);
            g_tooltip_canvas->present(0, 0, &clip);
            //g_tooltip_canvas->BeginDraw();
            //RECT rc;
            //GetClientRect(g_hTooltip, &rc);
            //litehtml::position clip(0, 0, rc.right - rc.left, rc.bottom - rc.top);
            //g_tooltip_canvas->m_doc->draw(
            //    g_tooltip_canvas->getContext(),   // 强制转换
            //    0, 0, &clip);
            //g_tooltip_canvas->EndDraw();

        }
        return 0;
    }
    case WM_DESTROY:
        return 0;
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProc(hwnd, m, w, l);
}

const wchar_t THUMB_CLASS[] = L"ThumbPreview";

void register_thumb_class()
{
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = TooltipProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = THUMB_CLASS;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);
}

const wchar_t IMAGEVIEW_CLASS[] = L"Imageview";

void register_imageview_class()
{
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = ImageviewProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = IMAGEVIEW_CLASS;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);
}

const wchar_t SCROLLBAR_CLASS_NAME[] = L"ScrollBarEx";

void register_scrollbar_class()
{
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = ScrollBarEx::WndProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = SCROLLBAR_CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassExW(&wc);
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
    register_thumb_class();
    register_imageview_class();
    register_scrollbar_class();

    // 在 CreateWindow 之前
    HMENU hMenu = LoadMenu(g_hInst, MAKEINTRESOURCE(IDR_MENU_MAIN));

    if (!hMenu) {
        MessageBox(nullptr, L"LoadMenu 失败", L"Error", MB_ICONERROR);
        return 0;
    }

    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        SetProcessDPIAware();

    g_hWnd = CreateWindowW(L"EPUBLite", L"EPUB Lite Reader",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, 800, 600,
        nullptr, nullptr, h, nullptr);
    // 放在主窗口 CreateWindow 之后
    g_hStatus = CreateWindowEx(
        0, STATUSCLASSNAME, L"就绪",
        WS_CHILD  | SBARS_SIZEGRIP,
        0, 0, 0, 0,           // 位置和大小由 WM_SIZE 调整
        g_hWnd, nullptr, g_hInst, nullptr);

    // 一次性创建
    const wchar_t TOC_CLASS[] = L"TocPanelClass";
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = TocPanel::WndProc;          // 你的新 WndProc
    wc.hInstance = g_hInst;
    wc.lpszClassName = TOC_CLASS;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;             // 自绘
    wc.cbWndExtra = sizeof(LONG_PTR);   // ← 必须有
    RegisterClassExW(&wc);


    // 2. 创建
    g_hwndToc = CreateWindowExW(
        WS_EX_COMPOSITED,          // 双缓冲
        TOC_CLASS,                 // 用注册的类名
        nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL,
        0, 0, 200, 600,
        g_hWnd, (HMENU)100, g_hInst, nullptr);



    g_hwndSplit = CreateWindowExW(
        0, WC_STATICW, nullptr,
        WS_CHILD  | SS_ETCHEDVERT,
        200, 0, 2, 600,          // 2 px 宽
        g_hWnd, (HMENU)101, g_hInst, nullptr);
    g_hView = CreateWindowExW(
        0, L"EPUBView", nullptr,
        WS_CHILD   | WS_CLIPSIBLINGS ,
        0, 0, 1, 1,
        g_hWnd, (HMENU)101, g_hInst, nullptr);

    g_hTooltip = CreateWindowExW(
         WS_EX_COMPOSITED,
        THUMB_CLASS, nullptr,
        WS_POPUP  | WS_THICKFRAME | WS_CLIPCHILDREN |WS_BORDER,
        0, 0, 300, 200,
        g_hView, nullptr, g_hInst, nullptr);


    g_hImageview = CreateWindowExW(
        WS_EX_COMPOSITED,
        IMAGEVIEW_CLASS, nullptr,
        WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN | WS_BORDER,
        0, 0, 300, 200,
        g_hView, nullptr, g_hInst, nullptr);

  

    g_hViewScroll = CreateWindowExW(0, SCROLLBAR_CLASS_NAME, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, 0, 0, g_hWnd, nullptr,
        g_hInst, nullptr);


    // 1. 在全局或合适位置声明
    AccelManager gAccel(g_hWnd);

    // 2. 在 WinMain 里，窗口创建完成后立刻注册
    // 书签栏
    gAccel.set(IDM_TOGGLE_TOC_WINDOW, FVIRTKEY, VK_TAB);   // 无修饰符的 Tab
    
    // 刷新
    gAccel.set(ID_EPUB_RELOAD, FVIRTKEY, VK_F5);    // 新增 F5

    // 字体
    gAccel.add(ID_FONT_BIGGER, FCONTROL | FVIRTKEY, VK_OEM_PLUS);
    gAccel.add(ID_FONT_BIGGER, FCONTROL | FVIRTKEY, VK_ADD);
    gAccel.add(ID_FONT_SMALLER, FCONTROL | FVIRTKEY, VK_OEM_MINUS);
    gAccel.add(ID_FONT_SMALLER, FCONTROL | FVIRTKEY, VK_SUBTRACT);
    gAccel.add(ID_FONT_RESET, FCONTROL | FVIRTKEY, VK_BACK);


    // 行高
    gAccel.add(ID_LINE_HEIGHT_UP, FCONTROL | FSHIFT | FVIRTKEY, VK_OEM_PLUS);
    gAccel.add(ID_LINE_HEIGHT_UP, FCONTROL | FSHIFT | FVIRTKEY, VK_ADD);
    gAccel.add(ID_LINE_HEIGHT_DOWN, FCONTROL | FSHIFT | FVIRTKEY, VK_OEM_MINUS);
    gAccel.add(ID_LINE_HEIGHT_DOWN, FCONTROL | FSHIFT | FVIRTKEY, VK_SUBTRACT);
    gAccel.add(ID_LINE_HEIGHT_RESET, FCONTROL | FSHIFT | FVIRTKEY, VK_BACK);


    // 文档宽度
    gAccel.add(ID_WIDTH_BIGGER, FALT | FVIRTKEY, VK_OEM_PLUS);
    gAccel.add(ID_WIDTH_BIGGER, FALT | FVIRTKEY, VK_ADD);
    gAccel.add(ID_WIDTH_SMALLER, FALT | FVIRTKEY, VK_SUBTRACT);
    gAccel.add(ID_WIDTH_SMALLER, FALT | FVIRTKEY, VK_OEM_MINUS);
    gAccel.add(ID_WIDTH_RESET, FALT | FVIRTKEY, VK_BACK);
  
    // 新增：复制文本（Ctrl + C）
    gAccel.add(ID_EDIT_COPY, FCONTROL | FVIRTKEY, 'C');

    g_pSplashImg = LoadPngFromResource(g_hInst, IDB_PNG1);

    g_bootstrap = std::make_unique<AppBootstrap>();
    SetMenu(g_hWnd, hMenu);            // ← 放在 CreateWindow 之后

    CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_CSS,
        MF_BYCOMMAND | (g_cfg.enableCSS ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_JS,
        MF_BYCOMMAND | (g_cfg.enableJS ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_GLOBAL_CSS,
        MF_BYCOMMAND | (g_cfg.enableGlobalCSS ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_PREPROCESS_HTML,
        MF_BYCOMMAND | (g_cfg.enablePreprocessHTML ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_HOVER_PREVIEW,
        MF_BYCOMMAND | (g_cfg.enableHoverPreview ? MF_CHECKED : MF_UNCHECKED));

 


    EnableMenuItem(hMenu, IDM_TOGGLE_JS, MF_BYCOMMAND | MF_GRAYED);
    EnableMenuItem(hMenu, IDM_TOGGLE_PREPROCESS_HTML, MF_BYCOMMAND | MF_GRAYED);

    EnableMenuItem(hMenu, IDM_TOGGLE_MENUBAR_WINDOW, MF_BYCOMMAND | MF_GRAYED);
    EnableMenuItem(hMenu, IDM_TOGGLE_SCROLLBAR_WINDOW, MF_BYCOMMAND | MF_GRAYED);
    //EnableMenuItem(hMenu, IDM_TOGGLE_STATUS_WINDOW, MF_BYCOMMAND | MF_GRAYED);
    //EnableMenuItem(hMenu, IDM_TOGGLE_TOC_WINDOW, MF_BYCOMMAND | MF_GRAYED);
    EnableClearType();
    // =====初始化隐藏=====
    PostMessage(g_hWnd, WM_COMMAND, MAKEWPARAM(IDM_TOGGLE_STATUS_WINDOW, 0), 0);
    PostMessage(g_hWnd, WM_COMMAND, MAKEWPARAM(IDM_TOGGLE_TOC_WINDOW, 0), 0);
    
    ShowWindow(g_hView, SW_HIDE);
    ShowWindow(g_hTooltip, SW_HIDE);
    // ====================
    ShowWindow(g_hWnd, n);
    UpdateWindow(g_hWnd);
    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (!gAccel.translate(&msg)) {   // ← 先给 AccelManager
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    GdiplusShutdown(gdiplusToken);
    return static_cast<int>(msg.wParam);
}

// ---------- 目录解析 ----------



// ---------- 点击目录跳转 ----------
void EPUBBook::OnTreeSelChanged(const wchar_t* href)
{
    if (!href || !*href) return;


    /* 1. 分离文件路径与锚点 */
    std::wstring whref(href);
    size_t pos = whref.find(L'#');
    std::wstring file_path = (pos == std::wstring::npos) ? whref : whref.substr(0, pos);
    std::string  id = (pos == std::wstring::npos) ? "" :
        w2a(whref.substr(pos + 1));

    if (file_path != g_last_html_path)
    {
        g_vd->load_book(g_book, g_container, g_cfg.document_width);
        g_states.needRelayout.store(true);
        g_vd->clear();
        g_vd->load_html(file_path);

        g_last_html_path = file_path;
        UpdateCache();
    }
    //SendMessage(g_hView, WM_EPUB_UPDATE_SCROLLBAR, 0, 0);


/* 3. 跳转到锚点 */
    if (!id.empty())
    {
        std::wstring cssSel = a2w(id);   // 转成宽字符
        // WM_APP + 3 约定为“跳转到锚点选择器”
        PostMessageW(g_hView, WM_EPUB_ANCHOR,
            reinterpret_cast<WPARAM>(_wcsdup(cssSel.c_str())), 0);
    }
    InvalidateRect(g_hView, nullptr, true);
    UpdateWindow(g_hWnd);
}

// SimpleContainer.cpp
void SimpleContainer::load_image(const char* src, const char* /*baseurl*/, bool)
{
    if (m_img_cache.contains(src)) return;

    std::wstring wpath = g_book->m_zipIndex.find(a2w(src));
    MemFile mf = g_book->read_zip(wpath.c_str());
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
        m_img_cache.emplace(src, std::move(frame));
    }
    else
    {
        OutputDebugStringA(("EPUB decode failed: " + std::string(src) + "\n").c_str());
    }
}

litehtml::pixel_t SimpleContainer::text_width(const char* text, litehtml::uint_ptr hFont)
{
    return m_backend->text_width(text, hFont);
}


void SimpleContainer::get_image_size(const char* src, const char* baseurl, litehtml::size& sz) {
    if (!m_img_cache.contains(src)) { sz.width = sz.height = 0; return; }
    auto img = m_img_cache[src];
    sz.width = img.width;
    sz.height = img.height;

}

// get_client_rect -> get_viewport
void SimpleContainer::get_viewport(litehtml::position& client) const
{
    // 1. 取客户区物理像素
    RECT rc{};
    GetClientRect(g_hView, &rc);

    // 2. 计算滚动条宽度（垂直滚动条存在时）
    int scrollW = 0;
    if (GetWindowLongPtr(g_hView, GWL_STYLE) & WS_VSCROLL)
        scrollW = GetSystemMetrics(SM_CXVSCROLL);

    // 3. DPI 缩放因子
    HDC hdc = GetDC(g_hView);
    int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
    int dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(g_hView, hdc);

    float scaleX = 96.0f / dpiX;
    float scaleY = 96.0f / dpiY;

    // 4. 逻辑像素
    int width = static_cast<int>((rc.right - scrollW) * scaleX);
    int height = static_cast<int>((rc.bottom) * scaleY);

    client = litehtml::position(0, 0, width, height);
}

litehtml::element::ptr
SimpleContainer::create_element(const char* tag,
    const litehtml::string_map& attrs,
    const std::shared_ptr<litehtml::document>& doc)
{
    if (litehtml::t_strcasecmp(tag, "script") != 0)
        return nullptr;   // 让 litehtml 自己建别的节点
    if (!g_cfg.enableJS) { return nullptr; }

    /* 1. 建节点（litehtml 会把内联文本自动收进来） */
    auto el = std::make_shared<litehtml::html_tag>(doc);
    el->set_tagName(tag);

    /* 2. 记录到待执行列表 */
    AppBootstrap::script_info si;
    si.el = el;
    g_bootstrap->m_pending_scripts.emplace_back(std::move(si));
    return el;
    // TODO: 处理 <math> 标签
    // TODO: 处理 <script> 标签
    // TODO: 处理 <svg> 标签
}



void SimpleContainer::import_css(litehtml::string& text,
    const litehtml::string& url,
    litehtml::string& baseurl)
{

    if (!g_cfg.enableCSS) {
        //text.clear();           // 禁用所有外部/内部 CSS
        return;
    }
    // url 可能是相对路径，baseurl 是当前 html 所在目录
    std::wstring w_path = g_book->m_zipIndex.find(a2w(url));
    MemFile mf = g_book->read_zip(w_path.c_str());
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
    return;
}

// ---------- 4. 链接注册 --------------------------------------------------
void SimpleContainer::link(const std::shared_ptr<litehtml::document>& doc,
    const litehtml::element::ptr& el)
{
    OutputDebugStringA(el->get_tagName());
    OutputDebugStringA("\n");
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

    std::string_view sv{ url };
    if (sv.starts_with('#'))
    { 
        /* 锚点 */ 
        std::wstring cssSel = a2w(url + 1);   // 去掉开头的 '#'  

        PostMessageW(g_hView, WM_EPUB_ANCHOR,
            reinterpret_cast<WPARAM>(_wcsdup(cssSel.c_str())), 0);
    }
    else if (sv.starts_with("http") || sv.starts_with("mailto:")) { /* 外部 */ }
    else 
    { 
        /* 章节跳转 */
        std::wstring href = g_book->m_zipIndex.find(a2w(url));
        wchar_t* url_copy = _wcsdup(href.c_str());
        PostMessageW(g_hView, WM_EPUB_NAVIGATE,
            reinterpret_cast<WPARAM>(url_copy), 0);
    }

}

bool SimpleContainer::on_element_click(const litehtml::element::ptr& el)
{
    OutputDebugStringA(el->get_tagName());
    OutputDebugStringA("\n");
    el->set_pseudo_class(litehtml::_hover_, true);
    if (std::strcmp(el->get_tagName(), "img") == 0 && g_cfg.enableImagePreview) { g_book->show_imageview(el); }
    return true;
}
void SimpleContainer::on_mouse_event(const litehtml::element::ptr& el,
    litehtml::mouse_event event)
{
    if (!g_cfg.enableHoverPreview) return;

    if (event == litehtml::mouse_event::mouse_event_enter)
    {
        auto link = g_book->find_link_in_chain(el);

        std::string html;
        if (!link) { return ; }
        const char* href_raw = link->get_attr("href");
        if (!href_raw) { return; }
        std::string id = g_book->extract_anchor(href_raw);
        if (id.empty()) { return; }
        html = g_book->html_of_anchor_paragraph(g_canvas->m_doc.get(), id);
  
         //如果已存在，先杀掉
        if (g_tooltipTimer)
        {
            timeKillEvent(g_tooltipTimer);
            g_tooltipTimer = 0;
        }

        // 用 new 把 element 指针传进去
        struct Payload { std::string html; };
        auto* p = new Payload{ std::move(html)};

        g_tooltipTimer = timeSetEvent(
            g_cfg.tooltip_delay_ms,          // 延迟
            1,                         // 分辨率 1ms
            [](UINT, UINT, DWORD_PTR dwUser, DWORD_PTR, DWORD_PTR)
            {
                auto* t = reinterpret_cast<Payload*>(dwUser);
                g_book->show_tooltip(std::move(t->html));
                delete t;
            },
            reinterpret_cast<DWORD_PTR>(p),
            TIME_ONESHOT);             // 一次性
    }
    else
    {
        if (g_tooltipTimer)
        {
            timeKillEvent(g_tooltipTimer);
            g_tooltipTimer = 0;
        }
        g_book->hide_tooltip();
    }
}




// 内部工具：UTF-8 → UTF-16
static std::vector<UChar> utf8_to_utf16(const char* src)
{
    std::vector<UChar> buf;
    if (!src || !*src) return buf;
    int32_t len8 = static_cast<int32_t>(std::strlen(src));
    UErrorCode status = U_ZERO_ERROR;
    int32_t len16 = 0;
    u_strFromUTF8(nullptr, 0, &len16, src, len8, &status);
    buf.resize(len16 + 1);
    status = U_ZERO_ERROR;
    u_strFromUTF8(buf.data(), len16 + 1, nullptr, src, len8, &status);
    if (U_FAILURE(status)) buf.clear();
    else buf.resize(len16);
    return buf;
}

// 内部工具：UTF-16 → 堆上 UTF-8（以 '\0' 结尾，可直接传给 on_word/on_space）
static char* utf16_to_heap_utf8(const UChar* src, int32_t len)
{
    if (!src || len <= 0) return nullptr;
    UErrorCode status = U_ZERO_ERROR;
    int32_t len8 = 0;
    u_strToUTF8(nullptr, 0, &len8, src, len, &status);
    char* out = new char[len8 + 1];
    status = U_ZERO_ERROR;
    u_strToUTF8(out, len8 + 1, nullptr, src, len, &status);
    if (U_FAILURE(status)) { delete[] out; return nullptr; }
    return out;
}


void SimpleContainer::split_text(
    const char* text,
    const std::function<void(const char*)>& on_word,
    const std::function<void(const char*)>& on_space)
{
    if (!text || !*text) return;

    /* 1. UTF-8 → UTF-16 */
    auto u16 = utf8_to_utf16(text);
    if (u16.empty()) return;

    /* 2. 创建 ICU 单词边界迭代器（系统默认 locale，支持多语言） */
    UErrorCode status = U_ZERO_ERROR;
    UBreakIterator* brk = ubrk_open(
        UBRK_WORD, nullptr,
        u16.data(), static_cast<int32_t>(u16.size()),
        &status);
    if (U_FAILURE(status)) return;

    /* 3. 遍历所有边界区间 */
    int32_t prev = ubrk_first(brk);
    for (int32_t curr = ubrk_next(brk);
        curr != UBRK_DONE;
        prev = curr, curr = ubrk_next(brk))
    {
        /* 3.1 判断区间是否全为空格 */
        bool all_space = true;
        UChar32 cp;
        int32_t idx = prev;
        while (idx < curr) {
            U16_NEXT(u16.data(), idx, curr, cp);
            if (!u_isspace(cp)) { all_space = false; break; }
        }

        /* 3.2 转回 UTF-8 并回调 */
        char* out = utf16_to_heap_utf8(u16.data() + prev, curr - prev);
        if (!out) continue;

        if (all_space && on_space)      on_space(out);
        else if (!all_space && on_word) on_word(out);

        delete[] out;   // 回调后立即释放
    }

    ubrk_close(brk);
}

//void SimpleContainer::split_text(const char* text,
//    const std::function<void(const char*)>& on_word,
//    const std::function<void(const char*)>& on_space)
//{
//    if (!text || !*text) return;
//
//    // UTF-8 → ICU UnicodeString
//    icu::UnicodeString ustr = icu::UnicodeString::fromUTF8(text);
//
//    // 创建行断行迭代器（UAX #14）
//    UErrorCode status = U_ZERO_ERROR;
//    std::unique_ptr<icu::BreakIterator> brk(
//        icu::BreakIterator::createLineInstance(icu::Locale::getDefault(), status));
//    if (U_FAILURE(status)) return;
//
//    brk->setText(ustr);
//
//    int32_t prev = brk->first();
//    for (int32_t curr = brk->next(); curr != icu::BreakIterator::DONE;
//        prev = curr, curr = brk->next())
//    {
//        icu::UnicodeString seg(ustr, prev, curr - prev);
//
//        // 判断这一段是不是纯空格
//        bool all_space = true;
//        for (int32_t i = 0; i < seg.length(); ++i) {
//            if (!u_isspace(seg.char32At(i))) { all_space = false; break; }
//        }
//
//        std::string out;
//        seg.toUTF8String(out);
//
//        if (all_space) {
//            if (on_space) on_space(out.c_str());
//        }
//        else {
//            if (on_word) on_word(out.c_str());
//        }
//    }
//}
//✅ 使用
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



// ---------- 9. 媒体查询 -----------------------------------------------
void SimpleContainer::get_media_features(litehtml::media_features& mf) const
{
    // 1. 窗口客户区（物理像素）
    RECT rc;
    GetClientRect(g_hWnd, &rc);
    mf.width = MulDiv(rc.right - rc.left, GetDpiForWindow(g_hWnd), 96);
    mf.height = MulDiv(rc.bottom - rc.top, GetDpiForWindow(g_hWnd), 96);

    // 2. 屏幕物理分辨率
    const UINT dpiX = GetDpiForWindow(g_hWnd);   // 也可用 GetDpiForSystem
    mf.resolution = dpiX;
    mf.device_width = MulDiv(GetSystemMetricsForDpi(SM_CXSCREEN, dpiX), dpiX, 96);
    mf.device_height = MulDiv(GetSystemMetricsForDpi(SM_CYSCREEN, dpiX), dpiX, 96);

    // 3. 颜色深度（24 位）
    HDC hdc = GetDC(nullptr);
    mf.color = GetDeviceCaps(hdc, BITSPIXEL);   // 通常 24 或 32
    mf.monochrome = 0;

    ReleaseDC(nullptr, hdc);
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


void SimpleContainer::init_dpi() {
    if (HDC hdc = GetDC(nullptr)) {
        int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
        m_px_per_pt = static_cast<float>(dpi) / 72.0f;
        ReleaseDC(nullptr, hdc);
    }
}
litehtml::pixel_t SimpleContainer::pt_to_px(float pt) const {
    // 乘法 + 位移，比 MulDiv 更快
    return static_cast<litehtml::pixel_t>(
        pt * m_px_per_pt + 0.5f);
}


std::string preprocess_js(std::string html)
{
    if (!g_cfg.enableJS) {
        // 1) 删除 <script ...>...</script>
        static const std::regex reScriptPair(
            R"(<\s*script\b[^>]*>.*?</script>)",
            std::regex::icase | std::regex::optimize | std::regex::nosubs);

        // 2) 删除自闭合 <script ... />
        static const std::regex reScriptSelf(
            R"(<\s*script\b[^>]*\/>)",
            std::regex::icase | std::regex::optimize | std::regex::nosubs);

        html = std::regex_replace(html, reScriptPair, "");
        html = std::regex_replace(html, reScriptSelf, "");
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
            std::wstring w_path = g_book->m_zipIndex.find(a2w(src));
            MemFile mf = g_book->read_zip(w_path.c_str());
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
    //html = std::regex_replace(html,
    //    std::regex(R"(<title\b[^>]*?/\s*>)", std::regex::icase),
    //    "<title></title>");

    html = std::regex_replace(
        html,
        std::regex(R"(<([a-zA-Z][a-zA-Z0-9]*)\b([^>]*?)/\s*>)", std::regex::icase),
        "<$1$2></$1>");
    //-------------------------------------------------
    // 2. 自闭合 <script .../> → <script ...>code</script>
    //-------------------------------------------------
  // 2. 自闭合 <script .../> → <script ...>code</script>
    html = preprocess_js(html);

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
    m_backend->draw_text(hdc, text, hFont, color, pos);
}

void SimpleContainer::draw_image(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const std::string& url, const std::string& base_url)
{
    m_backend->draw_image(hdc, layer, url, base_url);
}

void SimpleContainer::draw_solid_fill(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::web_color& color)
{
    m_backend->draw_solid_fill(hdc, layer, color);
}

void SimpleContainer::draw_linear_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::background_layer::linear_gradient& gradient)
{
    m_backend->draw_linear_gradient(hdc, layer, gradient);
}

void SimpleContainer::draw_radial_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::background_layer::radial_gradient& gradient)
{
    m_backend->draw_radial_gradient(hdc, layer, gradient);
}

void SimpleContainer::draw_conic_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::background_layer::conic_gradient& gradient)
{
    m_backend->draw_conic_gradient(hdc, layer, gradient);
}

void SimpleContainer::draw_borders(litehtml::uint_ptr hdc,
    const litehtml::borders& borders,
    const litehtml::position& pos,
    bool root)
{
    m_backend->draw_borders(hdc, borders, pos, root);
}


litehtml::uint_ptr SimpleContainer::create_font(const litehtml::font_description& descr, const litehtml::document* doc, litehtml::font_metrics* fm)
{
    return m_backend->create_font(descr, doc, fm);
}

void SimpleContainer::delete_font(litehtml::uint_ptr hFont)
{
    m_backend->delete_font(hFont);
}


// ---------- 11. 列表标记 ----------------------------------------------
void SimpleContainer::draw_list_marker(litehtml::uint_ptr hdc,
    const litehtml::list_marker& marker)
{
    m_backend->draw_list_marker(hdc, marker);
}


void SimpleContainer::set_clip(const litehtml::position& pos,
    const litehtml::border_radiuses& radius)
{
    m_backend->set_clip(pos, radius);
}

void SimpleContainer::del_clip()
{
    m_backend->del_clip();
}

// DirectWrite backend
/* ---------- 构造 ---------- */
D2DCanvas::D2DCanvas(int w, int h, HWND hwnd)
    : m_w(w), m_h(h), m_hwnd(hwnd) {


    /* 1) D2D 工厂（1.0 足够） */
    HRESULT hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory),
        nullptr,
        reinterpret_cast<void**>(m_d2dFactory.GetAddressOf()));
    if (FAILED(hr)) {
        OutputDebugStringA("D2D1CreateFactory failed\n");
        return;
    }

    /* 2) 计算窗口 DPI 缩放 */
    float dpiX = 96.0f, dpiY = 96.0f;
    m_d2dFactory->GetDesktopDpi(&dpiX, &dpiY);
    const float scale = dpiX / 96.0f;

    /* 3) 创建 HwndRenderTarget（直接画到窗口） */
    hr = m_d2dFactory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                D2D1_ALPHA_MODE_PREMULTIPLIED),
            dpiX, dpiY),
        D2D1::HwndRenderTargetProperties(
            m_hwnd,
            D2D1::SizeU(
                static_cast<UINT>(m_w * scale),
                static_cast<UINT>(m_h * scale))),
        &m_rt);
    if (FAILED(hr)) {
        OutputDebugStringA("CreateHwndRenderTarget failed\n");
        return;
    }


}


void D2DCanvas::BeginDraw()
{

    m_rt->BeginDraw();
    m_rt->Clear(D2D1::ColorF(D2D1::ColorF::White));

    // 1. 保存原始矩阵
    m_rt->GetTransform(&m_oldMatrix);

    // 3. 以鼠标位置为中心整体缩放
    m_rt->SetTransform(
        D2D1::Matrix3x2F::Scale(
            m_zoom_factor,
            m_zoom_factor,
            D2D1::Point2F(static_cast<float>(0),
                static_cast<float>(0))));
}
void D2DCanvas::EndDraw()
{
    // 恢复原始矩阵
    m_rt->SetTransform(m_oldMatrix);

    m_rt->EndDraw();
}

// ---------- 辅助：UTF-8 ↔ UTF-16 ----------



// ---------- 实现 ----------
ComPtr<ID2D1SolidColorBrush> D2DBackend::getBrush(litehtml::uint_ptr hdc, const litehtml::web_color& c)
{
    uint32_t key = (c.alpha << 24) | (c.red << 16) | (c.green << 8) | c.blue;
    auto it = m_brushPool.find(key);
    if (it != m_brushPool.end()) return it->second;
    ID2D1RenderTarget* rt = reinterpret_cast<ID2D1RenderTarget*>(hdc);
    ComPtr<ID2D1SolidColorBrush> brush;
    rt->CreateSolidColorBrush(
        D2D1::ColorF(c.red / 255.f, c.green / 255.f, c.blue / 255.f, c.alpha / 255.f),
        &brush);
    m_brushPool[key] = brush;
    return brush;
}

ComPtr<IDWriteTextLayout> D2DBackend::getLayout(const std::wstring& txt,
    const FontPair* fp,
    float maxW)
{
    LayoutKey k{ txt, fp->descr.hash(), maxW };
    auto it = m_layoutCache.find(k);
    if (it != m_layoutCache.end()) return it->second;

    ComPtr<IDWriteTextLayout> layout;
    m_dwrite->CreateTextLayout(txt.c_str(), (UINT32)txt.size(),
        fp->format.Get(), maxW, 512.f, &layout);
    if (!layout) return nullptr;

    // 换行/截断
    bool nowrap = false;
    //layout->SetWordWrapping(nowrap ? DWRITE_WORD_WRAPPING_NO_WRAP
    //    : DWRITE_WORD_WRAPPING_WRAP);
    if (nowrap) {
        DWRITE_TRIMMING trim{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        layout->SetTrimming(&trim, nullptr);
    }
    m_layoutCache[k] = layout;
    return layout;
}

void D2DBackend::record_char_boxes(ID2D1RenderTarget* rt,
    IDWriteTextLayout* layout,
    const std::wstring& wtxt,
    const litehtml::position& pos)
{
    LineBoxes line;
    float originX = static_cast<float>(pos.x);
    float originY = static_cast<float>(pos.y);

    for (size_t i = 0; i < wtxt.size(); ++i)
    {
        DWRITE_HIT_TEST_METRICS htm;
        float left, top;
        layout->HitTestTextPosition(i, FALSE, &left, &top, &htm);

        CharBox cb;
        cb.ch = wtxt[i];
        cb.rect = D2D1::RectF(
            originX + left,
            originY + top,
            originX + left + htm.width,
            originY + top + htm.height);
        cb.offset = g_plainText.size() + i;
        line.push_back(cb);
    }
    g_lines.emplace_back(std::move(line));

    // 同时累积纯文本
    g_plainText += wtxt;
}


void D2DBackend::draw_text(litehtml::uint_ptr hdc,
    const char* text,
    litehtml::uint_ptr hFont,
    litehtml::web_color color,
    const litehtml::position& pos)
{
    if (!text || !*text || !hFont) return;
    auto* rt = reinterpret_cast<ID2D1RenderTarget*>(hdc);
    FontPair* fp = reinterpret_cast<FontPair*>(hFont);
    if (!fp) return;

    // 1. 画刷
    auto brush = getBrush(hdc, color);
    if (!brush) return;

    // 2. 文本
    std::wstring wtxt = a2w(text);
    if (wtxt.empty()) return;

    float maxW = 8192.0f;
    auto layout = getLayout(wtxt, fp, maxW);
    if (!layout) return;
    record_char_boxes(rt, layout.Get(), wtxt, pos);
    // 3. 绘制文本
    rt->DrawTextLayout(D2D1::Point2F(static_cast<float>(pos.x),
        static_cast<float>(pos.y)),
        layout.Get(), brush.Get());

    // 4. 绘制装饰线（下划线 / 删除线 / 上划线）
    draw_decoration(hdc, fp, color, pos, layout.Get());
}
void D2DBackend::draw_decoration(litehtml::uint_ptr hdc, const FontPair* fp,
    litehtml::web_color color,
    const litehtml::position& pos,
    IDWriteTextLayout* layout)
{
    if (fp->descr.decoration_line == litehtml::text_decoration_line_none)
        return;
    auto* rt = reinterpret_cast<ID2D1RenderTarget*>(hdc);
    /* 1. 文本整体尺寸 */
    DWRITE_TEXT_METRICS tm{};
    layout->GetMetrics(&tm);
    if (tm.width <= 0) return;

    /* 2. 取第一行的 baseline */
    std::vector<DWRITE_LINE_METRICS> lineMetrics;
    UINT32 lineCount = 0;
    layout->GetLineMetrics(nullptr, 0, &lineCount);
    if (lineCount == 0) return;
    lineMetrics.resize(lineCount);
    layout->GetLineMetrics(lineMetrics.data(), lineCount, &lineCount);

    const float baseline = lineMetrics[0].baseline;
    const float yBase = static_cast<float>(pos.y) + baseline;

    /* 3. 画刷 */
    auto brush = getBrush(hdc, fp->descr.decoration_color.is_current_color
        ? color
        : fp->descr.decoration_color);
    if (!brush) return;

    /* 4. 线粗：先用 1 px，后续可按 decoration_thickness 计算 */
    const float thick = fp->descr.decoration_thickness.val();

    /* 5. 绘制三种装饰线 */
    const float x0 = static_cast<float>(pos.x);
    const float x1 = x0 + tm.width;

    /* 下划线 */
    if (fp->descr.decoration_line & litehtml::text_decoration_line_underline)
    {
        const float y = yBase + 1.0f;   // 可根据字体度量再微调
        rt->DrawLine({ x0, y }, { x1, y }, brush.Get(), thick);
    }

    /* 删除线 */
    if (fp->descr.decoration_line & litehtml::text_decoration_line_line_through)
    {
        const float y = yBase - lineMetrics[0].height * 0.35f;
        rt->DrawLine({ x0, y }, { x1, y }, brush.Get(), thick);
    }

    /* 上划线 */
    if (fp->descr.decoration_line & litehtml::text_decoration_line_overline)
    {
        const float y = yBase - lineMetrics[0].height;
        rt->DrawLine({ x0, y }, { x1, y }, brush.Get(), thick);
    }
}
// ----------------------------------------------------------
// 工具：根据 box 类型返回实际矩形
// ----------------------------------------------------------
static litehtml::position clip_box(const litehtml::background_layer& layer,
    litehtml::background_box box_type)
{
    switch (box_type)
    {
    case litehtml::background_box_content:
    case litehtml::background_box_padding:
        // 你的版本没有 content_box / padding_box，统一回退到 border_box
        return layer.border_box;
    default:
        return layer.border_box;
    }
}

// ----------------------------------------------------------
// 工具：加载位图（WIC -> D2D）
// ----------------------------------------------------------


// ----------------------------------------------------------
// 主函数：draw_image
// ----------------------------------------------------------
void D2DBackend::draw_image(litehtml::uint_ptr hdc,
    const litehtml::background_layer& layer,
    const std::string& url,
    const std::string& base_url)
{
    if (url.empty()) return;
    auto* rt = reinterpret_cast<ID2D1RenderTarget*>(hdc);

    /* ---------- 1. 取缓存位图 ---------- */
    auto it = g_container->m_img_cache.find(url);
    if (it == g_container->m_img_cache.end()) return;
    const ImageFrame& frame = it->second;
    if (frame.rgba.empty()) return;

    ComPtr<ID2D1Bitmap> bmp;

    D2D1_BITMAP_PROPERTIES bp =
        D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                D2D1_ALPHA_MODE_PREMULTIPLIED));
    rt->CreateBitmap(
        D2D1::SizeU(frame.width, frame.height),
        frame.rgba.data(),
        frame.stride,
        bp,
        &bmp);
    //auto d2d_it = m_d2dBitmapCache.find(url);
    //if (d2d_it != m_d2dBitmapCache.end() && false)
    //{
    //    bmp = d2d_it->second;
    //}
    //else
    //{
    //    D2D1_BITMAP_PROPERTIES bp =
    //        D2D1::BitmapProperties(
    //            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
    //                D2D1_ALPHA_MODE_PREMULTIPLIED));
    //    if (SUCCEEDED(rt->CreateBitmap(
    //        D2D1::SizeU(frame.width, frame.height),
    //        frame.rgba.data(),
    //        frame.stride,
    //        bp,
    //        &bmp)))
    //    {
    //        m_d2dBitmapCache.emplace(url, bmp);
    //    }
    //}
    if (!bmp) return;

    /* ---------- 2. 计算目标矩形 ---------- */
    D2D1_RECT_F dst = D2D1::RectF(
        float(layer.border_box.left()),
        float(layer.border_box.top()),
        float(layer.border_box.right()),
        float(layer.border_box.bottom()));

    /* ---------- 3. 计算绘制区域（cover / contain / stretch） ---------- */
    float imgW = float(bmp->GetPixelSize().width);
    float imgH = float(bmp->GetPixelSize().height);
    if (imgW == 0 || imgH == 0) return;

    float dstW = dst.right - dst.left;
    float dstH = dst.bottom - dst.top;

    // 这里只演示 cover（填满 + 居中）
    float scale = std::max(dstW / imgW, dstH / imgH);
    float bgW = imgW * scale;
    float bgH = imgH * scale;
    float bgX = dst.left + (dstW - bgW) * 0.5f;
    float bgY = dst.top + (dstH - bgH) * 0.5f;

    D2D1_RECT_F drawRect = { bgX, bgY, bgX + bgW, bgY + bgH };

    /* ---------- 4. 绘制 ---------- */
    rt->DrawBitmap(bmp.Get(), drawRect, 1.0f,
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
        D2D1::RectF(0, 0, imgW, imgH));

}

inline bool D2DBackend::is_all_zero(const litehtml::border_radiuses& r)
{
    return r.top_left_x == 0 && r.top_left_y == 0 &&
        r.top_right_x == 0 && r.top_right_y == 0 &&
        r.bottom_right_x == 0 && r.bottom_right_y == 0 &&
        r.bottom_left_x == 0 && r.bottom_left_y == 0;
}
void D2DBackend::draw_solid_fill(litehtml::uint_ptr hdc,
    const litehtml::background_layer& layer,
    const litehtml::web_color& color)
{
    // 1. 取出 D2D 渲染目标
    ID2D1RenderTarget* rt = reinterpret_cast<ID2D1RenderTarget*>(hdc);
    if (!rt) return;

    // 2. 创建/复用纯色画刷
    ComPtr<ID2D1SolidColorBrush> brush;
    rt->CreateSolidColorBrush(
        D2D1::ColorF(color.red / 255.0f,
            color.green / 255.0f,
            color.blue / 255.0f,
            color.alpha / 255.0f),
        &brush);
    if (!brush) return;

    // 3. 计算要填充的矩形（border_box）
    D2D1_RECT_F rc = D2D1::RectF(
        static_cast<float>(layer.border_box.left()),
        static_cast<float>(layer.border_box.top()),
        static_cast<float>(layer.border_box.right()),
        static_cast<float>(layer.border_box.bottom()));


    // 4. 若存在圆角，用圆角矩形；否则直接矩形
    if (is_all_zero(layer.border_radius))
    {
        rt->FillRectangle(rc, brush.Get());
    }
    else
    {
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
            rc,
            static_cast<float>(layer.border_radius.top_left_x),
            static_cast<float>(layer.border_radius.top_left_y));
        rt->FillRoundedRectangle(rr, brush.Get());
    }
}


void D2DBackend::draw_linear_gradient(
    litehtml::uint_ptr hdc,
    const litehtml::background_layer& layer,
    const litehtml::background_layer::linear_gradient& g)
{
    ID2D1RenderTarget* rt = reinterpret_cast<ID2D1RenderTarget*>(hdc);
    if (!rt) return;

    // 1. 把 color_points 转成 D2D 色标
    std::vector<D2D1_GRADIENT_STOP> stops;
    stops.reserve(g.color_points.size());
    for (const auto& cp : g.color_points)
    {
        stops.push_back(D2D1::GradientStop(
            static_cast<float>(cp.offset),
            D2D1::ColorF(
                cp.color.red / 255.0f,
                cp.color.green / 255.0f,
                cp.color.blue / 255.0f,
                cp.color.alpha / 255.0f)));
    }

    // 2. 创建 stop collection
    Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> stopColl;
    if (FAILED(rt->CreateGradientStopCollection(
        stops.data(),
        static_cast<UINT>(stops.size()),
        D2D1_GAMMA_2_2,
        D2D1_EXTEND_MODE_CLAMP,
        &stopColl)))
        return;

    // 3. 创建线性渐变画刷
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> brush;
    if (FAILED(rt->CreateLinearGradientBrush(
        D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(static_cast<float>(g.start.x),
                static_cast<float>(g.start.y)),
            D2D1::Point2F(static_cast<float>(g.end.x),
                static_cast<float>(g.end.y))),
        stopColl.Get(),
        &brush)))
        return;

    // 4. 计算要填充的矩形
    const D2D1_RECT_F rc = D2D1::RectF(
        static_cast<float>(layer.border_box.left()),
        static_cast<float>(layer.border_box.top()),
        static_cast<float>(layer.border_box.right()),
        static_cast<float>(layer.border_box.bottom()));

    // 5. 圆角判断
    auto& r = layer.border_radius;
    bool no_radius = r.top_left_x == 0 && r.top_left_y == 0 &&
        r.top_right_x == 0 && r.top_right_y == 0 &&
        r.bottom_right_x == 0 && r.bottom_right_y == 0 &&
        r.bottom_left_x == 0 && r.bottom_left_y == 0;

    if (no_radius)
        rt->FillRectangle(rc, brush.Get());
    else
    {
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
            rc,
            static_cast<float>(r.top_left_x),
            static_cast<float>(r.top_left_y));
        rt->FillRoundedRectangle(rr, brush.Get());
    }
}


void D2DBackend::draw_radial_gradient(
    litehtml::uint_ptr hdc,
    const litehtml::background_layer& layer,
    const litehtml::background_layer::radial_gradient& g)
{
    ID2D1RenderTarget* rt = reinterpret_cast<ID2D1RenderTarget*>(hdc);
    if (!rt) return;

    // 1. 构造 D2D 色标
    std::vector<D2D1_GRADIENT_STOP> stops;
    stops.reserve(g.color_points.size());
    for (const auto& cp : g.color_points)
    {
        stops.push_back(D2D1::GradientStop(
            static_cast<float>(cp.offset),
            D2D1::ColorF(
                cp.color.red / 255.0f,
                cp.color.green / 255.0f,
                cp.color.blue / 255.0f,
                cp.color.alpha / 255.0f)));
    }

    // 2. 创建 stop collection
    ComPtr<ID2D1GradientStopCollection> stopColl;
    if (FAILED(rt->CreateGradientStopCollection(
        stops.data(),
        static_cast<UINT>(stops.size()),
        D2D1_GAMMA_2_2,
        D2D1_EXTEND_MODE_CLAMP,
        &stopColl)))
        return;

    // 3. 创建径向渐变画刷
    ComPtr<ID2D1RadialGradientBrush> brush;
    if (FAILED(rt->CreateRadialGradientBrush(
        D2D1::RadialGradientBrushProperties(
            D2D1::Point2F(static_cast<float>(g.position.x),
                static_cast<float>(g.position.y)), // 圆心
            D2D1::Point2F(0.0f, 0.0f),                  // 偏移（0,0）即可
            static_cast<float>(g.radius.x),                 // rx
            static_cast<float>(g.radius.y)),                // ry（保持圆形）
        stopColl.Get(),
        &brush)))
        return;

    // 4. 计算填充区域
    const D2D1_RECT_F rc = D2D1::RectF(
        static_cast<float>(layer.border_box.left()),
        static_cast<float>(layer.border_box.top()),
        static_cast<float>(layer.border_box.right()),
        static_cast<float>(layer.border_box.bottom()));

    // 5. 圆角判断
    auto& r = layer.border_radius;
    bool no_radius = r.top_left_x == 0 && r.top_left_y == 0 &&
        r.top_right_x == 0 && r.top_right_y == 0 &&
        r.bottom_right_x == 0 && r.bottom_right_y == 0 &&
        r.bottom_left_x == 0 && r.bottom_left_y == 0;

    if (no_radius)
        rt->FillRectangle(rc, brush.Get());
    else
    {
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
            rc,
            static_cast<float>(r.top_left_x),
            static_cast<float>(r.top_left_y));
        rt->FillRoundedRectangle(rr, brush.Get());
    }
}


// 角度归一化到
static inline float normalize_angle(float a)
{
    a = fmodf(a, 2.0f * float(M_PI));
    return a < 0 ? a + 2.0f * float(M_PI) : a;
}

// 在色标数组中按角度(0..1) 线性插值颜色
static litehtml::web_color sample_color(float t,
    const std::vector<litehtml::background_layer::color_point>& stops)
{
    if (stops.empty()) return {};
    if (stops.size() == 1) return stops.front().color;

    // 保证色标有序
    auto cmp = [](const litehtml::background_layer::color_point& a,
        const litehtml::background_layer::color_point& b)
        { return a.offset < b.offset; };
    if (!std::is_sorted(stops.begin(), stops.end(), cmp))
    {
        std::vector<litehtml::background_layer::color_point> tmp = stops;
        std::sort(tmp.begin(), tmp.end(), cmp);
        return sample_color(t, tmp);
    }

    // 找到区间
    auto it = std::lower_bound(stops.begin(), stops.end(), t,
        [](const litehtml::background_layer::color_point& s, float v)
        { return s.offset < v; });

    if (it == stops.end()) return stops.back().color;
    if (it == stops.begin()) return stops.front().color;

    const auto& prev = *(it - 1);
    const auto& next = *it;
    float factor = (t - prev.offset) / (next.offset - prev.offset);
    factor = std::clamp(factor, 0.0f, 1.0f);

    litehtml::web_color c;
    c.red = static_cast<BYTE>(prev.color.red + (next.color.red - prev.color.red) * factor);
    c.green = static_cast<BYTE>(prev.color.green + (next.color.green - prev.color.green) * factor);
    c.blue = static_cast<BYTE>(prev.color.blue + (next.color.blue - prev.color.blue) * factor);
    c.alpha = static_cast<BYTE>(prev.color.alpha + (next.color.alpha - prev.color.alpha) * factor);
    return c;
}

void D2DBackend::draw_conic_gradient(
    litehtml::uint_ptr hdc,
    const litehtml::background_layer& layer,
    const litehtml::background_layer::conic_gradient& g)
{
    ID2D1RenderTarget* rt = reinterpret_cast<ID2D1RenderTarget*>(hdc);
    if (!rt) return;

    // 1. 计算填充矩形
    const D2D1_RECT_F rc = D2D1::RectF(
        static_cast<float>(layer.border_box.left()),
        static_cast<float>(layer.border_box.top()),
        static_cast<float>(layer.border_box.right()),
        static_cast<float>(layer.border_box.bottom()));

    const float w = rc.right - rc.left;
    const float h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    // 2. 生成位图大小（固定 512×512，可改）
    const UINT bmpSize = 512;
    const UINT stride = bmpSize * 4;
    std::vector<BYTE> pixels(bmpSize * bmpSize * 4, 0); // BGRA

    // 3. 逐像素填色
    for (UINT y = 0; y < bmpSize; ++y)
    {
        for (UINT x = 0; x < bmpSize; ++x)
        {
            // 归一化到 [-1,1]
            float nx = (x / float(bmpSize - 1)) * 2.0f - 1.0f;
            float ny = (y / float(bmpSize - 1)) * 2.0f - 1.0f;

            float angle = atan2f(ny, nx);          // -π..π
            angle += float(M_PI);                  // 0..2π
            angle = normalize_angle(angle + g.angle); // 支持全局旋转
            float t = angle / (2.0f * float(M_PI));   // 0..1

            litehtml::web_color c = sample_color(t, g.color_points);

            UINT idx = (y * bmpSize + x) * 4;
            pixels[idx + 0] = c.blue;
            pixels[idx + 1] = c.green;
            pixels[idx + 2] = c.red;
            pixels[idx + 3] = c.alpha;
        }
    }

    // 4. 创建 D2D 位图
    ComPtr<ID2D1Bitmap> bmp;
    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(rt->CreateBitmap(
        D2D1::SizeU(bmpSize, bmpSize),
        pixels.data(),
        stride,
        props,
        &bmp)))
        return;

    // 5. 圆角判断
    auto& r = layer.border_radius;
    bool no_radius = r.top_left_x == 0 && r.top_left_y == 0 &&
        r.top_right_x == 0 && r.top_right_y == 0 &&
        r.bottom_right_x == 0 && r.bottom_right_y == 0 &&
        r.bottom_left_x == 0 && r.bottom_left_y == 0;

    // 6. 绘制
    rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (no_radius)
    {
        rt->DrawBitmap(bmp.Get(), rc);
    }
    else
    {
        // 用圆角矩形裁剪
        ComPtr<ID2D1Layer> layerPtr;
        rt->CreateLayer(nullptr, &layerPtr);
        rt->PushLayer(
            D2D1::LayerParameters(
                rc,
                nullptr,
                D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                D2D1::IdentityMatrix(),
                1.0f,
                nullptr,
                D2D1_LAYER_OPTIONS_NONE),
            layerPtr.Get());

        rt->DrawBitmap(bmp.Get(), rc);

        rt->PopLayer();
    }
}

void D2DBackend::draw_list_marker(
    litehtml::uint_ptr hdc,
    const litehtml::list_marker& marker)
{
    ID2D1RenderTarget* rt = reinterpret_cast<ID2D1RenderTarget*>(hdc);
    if (!rt) return;

    // 1. 基础信息
    const float x = static_cast<float>(marker.pos.x);
    const float y = static_cast<float>(marker.pos.y);
    const float sz = static_cast<float>(marker.pos.width);
    const D2D1_COLOR_F color = D2D1::ColorF(
        marker.color.red / 255.0f,
        marker.color.green / 255.0f,
        marker.color.blue / 255.0f,
        marker.color.alpha / 255.0f);

    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(rt->CreateSolidColorBrush(color, &brush)))
        return;

    rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    switch (marker.marker_type)
    {
    case litehtml::list_style_type_disc:
    {
        rt->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(x + sz * 0.5f, y + sz * 0.5f), sz * 0.5f, sz * 0.5f),
            brush.Get());
    }
    break;

    case litehtml::list_style_type_circle:
    {
        rt->DrawEllipse(
            D2D1::Ellipse(D2D1::Point2F(x + sz * 0.5f, y + sz * 0.5f), sz * 0.5f, sz * 0.5f),
            brush.Get(),
            sz * 0.1f); // 线宽
    }
    break;

    case litehtml::list_style_type_square:
    {
        const D2D1_RECT_F rc = D2D1::RectF(x, y, x + sz, y + sz);
        rt->FillRectangle(rc, brush.Get());
    }
    break;

    default:
        // 其他类型（decimal、lower-alpha 等）由文本层绘制，这里忽略
        break;
    }
}


namespace {

    struct SideInfo {
        float          width = 0;
        litehtml::web_color color{};
        litehtml::border_style style = litehtml::border_style_solid;
    };

    // 根据 style 计算明暗色
    D2D1_COLOR_F AdjustColor(const litehtml::web_color& c, float factor)
    {
        return D2D1::ColorF(
            std::clamp(c.red * factor / 255.0f, 0.0f, 1.0f),
            std::clamp(c.green * factor / 255.0f, 0.0f, 1.0f),
            std::clamp(c.blue * factor / 255.0f, 0.0f, 1.0f),
            c.alpha / 255.0f);
    }

} // namespace

void D2DBackend::draw_borders(litehtml::uint_ptr hdc,
    const litehtml::borders& borders,
    const litehtml::position& draw_pos,
    bool root)
{
    if (!hdc) return;
    auto* rt = reinterpret_cast<ID2D1RenderTarget*>(hdc);
    // 1. 收集四边
    std::array<SideInfo, 4> sides = {
        SideInfo{ (float)borders.top.width,    borders.top.color,    borders.top.style },
        SideInfo{ (float)borders.right.width,  borders.right.color,  borders.right.style },
        SideInfo{ (float)borders.bottom.width, borders.bottom.color, borders.bottom.style },
        SideInfo{ (float)borders.left.width,   borders.left.color,   borders.left.style }
    };

    if (std::all_of(sides.begin(), sides.end(),
        [](const SideInfo& s) { return s.width <= 0; }))
        return;

    // 2. 建立工厂
    ComPtr<ID2D1Factory> factory;
    rt->GetFactory(&factory);

    // 3. 构造外轮廓
    auto build_rounded_rect = [&](float l, float t, float r, float b,
        const litehtml::border_radiuses& rad,
        ComPtr<ID2D1PathGeometry>& out) -> bool
        {
            ComPtr<ID2D1PathGeometry> geo;
            if (FAILED(factory->CreatePathGeometry(&geo))) return false;
            ComPtr<ID2D1GeometrySink> sink;
            if (FAILED(geo->Open(&sink))) return false;

            float rtl = (float)rad.top_left_x, rtr = (float)rad.top_right_x;
            float rbr = (float)rad.bottom_right_x, rbl = (float)rad.bottom_left_x;

            sink->BeginFigure(D2D1::Point2F(l + rtl, t), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(D2D1::Point2F(r - rtr, t));
            if (rbr > 0) sink->AddArc(D2D1::ArcSegment(
                D2D1::Point2F(r - rbr, b),
                D2D1::SizeF(rbr, rbr),
                0.0f,
                D2D1_SWEEP_DIRECTION_CLOCKWISE,
                D2D1_ARC_SIZE_SMALL));

            if (rbl > 0) sink->AddArc(D2D1::ArcSegment(
                D2D1::Point2F(l, b - rbl),
                D2D1::SizeF(rbl, rbl),
                0.0f,
                D2D1_SWEEP_DIRECTION_CLOCKWISE,
                D2D1_ARC_SIZE_SMALL));

            if (rtl > 0) sink->AddArc(D2D1::ArcSegment(
                D2D1::Point2F(l + rtl, t),
                D2D1::SizeF(rtl, rtl),
                0.0f,
                D2D1_SWEEP_DIRECTION_CLOCKWISE,
                D2D1_ARC_SIZE_SMALL));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            out = std::move(geo);
            return true;
        };

    float l = (float)draw_pos.left();
    float t = (float)draw_pos.top();
    float r = (float)draw_pos.right();
    float b = (float)draw_pos.bottom();

    ComPtr<ID2D1PathGeometry> outer, inner;
    if (!build_rounded_rect(l, t, r, b, borders.radius, outer)) return;

    // 4. 内轮廓（统一用最小边宽）
    float minW = std::min({ sides[0].width, sides[1].width, sides[2].width, sides[3].width });
    litehtml::border_radiuses innerRad = borders.radius;
    innerRad.top_left_x = std::max(innerRad.top_left_x - minW, 0.0f);
    innerRad.top_right_x = std::max(innerRad.top_right_x - minW, 0.0f);
    innerRad.bottom_right_x = std::max(innerRad.bottom_right_x - minW, 0.0f);
    innerRad.bottom_left_x = std::max(innerRad.bottom_left_x - minW, 0.0f);

    if (!build_rounded_rect(l + sides[3].width, t + sides[0].width,
        r - sides[1].width, b - sides[2].width,
        innerRad, inner)) return;

    // 5. 创建边框几何 = outer - inner
    ComPtr<ID2D1PathGeometry> borderGeo;
    factory->CreatePathGeometry(&borderGeo);
    ComPtr<ID2D1GeometrySink> sink;
    borderGeo->Open(&sink);
    outer->CombineWithGeometry(inner.Get(), D2D1_COMBINE_MODE_EXCLUDE,
        nullptr, sink.Get());
    sink->Close();

    // 6. 画四边（按顺序 top/right/bottom/left）
    const std::array<const char*, 4> sideNames = { "top","right","bottom","left" };
    const std::array<float, 4> offsets = { 0, 0, 0, 0 }; // 预留
    (void)offsets;

    // 6-a 纯色简单实现：先整体填充背景色，再描边
    // 这里为了演示，只画四条独立路径，实际可优化
    for (int idx = 0; idx < 4; ++idx)
    {
        const SideInfo& side = sides[idx];
        if (side.width <= 0) continue;

        ComPtr<ID2D1SolidColorBrush> brush;
        D2D1_COLOR_F clr;
        switch (side.style)
        {
        case litehtml::border_style_groove:
            clr = AdjustColor(side.color, 0.75f); break;
        case litehtml::border_style_ridge:
            clr = AdjustColor(side.color, 1.25f); break;
        case litehtml::border_style_inset:
            clr = AdjustColor(side.color, 0.60f); break;
        case litehtml::border_style_outset:
            clr = AdjustColor(side.color, 1.40f); break;
        default:
            clr = D2D1::ColorF(side.color.red / 255.0f,
                side.color.green / 255.0f,
                side.color.blue / 255.0f,
                side.color.alpha / 255.0f);
        }
        rt->CreateSolidColorBrush(clr, &brush);

        // 为每条边单独构造路径（略繁琐，但保证独立颜色）
        ComPtr<ID2D1PathGeometry> sidePath;
        factory->CreatePathGeometry(&sidePath);
        ComPtr<ID2D1GeometrySink> sideSink;
        sidePath->Open(&sideSink);

        switch (idx)
        {
        case 0: // top
            sideSink->BeginFigure(D2D1::Point2F(l + borders.radius.top_left_x, t), D2D1_FIGURE_BEGIN_HOLLOW);
            sideSink->AddLine(D2D1::Point2F(r - borders.radius.top_right_x, t));
            break;
        case 1: // right
            sideSink->BeginFigure(D2D1::Point2F(r, t + borders.radius.top_right_x), D2D1_FIGURE_BEGIN_HOLLOW);
            sideSink->AddLine(D2D1::Point2F(r, b - borders.radius.bottom_right_x));
            break;
        case 2: // bottom
            sideSink->BeginFigure(D2D1::Point2F(r - borders.radius.bottom_right_x, b), D2D1_FIGURE_BEGIN_HOLLOW);
            sideSink->AddLine(D2D1::Point2F(l + borders.radius.bottom_left_x, b));
            break;
        case 3: // left
            sideSink->BeginFigure(D2D1::Point2F(l, b - borders.radius.bottom_left_x), D2D1_FIGURE_BEGIN_HOLLOW);
            sideSink->AddLine(D2D1::Point2F(l, t + borders.radius.top_left_x));
            break;
        }
        sideSink->EndFigure(D2D1_FIGURE_END_OPEN);
        sideSink->Close();

        // 描边
        rt->DrawGeometry(sidePath.Get(), brush.Get(), side.width);
    }
}
// 工具：转小写
std::wstring  D2DBackend::toLower(std::wstring s)
{
    std::transform(s.begin(), s.end(), s.begin(), towlower);
    return s;
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
                out.emplace_back(face);
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
        out.emplace_back(face);
    }
    return out;
};

void D2DBackend::make_font_metrics(const ComPtr<IDWriteFont>& dwFont,
    const litehtml::font_description& descr,
    litehtml::font_metrics* fm)
{
  
    DWRITE_FONT_METRICS fm0 = {};
    dwFont->GetMetrics(&fm0);

    float dip = static_cast<float>(descr.size) / fm0.designUnitsPerEm;

    fm->font_size = descr.size;

    
    fm->ascent = fm0.ascent * dip;
    fm->descent = fm0.descent * dip;

    fm->height = (fm0.ascent + fm0.descent + fm0.lineGap) * dip;
    fm->x_height = fm0.xHeight * dip ;

    //float contentHeight = (fm0.ascent + fm0.descent) * dip;
    //float leading = fm->height - contentHeight;
    //fm->ascent = fm0.ascent * dip + leading * 0.5f;
    //fm->descent = fm->height - fm->ascent;   // 关键！
    // 1. 等宽数字 0 的宽度
    //ComPtr<IDWriteTextLayout> tmpLayout;
    //std::wstring zero = L"0";
    //m_dwrite->CreateTextLayout(zero.c_str(), 1, fmt.Get(),
    //    65536.0f, 65536.0f, &tmpLayout);
    //DWRITE_TEXT_METRICS tm = {};
    //if (tmpLayout) tmpLayout->GetMetrics(&tm);
    //fm->ch_width = static_cast<int>(tm.widthIncludingTrailingWhitespace + 0.5f);
    //if (fm->ch_width <= 0) fm->ch_width = fm->font_size * 3 / 5; // 兜底

    // 2. 上下标偏移：简单取 x-height 的 1/2
    fm->draw_spaces = descr.style == litehtml::font_style_italic || descr.decoration_line != litehtml::text_decoration_line_none;
    fm->sub_shift = fm->x_height / 2;
    fm->super_shift = fm->x_height / 2;
 
    // 3. 修正 baseline
    g_line_height = fm->height;
}
litehtml::uint_ptr D2DBackend::create_font(const litehtml::font_description& descr,
    const litehtml::document* doc,
    litehtml::font_metrics* fm)
{

    if (!m_dwrite || !fm) return 0;

    /*----------------------------------------------------------
      1. 把 font-family 字符串拆成单个字体名
    ----------------------------------------------------------*/

    std::vector<std::wstring> faces = split_font_list(
        descr.family.empty() ? g_cfg.default_font_name : descr.family);

    // 默认字体兜底
    faces.push_back(a2w(g_cfg.default_font_name));

    FontCachePair fcp;
    for (auto f : faces)
    {
        fcp = m_fontCache.get(f, descr, m_sysFontColl.Get());
        fcp.fmt;
        if (fcp.fmt) break;
        else
        {
            std::wstring txt = L"[DWrite] 加载字体失败： " + f + L"\n";
            OutputDebugStringW(txt.c_str());
        }
    }


    if (!fcp.fmt) {
        OutputDebugStringW(L"[DWrite] 加载默认字体失败\n");
        return 0;
    }

    *fm = fcp.fm;
    g_line_height = fm->height;
    FontPair* fp = new FontPair{ fcp.fmt, descr };

    return reinterpret_cast<litehtml::uint_ptr>(fp);
}

void D2DBackend::delete_font(litehtml::uint_ptr h)
{
    if (!h) return;
    //auto* fp = reinterpret_cast<FontPair*>(h);
    //delete fp;              // 4. 真正释放
}

litehtml::pixel_t D2DBackend::text_width(const char* text,
    litehtml::uint_ptr hFont)
{
    if (!text || !*text || !hFont) return 0;

    FontPair* fp = reinterpret_cast<FontPair*>(hFont);
    if (!fp || !fp->format) return 0;

    std::wstring wtxt = a2w(text);
    if (wtxt.empty()) return 0;

    // 1. 创建 TextLayout
 
    float maxW = 8192.0f;
    auto layout = getLayout(wtxt, fp, maxW);
    if (!layout) { return 0; }
    // 3. 取逻辑宽度（已含空白、连字、kerning）
    DWRITE_TEXT_METRICS tm{};
    HRESULT hr = layout->GetMetrics(&tm);
    if (FAILED(hr)) { return 0; }

    // 4. DPI → 物理像素（Win7 也支持）
    float dpi = 96.0f;               // 若你有当前 DPI，替换之
    float physical = tm.widthIncludingTrailingWhitespace * dpi / 96.0f;

    return physical;
}


void D2DBackend::build_rounded_rect_path(
    ComPtr<ID2D1GeometrySink>& sink,
    const litehtml::position& pos,
    const litehtml::border_radiuses& bdr)
{
    float l = float(pos.left()), t = float(pos.top());
    float r = float(pos.right()), b = float(pos.bottom());

    float rtl = float(bdr.top_left_x);
    float rtr = float(bdr.top_right_x);
    float rbr = float(bdr.bottom_right_x);
    float rbl = float(bdr.bottom_left_x);

    sink->BeginFigure(D2D1::Point2F(l + rtl, t), D2D1_FIGURE_BEGIN_FILLED);

    // top edge
    sink->AddLine(D2D1::Point2F(r - rtr, t));
    if (rtr > 0) sink->AddArc(D2D1::ArcSegment(
        D2D1::Point2F(r, t + rtr), D2D1::SizeF(rtr, rtr),
        0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));

    // right edge
    sink->AddLine(D2D1::Point2F(r, b - rbr));
    if (rbr > 0) sink->AddArc(D2D1::ArcSegment(
        D2D1::Point2F(r - rbr, b), D2D1::SizeF(rbr, rbr),
        0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));

    // bottom edge
    sink->AddLine(D2D1::Point2F(l + rbl, b));
    if (rbl > 0) sink->AddArc(D2D1::ArcSegment(
        D2D1::Point2F(l, b - rbl), D2D1::SizeF(rbl, rbl),
        0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));

    // left edge
    sink->AddLine(D2D1::Point2F(l, t + rtl));
    if (rtl > 0) sink->AddArc(D2D1::ArcSegment(
        D2D1::Point2F(l + rtl, t), D2D1::SizeF(rtl, rtl),
        0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));

    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
}

void D2DBackend::set_clip(const litehtml::position& pos,
    const litehtml::border_radiuses& bdr)
{
    //if (!m_rt) return;

    //// 无圆角 → 矩形裁剪
    //if (is_all_zero(bdr))
    //{
    //    m_rt->PushAxisAlignedClip(
    //        D2D1::RectF(float(pos.left()), float(pos.top()),
    //            float(pos.right()), float(pos.bottom())),
    //        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    //    m_clipStack.emplace_back(nullptr);          // 标记为矩形
    //    return;
    //}

    //// 有圆角 → 用 PathGeometry + Layer
    //ComPtr<ID2D1Factory> factory;
    //m_rt->GetFactory(&factory);

    //ComPtr<ID2D1PathGeometry> path;
    //factory->CreatePathGeometry(&path);
    //ComPtr<ID2D1GeometrySink> sink;
    //path->Open(&sink);
    //build_rounded_rect_path(sink, pos, bdr);        // 见下
    //sink->Close();

    //ComPtr<ID2D1Layer> layer;
    //if (SUCCEEDED(m_rt->CreateLayer(nullptr, &layer)))
    //{
    //    m_rt->PushLayer(
    //        D2D1::LayerParameters(D2D1::InfiniteRect(), path.Get()),
    //        layer.Get());
    //    m_clipStack.emplace_back(std::move(layer));
    //}
}

void D2DBackend::del_clip()
{
    //if (m_clipStack.empty()) return;
    //if (m_clipStack.back())
    //    m_rt->PopLayer();           // 圆角
    //else
    //    m_rt->PopAxisAlignedClip(); // 矩形
    //m_clipStack.pop_back();
}

// GDI backend
GdiCanvas::GdiCanvas(int w, int h) : m_w(w), m_h(h)
{

    // 创建后端
    m_backend = std::make_unique<GdiBackend>(m_w, m_h);
}

GdiCanvas::~GdiCanvas()
{
    if (m_old) SelectObject(m_memDC, m_old);
    if (m_bmp) DeleteObject(m_bmp);
    if (m_memDC) DeleteDC(m_memDC);
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

void GdiBackend::draw_image(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const std::string& url, const std::string& base_url)
{
}

void GdiBackend::draw_solid_fill(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::web_color& color)
{
}

void GdiBackend::draw_linear_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::background_layer::linear_gradient& gradient)
{
}

void GdiBackend::draw_radial_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::background_layer::radial_gradient& gradient)
{
}

void GdiBackend::draw_conic_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::background_layer::conic_gradient& gradient)
{
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

void GdiBackend::draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker& marker)
{
}

litehtml::uint_ptr GdiBackend::create_font(const litehtml::font_description& descr, const litehtml::document* doc, litehtml::font_metrics* fm)
{
    return litehtml::uint_ptr();
}



void GdiBackend::delete_font(litehtml::uint_ptr h)
{
    if (h) {
        GdiFont* f = reinterpret_cast<GdiFont*>(h);
        DeleteObject(f->hFont);
        delete f;
    }
}




litehtml::pixel_t GdiBackend::text_width(const char* text, litehtml::uint_ptr hFont)
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
void GdiBackend::set_clip(const litehtml::position& pos, const litehtml::border_radiuses& bdr_radius)
{
}
void GdiBackend::del_clip()
{
}


void EPUBBook::load_all_fonts() {

        //auto fonts = collect_epub_fonts();
        FontKey key{ L"serif", 400, false, 0 };
        m_fontBin[key] = { g_cfg.default_serif };
        key = { L"sans-serif", 400, false, 0 };
        m_fontBin[key] = { g_cfg.default_sans_serif };
        key = { L"monospace", 400, false, 0 };
        m_fontBin[key] = { g_cfg.default_monospace };
        build_epub_font_index(g_book->ocf_pkg_, g_book.get());

        //g_container->m_backend->load_all_fonts(fonts);
 
}



/* ---------- 1. 静态工厂 ---------- */

void SimpleContainer::makeBackend()
{

    switch (g_cfg.fontRenderer) {
    case Renderer::GDI: {
        m_backend = std::make_unique<GdiBackend>(0, 0);
        break;
    }
    case Renderer::D2D: {
        m_backend =  std::make_unique<D2DBackend>();
        break;
    }

    }
}





//------------------------------------------
// 公共辅助：从 EPUB 提取字体 blob
//------------------------------------------

static std::wstring make_safe_filename(std::wstring_view src)
{
    // 1. 去掉路径，只保留纯文件名
    size_t last = src.find_last_of(L"/\\");
    std::wstring name = (last == std::wstring::npos)
        ? std::wstring{ src }
    : std::wstring{ src.substr(last + 1) };

    // 2. 去掉前后空格/句点
    const std::wregex trim_re(L"^[ \\.]+|[ \\.]+$");
    name = std::regex_replace(name, trim_re, L"");

    // 3. 替换非法字符为单个下划线
    const std::wregex illegal_re(L"[<>:\"/\\\\|?*\\x00-\\x1F]+");
    name = std::regex_replace(name, illegal_re, L"_");

    // 4. 处理 Windows 保留设备名
    const std::wregex reserved_re(
        L"^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\\..*)?$",
        std::regex_constants::icase);
    if (std::regex_match(name, reserved_re))
        name = L"_" + name;

    // 5. 空文件名兜底
    if (name.empty())
        name = L"file";

    // 6. 拼上序号
    return  name;
}


namespace fs = std::filesystem;

// 大小写不敏感的 set / map 比较器
struct CaseInsensitiveLess
{
    bool operator()(const std::wstring& a, const std::wstring& b) const
    {
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    }
};

inline int hex_to_int(wchar_t c)
{
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    return 0;   // 非法字符按 0 处理
}
// 简单 URL decode（仅处理 %20 等）
std::wstring url_decode(const std::wstring& in)
{
    std::wstring out;
    for (size_t i = 0; i < in.size(); ++i)
    {
        if (in[i] == L'%' && i + 2 < in.size())
        {
            int hi = hex_to_int(in[i + 1]);
            int lo = hex_to_int(in[i + 2]);
            out.push_back(static_cast<wchar_t>((hi << 4) | lo));
            i += 2;
        }
        else
            out.push_back(in[i]);
    }
    return out;
}



//std::wstring EPUBBook::get_font_family_name(const std::vector<uint8_t>& data)
//{
//    stbtt_fontinfo info;
//
//    if (!stbtt_InitFont(&info, data.data(), 0))
//        return L"";
//
//    struct Try {
//        int platform, encoding, language, nameID;
//    } tries[] = {
//        // 先取 Typographic Family (ID 16)
//        {STBTT_PLATFORM_ID_MICROSOFT, STBTT_MS_EID_UNICODE_BMP,
//         STBTT_MS_LANG_ENGLISH, 16},
//         // 再取 Family (ID 1)
//         {STBTT_PLATFORM_ID_MICROSOFT, STBTT_MS_EID_UNICODE_BMP,
//          STBTT_MS_LANG_ENGLISH, 1},
//          // mac 平台兜底
//          {STBTT_PLATFORM_ID_MAC, STBTT_MAC_EID_ROMAN, 0, 1},
//    };
//
//    for (const auto& t : tries) {
//        int len = 0;
//        const char* p = (const char*)stbtt_GetFontNameString(
//            &info, &len, t.platform, t.encoding, t.language, t.nameID);
//        if (p && len > 0) {
//            std::wstring name;
//            name.reserve(len / 2);
//            for (int i = 0; i < len; i += 2) {
//                wchar_t ch = (wchar_t(p[i]) << 8) | wchar_t(p[i + 1]);
//                if (ch == 0) break;          // 保险：遇到 NUL 终止
//                name.push_back(ch);
//            }
//            if (!name.empty())
//                return name;
//        }
//    }
//    return L"";
//}


// 生成临时目录，返回路径（带反斜杠）
static std::wstring make_temp_dir()
{
    wchar_t tmp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"epub_fonts\\";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

void EPUBBook::build_epub_font_index(const OCFPackage& pkg, EPUBBook* book)
{
    if (!book) return;

    // 1. 创建临时目录
    static std::wstring tempDir = make_temp_dir();
    int fontSerial = 0;          // 重命名计数器

    // 2. 正则
    const std::wregex rx_face(LR"(@font-face\s*\{([^}]*)\})", std::regex::icase);
    const std::wregex rx_fam(LR"(font-family\s*:\s*['"]?([^;'"}]+)['"]?)", std::regex::icase);
    const std::wregex rx_url(LR"(url\s*\(\s*['"]?([^)'"]+)['"]?\s*\))", std::regex::icase);
    const std::wregex rx_loc(LR"(local\s*\(\s*['"]?([^)'"]+)['"]?\s*\))", std::regex::icase);
    const std::wregex rx_w(LR"(font-weight\s*:\s*(\d+|bold))", std::regex::icase);
    const std::wregex rx_i(LR"(font-style\s*:\s*(italic|oblique))", std::regex::icase);

    // 3. 遍历所有 CSS
    for (const auto& item : pkg.manifest)
    {
        if (item.media_type != L"text/css") continue;

        std::wstring cssPath = pkg.opf_dir + item.href;
        MemFile cssFile = book->read_zip(book->m_zipIndex.find(cssPath).c_str());
        if (cssFile.data.empty()) continue;
       
        std::wstring css = a2w({ (char*)cssFile.data.data(), cssFile.data.size() });

        for (std::wsregex_iterator it(css.begin(), css.end(), rx_face), end; it != end; ++it)
        {
            std::wstring block = it->str();
            std::wsmatch m;

            std::wstring family;
            std::vector<std::wstring> paths;   // 可能多个 src
            int weight = 400;
            bool italic = false;

            // family
            if (std::regex_search(block, m, rx_fam)) family = m[1];

            // weight / style
            if (std::regex_search(block, m, rx_w))
                weight = (m[1] == L"bold" || m[1] == L"700") ? 700 : std::stoi(m[1]);
            if (std::regex_search(block, m, rx_i)) italic = true;

            // 解析 src 中所有 url(...) 和 local(...)
            for (std::wsregex_iterator srcIt(block.begin(), block.end(), rx_url), srcEnd; srcIt != srcEnd; ++srcIt)
            {
                std::wstring url = (*srcIt)[1];

                // 跳过网络字体
                if (url.starts_with(L"http://") || url.starts_with(L"https://"))
                    continue;

                // 去掉 query/fragment
                if (auto pos = url.find(L'?'); pos != std::wstring::npos) url.erase(pos);
                if (auto pos = url.find(L'#'); pos != std::wstring::npos) url.erase(pos);

                // 保留扩展名
                std::wstring ext = L".ttf";
                if (auto dot = url.rfind(L'.'); dot != std::wstring::npos)
                {
                    ext = url.substr(dot);
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                    static const std::unordered_set<std::wstring> ok{ L".ttf", L".otf", L".woff", L".woff2", L".ttc" };
                    if (!ok.contains(ext)) ext = L".ttf";
                }

                // 解压
                std::wstring fontPath = pkg.opf_dir + url;
                MemFile fontFile = book->read_zip(book->m_zipIndex.find(fontPath).c_str());
                if (fontFile.data.empty()) continue;

                std::wstring tempFont = tempDir + std::to_wstring(fontSerial++) + ext;
                HANDLE h = CreateFileW(tempFont.c_str(), GENERIC_WRITE, 0, nullptr,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                DWORD written = 0;
                WriteFile(h, fontFile.data.data(), (DWORD)fontFile.data.size(), &written, nullptr);
                CloseHandle(h);

                paths.push_back(tempFont);
            }

            // local(...)
            for (std::wsregex_iterator locIt(block.begin(), block.end(), rx_loc), locEnd; locEnd != locIt; ++locIt)
            {
                paths.push_back((*locIt)[1]);
            }

            if (family.empty() || paths.empty()) continue;

            FontKey key{ family, weight, italic, 0 };
            book->m_fontBin[key] = std::move(paths);
        }
    }
}
// 主函数 ------------------------------------------------------------
//void EPUBBook::build_epub_font_index(const OCFPackage& pkg, EPUBBook* book)
//{
//    if (!book) return;
//
//    const std::regex rx_face(R"(@font-face\s*\{([^}]*)\})", std::regex::icase);
//    const std::regex rx_fam(R"(font-family\s*:\s*['"]?([^;'"}]+)['"]?)", std::regex::icase);
//    const std::regex rx_src(R"(src\s*:\s*url\s*\(\s*['"]?([^)'"]+)['"]?\s*\))", std::regex::icase);
//    const std::regex rx_w(R"(font-weight\s*:\s*(\d+|bold))", std::regex::icase);
//    const std::regex rx_i(R"(font-style\s*:\s*(italic|oblique))", std::regex::icase);
//
//    for (const auto& item : pkg.manifest)
//    {
//        if (item.media_type != L"text/css") continue;
//
//        // 1. 读 CSS
//        std::wstring css_path = pkg.opf_dir + item.href;
//        MemFile css_file = book->read_zip(book->m_zipIndex.find(css_path).c_str());
//        if (css_file.data.empty()) continue;
//
//        std::string css(css_file.data.begin(), css_file.data.end());
//
//        // 2. 遍历 @font-face
//        for (std::sregex_iterator it(css.begin(), css.end(), rx_face), end; it != end; ++it)
//        {
//            std::string block = it->str();
//            std::smatch m;
//
//            std::wstring family;
//            std::wstring url;
//            int weight = 400;
//            bool italic = false;
//
//            if (std::regex_search(block, m, rx_fam)) family = a2w(m[1].str().c_str());
//            if (std::regex_search(block, m, rx_src)) url = a2w(m[1].str().c_str());
//            if (std::regex_search(block, m, rx_w))   weight = (m[1] == "bold" || m[1] == "700") ? 700 : 400;
//            if (std::regex_search(block, m, rx_i)) italic = true;
//
//            if (family.empty() || url.empty()) continue;
//
//            // 3. 读字体
//            std::wstring font_path = g_book->m_zipIndex.find(url);
//
//
//
//            MemFile font_file = book->read_zip(book->m_zipIndex.find(font_path).c_str());
//            if (font_file.data.empty()) continue;
//  
//  
//            std::wstring real_name = get_font_family_name(font_file.data);
//
//   
//                // 5. 填充索引
//            FontKey key{ family, weight, italic , 0 };
//            g_book->m_fontBin[key] = std::move(real_name);
//        }
//    }
//}



std::vector<std::pair<std::wstring, std::vector<uint8_t>>>
EPUBBook::collect_epub_fonts()
{
    std::vector<std::pair<std::wstring, std::vector<uint8_t>>> fonts;
    const std::set<std::wstring, CaseInsensitiveLess> font_exts = {
        L".ttf", L".otf", L".woff", L".woff2"
    };
    std::unordered_set<std::wstring> used_names;

    for (const auto& item : g_book->ocf_pkg_.manifest)
    {
        std::wstring href = item.href;
        std::wstring ext;               // 最终扩展名，带前导点
        size_t lastDot = href.find_last_of(L'.');
        if (lastDot != std::wstring::npos && lastDot + 1 < href.size())
        {
            ext = href.substr(lastDot);   // 包含点
            std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        }
        else
        {
            continue;   // 没有扩展名，跳过
        }
        if (!font_exts.count(ext)) continue;

        std::wstring href_key = url_decode(item.href);
        std::wstring wpath = g_book->m_zipIndex.find(href_key);
        if (wpath.empty()) continue;

        // 生成唯一文件名（不再用 index 参数）
        std::wstring base = href;
        std::wstring safe = make_safe_filename(base);   // 现在只接受一个参数
        std::wstring unique = safe;
        int counter = 1;
        while (used_names.count(unique))
        {
            fs::path tmp(unique);
            unique = tmp.stem().wstring() + L"_" + std::to_wstring(counter++) + tmp.extension().wstring();
        }
        used_names.insert(unique);

        try
        {
            MemFile mf = g_book->read_zip(wpath.c_str());
            if (!mf.data.empty())
                fonts.emplace_back(std::move(unique), std::move(mf.data));
        }
        catch (...) { /* log & skip */ }
    }
    return fonts;
}
//------------------------------------------
// 3.1  GDI 实现
//------------------------------------------
void GdiBackend::load_all_fonts(std::vector<std::pair<std::wstring, std::vector<uint8_t>>>& fonts)
{

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
void D2DBackend::load_all_fonts(std::vector<std::pair<std::wstring, std::vector<uint8_t>>>& fonts)
{
    if (fonts.empty()) return;

    // 先清理旧字体
    //unload_fonts();

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

                    OutputDebugStringW(std::format(L"[DWrite] 加载字体: {} ", name).c_str());
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
    //make_tooltip_backend();
    if (g_cfg.enableJS) { enableJS(); }
    if (!g_container) {
        g_container = std::make_shared<SimpleContainer>();
    }
    if (!g_book){ g_book = std::make_unique<EPUBBook>(); }
    if (!g_vd){g_vd = std::make_unique<VirtualDoc>();}
    if(!g_recorder){g_recorder = std::make_unique<ReadingRecorder>();}
    if (!g_toc) 
    { 
        g_toc = std::make_unique<TocPanel>(); 
        g_toc->GetWindow(g_hwndToc);
    }
    if (!g_canvas) { g_canvas = std::make_unique<D2DCanvas>(10, 10, g_hView); }
    if(!g_tooltip_canvas){ g_tooltip_canvas = std::make_unique<D2DCanvas>(10, 10, g_hTooltip); }
    if(!g_imageview_canvas){ g_imageview_canvas = std::make_unique<D2DCanvas>(10, 10, g_hImageview); }
    if(!g_scrollbar) 
    {
        g_scrollbar = std::make_unique<ScrollBarEx>();
        g_scrollbar->GetWindow(g_hViewScroll);
    }
    // 绑定目录点击 -> 章节跳转
    g_toc->SetOnNavigate([](const std::wstring& href) {
        g_book->OnTreeSelChanged(href.c_str());
        });
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

litehtml::uint_ptr D2DCanvas::getContext() { return reinterpret_cast<litehtml::uint_ptr>(m_rt.Get()); }




void GdiCanvas::resize(int width, int height) {
    m_w = width;
    m_h = height;
}


void D2DCanvas::resize(int w, int h)
{
    if (w <= 0 || h <= 0) return;

    m_w = w;
    m_h = h;

    if (m_rt) {
        D2D1_SIZE_U size{ static_cast<UINT32>(w), static_cast<UINT32>(h) };
        if (SUCCEEDED(m_rt->Resize(size))) return;   // DPI 不变时直接 Resize
        m_rt.Reset();                               // 失败就重建
    }

    /* 重新创建 HwndRenderTarget */
    RECT rc; GetClientRect(m_hwnd, &rc);
    D2D1_RENDER_TARGET_PROPERTIES rtp =
        D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                D2D1_ALPHA_MODE_PREMULTIPLIED),
            0, 0, D2D1_RENDER_TARGET_USAGE_NONE,
            D2D1_FEATURE_LEVEL_DEFAULT);

    D2D1_HWND_RENDER_TARGET_PROPERTIES hrtp =
        D2D1::HwndRenderTargetProperties(
            m_hwnd,
            D2D1::SizeU(rc.right, rc.bottom),
            D2D1_PRESENT_OPTIONS_IMMEDIATELY);

    m_d2dFactory->CreateHwndRenderTarget(&rtp, &hrtp, &m_rt);
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






void AppBootstrap::enableJS()
{
    //if (!m_jsrt) m_jsrt = std::make_unique<js_runtime>(g_doc.get());
    //if (!m_jsrt->switch_engine("duktape"))
    //    OutputDebugStringA("[Duktape] Duktape init failed\n");
    //else {
    //    OutputDebugStringA("[Duktape] Duktape init OK\n");
    //    m_jsrt->set_logger(OutputDebugStringA);
    //    m_jsrt->eval("console.log('hello from duktape\n');");
    //}
}

void AppBootstrap::disableJS()
{
    //m_jsrt.reset();   // 直接销毁即可，js_runtime 会负责 shutdown
}

void AppBootstrap::run_pending_scripts()
{
    //    if (!m_jsrt) return;          // 没有 JS 引擎就跳过
    //    for (const auto& script : m_pending_scripts)
    //    {
    //        litehtml::string code;
    //        script.el->get_text(code);  // 取出 <script> 里的纯文本
    //        if (!code.empty())
    //            m_jsrt->eval(code, "<script>");  // 交给 QuickJS / Duktape / V8
    //    }
    //    m_pending_scripts.clear();    // 执行完清空
}

void AppBootstrap::bind_host_objects()
{
    //if (!m_jsrt) return;
   // m_jsrt->bind_document(g_doc.get());   // js_runtime 内部会转发到当前引擎
}





/* ---------- 实现 ---------- */

std::string EPUBBook::extract_anchor(const char* href)
{
    if (!href) return "";
    const char* p = std::strrchr(href, '#');
    return p ? (p + 1) : "";

}
litehtml::element::ptr EPUBBook::find_link_in_chain(litehtml::element::ptr start)
{
    for (auto cur = start; cur; cur = cur->parent())
    {
        const char* tag = cur->get_tagName();
        if (std::strcmp(tag, "p") == 0) break;
        if (std::strcmp(tag, "a") == 0) return cur;
    }
    return nullptr;
}

 bool EPUBBook::skip_attr(const std::string& val)
{
    if (val.empty()) return true;
    if (val == "0")  return true;
    return std::all_of(val.begin(), val.end(),
        [](unsigned char c) { return std::isspace(c); });
}

 std::string EPUBBook::get_html(litehtml::element::ptr el)
{
    if (!el) return "";
    std::string out;
    out += "<";
    out += el->get_tagName();

    // 常见属性名，按需增删
    static const char* attr_names[] = {
        "id","class","style","title","alt","href","src","type","name","value"
    };

    for (const char* name : attr_names)
    {
        const char* val = el->get_attr(name);
        if (val && !skip_attr(val))
        {
            out += " ";
            out += name;
            out += "=\"";
            out += val;
            out += "\"";
        }
    }

    out += ">";

    for (auto child : el->children())
    {
        if (child->is_text())
        {
            std::string txt;
            child->get_text(txt);
            out += txt;
        }
        else
        {
            out += get_html(child);
        }
    }

    out += "</";
    out += el->get_tagName();
    out += ">";
    return out;
}

 static void gumbo_serialize(const GumboNode* node, std::string& out)
 {
     if (node->type == GUMBO_NODE_TEXT)
     {
         out.append(node->v.text.text);
         return;
     }
     if (node->type != GUMBO_NODE_ELEMENT) return;

     const GumboElement& elem = node->v.element;
     out.push_back('<');
     out.append(gumbo_normalized_tagname(elem.tag));

     // 属性
     for (unsigned int i = 0; i < elem.attributes.length; ++i)
     {
         auto* attr = static_cast<GumboAttribute*>(elem.attributes.data[i]);
         out.append(" ").append(attr->name).append("=\"")
             .append(attr->value).append("\"");
     }

     if (elem.tag == GUMBO_TAG_IMG || elem.tag == GUMBO_TAG_BR)
     {
         // 自闭合
         out.append(" />");
     }
     else
     {
         out.push_back('>');
         for (unsigned int i = 0; i < elem.children.length; ++i)
             gumbo_serialize(static_cast<GumboNode*>(elem.children.data[i]), out);
         out.append("</").append(gumbo_normalized_tagname(elem.tag)).push_back('>');
     }
 }

 // 从一段 HTML 中提取第一个 <img> 的 outerHTML；没有则返回空串
 static std::string extract_first_img(const std::string& html)
 {
     GumboOutput* out = gumbo_parse(html.c_str());
     std::string result;

     // 深度优先找 <img>
     std::function<void(const GumboNode*)> dfs = [&](const GumboNode* node)
         {
             if (!result.empty()) return;           // 已找到
             if (node->type != GUMBO_NODE_ELEMENT) return;

             const GumboElement& elem = node->v.element;
             if (elem.tag == GUMBO_TAG_IMG)
             {
                 gumbo_serialize(node, result);
                 return;
             }
             for (unsigned int i = 0; i < elem.children.length; ++i)
                 dfs(static_cast<GumboNode*>(elem.children.data[i]));
         };
     dfs(out->root);

     gumbo_destroy_output(&kGumboDefaultOptions, out);
     return result;
 }
// 3. 核心函数：用 select_one 找 id，再向上找 <p>
std::string EPUBBook::html_of_anchor_paragraph(litehtml::document* doc, const std::string& anchorId)
{
    if (anchorId.empty()) return "";
    // 构造 CSS 选择器
    std::string sel = "[id=\"" + anchorId + "\"]";
    //std::string sel = "#" + anchorId;
    auto target = doc->root()->select_one(sel);
    if (!target) return "";

    // 向上找最近的 <p>
    auto p = target;
    while (p)
    {
        if (std::strcmp(p->get_tagName(), "figure") == 0) { break; }
        const char* cls = p->get_attr("class");
        if (!cls ||
            (std::strcmp(cls, "duokan-footnote-item") != 0 &&
                std::strcmp(cls, "fig") != 0 &&
                std::strcmp(cls, "figimage") != 0 &&
                std::strcmp(cls, "figure") != 0 ))
        {
            p = p->parent();
        }
        else
        {
            break;          // 找到目标 class
        }
    }

    if (!p) return "";          // 兜底：直接返回自身

    std::string inner = get_html(p);

    // 用 gumbo 处理
    std::string imgOnly = extract_first_img(inner);
    if (!imgOnly.empty())
    {
        return "<style>img{display:block;width:100%;height:auto;}</style>" + imgOnly;
    }
    else
    {
        return "<style>img{display:block;width:100%;height:auto;}</style>" + inner;
    }
}


std::string EPUBBook::get_html_of_image(litehtml::element::ptr start)
{
    if (!start && std::strcmp(start->get_tagName(), "img") != 0 ) { return ""; }
    std::string inner = get_html(start);
    return "<style>img{display:block;width:100%;height:auto;}</style>" + inner;
   
}
void EPUBBook::show_imageview(const litehtml::element::ptr& el)
{
    std::string html = get_html_of_image(el);
    if (html.empty()) { return; }
    POINT pt;
    GetCursorPos(&pt);   // pt.x, pt.y 为屏幕坐标
    //ScreenToClient(g_hView, &pt);   // 现在 pt.x, pt.y 是相对于窗口客户区的坐标


    g_imageview_canvas->m_doc = litehtml::document::createFromString(
        { html.c_str(), litehtml::encoding::utf_8 }, g_container.get());
    int width = g_cfg.tooltip_width;
    g_imageview_canvas->m_doc->render(width);

    int height = g_imageview_canvas->m_doc->height();
    auto tip_x = pt.x - width/2;
    auto tip_y = pt.y - height/2;


 
    DWORD style = GetWindowLong(g_hImageview, GWL_STYLE);
    DWORD exStyle = GetWindowLong(g_hImageview, GWL_EXSTYLE);
    UINT dpi = GetDpiForWindow(g_hImageview);
    RECT r{ 0, 0, width, height };
    AdjustWindowRectExForDpi(&r, style, FALSE, exStyle, dpi);
    g_imageview_canvas->resize(width, height);
    SetWindowPos(g_hImageview, HWND_TOPMOST,
        tip_x, tip_y,
        r.right - r.left, r.bottom - r.top,
        SWP_SHOWWINDOW | SWP_NOACTIVATE);



    g_states.isImageviewUpdate.store(true);
    InvalidateRect(g_hImageview, nullptr, true);
}
void EPUBBook::show_tooltip(const std::string txt)
{
    auto html = txt;
    //OutputDebugStringA(("[show_tooltip] " + std::to_string(x) + " " + std::to_string(y) + "\n").c_str());
    if (html.empty()) {  return; }

    //if (g_vd && !g_vd->m_blocks.empty())
    //{
    //    html = "<html>" + g_vd->m_blocks.back().head + "<body>" + html + "</body></html>";
    //}
    POINT pt;
    GetCursorPos(&pt);   




    g_tooltip_canvas->m_doc = litehtml::document::createFromString(
        { html.c_str(), litehtml::encoding::utf_8 }, g_container.get());
    int width = g_cfg.tooltip_width;
    g_tooltip_canvas->m_doc->render(width);
    int height = g_tooltip_canvas->m_doc->height();


    int tip_x = pt.x - width / 2;
    int tip_y = pt.y - height - 20;
    if (tip_y < 0) { tip_y = pt.y + 20; }
    DWORD style = GetWindowLong(g_hTooltip, GWL_STYLE);
    DWORD exStyle = GetWindowLong(g_hTooltip, GWL_EXSTYLE);
    UINT dpi = GetDpiForWindow(g_hTooltip);
    RECT r{ 0, 0, width, height };
    AdjustWindowRectExForDpi(&r, style, FALSE, exStyle, dpi);
    g_tooltip_canvas->resize(width, height);
    SetWindowPos(g_hTooltip, HWND_TOPMOST,
        tip_x, tip_y,
        r.right - r.left, r.bottom - r.top,
        SWP_SHOWWINDOW | SWP_NOACTIVATE);


    g_states.isTooltipUpdate.store(true);
    InvalidateRect(g_hTooltip, nullptr, true);

}
void EPUBBook::hide_imageview()
{

    if (g_hImageview && IsWindowVisible(g_hImageview))
    {
        ShowWindow(g_hImageview, SW_HIDE);
        if (g_imageview_canvas && g_imageview_canvas->m_doc)
        {
            g_imageview_canvas->m_doc.reset();
        }

    }
}
void EPUBBook::hide_tooltip()
{

    if (g_hTooltip  && IsWindowVisible(g_hTooltip) )
    {
        ShowWindow(g_hTooltip, SW_HIDE);
        if (g_tooltip_canvas && g_tooltip_canvas->m_doc)
        {
            g_tooltip_canvas->m_doc.reset();
        }

    }
}



// 把单个 element 序列化成 HTML
static void element_to_html(const litehtml::element::ptr& el,
    std::string& out)
{
    if (!el) return;

    if (el->is_text())
    {
        // 文本节点
        std::string txt;
        el->get_text(txt);
        out += txt;
        return;
    }

    // 开始标签
    out += "<";
    out += el->get_tagName();

    // 属性
    auto attrs = el->dump_get_attrs();
    for (const auto& [name, val] : attrs)
    {
        out += " " + name + "=\"" + val + "\"";
    }

    if (el->children().empty())
    {
        // 自闭合
        out += " />";
    }
    else
    {
        out += ">";

        // 子节点
        for (const auto& child : el->children())
            element_to_html(child, out);

        // 结束标签
        out += "</";
        out += el->get_tagName();
        out += ">";
    }
}

// 根据锚点 id 返回对应元素的 HTML
std::string EPUBBook::get_anchor_html(litehtml::document* doc,
    const std::string& anchor)
{
    if (!doc || anchor.empty()) return {};

    litehtml::element::ptr root = doc->root();
    if (!root) return {};

    litehtml::element::ptr el =
        root->select_one(("#" + anchor).c_str());
    if (!el) return {};

    std::string html;
    element_to_html(el, html);
    return html;
}














void EPUBBook::parse_ncx_points(tinyxml2::XMLElement* navPoint, int level,
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
            np.href = m_zipIndex.find(np.href);
        np.order = level;               // 层级深度
        out.emplace_back(std::move(np));

        // 递归子 <navPoint>
        parse_ncx_points(pt->FirstChildElement("navPoint"), level + 1, opf_dir, out);
    }
}

void EPUBBook::parse_nav_list(tinyxml2::XMLElement* ol, int level,
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
            np.href = m_zipIndex.find(np.href);
        np.order = level;               // 层级深度
        out.emplace_back(std::move(np));

        // 递归子 <ol>
        if (auto* sub = li->FirstChildElement("ol"))
            parse_nav_list(sub, level + 1, opf_dir, out);
    }
}

std::wstring EPUBBook::extract_text(const tinyxml2::XMLElement* a)
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



EPUBBook::~EPUBBook() {
    mz_zip_reader_end(&zip);

    //for (const auto& path : g_tempFontFiles)
    //{
    //    RemoveFontResourceExW(path.c_str(), FR_PRIVATE, 0);
    //    DeleteFileW(path.c_str());
    //}
    //g_tempFontFiles.clear();
}

GdiBackend::GdiBackend(int width, int height) : m_w(width), m_h(height) {    // 创建内存 DC 与 32-bit 位图
    //HDC screenDC = GetDC(nullptr);
    //m_memDC = CreateCompatibleDC(screenDC);
    //BITMAPINFO bi{};
    //bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    //bi.bmiHeader.biWidth = w;
    //bi.bmiHeader.biHeight = -h;   // top-down DIB
    //bi.bmiHeader.biPlanes = 1;
    //bi.bmiHeader.biBitCount = 32;
    //bi.bmiHeader.biCompression = BI_RGB;
    //m_bmp = CreateDIBSection(screenDC, &bi, DIB_RGB_COLORS, nullptr, nullptr, 0);
    //m_old = (HBITMAP)SelectObject(m_memDC, m_bmp);
    //ReleaseDC(nullptr, screenDC);
}

GdiBackend::~GdiBackend() {
    if (!g_tempFontFiles.empty()) {
        for (const auto& p : g_tempFontFiles)
        {
            RemoveFontResourceExW(p.c_str(), FR_PRIVATE, 0);
            DeleteFileW(p.c_str());
        }
        g_tempFontFiles.clear();
    }
}

SimpleContainer::SimpleContainer()
 {
    makeBackend(); 
    init_dpi();
}

SimpleContainer::~SimpleContainer()
{
    clear();
}



ZipIndexW::ZipIndexW(mz_zip_archive& zip)
{
    build(zip);
}

std::wstring ZipIndexW::find(std::wstring href) const {
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


std::wstring ZipIndexW::url_decode(const std::wstring& in)
{
    wchar_t out[INTERNET_MAX_URL_LENGTH];
    DWORD len = INTERNET_MAX_URL_LENGTH;
    if (SUCCEEDED(UrlCanonicalizeW(in.c_str(), out, &len, URL_UNESCAPE)))
        return std::wstring(out, len);
    return in;
}

std::wstring ZipIndexW::normalize_key(std::wstring href)
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

void ZipIndexW::build(mz_zip_archive& zip) {
    mz_uint idx = 0;
    mz_zip_archive_file_stat st{};
    while (mz_zip_reader_file_stat(&zip, idx++, &st)) {
        // miniz 返回 UTF-8，转成 wstring
        std::wstring wpath = a2w(st.m_filename);
        std::wstring key = normalize_key(wpath);
        map_.emplace(std::move(key), std::move(wpath));
    }
}

void SimpleContainer::clear()
{

    m_img_cache.clear();

    m_anchor_map.clear();
    m_doc.reset();
    m_backend->clear();
}

litehtml::pixel_t SimpleContainer::get_default_font_size() const
{
    return g_cfg.default_font_size;
}
const char* SimpleContainer::get_default_font_name() const
{
    return g_cfg.default_font_name.c_str();
}

void EPUBBook::clear()
{



    mz_zip_reader_end(&zip);
    cache.clear();

    ocf_pkg_ = {};
    m_fontBin.clear();
    m_file_path = L"";
}

void D2DCanvas::clear()
{
    
}

void D2DBackend::clear()
{
    m_privateFonts.Reset();
    m_d2dBitmapCache.clear();
    m_clipStack.clear();
    m_fontCache.clear();
    //m_layoutCache.clear();
    m_brushPool.clear();
}

void GdiCanvas::clear() {}

void GdiBackend::clear() {}



// ---------- 实现 ----------
D2DBackend::D2DBackend()
{

    /* 4) DirectWrite 工厂 */
    IDWriteFactory* pRaw = nullptr;
    HRESULT hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(&pRaw));   // OK
    m_dwrite.Attach(pRaw);   // 把裸指针交给 ComPtr 管理
    if (FAILED(hr)) {
        OutputDebugStringA("DWriteCreateFactory failed\n");
        return;
    }
    m_dwrite->CreateTextAnalyzer(&m_analyzer);
    /* 5) 系统字体集合 */
    hr = m_dwrite->GetSystemFontCollection(&m_sysFontColl, FALSE);
    if (FAILED(hr)) {
        OutputDebugStringA("GetSystemFontCollection failed\n");
    }
}


FontCache::FontCache() {
    DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(m_dw.GetAddressOf()));   // 
    m_loader = new FileCollectionLoader();
    m_dw->RegisterFontCollectionLoader(m_loader);
}

/* ---------------------------------------------------------- */
FontCachePair
FontCache::get(std::wstring& familyName, const litehtml::font_description& descr,
     IDWriteFontCollection* sysColl) {
    // 1. 构造键
    FontKey key{
        
        std::wstring(familyName.begin(), familyName.end()),
        descr.weight,
        descr.style == litehtml::font_style_italic,
        descr.size
    };
    SetStatus(1, key.family.c_str());
    // 2. 读缓存
    {
        std::shared_lock sl(m_mtx);
        if (auto it = m_map.find(key); it != m_map.end())
            return it->second;
    }

    // 3. 未命中，创建并写入
    auto fcp = create(key, sysColl);
    {
        std::unique_lock ul(m_mtx);
        m_map[key] = fcp;          // 若并发重复，后写覆盖，无妨
    }
    return fcp;
}
 ComPtr<IDWriteFontCollection>
FontCache::CreatePrivateCollectionFromFile(IDWriteFactory* dw, const wchar_t* path)
{
     ComPtr<IDWriteFontFile> file;
     if (FAILED(dw->CreateFontFileReference(path, nullptr, &file)))
         return nullptr;
     BOOL isSupported = FALSE;
     DWRITE_FONT_FILE_TYPE fileType = DWRITE_FONT_FILE_TYPE_UNKNOWN;
     DWRITE_FONT_FACE_TYPE faceType = DWRITE_FONT_FACE_TYPE_UNKNOWN;
     UINT32  faceCount = 0;
     if (FAILED(file->Analyze(
                &isSupported,
                &fileType,
                &faceType,
                &faceCount)) || !isSupported || faceCount < 1)
     {
         return nullptr;
     }
     // 用系统自带的“文件集合加载器”
     ComPtr<IDWriteFontCollection> collection;
     IDWriteFontFile* files[] = { file.Get() };
     IDWriteFontFile* key[] = { file.Get() };
     if (FAILED(m_dw->CreateCustomFontCollection(
         m_loader,
         key,
         sizeof(key),
         &collection)))
         return nullptr;

     return collection;
}

 FontCachePair
     FontCache::create(const FontKey& key, IDWriteFontCollection* sysColl)
 {



     /* ---------- 1. 候选列表（路径优先） ---------- */
     std::vector<std::wstring_view> tryNames{ key.family };
     if (g_book)
     {
         FontKey exact{ key.family, key.weight, key.italic, 0 };
         if (auto it = g_book->m_fontBin.find(exact); it != g_book->m_fontBin.end())
             tryNames.insert(tryNames.end(), it->second.begin(), it->second.end());
         for (const auto& kv : g_book->m_fontBin)
             if (kv.first.family == key.family)
                 tryNames.insert(tryNames.end(), kv.second.begin(), kv.second.end());
     }

     /* ---------- 2. 工具：一次性生成 metrics ---------- */
     auto makeMetrics = [](IDWriteFont* font, float size) -> litehtml::font_metrics
         {
             DWRITE_FONT_METRICS m{};
             font->GetMetrics(&m);
             const float dip = size / static_cast<float>(m.designUnitsPerEm);
             litehtml::font_metrics fm{};
             fm.font_size = static_cast<litehtml::pixel_t>(size + 0.5f);
             fm.ascent = static_cast<litehtml::pixel_t>(m.ascent * dip + 0.5f);
             fm.descent = static_cast<litehtml::pixel_t>(m.descent * dip + 0.5f);
             fm.height = static_cast<litehtml::pixel_t>((m.ascent + m.descent + m.lineGap) * dip + 0.5f);
             fm.x_height = static_cast<litehtml::pixel_t>(m.xHeight * dip + 0.5f);
             fm.ch_width = fm.font_size * 3 / 5;
             fm.sub_shift = fm.super_shift = fm.x_height / 2;
  
             return fm;
         };

     /* ---------- 3. 路径字体（私有集合） ---------- */

     for (std::wstring_view name : tryNames)
     {
         if (name.find(L':') == std::wstring_view::npos) continue;
         auto& coll = collCache[name];
         if (!coll)
         {
             coll = CreatePrivateCollectionFromFile(m_dw.Get(), std::wstring(name).c_str());
             if(coll){ collCache.emplace(name, coll); }
         }
         if (!coll) continue;

         UINT32 familyCount = coll->GetFontFamilyCount();
         if (familyCount == 0) continue;
     
         ComPtr<IDWriteFontFamily> family;
         if (FAILED(coll->GetFontFamily(0, &family))) continue;

         ComPtr<IDWriteFont> font;
         if (FAILED(family->GetFirstMatchingFont(
             static_cast<DWRITE_FONT_WEIGHT>(key.weight),
             DWRITE_FONT_STRETCH_NORMAL,
             key.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
             &font))) continue;

         wchar_t familyName[LF_FACESIZE]{};
         {
             ComPtr<IDWriteLocalizedStrings> names;
             if (SUCCEEDED(family->GetFamilyNames(&names)))
             {
                 UINT32 idx = 0, len = 0;
                 BOOL exists = FALSE;
                 names->FindLocaleName(L"en-us", &idx, &exists);
                 if (!exists) idx = 0;
                 names->GetStringLength(idx, &len);
                 if (len < LF_FACESIZE)
                     names->GetString(idx, familyName, len + 1);
             }
         }

         ComPtr<IDWriteTextFormat> fmt;
         if (SUCCEEDED(m_dw->CreateTextFormat(
             familyName[0] ? familyName : L"",
             coll.Get(),
             static_cast<DWRITE_FONT_WEIGHT>(key.weight),
             key.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
             DWRITE_FONT_STRETCH_NORMAL,
             static_cast<float>(key.size), L"en-us", &fmt)))
         {
             FontCachePair result{ fmt, makeMetrics(font.Get(), static_cast<float>(key.size)) };
             return result;
         }
     }

     /* ---------- 4. 系统字体 ---------- */
     if (sysColl)
     {
         for (std::wstring_view name : tryNames)
         {
             UINT32 index = 0;
             BOOL exists = FALSE;
             sysColl->FindFamilyName(std::wstring(name).c_str(), &index, &exists);
             if (!exists) continue;

             ComPtr<IDWriteFontFamily> family;
             if (FAILED(sysColl->GetFontFamily(index, &family))) continue;

             ComPtr<IDWriteFont> font;
             if (FAILED(family->GetFirstMatchingFont(
                 static_cast<DWRITE_FONT_WEIGHT>(key.weight),
                 DWRITE_FONT_STRETCH_NORMAL,
                 key.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
                 &font))) continue;

             ComPtr<IDWriteTextFormat> fmt;
             if (SUCCEEDED(m_dw->CreateTextFormat(
                 std::wstring(name).c_str(), sysColl,
                 static_cast<DWRITE_FONT_WEIGHT>(key.weight),
                 key.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
                 DWRITE_FONT_STRETCH_NORMAL,
                 static_cast<float>(key.size), L"en-us", &fmt)))
             {
                 FontCachePair result{ fmt, makeMetrics(font.Get(), static_cast<float>(key.size)) };
                 return result;
             }
         }
     }

     /* ---------- 5. 失败 ---------- */
     return { nullptr, {} };
 }
/* ---------------------------------------------------------- */
//FontCachePair
//FontCache::create(const FontKey& key, IDWriteFontCollection* privateColl, IDWriteFontCollection* sysColl) {
//    // 候选家族列表：精确 → 仅 family → 默认
//    std::wstring tryName;
//    tryName = key.family;
//
//    // 若 g_book->m_fontBin 有映射，追加真实文件名
//    if (g_book) {
//        FontKey exact{ key.family, key.weight, key.italic, 0 }; // size 忽略
//        if (auto it = g_book->m_fontBin.find(exact); it != g_book->m_fontBin.end())
//            tryName = it->second;
//
//        // 退而求其次：仅 family
//        for (const auto& kv : g_book->m_fontBin)
//            if (kv.first.family == key.family) { tryName = kv.second; break; }
//    }
//
//
//    // 逐个尝试
//
//    for (auto coll : { privateColl, sysColl }) {   // 先私有，再系统
//        if (!coll) continue;
//        UINT32 index = 0;
//        Microsoft::WRL::ComPtr<IDWriteFontFamily> dwFamily;
//        if (!findFamily(coll, tryName, dwFamily, index)) continue;
//
//        Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
//        if (SUCCEEDED(m_dw->CreateTextFormat(
//            tryName.c_str(), coll,
//            static_cast<DWRITE_FONT_WEIGHT>(key.weight),
//            key.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
//            DWRITE_FONT_STRETCH_NORMAL,
//            static_cast<float>(key.size),
//            L"en-us",
//            &fmt))) 
//        {
//            ComPtr<IDWriteFont>  dwFont;
//            dwFamily->GetFirstMatchingFont(
//                static_cast<DWRITE_FONT_WEIGHT>(key.weight),
//                DWRITE_FONT_STRETCH_NORMAL,
//                key.italic == litehtml::font_style_italic ? DWRITE_FONT_STYLE_ITALIC
//                : DWRITE_FONT_STYLE_NORMAL,
//                &dwFont);
//            FontCachePair fcp;
//            fcp.fmt = fmt;
//            fcp.dwFont = dwFont;
//            return std::move(fcp);   // 成功
//        }
//    }
//
//    // 理论上不会走到这里，除非默认字体也失败
//    
//    return { nullptr, nullptr };
//    
//}

/* ---------------------------------------------------------- */
bool FontCache::findFamily(IDWriteFontCollection* coll,
    const std::wstring& target,
    Microsoft::WRL::ComPtr<IDWriteFontFamily>& family, 
    UINT32& index)
{
    // 1) 快路径：DWrite 自带
    //BOOL exists = FALSE;
    //if (SUCCEEDED(coll->FindFamilyName(target.c_str(), &index, &exists)) && exists)
    //    return true;

    // 2) 慢路径：逐 family 遍历
    UINT32 count = coll->GetFontFamilyCount();
    for (UINT32 i = 0; i < count; ++i)
    {
      
        if (FAILED(coll->GetFontFamily(i, &family)))
            continue;

        Microsoft::WRL::ComPtr<IDWriteLocalizedStrings> names;
        if (FAILED(family->GetFamilyNames(&names)))
            continue;

        UINT32 len = 0;
        if (FAILED(names->GetStringLength(0, &len)))
            continue;

        std::wstring buf(len + 1, L'\0');
        if (FAILED(names->GetString(0, buf.data(), len + 1)))
            continue;
        buf.resize(len);

        if (_wcsicmp(buf.c_str(), target.c_str()) == 0)
        {
            index = i;
            return true;
        }
    }
    return false;   // 真没找到
}

void FontCache::clear() {
    std::unique_lock ul(m_mtx);
    m_map.clear();          // ComPtr 归零，DWrite 对象随之释放
}











VirtualDoc::VirtualDoc()
{
}

VirtualDoc::~VirtualDoc()
{
    clear();

}

void VirtualDoc::load_book(std::shared_ptr<EPUBBook> book, std::shared_ptr<SimpleContainer> container, int render_width)
{
    m_book = book;
    m_container = container;

    m_spine = book->ocf_pkg_.spine;
}


// ---------- 分页 ----------
std::wstring VirtualDoc::get_href_by_id(int id)
{

    if (id < m_spine.size() && id >= 0)
    {
        return m_spine[id].href;
    }
    return L"";
}

int VirtualDoc::get_id_by_href(std::wstring& href)
{
    for (int i = 0; i < m_spine.size(); i++)
    {
        if (m_spine[i].href == href) {
            return i;
        }
    }
    return -1;
}

void VirtualDoc::merge_block(HtmlBlock& dst, HtmlBlock& src, bool isAddToBottom)
{

    dst.head = src.head;
    // 2. 把新的 body_blocks 追加到尾部
    if (isAddToBottom)
    {

        dst.body_blocks.insert(
            dst.body_blocks.end(),
            src.body_blocks.begin(),
            src.body_blocks.end());

    }
    // 追加到顶部
    else
    {
        dst.body_blocks.insert(
            dst.body_blocks.begin(),
            src.body_blocks.begin(),
            src.body_blocks.end());
    }
}


HtmlBlock VirtualDoc::get_html_block(std::string html, int spine_id)
{
    HtmlBlock block;
    block.spine_id = spine_id;
    block.head = get_head(html);
    block.body_blocks = get_body_blocks(html, spine_id);
    
    // 插入一段空白
    BodyBlock bi;
    bi.spine_id = spine_id;
    bi.height = 0;
    bi.html = "<div style = \"height:" + std::to_string(g_cfg.split_space_height) + "px; \"></div>";
    bi.block_id = block.body_blocks.back().block_id + 1;
    block.body_blocks.push_back(std::move(bi));

    return block;
}


// ---------- 工具：节点序列化 ----------
bool VirtualDoc::gumbo_tag_is_void(GumboTag tag)
{
    switch (tag)
    {
    case GUMBO_TAG_AREA:
    case GUMBO_TAG_BASE:
    case GUMBO_TAG_BR:
    case GUMBO_TAG_COL:
    case GUMBO_TAG_EMBED:
    case GUMBO_TAG_HR:
    case GUMBO_TAG_IMG:
    case GUMBO_TAG_INPUT:
    case GUMBO_TAG_LINK:
    case GUMBO_TAG_META:
    case GUMBO_TAG_PARAM:
    case GUMBO_TAG_SOURCE:
    case GUMBO_TAG_TRACK:
    case GUMBO_TAG_WBR:
        return true;
    default:
        return false;
    }
}

void VirtualDoc::serialize_element(const GumboElement& el, std::ostream& out) {
    out << '<' << gumbo_normalized_tagname(el.tag);
    for (unsigned int i = 0; i < el.attributes.length; ++i) {
        auto* attr = static_cast<GumboAttribute*>(el.attributes.data[i]);
        out << ' ' << attr->name << "=\"" << attr->value << '"';
    }
    if (el.children.length == 0 && gumbo_tag_is_void(el.tag)) {
        out << " />";
        return;
    }
    out << '>';
    for (unsigned int i = 0; i < el.children.length; ++i)
        serialize_node(static_cast<GumboNode*>(el.children.data[i]), out);
    out << "</" << gumbo_normalized_tagname(el.tag) << '>';
}

void VirtualDoc::serialize_node(const GumboNode* node, std::ostream& out) {
    if (!node) return;
    switch (node->type) {
    case GUMBO_NODE_TEXT:
    case GUMBO_NODE_CDATA:
        out << node->v.text.text;
        break;
    case GUMBO_NODE_WHITESPACE:
        out << node->v.text.text;
        break;
    case GUMBO_NODE_ELEMENT:
        serialize_element(node->v.element, out);
        break;
    default: break;
    }
}

// ---------- 1. 提取 <head> ----------
std::string VirtualDoc::get_head(std::string& html) {
    GumboOutput* out = gumbo_parse(html.c_str());
    std::string result;
    if (out->root->type == GUMBO_NODE_ELEMENT) {
        for (unsigned int i = 0; i < out->root->v.element.children.length; ++i) {
            auto* node = static_cast<GumboNode*>(out->root->v.element.children.data[i]);
            if (node->type == GUMBO_NODE_ELEMENT &&
                node->v.element.tag == GUMBO_TAG_HEAD) {
                std::ostringstream oss;
                serialize_element(node->v.element, oss);
                result = oss.str();
                break;
            }
        }
    }
    gumbo_destroy_output(&kGumboDefaultOptions, out);
    return result;
}

// ---------- 2. 切 <body> ----------
std::vector<BodyBlock> VirtualDoc::get_body_blocks(std::string& html,
    int spine_id,
    size_t max_chunk_bytes) {
    std::vector<BodyBlock> blocks;
    GumboOutput* out = gumbo_parse(html.c_str());
    GumboNode* body = nullptr;

    // 找到 body
    if (out->root->type == GUMBO_NODE_ELEMENT) {
        for (unsigned int i = 0; i < out->root->v.element.children.length; ++i) {
            auto* node = static_cast<GumboNode*>(out->root->v.element.children.data[i]);
            if (node->type == GUMBO_NODE_ELEMENT &&
                node->v.element.tag == GUMBO_TAG_BODY) {
                body = node;
                break;
            }
        }
    }
    if (!body) { gumbo_destroy_output(&kGumboDefaultOptions, out); return blocks; }

    // 收集 body 的直接子节点
    std::vector<const GumboNode*> nodes;
    auto& children = body->v.element.children;
    for (unsigned int i = 0; i < children.length; ++i)
        nodes.emplace_back(static_cast<GumboNode*>(children.data[i]));

    // 分块
    std::ostringstream current;
    size_t current_bytes = 0;
    int block_id = 0;

    auto flush = [&]() {
        if (current.str().empty()) return;
        BodyBlock bb;
        bb.spine_id = spine_id;
        bb.block_id = block_id++;
        bb.html = current.str();
        blocks.emplace_back(std::move(bb));
        current.str("");
        current.clear();
        current_bytes = 0;
        };

    for (const GumboNode* n : nodes) {
        std::ostringstream tmp;
        serialize_node(n, tmp);
        std::string frag = tmp.str();
        if (current_bytes + frag.size() > max_chunk_bytes && !current.str().empty())
            flush();
        current << frag;
        current_bytes += frag.size();
    }
    flush(); // 最后一块
    gumbo_destroy_output(&kGumboDefaultOptions, out);
    return blocks;
}

void VirtualDoc::load_html(std::wstring& href)
{

    auto id = get_id_by_href(href);
    if(id < 0)
    {
        OutputDebugStringW(href.c_str());
        OutputDebugStringW(L" 未找到\n");
        return ;
    }
 
    load_by_id(id, true);
    // 先渲染好加载的章节
    {
        std::string text;
        text += "<html>" + m_blocks.back().head + "<body>";
        for (auto hb : m_blocks)
        {
            for (auto b : hb.body_blocks)
            {
                text += b.html;
            }
        }

        text += "</body></html>";
        m_doc = litehtml::document::createFromString({ text.c_str(), litehtml::encoding::utf_8 }, m_container.get());
        m_doc->render(g_cfg.document_width);
        m_blocks.back().height = m_doc->height();
        m_height = m_doc->height();
    }

    //id -= 1;
    //load_by_id(m_top_block, id, false);
}

float VirtualDoc::get_height_by_id(int spine_id)
{
    for (auto b: m_blocks)
    {
        if(b.spine_id == spine_id)
        {
            return b.height;
        }
    }
    return 0;
}
void VirtualDoc::reload()
{
    if (m_blocks.empty()) return;
    if (g_canvas) { g_canvas->clear_selection(); }
    // 1. 记录当前滚动百分比
    ScrollPosition old = get_scroll_position();
    double percent = 0.0;
    auto old_height = get_height_by_id(old.spine_id);
    auto href = get_href_by_id(old.spine_id);
    if ( old_height > 0)          // 旧文档高度
        percent = double(old.offset) / old_height;

    // 2. 重新加载
    m_blocks.clear();
    load_html(href);
    UpdateCache();

    // 3. 把百分比换算成新的像素值
    int newOffset = static_cast<int>(std::round(percent * get_height_by_id(old.spine_id)));


    g_offsetY = newOffset;


    InvalidateRect(g_hView, nullptr, TRUE);
    UpdateWindow(g_hView);
}
bool VirtualDoc::load_by_id( int spine_id, bool isPushBack)
{
    std::wstring href = get_href_by_id(spine_id);
    if (href.empty()) { return false; }
    std::string html = m_book->load_html(href);
    if (html.empty()) { return false; }
    if (g_cfg.enableGlobalCSS) { html = insert_global_css(html); }
    html = inject_css(html);
    if (g_cfg.enablePreprocessHTML) { html = PreprocessHTML(html); }
    auto block = get_html_block(html, spine_id);
 
    if (isPushBack){ m_blocks.push_back(std::move(block)); }
    else
    { 
        m_blocks.emplace(m_blocks.begin(), std::move(block));
    }

    return true;
}

ScrollPosition VirtualDoc::get_scroll_position()
{
    ScrollPosition pos{};
    if (m_blocks.empty()) { return pos; }
    pos.offset = g_offsetY;
    for (auto hb: m_blocks)
    {
        pos.spine_id = hb.spine_id;
        pos.height = hb.height;
        if ((pos.offset - hb.height) < 0) {break; }
        pos.offset -= hb.height;
    }
    return pos;
}

litehtml::document::ptr VirtualDoc::get_doc(int client_h, int& scrollY, int& y_offset)
{
    if (!m_book || !m_container) { return nullptr; }


    y_offset += scrollY;
    //OutputDebugStringA(std::to_string(y_offset).c_str());
    //OutputDebugStringA("\n");
   
    if (y_offset < 0)
    {
        bool isOK = insert_prev_chapter();
        if (isOK) { g_states.needRelayout.store(true); }
        else  { y_offset = 0; }
    
    }
    if (y_offset > static_cast<int>(m_height - client_h))
    {
        bool isOK = insert_next_chapter();
        if (isOK) { g_states.needRelayout.store(true); }
        else { y_offset = std::min(static_cast<int>(m_height - client_h), y_offset); }
    }
    if(m_height < client_h * 3)
    {
        bool isOK = insert_next_chapter();
        if (isOK) { g_states.needRelayout.store(true); }
        else { y_offset = std::min(static_cast<int>(m_height - client_h), y_offset); }
    }

    if (g_states.needRelayout.exchange(false)) 
    {

        std::string text;
        text += "<html>" + m_blocks.back().head + "<body>";
        for (auto hb: m_blocks)
        {
            for (auto b: hb.body_blocks)
            {
                text += b.html;
            }
        }

        text += "</body></html>";
        m_doc = litehtml::document::createFromString({ text.c_str(), litehtml::encoding::utf_8 }, m_container.get());

        m_doc->render(g_cfg.document_width);
        m_height = get_height();
        if (m_blocks.size() == 1) { m_blocks.back().height = m_doc->height(); };
        if (m_blocks.size() > 1)
        {
            if (m_blocks.front().height == 0.0f)
            {
                float height = std::max(m_doc->height() - m_height, 0.0f);
                y_offset += height;
                m_blocks.front().height = height;
            }
            else if (m_blocks.back().height == 0.0f)
            {
                float height = std::max(m_doc->height() - m_height, 0.0f);
                
                m_blocks.back().height = height;
            }
        }


        
    }
    scrollY = 0;

    ScrollPosition p = get_scroll_position();

    m_current_id = p.spine_id;
    //std::wstring href = get_href_by_id(m_current_id);
    
    SendMessage(g_hViewScroll, SBM_SETPOSITION,
        p.spine_id, MAKELPARAM(p.height, p.offset));
        //g_toc->SetHighlightByHref(href);

    return m_doc;
}


float VirtualDoc::get_height()
{
    float height = 0.0f;
    if (m_blocks.empty()) { return height; }
    for (auto b: m_blocks)
    {
        height += b.height;
    }
    return height;
}
bool VirtualDoc::insert_prev_chapter()
{
    ScrollPosition p = get_scroll_position();
    auto id = p.spine_id - 1;
    if (exists(id)) { return true; }
    return load_by_id(id, false);
}
bool VirtualDoc::insert_next_chapter()
{
    ScrollPosition p = get_scroll_position();
    auto id = p.spine_id + 1;
    if (exists(id)) { return true; }

    return load_by_id(id, true);
}

bool VirtualDoc::exists(int spine_id)
{
    for (auto b: m_blocks)
    {
        if (b.spine_id == spine_id) { return true; }
    }
    return false;
}

void VirtualDoc::clear()
{
    m_blocks.clear();
    g_offsetY = 0;
    m_doc.reset();
}


/* ---------- 工具 ---------- */
static int64_t nowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

/* ---------- 构造/析构 ---------- */
ReadingRecorder::ReadingRecorder() 
{ 
    initDB(); 
    m_book_record = {};
    m_time_frag = {};
}
ReadingRecorder::~ReadingRecorder() 
{ 
    if (m_dbBook) sqlite3_close(m_dbBook); 
    if (m_dbTime) sqlite3_close(m_dbTime);
}

/* ---------- 初始化数据库 ---------- */
void ReadingRecorder::initDB() {
    namespace fs = std::filesystem;
    fs::create_directories("data");

    /* ---------- Books.db ---------- */
    if (sqlite3_open("data/Books.db", &m_dbBook) != SQLITE_OK)
        throw std::runtime_error("sqlite open failed");

    sqlite3_exec(m_dbBook, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS books(
            id               INTEGER PRIMARY KEY AUTOINCREMENT,
            path             TEXT UNIQUE,
            title            TEXT,
            author           TEXT,
            open_count       INTEGER DEFAULT 0,
            total_words      INTEGER DEFAULT 0,
            last_spine_id    INTEGER DEFAULT 0,
            last_offset      INTEGER DEFAULT 0,
            font_size        INTEGER DEFAULT 0,
            line_height_mul  REAL    DEFAULT 0.0,
            doc_width        INTEGER DEFAULT 0,
            total_time_s     INTEGER DEFAULT 0,
            first_open_us    INTEGER DEFAULT 0,
            last_open_us     INTEGER DEFAULT 0,
            enableCSS        INTEGER DEFAULT 1,
            enableJS         INTEGER DEFAULT 0,
            enableGlobalCSS  INTEGER DEFAULT 0,
            enablePreHTML    INTEGER DEFAULT 1,
            displayTOC       INTEGER DEFAULT 1,
            displayStatus    INTEGER DEFAULT 1,
            displayMenu      INTEGER DEFAULT 1,
            displayScroll    INTEGER DEFAULT 1
        );
    )";
    sqlite3_exec(m_dbBook, sql, nullptr, nullptr, nullptr);

    /* ---------- Time.db ---------- */
    if (sqlite3_open("data/Time.db", &m_dbTime) != SQLITE_OK)
        throw std::runtime_error("sqlite open Time.db failed");
    sqlite3_exec(m_dbTime, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    const char* sqlTime = R"(
        CREATE TABLE IF NOT EXISTS reading_time(
            id            INTEGER PRIMARY KEY AUTOINCREMENT,
            path     TEXT,
            title         TEXT,
            authors       TEXT,
            spine_id      INTEGER,
            current_chapter TEXT,
            start_time    REAL,
            end_time      REAL,
            duration      INTEGER
        );
    )";
    sqlite3_exec(m_dbTime, sqlTime, nullptr, nullptr, nullptr);
}

/* ---------- 打开书 ---------- */
void ReadingRecorder::openBook(const std::string absolutePath) {
    m_book_record = {};
    m_time_frag = {};
    BookRecord rec;
    rec.path = absolutePath;

    const char* select = "SELECT * FROM books WHERE path=?;";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(m_dbBook, select, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, absolutePath.c_str(), -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        // 已存在
        rec.id = sqlite3_column_int64(stmt, 0);
        auto colText = [](sqlite3_stmt* s, int idx) -> std::string {
            const char* p = reinterpret_cast<const char*>(sqlite3_column_text(s, idx));
            return p ? std::string(p, sqlite3_column_bytes(s, idx)) : "";
            };
        rec.title = colText(stmt, 2);
        rec.author = colText(stmt, 3);
        rec.openCount = sqlite3_column_int(stmt, 4);
        rec.totalWords = sqlite3_column_int(stmt, 5);
        rec.lastSpineId = sqlite3_column_int(stmt, 6);
        rec.lastOffset = sqlite3_column_int(stmt, 7);
        rec.fontSize = sqlite3_column_int(stmt, 8);
        rec.lineHeightMul = static_cast<float>(sqlite3_column_double(stmt, 9));
        rec.docWidth = sqlite3_column_int(stmt, 10);
        rec.totalTime = sqlite3_column_int(stmt, 11);
        rec.lastOpenTimestamp = sqlite3_column_int64(stmt, 13);
        rec.enableCSS = sqlite3_column_int(stmt, 14);
        rec.enableJS = sqlite3_column_int(stmt, 15);
        rec.enableGlobalCSS = sqlite3_column_int(stmt, 16);
        rec.enablePreHTML = sqlite3_column_int(stmt, 17);
        rec.displayTOC = sqlite3_column_int(stmt, 18);
        rec.displayStatus = sqlite3_column_int(stmt, 19);
        rec.displayMenu = sqlite3_column_int(stmt, 20);
        rec.displayScroll = sqlite3_column_int(stmt, 21);
    }
    else {
        // 新书：用当前 g_book 状态插入
        const char* insert = R"(
            INSERT INTO books(path, first_open_us, last_open_us)
            VALUES(?, ?, ?);
        )";
        sqlite3_prepare_v2(m_dbBook, insert, -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, absolutePath.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 2, nowUs());
        sqlite3_bind_int64(stmt, 3, nowUs());
        sqlite3_step(stmt);

        rec.id = sqlite3_last_insert_rowid(m_dbBook);

    }
    sqlite3_finalize(stmt);

    // 更新打开次数 & 最后打开时间
    const char* update = "UPDATE books SET open_count=open_count+1, last_open_us=? WHERE id=?;";
    sqlite3_prepare_v2(m_dbBook, update, -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, nowUs());
    sqlite3_bind_int64(stmt, 2, rec.id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    m_book_record =  std::move(rec);
}
int64_t ReadingRecorder::getTotalTime()
{
    const char* sql = "SELECT COALESCE(SUM(duration),0) FROM reading_time;";
    sqlite3_stmt* stmt = nullptr;
    int64_t totalUs = 0;

    if (sqlite3_prepare_v2(m_dbTime, sql, -1, &stmt, nullptr) == SQLITE_OK)
    {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            totalUs = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return totalUs / 1'000'000;   // 返回秒
}
/* ---------- 写入 ---------- */
void ReadingRecorder::flush() {
    if (m_book_record.id < 0) return;   // 无效记录

    flushBookRecord();
    flushTimeRecord();
}

void ReadingRecorder::flushBookRecord()
{
    auto& rec = m_book_record;
    const char* sql = R"(
        UPDATE books SET
            title           = ?,
            author          = ?,
            total_words     = ?,
            last_spine_id   = ?,
            last_offset     = ?,
            font_size       = ?,
            line_height_mul = ?,
            doc_width       = ?, 
            total_time_s    = ?,
            enableCSS       = ?,
            enableJS        = ?,
            enableGlobalCSS = ?,
            enablePreHTML   = ?,
            displayTOC      = ?,
            displayStatus   = ?,
            displayMenu     = ?,
            displayScroll   = ?
        WHERE id = ?;
    )";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(m_dbBook, sql, -1, &stmt, nullptr);

    sqlite3_bind_text(stmt, 1, rec.title.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, rec.author.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, rec.totalWords);
    sqlite3_bind_int(stmt, 4, rec.lastSpineId);
    sqlite3_bind_int(stmt, 5, rec.lastOffset);
    sqlite3_bind_int(stmt, 6, rec.fontSize);
    sqlite3_bind_double(stmt, 7, rec.lineHeightMul);
    sqlite3_bind_double(stmt, 8, rec.docWidth);
    sqlite3_bind_int(stmt, 9, rec.totalTime);
    sqlite3_bind_int(stmt, 10, rec.enableCSS);
    sqlite3_bind_int(stmt, 11, rec.enableJS);
    sqlite3_bind_int(stmt, 12, rec.enableGlobalCSS);
    sqlite3_bind_int(stmt, 13, rec.enablePreHTML);
    sqlite3_bind_int(stmt, 14, rec.displayTOC);
    sqlite3_bind_int(stmt, 15, rec.displayStatus);
    sqlite3_bind_int(stmt, 16, rec.displayMenu);
    sqlite3_bind_int(stmt, 17, rec.displayScroll);
    sqlite3_bind_int64(stmt, 18, rec.id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void ReadingRecorder::flushTimeRecord()
{
    if (m_time_frag.empty()) return;

    /* 0. 先把缓存拿出来，防止 flush 期间又被写入 */
    std::vector<timeFragment> batch = std::move(m_time_frag);
    m_time_frag.clear();                 // 立即清空原缓存

    /* 1. 按时间升序 */
    std::sort(batch.begin(), batch.end(),
        [](const timeFragment& a, const timeFragment& b)
        { return a.timestamp < b.timestamp; });

    /* 2. 事务开始 */
    char* err = nullptr;
    if (sqlite3_exec(m_dbTime, "BEGIN;", nullptr, nullptr, &err) != SQLITE_OK)
    {
        OutputDebugStringA(("BEGIN failed: " + std::string(err) + "\n").c_str());
        sqlite3_free(err);
        return;
    }

    constexpr int64_t MERGE_THRESHOLD_US = 2'000'000;

    for (const timeFragment& frag : batch)
    {
        /* 3. 查询最近一条 */
        const char* sqlSel = R"(
            SELECT id, end_time, duration
            FROM reading_time
            WHERE path = ? AND current_chapter = ? AND spine_id = ?
            ORDER BY end_time DESC
            LIMIT 1;
        )";
        sqlite3_stmt* sel = nullptr;
        if (sqlite3_prepare_v2(m_dbTime, sqlSel, -1, &sel, nullptr) != SQLITE_OK)
        {
            OutputDebugStringA(("prepare SELECT failed\n"));
            continue;
        }
        sqlite3_bind_text(sel, 1, frag.path.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(sel, 2, frag.chapter.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(sel, 3, frag.spine_id);

        bool merged = false;
        if (sqlite3_step(sel) == SQLITE_ROW)
        {
            int     oldId = sqlite3_column_int(sel, 0);
            int64_t oldEnd = sqlite3_column_int64(sel, 1);
            if (frag.timestamp - oldEnd <= MERGE_THRESHOLD_US)
            {
                const char* sqlUpd = R"(
                    UPDATE reading_time
                    SET end_time = ?,
                        duration = duration + (? - end_time)
                    WHERE id = ?;
                )";
                sqlite3_stmt* upd = nullptr;
                if (sqlite3_prepare_v2(m_dbTime, sqlUpd, -1, &upd, nullptr) == SQLITE_OK)
                {
                    sqlite3_bind_int64(upd, 1, frag.timestamp);
                    sqlite3_bind_int64(upd, 2, frag.timestamp);
                    sqlite3_bind_int(upd, 3, oldId);
                    if (sqlite3_step(upd) != SQLITE_DONE)
                        OutputDebugStringA(("UPDATE step failed\n"));
                    sqlite3_finalize(upd);
                }
                else
                {
                    OutputDebugStringA(("prepare UPDATE failed\n"));
                }
                merged = true;
            }
        }
        sqlite3_finalize(sel);

        if (!merged)
        {
            const char* sqlIns = R"(
                INSERT INTO reading_time
                (path, title, authors, spine_id, current_chapter,
                 start_time, end_time, duration)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?);
            )";
            sqlite3_stmt* ins = nullptr;
            if (sqlite3_prepare_v2(m_dbTime, sqlIns, -1, &ins, nullptr) == SQLITE_OK)
            {
                sqlite3_bind_text(ins, 1, frag.path.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_text(ins, 2, frag.title.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_text(ins, 3, frag.author.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_int(ins, 4, frag.spine_id);
                sqlite3_bind_text(ins, 5, frag.chapter.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_int64(ins, 6, frag.timestamp);
                sqlite3_bind_int64(ins, 7, frag.timestamp);
                sqlite3_bind_int64(ins, 8, 0);

                if (sqlite3_step(ins) != SQLITE_DONE)
                    OutputDebugStringA(("INSERT step failed\n"));
                sqlite3_finalize(ins);
            }
            else
            {
                OutputDebugStringA(("prepare INSERT failed\n"));
            }
        }
    }

    /* 4. 提交事务 */
    if (sqlite3_exec(m_dbTime, "COMMIT;", nullptr, nullptr, &err) != SQLITE_OK)
    {
        OutputDebugStringA(("COMMIT failed: " + std::string(err) + "\n").c_str());
        sqlite3_free(err);
    }
}
void CALLBACK OnFlush(UINT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR)
{
    // 直接在工作线程/回调里刷新
    if (g_recorder) { g_recorder->flush(); }
    g_flushTimer = 0;
}
void ReadingRecorder::updateRecord()
{
    if (m_book_record.id < 0) { return; }
 
    if (g_book)
    {
        m_book_record.displayMenu = g_cfg.displayMenuBar;
        m_book_record.displayScroll = g_cfg.displayScrollBar;
        m_book_record.displayStatus = g_cfg.displayStatusBar;
        m_book_record.displayTOC = g_cfg.displayTOC;

        m_book_record.enableCSS = g_cfg.enableCSS;
        m_book_record.enableGlobalCSS = g_cfg.enableGlobalCSS;
        m_book_record.enableJS = g_cfg.enableJS;
        m_book_record.enablePreHTML = g_cfg.enablePreprocessHTML;

        m_book_record.fontSize = g_cfg.default_font_size;
        m_book_record.lineHeightMul = g_cfg.line_height_multiplier;
        m_book_record.docWidth = g_cfg.document_width;
        m_book_record.totalTime += 1;


        if (m_book_record.title.empty())
        {
            auto titIt = g_book->ocf_pkg_.meta.find(L"dc:title");
            m_book_record.title = titIt != g_book->ocf_pkg_.meta.end() ? w2a(titIt->second) : "";
        }
        if(m_book_record.author.empty())
        {
            auto authIt = g_book->ocf_pkg_.meta.find(L"dc:creator");
            m_book_record.author = authIt != g_book->ocf_pkg_.meta.end() ? w2a(authIt->second) : "";
        }

        timeFragment tf;
        tf.path = m_book_record.path;
        tf.title = m_book_record.title;
        tf.author = m_book_record.author;
        tf.timestamp = nowUs();
        
        if (g_vd) {
            ScrollPosition p = g_vd->get_scroll_position();
            m_book_record.lastSpineId = p.spine_id;
            m_book_record.lastOffset = p.offset;
            
            tf.spine_id = p.spine_id;
            tf.chapter = w2a(g_book->get_chapter_name_by_id(p.spine_id));
        }
        m_time_frag.push_back(std::move(tf));
        if (!g_flushTimer)
        {
            g_flushTimer = timeSetEvent(g_cfg.record_flush_interval_ms, 0, OnFlush, 0, TIME_ONESHOT);
        }

    }
}

void TocPanel::clear()
{
    nodes_.clear();
    roots_.clear();
    vis_.clear();      // 可见行索引
    lineH_ = 20;
    scrollY_ = 0;
    totalH_ = 0;
    selLine_ = -1;

}

void TocPanel::GetWindow(HWND hwnd)
{
    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)this);
    hwnd_ = hwnd;
}


void TocPanel::Load(const std::vector<OCFNavPoint>& flat)
{
    // 复用你原来的 BuildTree 算法
    nodes_.clear();
    roots_.clear();
    nodes_.reserve(flat.size());
    std::vector<size_t> st;
    st.push_back(SIZE_MAX);
    for (const auto& np : flat)
    {
        while (st.size() > static_cast<size_t>(np.order + 1)) st.pop_back();
        size_t idx = nodes_.size();
        nodes_.push_back(Node{ &np });
        if (st.back() != SIZE_MAX)
            nodes_[st.back()].childIdx.push_back(idx);
        else
            roots_.push_back(idx);
        st.push_back(idx);
    }
    for (auto& n : nodes_) n.expanded = false;
    RebuildVisible();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

/* ---------- 内部实现 ---------- */
LRESULT CALLBACK TocPanel::WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_NCCREATE)
        SetWindowLongPtr(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCT*)l)->lpCreateParams);
    TocPanel* self = (TocPanel*)GetWindowLongPtr(h, GWLP_USERDATA);
    return self ? self->HandleMsg(m, w, l) : DefWindowProc(h, m, w, l);
}

LRESULT TocPanel::HandleMsg(UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; OnPaint(BeginPaint(hwnd_, &ps)); EndPaint(hwnd_, &ps); } return 0;
    case WM_LBUTTONDOWN: OnLButtonDown(GET_X_LPARAM(l), GET_Y_LPARAM(l)); return 0;
    case WM_MOUSEWHEEL:  OnMouseWheel(GET_WHEEL_DELTA_WPARAM(w)); return 0;
    case WM_VSCROLL:     OnVScroll(LOWORD(w), HIWORD(w)); return 0;
    case WM_KEYDOWN:
        if (w == VK_UP && selLine_ > 0) { selLine_--; EnsureVisible(selLine_); InvalidateRect(hwnd_, nullptr, FALSE); }
        if (w == VK_DOWN && selLine_ + 1 < (int)vis_.size()) { selLine_++; EnsureVisible(selLine_); InvalidateRect(hwnd_, nullptr, FALSE); }
        return 0;
    }
    return DefWindowProc(hwnd_, m, w, l);
}

TocPanel::TocPanel()
{
    // 16 px 高，默认宽度，正常粗细，不斜体，不 underline，不 strikeout
    hFont_ = CreateFontW(18, 0, 0, 0,
        FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        L"Microsoft YaHei");   // 字体名

}
TocPanel::~TocPanel()
{
    if (hFont_) DeleteObject(hFont_);
}
void TocPanel::RebuildVisible()
{
    vis_.clear();
    std::function<void(size_t)> walk = [&](size_t idx) {
        vis_.push_back(idx);
        const Node& n = nodes_[idx];
        if (n.expanded)
            for (size_t c : n.childIdx) walk(c);
        };
    for (size_t r : roots_) walk(r);

    // 1. 总高度（像素）
    totalH_ = (int)vis_.size() * lineH_;

    // 2. 客户区高度（像素）
    RECT rc;
    GetClientRect(hwnd_, &rc);
    int clientH = rc.bottom - rc.top;

    // 3. 设置滚动条
    SCROLLINFO si{ sizeof(si) };
    si.fMask = SIF_RANGE | SIF_PAGE;
    si.nMin = 0;
    si.nMax = totalH_;          // 像素
    si.nPage = clientH;          // 像素
    SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
}

int TocPanel::HitTest(int y) const
{
    int line = (y + scrollY_) / lineH_;
    return (line >= 0 && line < (int)vis_.size()) ? line : -1;
}

void TocPanel::Toggle(int line)
{
    size_t idx = vis_[line];
    nodes_[idx].expanded = !nodes_[idx].expanded;
    RebuildVisible();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void TocPanel::EnsureVisible(int line)
{
    RECT rc; GetClientRect(hwnd_, &rc);
    int y = line * lineH_;
    if (y < scrollY_) scrollY_ = y;
    else if (y + lineH_ > scrollY_ + rc.bottom) scrollY_ = y + lineH_ - rc.bottom;
    SetScrollPos(hwnd_, SB_VERT, scrollY_, TRUE);
}

void TocPanel::OnPaint(HDC hdc)
{
    RECT rc; GetClientRect(hwnd_, &rc);
    /* 1. 先把整块客户区刷成背景色，解决残影 */
    FillRect(hdc, &rc, GetSysColorBrush(COLOR_WINDOW));
    int first = scrollY_ / lineH_;
    int last = std::min(first + rc.bottom / lineH_ + 1, (long)vis_.size());
    HFONT hOld = (HFONT)SelectObject(hdc, hFont_);


    for (int i = first; i < last; ++i)
    {
        const Node& n = nodes_[vis_[i]];

        // 行矩形：整体向下、向右各偏移 marginTop / marginLeft
        RECT r{ marginLeft,
                marginTop + i * lineH_ - scrollY_,
                rc.right,
                marginTop + (i + 1) * lineH_ - scrollY_ };

        HBRUSH br = (i == selLine_)
            ? GetSysColorBrush(COLOR_HIGHLIGHT)
            : GetSysColorBrush(COLOR_WINDOW);
        FillRect(hdc, &r, br);

        int indent = n.nav->order * 16;
        WCHAR sign[2] = L"";
        if (!n.childIdx.empty())
            sign[0] = n.expanded ? L'−' : L'+';

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, GetSysColor(i == selLine_
            ? COLOR_HIGHLIGHTTEXT
            : COLOR_WINDOWTEXT));

        // 文字再缩进：左侧留白 + 层级缩进
        int textLeft = marginLeft + indent;
        TextOutW(hdc, textLeft, r.top + 2, sign, lstrlenW(sign));
        textLeft += 12;
        TextOutW(hdc, textLeft, r.top + 2,
            n.nav->label.c_str(),
            static_cast<int>(n.nav->label.size()));
    }
    SelectObject(hdc, hOld);   // 恢复
}

void TocPanel::OnLButtonDown(int x, int y)
{
    int line = HitTest(y);
    if (line < 0) return;

    const Node& n = nodes_[vis_[line]];
    if (n.childIdx.empty())
    {
        selLine_ = line;
        InvalidateRect(hwnd_, nullptr, false);
        if (onNavigate_) onNavigate_(n.nav->href);
    }
    else
    {
        selLine_ = line;

        Toggle(line);
    }
}
void TocPanel::SetHighlightByHref(const std::wstring& href)
{
    // ---------- 1. 找目标节点 ----------
    size_t target = nodes_.size();
    for (size_t i = 0; i < nodes_.size(); ++i)
        if (nodes_[i].nav && nodes_[i].nav->href == href)
        {
            target = i; break;
        }
    if (target == nodes_.size()) return;

    // ---------- 2. 记录路径并展开 ----------
    // path 只需存需要展开的节点，最多树高
    std::vector<size_t> path;
    std::function<bool(size_t)> dfs = [&](size_t idx) -> bool
        {
            if (idx == target) return true;          // 命中目标

            Node& n = nodes_[idx];
            if (!n.expanded)                       // 折叠就展开
                n.expanded = true;

            for (size_t c : n.childIdx)
                if (dfs(c))
                {
                    path.push_back(idx);           // 回溯时记录父节点
                    return true;
                }
            return false;
        };

    for (size_t r : roots_)                    // 支持多根
        if (dfs(r)) break;

    // 3. 重建可见表（O(N) 一次遍历）
    RebuildVisible();

    // 4. 直接取行号（yLine 已在 RebuildVisible 中更新）
    selLine_ = -1;
    for (size_t i = 0; i < vis_.size(); ++i)
        if (vis_[i] == target) { selLine_ = static_cast<int>(i); break; }

    if (selLine_ != -1)
        EnsureVisible(selLine_);
    // 5. 重绘
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void TocPanel::OnVScroll(int code, int pos)
{
    RECT rc;
    GetClientRect(hwnd_, &rc);
    int clientH = rc.bottom - rc.top;
    int maxY = std::max(0, totalH_ - clientH);

    switch (code)
    {
    case SB_LINEUP:      scrollY_ -= lineH_; break;
    case SB_LINEDOWN:    scrollY_ += lineH_; break;
    case SB_PAGEUP:      scrollY_ -= clientH; break;   // 按页滚 = 客户区高度
    case SB_PAGEDOWN:    scrollY_ += clientH; break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION:
    {
        SCROLLINFO si{ sizeof(si), SIF_TRACKPOS };
        if (GetScrollInfo(hwnd_, SB_VERT, &si))
            scrollY_ = si.nTrackPos;      // 拿到 32 位真实位置
        break;
    }
    }

    scrollY_ = std::max(0, std::min(scrollY_, maxY));

    SetScrollPos(hwnd_, SB_VERT, scrollY_, TRUE);
    InvalidateRect(hwnd_, nullptr, TRUE);
}
void TocPanel::OnMouseWheel(int delta)
{
    // 每 120 单位滚一行；可根据需要改成多行或整页
    int lines = delta / WHEEL_DELTA;          // WHEEL_DELTA = 120
    for (int i = 0; i < abs(lines); ++i)
    {
        OnVScroll(lines > 0 ? SB_LINEUP : SB_LINEDOWN, 0);
    }
}

void EPUBBook::LoadToc()
{
    if (g_toc)
    {
        g_toc->Load(ocf_pkg_.toc);                 // 代替 EPUBBook::LoadToc()
    }
}


bool ZipProvider::load(const std::wstring& file_path)
{
    namespace fs = std::filesystem;
    if (!fs::exists(file_path))
    {
        OutputDebugStringW(L"[ZipProvider] 文件不存在\n");
        return false;
    }


    mz_zip_reader_end(&m_zip);           // 1. 先关闭旧 zip
    memset(&m_zip, 0, sizeof(m_zip));

    if (!mz_zip_reader_init_file(&m_zip, w2a(file_path).c_str(), 0))
    {
        OutputDebugStringW((L"[ZipProvider] zip 打开失败：" +
            std::to_wstring(mz_zip_get_last_error(&m_zip)) + L"\n").c_str());
        return false;
    }

    m_zipIndex = ZipIndexW(m_zip);
    return true;
}
MemFile ZipProvider::get(const std::wstring& path)  const
{
    MemFile mf;
    std::string narrow_name = w2a(path);
    size_t uncomp_size = 0;
    void* p = mz_zip_reader_extract_file_to_heap(
        const_cast<mz_zip_archive*>(&m_zip),
        narrow_name.c_str(),
        &uncomp_size, 0);

    if (p) {
        mf.data.assign(static_cast<uint8_t*>(p),
            static_cast<uint8_t*>(p) + uncomp_size);
        mz_free(p);
    }
    return mf;
}

std::wstring ZipProvider::find(const std::wstring& path)
{
    return m_zipIndex.find(path);
}
MemFile LocalFileProvider::get(const std::wstring& path) const
{
    namespace fs = std::filesystem;
    std::error_code ec;

    // 文件不存在或非普通文件
    if (!fs::is_regular_file(path, ec) || ec)
        return {};

    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};

    file.seekg(0, std::ios::end);
    const auto len = static_cast<size_t>(file.tellg());
    file.seekg(0);

    MemFile mf;
    mf.data.resize(len);
    file.read(reinterpret_cast<char*>(mf.data.data()), len);

    if (!file)        // 读取失败
        return {};

    return mf;        // NRVO / move
}


bool EPUBParser::load(std::shared_ptr<IFileProvider> fp)
{
    m_fp = fp;
    parse_ocf();
    parse_opf();
    parse_toc();
    return true;
}

bool EPUBParser::parse_ocf()
{
    m_ocf_pkg = {};  // 清空
    auto mf = m_fp->get(L"META-INF/container.xml");
    if (mf.data.empty()) return false;

    tinyxml2::XMLDocument doc;
    if (doc.Parse(mf.begin(), mf.size()) != tinyxml2::XML_SUCCESS) { return false; }

    auto* rootfile = doc.FirstChildElement("container")
        ? doc.FirstChildElement("container")->FirstChildElement("rootfiles")
        : nullptr;
    rootfile = rootfile ? rootfile->FirstChildElement("rootfile") : nullptr;
    if (!rootfile || !rootfile->Attribute("full-path")) { return false; }

    m_ocf_pkg.rootfile = a2w(rootfile->Attribute("full-path"));
    m_ocf_pkg.opf_dir = m_ocf_pkg.rootfile.substr(0, m_ocf_pkg.rootfile.find_last_of(L'/') + 1);
    return true;
}
bool EPUBParser::parse_opf()
{
    auto opf = m_fp->get(m_ocf_pkg.rootfile.c_str());
    std::string xml(opf.begin(), opf.begin() + opf.size());
    if (opf.data.empty()) return false;
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS) return false;

    auto* manifest = doc.RootElement()
        ? doc.RootElement()->FirstChildElement("manifest")
        : nullptr;

    for (auto* it = manifest ? manifest->FirstChildElement("item") : nullptr;
        it; it = it->NextSiblingElement("item"))
    {
        OCFItem item;
        item.id = a2w(it->Attribute("id") ? it->Attribute("id") : "");
        item.href = a2w(it->Attribute("href") ? it->Attribute("href") : "");
        item.media_type = a2w(it->Attribute("media-type") ? it->Attribute("media-type") : "");
        item.properties = a2w(it->Attribute("properties") ? it->Attribute("properties") : "");


        // 只在 href 非空时拼绝对路径
        if (!item.href.empty())
            item.href = m_fp->find(item.href);

        m_ocf_pkg.manifest.emplace_back(std::move(item));
    }

    // spine
    auto* spine = doc.RootElement()
        ? doc.RootElement()->FirstChildElement("spine")
        : nullptr;
    // 先把 manifest 做成 id -> href 的映射
    std::unordered_map<std::wstring, std::wstring> id2href;
    for (const auto& m : m_ocf_pkg.manifest)
        id2href[m.id] = m.href;

    // 再解析 spine
    for (auto* it = spine ? spine->FirstChildElement("itemref") : nullptr;
        it; it = it->NextSiblingElement("itemref")) {

        OCFRef ref;
        ref.idref = a2w(it->Attribute("idref") ? it->Attribute("idref") : "");
        ref.href = id2href[ref.idref];   // 直接填进去
        ref.linear = a2w(it->Attribute("linear") ? it->Attribute("linear") : "yes");
        m_ocf_pkg.spine.emplace_back(std::move(ref));
    }
    // meta
    auto* meta = doc.RootElement()
        ? doc.RootElement()->FirstChildElement("metadata")
        : nullptr;
    for (auto* it = meta ? meta->FirstChildElement() : nullptr;
        it; it = it->NextSiblingElement()) {
        m_ocf_pkg.meta[a2w(it->Name())] = a2w(it->GetText() ? it->GetText() : "");
    }
    return true;
}
bool EPUBParser::parse_toc()
{
    std::wstring toc_path;
    for (const auto& it : m_ocf_pkg.manifest)
    {
        if (it.properties.find(L"nav") != std::wstring::npos ||
            it.id.find(L"ncx") != std::wstring::npos)
        {
            toc_path = it.href;
            break;
        }
    }
    if (toc_path.empty()) return false;

    m_ocf_pkg.toc_path = toc_path;
    auto toc = m_fp->get(toc_path.c_str());
    if (toc.data.empty()) return false;

    tinyxml2::XMLDocument doc;
    if (doc.Parse(toc.begin(), toc.size()) != tinyxml2::XML_SUCCESS) return false;

    bool is_nav = is_xhtml(toc_path);
    std::string opf_dir = w2a(m_ocf_pkg.opf_dir);

    m_ocf_pkg.toc.clear();

    if (is_nav)
    {
        auto* body = doc.FirstChildElement("html")
            ? doc.FirstChildElement("html")->FirstChildElement("body")
            : nullptr;
        if (!body) return false;

        for (auto* nav = body->FirstChildElement("nav");
            nav;
            nav = nav->NextSiblingElement("nav"))
        {
            const char* type = nav->Attribute("epub:type");
            if (type && std::string(type) == "toc")
            {
                parse_nav_list(nav->FirstChildElement("ol"), 0, opf_dir, m_ocf_pkg.toc);
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
            parse_ncx_points(navMap->FirstChildElement("navPoint"), 0, opf_dir, m_ocf_pkg.toc);
    }
    return true;
}

void EPUBParser::parse_ncx_points(tinyxml2::XMLElement* navPoint, int level,
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
            np.href = m_fp->find(np.href);
        np.order = level;               // 层级深度
        out.emplace_back(std::move(np));

        // 递归子 <navPoint>
        parse_ncx_points(pt->FirstChildElement("navPoint"), level + 1, opf_dir, out);
    }
}

void EPUBParser::parse_nav_list(tinyxml2::XMLElement* ol, int level,
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
            np.href = m_fp->find(np.href);
        np.order = level;               // 层级深度
        out.emplace_back(std::move(np));

        // 递归子 <ol>
        if (auto* sub = li->FirstChildElement("ol"))
            parse_nav_list(sub, level + 1, opf_dir, out);
    }
}

std::wstring EPUBParser::extract_text(const tinyxml2::XMLElement* a)
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

std::wstring EPUBBook::get_chapter_name_by_id(int spine_id)
{
    // 从给定 spine_id 开始，依次递减查找
    for (int id = spine_id; id >= 0; --id)
    {
        // 1. 取出 spine 对应的 ref
        if (id >= static_cast<int>(ocf_pkg_.spine.size()))
            continue;

        std::wstring href = ocf_pkg_.spine[id].href;


        if (href.empty())
            continue;

        // 3. 去掉锚点
        size_t pos = href.find(L'#');
        if (pos != std::wstring::npos)
            href = href.substr(0, pos);

        // 4. 与 toc 中的 href比对（同样去掉锚点）
        for (const auto& nav : ocf_pkg_.toc)
        {
            std::wstring nav_href = nav.href;
            pos = nav_href.find(L'#');
            if (pos != std::wstring::npos)
                nav_href = nav_href.substr(0, pos);

            if (nav_href == href)
                return nav.label;
        }
    }

    // 遍历到 id=0 仍未找到
    return L"";
}




// ---------- 静态 ----------



void ScrollBarEx::GetWindow(HWND hwnd)
{
    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)this);
    m_hwnd = hwnd;
}
// ---------- API ----------
void ScrollBarEx::SetSpineCount(int n)
{
    m_count = n;
    m_pos = {};
    InvalidateRect(m_hwnd, nullptr, false);
    UpdateWindow(m_hwnd);
}



void ScrollBarEx::SetPosition(int spineId, int totalHeightPx, int offsetPx)
{
    if (spineId >= 0 && spineId < m_count)
    {
        m_pos.spine_id = spineId;
        m_pos.height = totalHeightPx;
        m_pos.offset = offsetPx;
        InvalidateRect(m_hwnd, nullptr, false);
        UpdateWindow(m_hwnd);
    }
}

// ---------- 内部 ----------
LRESULT CALLBACK ScrollBarEx::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ScrollBarEx* self = (ScrollBarEx*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    switch (msg)
    {
    case WM_PAINT:          self->OnPaint(); return 0;
    case WM_LBUTTONDOWN:  self->OnLButtonDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); return 0;
    case WM_MOUSEMOVE:      self->OnMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); return 0;
    case WM_LBUTTONUP:    self->OnLButtonUp(); return 0;
    case WM_RBUTTONUP:    self->OnRButtonUp() ; return 0;
    case SBM_SETSPINECOUNT:
        if (self) self->SetSpineCount((int)wp);

        return 0;

    case SBM_SETPOSITION:
        if (self) self->SetPosition((int)wp, (int)LOWORD(lp), (int)HIWORD(lp));
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

void ScrollBarEx::OnPaint()
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(m_hwnd, &ps);

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const int W = rc.right - rc.left;
    const int H = rc.bottom - rc.top;
    const int CX = W / 2;

    /* ---- 双缓冲 ---- */
    HDC memDC = CreateCompatibleDC(hdc);
    BITMAPINFO bi{ 0 };
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits;
    HBITMAP bmp = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ oldBmp = SelectObject(memDC, bmp);
    FillRect(memDC, &rc, (HBRUSH)(COLOR_BTNFACE + 1));

    if (H > 0 && m_count > 0)
    {
        Gdiplus::Graphics g(memDC);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        /* ---------- 滚动条模式 ---------- */
        if (m_mouseIn)
        {
            /* 1. 竖线 */
            Gdiplus::Pen linePen(Gdiplus::Color(220, 220, 220), 3);
            g.DrawLine(&linePen, CX, 0, CX, H);

            /* 2. 滑块 */
            const double ratio = (m_pos.height > 0)
                ? std::max(0.0f, std::min(1.0f, m_pos.offset / m_pos.height))
                : 0.0;

            int thumbY = static_cast<int>(ratio * (H - thumbH));
            thumbY = std::max(0, std::min(thumbY, H - thumbH));

            Gdiplus::Rect r(CX - 6, thumbY, 12, thumbH);
            Gdiplus::GraphicsPath path;
            path.AddArc(r.X, r.Y, 6, 6, 180, 90);
            path.AddArc(r.X + r.Width - 6, r.Y, 6, 6, 270, 90);
            path.AddArc(r.X + r.Width - 6, r.Y + r.Height - 6, 6, 6, 0, 90);
            path.AddArc(r.X, r.Y + r.Height - 6, 6, 6, 90, 90);
            path.CloseFigure();
            Gdiplus::SolidBrush thumbBrush(Gdiplus::Color(0, 120, 215));
            g.FillPath(&thumbBrush, &path);

            /* 3. 顶部/底部横线 */
            const int aboveCnt = m_pos.spine_id;
            const int belowCnt = m_count - aboveCnt - 1;

            const int maxHalfW = W / 2 - 10;          // 最大半长，留边距
            const int topW = std::min(maxHalfW, aboveCnt * 3);   // 3px/章
            const int bottomW = std::min(maxHalfW, belowCnt * 3);

            Gdiplus::Pen markPen(Gdiplus::Color(160, 160, 160), 2);
            if (aboveCnt > 0) g.DrawLine(&markPen, CX - topW, 2, CX + topW, 2);
            if (belowCnt > 0) g.DrawLine(&markPen, CX - bottomW, H - 2, CX + bottomW, H - 2);
        }
        /* ---------- 圆点模式 ---------- */
        else
        {
            const bool crowded = (m_count > 500);
            if (m_count >= 1 && m_count < 30) dot_r = 2;
            else if (m_count >= 30 && m_count < 100) dot_r = 2;
            else if (m_count >= 100) dot_r = 1;
            const double step = static_cast<double>(H) / m_count;

            /* 串起所有点的浅色细线 */
            if (m_count > 1)
            {
                Gdiplus::Pen linkPen(Gdiplus::Color(220, 220, 220), 1);
                g.DrawLine(&linkPen, CX, step, CX, H - step);
            }

            for (int i = 0; i < m_count; ++i)
            {
                /* 拥挤时只画当前点 */
                if (i == m_pos.spine_id) continue;
     

                const int y = static_cast<int>((i + 0.5) * step);
    

                const int r = dot_r;
                Gdiplus::Color c =  Gdiplus::Color(200, 200, 200);

                Gdiplus::SolidBrush br(c);
                g.FillEllipse(&br, CX - r, y - r, 2 * r, 2 * r);
            }
            /* 画当前章节的点 */
            {
                Gdiplus::Color c = Gdiplus::Color(0, 120, 215);
               
                int r = ACTIVE_R;
                int y = (m_pos.spine_id + 0.5) * step;
                Gdiplus::SolidBrush br(c);
                g.FillEllipse(&br, CX - r, y - r, 2 * r, 2 * r);
            }
        }
    }
    BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(bmp);
    DeleteDC(memDC);
    EndPaint(m_hwnd, &ps);
}
bool ScrollBarEx::HitThumb(const POINT& pt) const
{

    if (m_count <= 0) return false;
    if (!m_mouseIn)return false;
    RECT rc; GetClientRect(m_hwnd, &rc);
    int H = rc.bottom - rc.top;
    const double ratio = (m_pos.height > 0)
        ? std::max(0.0f, std::min(1.0f, m_pos.offset / m_pos.height))
        : 0.0;

    int thumbY = static_cast<int>(ratio * (H - thumbH));
    thumbY = std::max(0, std::min(thumbY, H - thumbH));
    if (pt.y >= thumbY && (pt.y) <= (thumbY + thumbH))return true;
    return false;
}

void ScrollBarEx::OnLButtonDown(int x, int y)
{
    POINT pt{ x, y };
    if (HitThumb(pt))
    {
        SetCapture(m_hwnd);
        m_thumb.drag = true;


    }
}

void ScrollBarEx::OnMouseMove(int x, int y)
{
    /* 悬停高亮（可选） */
    POINT pt{ x, y };
    bool in = HitThumb(pt);
    if (in != m_thumb.hot)
    {
        m_thumb.hot = in;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    if (m_thumb.drag)
    {
        //ScreenToClient(m_hwnd, &pt);
        //RECT rc; GetClientRect(m_hwnd, &rc);
        //int h = rc.bottom - rc.top;
        //float ratio = static_cast<float>(pt.y / h);
        ///* 用偏移修正后的新顶边 */
        //int newTop = m_pos.height * ratio ;


        //g_offsetY = newTop;
        //UpdateCache();
        //InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void ScrollBarEx::OnLButtonUp()
{
    if (m_thumb.drag)
    {
        ReleaseCapture();
        m_thumb.drag = false;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

}

void ScrollBarEx::OnRButtonUp()
{
     m_mouseIn = !m_mouseIn;
     InvalidateRect(m_hwnd, nullptr, false);     
     UpdateWindow(m_hwnd); 
}

int64_t D2DCanvas::hit_test(float x, float y)
{

    for (const auto& line : g_lines)
        for (const auto& cb : line)
            if (x >= cb.rect.left && x <= cb.rect.right &&
                y >= cb.rect.top && y <= cb.rect.bottom)
                return cb.offset;

    return -1;
}

void D2DCanvas::on_lbutton_down(int x, int y)
{
    m_selecting = true;
    m_selStart = m_selEnd = hit_test((float)x, (float)y);
    UpdateCache();
}

void D2DCanvas::on_mouse_move(int x, int y)
{
    if (m_selecting)
    {
        auto result = hit_test((float)x, (float)y);
        if (result >= 0) 
        { 
            if (m_selStart < 0) { m_selStart = result; }
            m_selEnd = result; 
            UpdateCache();
            InvalidateRect(m_hwnd, nullptr, false);
            UpdateWindow(m_hwnd);
        }
 



    }
}

void D2DCanvas::on_lbutton_up()
{
    m_selecting = false;
    UpdateCache();
    //copy_to_clipboard();
}
void D2DCanvas::copy_to_clipboard()
{
    if (m_selStart == m_selEnd) return;

    // 确保选区不越界
    size_t start = std::min(m_selStart, m_selEnd);
    size_t end = std::max(m_selStart, m_selEnd);
    end = std::min(end, g_plainText.size());
    if (start >= end) return;

    size_t len = end - start;

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (len + 1) * sizeof(wchar_t));
    if (!hMem) return;                       // 内存不足
    wchar_t* dst = (wchar_t*)GlobalLock(hMem);
    if (!dst) { GlobalFree(hMem); return; }  // 锁失败

    memcpy(dst, g_plainText.c_str() + start, len * sizeof(wchar_t));
    dst[len] = L'\0';

    GlobalUnlock(hMem);

    if (OpenClipboard(g_hWnd))
    {
        EmptyClipboard();
        SetClipboardData(CF_UNICODETEXT, hMem);
        CloseClipboard();
    }
    else
    {
        GlobalFree(hMem);
    }
}

std::vector<RECT> D2DCanvas::get_selection_rows() const
{
    std::vector<RECT> rows;
    if (m_selStart == m_selEnd) return rows;

    const size_t start = std::min(m_selStart, m_selEnd);
    const size_t end = std::max(m_selStart, m_selEnd);

    /* 1. 先按原逻辑收集每一“词”的矩形 */
    for (const auto& line : g_lines)
    {
        if (line.empty()) continue;

        const size_t lineFirst = line.front().offset;
        const size_t lineLast = line.back().offset;
        if (lineLast < start || lineFirst >= end) continue;

        size_t idx0 = 0;
        while (idx0 < line.size() && line[idx0].offset < start) ++idx0;

        size_t idx1 = line.size() - 1;
        while (idx1 != static_cast<size_t>(-1) && line[idx1].offset >= end) --idx1;

        if (idx0 > idx1) continue;

        const D2D1_RECT_F& r0 = line[idx0].rect;
        const D2D1_RECT_F& r1 = line[idx1].rect;

        RECT row;
        row.left = static_cast<LONG>(r0.left);
        row.top = static_cast<LONG>(r0.top);
        row.right = static_cast<LONG>(r1.right);
        row.bottom = static_cast<LONG>(std::max(r0.bottom, r1.bottom));
        rows.push_back(row);
    }

    /* 2. 把同一水平行的矩形横向合并（最小改动） */
    if (rows.empty()) return rows;

    std::vector<RECT> merged;
    RECT cur = rows.front();

    for (size_t i = 1; i < rows.size(); ++i)
    {
        const RECT& r = rows[i];
        // 同一行：top 差值 ≤ 1 像素
        if (std::abs(r.top - cur.top) <= 1)
        {
            cur.left = std::min(cur.left, r.left);
            cur.right = std::max(cur.right, r.right);
            cur.bottom = std::max(cur.bottom, r.bottom);
        }
        else
        {
            merged.push_back(cur);
            cur = r;
        }
    }
    merged.push_back(cur);
    return merged;
}

void D2DCanvas::present(int x, int y, litehtml::position* clip)
{

    g_lines.clear();
    g_plainText.clear();

    BeginDraw();

    m_doc->draw(getContext(),   // 强制转换
        x, y, clip);


    // 高亮选中行
    if (!m_selBrush)
    {
        m_rt->CreateSolidColorBrush(
            D2D1::ColorF(0.2f, 0.5f, 1.0f, 0.4f),   // 半透明蓝
            &m_selBrush);
    }
    if (m_selStart != m_selEnd && m_selBrush && m_selStart >= 0 && m_selEnd >= 0)
    {
        m_rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        for (const auto& row : get_selection_rows())
        {
            D2D1_RECT_F r = D2D1::RectF(
                row.left ,
                row.top ,
                row.right ,
                row.bottom );
            m_rt->FillRectangle(r, m_selBrush.Get());
        }
    }

    EndDraw();



}
// 判断是否为单词边界（空格、标点、换行）
static bool is_word_boundary(wchar_t ch)
{
    return iswspace(ch) || iswpunct(ch) || ch == L'\r' || ch == L'\n';
}

//void D2DCanvas::on_lbutton_dblclk(int x, int y)
//{
//    if (g_plainText.empty()) return;
//
//    // 1. 把鼠标坐标转成逻辑字符偏移（UTF-16 code unit）
//    size_t clickPos = hit_test(x, y);
//    if (clickPos == static_cast<size_t>(-1) ||
//        clickPos >= g_plainText.size())
//        return;
//
//    // 2. 用 ICU 的 BreakIterator 找“单词”边界
//    UErrorCode err = U_ZERO_ERROR;
//    icu::UnicodeString us(g_plainText.data(), g_plainText.size());
//    UBreakIterator* bi = ubrk_open(UBRK_WORD, nullptr,
//        us.getBuffer(), us.length(),
//        &err);
//    if (U_FAILURE(err)) return;
//
//    // 3. 把 clickPos 之前最近的单词起点
//    int32_t start = ubrk_preceding(bi, static_cast<int32_t>(clickPos));
//    if (start == UBRK_DONE) start = 0;
//
//    // 4. 把 clickPos 之后最近的单词终点
//    int32_t end = ubrk_following(bi, static_cast<int32_t>(clickPos));
//    if (end == UBRK_DONE) end = static_cast<int32_t>(g_plainText.size());
//
//    ubrk_close(bi);
//
//    // 5. 赋给选区
//    m_selStart = static_cast<size_t>(start);
//    m_selEnd = static_cast<size_t>(end);
//
//    UpdateCache();
//}
void D2DCanvas::clear_selection()
{
    m_selStart = m_selEnd = -1;
    m_selecting = false;
}
void D2DCanvas::on_lbutton_dblclk(int x, int y)
{
    if (g_plainText.empty() || g_lines.empty()) return;

    /* 1. 字符偏移 */
    size_t clickPos = hit_test(x, y);
    if (clickPos == size_t(-1) || clickPos >= g_plainText.size())
        return;

    /* 2. 在 g_lines 里找到当前行 */
    size_t lineStart = 0, lineEnd = 0;
    for (const auto& line : g_lines)
    {
        if (line.empty()) continue;
        lineStart = line.front().offset;
        lineEnd = line.back().offset + 1;   // [start , end)
        if (clickPos >= lineStart && clickPos < lineEnd)
            break;
    }
    if (lineEnd <= lineStart) return;   // 没找到行

    /* 3. 在这一行里用 ICU 选词 */
    icu::UnicodeString us(g_plainText.data(), g_plainText.size());
    const UChar* buf = us.getBuffer();

    UErrorCode err = U_ZERO_ERROR;
    UBreakIterator* wordBI = ubrk_open(
        UBRK_WORD, nullptr,
        buf + lineStart,
        static_cast<int32_t>(lineEnd - lineStart),
        &err);
    if (U_FAILURE(err)) return;

    int32_t relPos = static_cast<int32_t>(clickPos - lineStart);

    int32_t wordStartRel = ubrk_preceding(wordBI, relPos);
    if (wordStartRel == UBRK_DONE) wordStartRel = 0;

    int32_t wordEndRel = ubrk_following(wordBI, relPos);
    if (wordEndRel == UBRK_DONE) wordEndRel = lineEnd - lineStart;

    ubrk_close(wordBI);

    int32_t wordStart = lineStart + wordStartRel;
    int32_t wordEnd = lineStart + wordEndRel;

    /* 4. 裁剪首尾空格/标点 */
    auto isVisible = [](UChar32 c) {
        return !u_isspace(c) && (u_isalnum(c) || c == 0x2019);
        };

    while (wordStart < wordEnd) {
        UChar32 c; int32_t idx = wordStart;
        U16_NEXT(buf, idx, wordEnd, c);
        if (isVisible(c)) break;
        wordStart = idx;
    }
    while (wordEnd > wordStart) {
        UChar32 c; int32_t idx = wordEnd;
        U16_PREV(buf, wordStart, idx, c);
        if (isVisible(c)) { wordEnd = idx + U16_LENGTH(c); break; }
        wordEnd = idx;
    }

    if (wordStart >= wordEnd) return;

    /* 5. 更新选区 */
    m_selStart = static_cast<size_t>(wordStart);
    m_selEnd = static_cast<size_t>(wordEnd);
    UpdateCache();
}