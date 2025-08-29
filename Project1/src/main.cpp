#include "main.h"

HWND  g_hwndTV = nullptr;    // 侧边栏 TreeView
HIMAGELIST g_hImg = nullptr;   // 图标(可选)
HWND      g_hWnd;
HWND g_hStatus = nullptr;   // 状态栏句柄
HWND g_hView = nullptr;
HWND g_hTooltip = nullptr;



enum class StatusBar{INFO = 0, FONT = 1};
static int g_scrollY = 0;   // 当前像素偏移
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


int g_center_offset = 0;


// 别名表：CSS 名 -> 真实字体名
//static std::unordered_map<std::wstring, std::set<std::wstring>>  g_fontAliasDynamic = {
//    {L"serif", g_cfg.font_serif},
//    {L"sans-serif", g_cfg.font_sans_serif},
//    {L"monospace", g_cfg.font_monospace}
//};

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


//std::unique_ptr<ICanvas>
//ICanvas::create(int w, int h, Renderer which)
//{
//    switch (which)
//    {
//    case Renderer::GDI:
//        return std::make_unique<GdiCanvas>(w, h);
//
//    case Renderer::D2D:
//        return std::make_unique<D2DCanvas>(w, h, g_d2dRT);
//    case Renderer::FreeType:
//        return std::make_unique<FreetypeCanvas>(w, h, 96);   // 96 dpi 示例
//    }
//    return nullptr;
//}
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
std::unique_ptr<ICanvas> g_tooltip_canvas;
ComPtr<ID2D1Factory1> g_tooltip_d2dFactory = nullptr;   // 原来是 ID2D1Factory = nullptr;
ComPtr<ID2D1HwndRenderTarget> g_tooltip_d2dRT = nullptr;   // ← 注意是 HwndRenderTarget 

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

/* static */
void EPUBBook::FreeTreeData(HWND tv)
{
    HTREEITEM hRoot = TreeView_GetRoot(tv);
    std::function<void(HTREEITEM)> freeData = [&](HTREEITEM h)
        {
            for (; h; h = TreeView_GetNextSibling(tv, h))
            {
                TVITEMW tvi{ TVIF_PARAM, h };
                TreeView_GetItem(tv, &tvi);
                delete reinterpret_cast<TVData*>(tvi.lParam);

                freeData(TreeView_GetChild(tv, h));
            }
        };
    freeData(hRoot);
}
void EPUBBook::LoadToc()
{
    SendMessage(g_hwndTV, WM_SETREDRAW, FALSE, 0);

    // ✅ 释放旧节点数据

    HTREEITEM hRoot = TreeView_GetRoot(g_hwndTV);
    TreeView_SelectItem(g_hwndTV, nullptr);   // 先取消选中
    FreeTreeData(g_hwndTV);                   // 释放 TVData*
    TreeView_DeleteAllItems(g_hwndTV);          // 此时不会再触发 TVN_SELCHANGED

    BuildTree(ocf_pkg_.toc, m_nodes, m_roots);
    for (size_t r : m_roots)
        InsertTreeNodeLazy(g_hwndTV, m_nodes[r], m_nodes, TVI_ROOT);

    SendMessage(g_hwndTV, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(g_hwndTV, nullptr, nullptr,
        RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW);
}

void EPUBBook::BuildTree(const std::vector<OCFNavPoint>& flat,
    std::vector<TreeNode>& nodes,
    std::vector<size_t>& roots)
{
    if (flat.empty()) return;
    nodes.clear();
    nodes.reserve(flat.size());
    std::vector<size_t> stack;
    stack.reserve(32);
    stack.push_back(SIZE_MAX);

    for (const auto& np : flat)
    {
        while (stack.size() > static_cast<size_t>(np.order + 1))
            stack.pop_back();

        size_t idx = nodes.size();
        nodes.emplace_back(&np);

        if (stack.back() != SIZE_MAX)
            nodes[stack.back()].childIdx.push_back(idx);
        else
            roots.push_back(idx);

        stack.push_back(idx);
    }
}

/* static */
HTREEITEM EPUBBook::InsertTreeNodeLazy(HWND tv,
    const TreeNode& node,
    const std::vector<TreeNode>& allNodes,
    HTREEITEM parent)
{
    TVINSERTSTRUCTW tvis = {};
    tvis.hParent = parent;
    tvis.hInsertAfter = TVI_LAST;
    tvis.itemex.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
    tvis.itemex.pszText = LPSTR_TEXTCALLBACKW;
    tvis.itemex.cChildren = node.childIdx.empty() ? 0 : 1;

    // ✅ 使用 TVData，而不是裸指针
    auto* data = new TVData{ &node, &allNodes };
    tvis.itemex.lParam = reinterpret_cast<LPARAM>(data);

    HTREEITEM hItem = TreeView_InsertItem(tv, &tvis);
    return hItem;
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


// ---------- 全局 ----------
HINSTANCE g_hInst;
std::shared_ptr<SimpleContainer> g_container;
std::shared_ptr<SimpleContainer> g_tooltip_container;
std::unique_ptr<EPUBBook>  g_book;
Paginator g_pg;
Gdiplus::Image* g_pSplashImg = nullptr;
std::future<void> g_parse_task;

// ---------- 工具 ----------
// 计算 render_item 的文档绝对矩形


// 工具：把客户区坐标 → 文档坐标
//litehtml::element::ptr element_from_point(litehtml::element::ptr root, int x, int y) {
//    if (!root) return nullptr; // 倒序遍历（后渲染的在上面，先匹配） 
//    const auto& ch = root->children(); 
//    for (auto it = ch.rbegin(); it != ch.rend(); ++it) 
//    { 
//        if (auto hit = element_from_point(*it, x, y)) 
//        { 
//            return hit; 
//        }
//    } // 检查自身 
//    //litehtml::position pos = root->get_placement(); 
//    litehtml::position pos = root->get_placement();
//
//    if (x >= pos.left() && x < pos.right() && y >= pos.top() && y < pos.bottom()) 
//    {
//        return root; 
//    } 
//    return nullptr; 
//}

// 向上找最近的 <a>，遇到 <p> 停止；返回 <a> 元素或 nullptr
litehtml::element::ptr find_link_in_chain(litehtml::element::ptr start)
{
    for (auto cur = start; cur; cur = cur->parent())
    {
        const char* tag = cur->get_tagName();
        if (std::strcmp(tag, "p") == 0) break;
        if (std::strcmp(tag, "a") == 0) return cur;
    }
    return nullptr;
}

// 从 href 中提取锚点 id（去掉 '#'）
std::string extract_anchor(const char* href)
{
    if (!href) return "";
    const char* p = std::strrchr(href, '#');
    return p ? (p+1 ) : "";

}


#include <vector>
#include <algorithm>
#include <unordered_set>

static bool skip_attr(const std::string& val)
{
    if (val.empty()) return true;
    if (val == "0")  return true;
    return std::all_of(val.begin(), val.end(),
        [](unsigned char c) { return std::isspace(c); });
}

static std::string get_html(litehtml::element::ptr el)
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

// 3. 核心函数：用 select_one 找 id，再向上找 <p>
std::string html_of_anchor_paragraph(litehtml::document* doc, const std::string& anchorId)
{
    if (anchorId.empty()) return "";
    // 构造 CSS 选择器
    std::string sel = "[id=\"" + anchorId + "\"]";
    //std::string sel = "#" + anchorId;
    auto target = doc->root()->select_one(sel);
    if (!target) return "";

    // 向上找最近的 <p>
    auto p = target;
    while (p && std::strcmp(p->get_tagName(), "p") != 0)
        p = p->parent();
    if (!p) p = target;          // 兜底：直接返回自身

    std::string inner = get_html(p);
    // 包一层带 inline-block 的 <p>

    //return text;
    return "<style>img{display:block;width:100%;height:auto;max-height:300px;}</style>" + inner;
}

LRESULT CALLBACK ViewWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_SIZE:
    {
        if (g_container && g_container->m_canvas) {
            g_states.needRelayout.store(true);
            g_container->resize(LOWORD(lp), HIWORD(lp) );
        }
        return 0;
    }
    case WM_LBUTTONDOWN: 
    {
        int x = GET_X_LPARAM(lp);
        int y = GET_Y_LPARAM(lp);
   
        if (!g_container->m_doc) { return 0; }

        int doc_x = x - g_center_offset;
        int doc_y = y + g_scrollY;
        litehtml::position::vector redraw_boxes;
        g_container->m_doc->on_lbutton_down(doc_x, doc_y, 0, 0, redraw_boxes);
    }
    case WM_LBUTTONUP:
    {
        int x = GET_X_LPARAM(lp);
        int y = GET_Y_LPARAM(lp);
        //g_book->hide_tooltip();
        if (!g_container->m_doc) { return 0; }

        int doc_x = x - g_center_offset;
        int doc_y = y + g_scrollY;
        litehtml::position::vector redraw_boxes;
        g_container->m_doc->on_lbutton_up(doc_x, doc_y, 0, 0, redraw_boxes);
        //auto root_render = g_container->m_doc->root_render();
        //auto hit = root_render->get_element_by_point(doc_x, doc_y, 0, 0);

        //auto link = find_link_in_chain(hit);

        //std::string html;
        //if (link)
        //{
        //    const char* href_raw = link->get_attr("href");
        //    if (!href_raw) {
        //        return 0;
        //    }
        //    std::wstring href = g_book->m_zipIndex.find(a2w(href_raw));

        //    g_book->OnTreeSelChanged(href.c_str());

        //}

    }
    case WM_MOUSEMOVE:
    {
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hWnd, 0 };
        TrackMouseEvent(&tme);   // 只订阅一次即可，系统会在离开时发 WM_MOUSELEAVE
        int x = GET_X_LPARAM(lp);
        int y = GET_Y_LPARAM(lp);

        int doc_x = x - g_center_offset;
        int doc_y = y + g_scrollY;

        if (!g_container->m_doc) break;
        litehtml::position::vector redraw_boxes;
        g_container->m_doc->on_mouse_over(doc_x, doc_y, 0, 0, redraw_boxes);

        //auto hit = g_container->m_doc->root_render()->get_element_by_point(doc_x, doc_y, 0, 0);
        //auto link = find_link_in_chain(hit);

        //std::string html;
        //if (link)
        //{
        //    const char* href_raw = link->get_attr("href");
        //    if (!href_raw) {
        //        g_book->hide_tooltip();
        //        break;
        //    }
        //    std::string href = href_raw;


        //    std::string id = extract_anchor(href.c_str());
        //    if (!id.empty())
        //        html = html_of_anchor_paragraph(g_container->m_doc.get(), id);
        //}

        //if (!html.empty())
        //{
        //    g_book->show_tooltip(std::move(html), x, y);
        //}
        //else
        //{
        //    g_book->hide_tooltip();
        //}
        //break;
    }
    case WM_EPUB_CACHE_UPDATED:
    {
        RECT rc; GetClientRect(g_hView, &rc);
        int height = rc.bottom - rc.top;
        g_container->m_canvas->BeginDraw();
        
        litehtml::position clip(g_center_offset, 0, g_cfg.document_width, height);
        g_container->m_doc->draw(g_container->m_canvas->getContext(),
            g_center_offset, -g_scrollY, &clip);   
        g_container->m_canvas->EndDraw();
        g_states.isCaching.exchange(false);
     
        UpdateWindow(g_hView);
  
        return 0;
    }
    case WM_EPUB_ANCHOR:
    {
        if (!g_container->m_doc) { return 0; }
        wchar_t* sel = reinterpret_cast<wchar_t*>(wp);
        if (sel) {
            std::string cssSel = "[id=\"" + w2a(sel) + "\"]";
            if (auto el = g_container->m_doc->root()->select_one(cssSel.c_str())) {
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
        UpdateWindow(hWnd);
        return 0;
    }
    case WM_MOUSELEAVE: 
    {
        if (g_container && g_container->m_doc) 
        {
            litehtml::position::vector redraw_box;
            g_container->m_doc->on_mouse_leave(redraw_box);
        }
        //g_book->hide_tooltip();
        return 0;
    }
    case WM_MOUSEACTIVATE:
    {
        //g_book->hide_tooltip();
        return 0;
    }
    case WM_NCMOUSEMOVE:
    {
        //g_book->hide_tooltip();
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
        //g_scrollY = std::clamp<int>(g_scrollY - zDelta, 0, std::max<int>(g_maxScroll - rc.bottom, 0));
        int scroll_step = zDelta / 120 * g_line_height;
        if (zDelta >= 0) { g_scrollY -= g_line_height; }
        else { g_scrollY += g_line_height; }
        SetScrollPos(hWnd, SB_VERT, g_scrollY, TRUE);
        UpdateCache();
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }
    case WM_PAINT:
    {
        if (!g_states.isCaching)
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(g_hWnd, &ps);
            if (g_container->m_canvas)
                g_container->m_canvas->present(hdc, 0, 0);
            EndPaint(g_hWnd, &ps);

        }

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
std::queue<Task> g_taskQ;
std::mutex g_qMtx;
std::condition_variable g_qCV;
std::thread g_worker;
bool g_stop = false;
void WorkerLoop()
{
    while (true) {
        Task t;
        {
            std::unique_lock<std::mutex> lk(g_qMtx);
            g_qCV.wait(lk, [] { return g_stop || !g_taskQ.empty(); });
            if (g_stop && g_taskQ.empty()) break;
            t = g_taskQ.front();
            g_taskQ.pop();
        }
        // 真正耗时的工作
        g_pg.load(g_container->m_doc.get(), t.width, t.height);
        g_pg.render(g_container->m_canvas.get(), t.scrollY);
        PostMessage(g_hView, WM_EPUB_CACHE_UPDATED, 0, 0);
    }
}
void ShutdownWorker()
{
    {
        std::lock_guard<std::mutex> lk(g_qMtx);
        g_stop = true;
    }
    g_qCV.notify_all();
    if (g_worker.joinable()) g_worker.join();
}
void UpdateCache()
{
    if (!g_container || !g_container->m_doc) return;

    RECT rc;
    GetClientRect(g_hView, &rc);
    int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0) return;

    {
        g_states.isCaching.store(true);
        std::lock_guard<std::mutex> lk(g_qMtx);
        g_taskQ = {}; // 清空旧任务
        g_taskQ.push({ w, h, g_scrollY });
    }
    g_qCV.notify_one(); // 唤醒后台线程
}
//// 4. UI 线程：只管发任务
//void UpdateCache()
//{
//    if (!g_container || !g_container->m_doc) return;
//
//    RECT rc; GetClientRect(g_hView, &rc);
//    int w = rc.right, h = rc.bottom ;
//    if (w <= 0 || h <= 0) return;
//
//    g_pg.load(g_container->m_doc.get(), w, h);
//    /* 3) 渲染整页 */
//    g_pg.render(g_container->m_canvas.get(), g_scrollY);
//}

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
        g_pg.clear();              // 如果你 Paginator 有 clear() 就调
        g_container->clear();
        g_container->m_canvas->clear();
        g_book->clear();
        InvalidateRect(g_hView, nullptr, true);
        InvalidateRect(g_hwndTV, nullptr, true);


        // 2. 立即释放旧对象，防止野指针
        //g_container.reset();
  
        // 3. 启动新任务
        SetStatus(0 ,L"正在加载...");
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

    case WM_EPUB_PARSED: {

        if (!g_states.isLoaded) {
            g_states.isLoaded = true;
            ShowWindow(g_hStatus, SW_SHOW);
            ShowWindow(g_hView, SW_SHOW);
            ShowWindow(g_hwndTV, SW_SHOW);
        }


        g_last_html_path = g_book->ocf_pkg_.spine[0].href;
        std::string html = g_book->load_html(g_last_html_path);
       
        g_book->init_doc(std::move(html));
        g_states.needRelayout.store(true, std::memory_order_release);
        // 2) 立即把第 0 页画到缓存位图
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
        
        SendMessage(g_hStatus, WM_SIZE, 0, 0);
        // 2. 分栏
        int parts[2] = { 120, -1 };   // -1 = 占满剩余宽度
        SendMessage(g_hStatus, SB_SETPARTS, 2, (LPARAM)parts);

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
        UpdateWindow(g_hView);
      
        InvalidateRect(g_hWnd, nullptr, TRUE);  // TRUE = 先发送 WM_ERASEBKGND
        UpdateWindow(g_hWnd);

        return 0;
    }
    case WM_LOAD_ERROR: {
        wchar_t* msg = (wchar_t*)l;
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

    case WM_MOUSEWHEEL: {
        //if(g_book){ g_book->hide_tooltip(); }
       
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
       //if (g_book) { g_book->hide_tooltip(); }
        PostQuitMessage(0);
        return 0;
    }
    case WM_NOTIFY:
    {
        LPNMHDR nm = reinterpret_cast<LPNMHDR>(l);
        if (nm->hwndFrom != g_hwndTV) break;

        switch (nm->code)
        {
        case TVN_GETDISPINFO:
        {
            auto* pdi = reinterpret_cast<NMTVDISPINFO*>(l);
            auto* data = reinterpret_cast<const TVData*>(pdi->item.lParam);
            if (!data || !data->node || !data->node->nav)   // 三重保护
            {
                pdi->item.pszText = const_cast <wchar_t*>(L"");
                break;
            }
            pdi->item.pszText = const_cast<wchar_t*>(data->node->nav->label.c_str());
            break;
        }

        case TVN_SELCHANGED:
        {
            auto* pnmtv = reinterpret_cast<NMTREEVIEW*>(l);
            TVITEMW tvi{ TVIF_PARAM, pnmtv->itemNew.hItem };
            if (TreeView_GetItem(g_hwndTV, &tvi))
            {
                auto* data = reinterpret_cast<const TVData*>(tvi.lParam);
                if (data && data->node && data->node->nav && !data->node->nav->href.empty())
                {
                    g_book->OnTreeSelChanged(data->node->nav->href.c_str());
                }
            }
            break;
        }

        case TVN_ITEMEXPANDING:
        {
            auto* pnmtv = reinterpret_cast<NMTREEVIEW*>(l);
            if (pnmtv->action != TVE_EXPAND) break;

            auto* data = reinterpret_cast<TVData*>(pnmtv->itemNew.lParam);
            if (!data || data->inserted) break;   // 已经插过直接返回

            HTREEITEM hParent = pnmtv->itemNew.hItem;
            for (size_t idx : data->node->childIdx)
            {
                const TreeNode& child = (*data->all)[idx];
                TVINSERTSTRUCTW tvis{};
                tvis.hParent = hParent;
                tvis.hInsertAfter = TVI_LAST;
                tvis.itemex.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
                tvis.itemex.pszText = LPSTR_TEXTCALLBACKW;
                tvis.itemex.cChildren = child.childIdx.empty() ? 0 : 1;
                auto* childData = new TVData{ &child, data->all };
                tvis.itemex.lParam = reinterpret_cast<LPARAM>(childData);
                TreeView_InsertItem(g_hwndTV, &tvis);
            }
            data->inserted = true;   // 标记已插入
            break;
        }
        break;
        }
    }
    case WM_VSCROLL: {  }
    case WM_HSCROLL: {  }
    case WM_COMMAND: {
        switch (LOWORD(w)) {
        case IDM_TOGGLE_CSS: {
            g_cfg.enableCSS = !g_cfg.enableCSS;
            CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_CSS,
                MF_BYCOMMAND | (g_cfg.enableCSS ? MF_CHECKED : MF_UNCHECKED));
            //if (!g_cfg.enableCSS)
            //{
            //    EnableMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_GLOBAL_CSS, MF_BYCOMMAND | MF_GRAYED);
            //}
            //else
            //{
            //    EnableMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_GLOBAL_CSS, MF_BYCOMMAND | MF_ENABLED);
            //}
            //break;
        }
        case IDM_TOGGLE_JS: {
            g_cfg.enableJS = !g_cfg.enableJS;          // 切换状态
            CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_JS,
                MF_BYCOMMAND | (g_cfg.enableJS ? MF_CHECKED : MF_UNCHECKED));

            break;
        }
        case IDM_TOGGLE_GLOBAL_CSS:
        {
            g_cfg.enableGlobalCSS = !g_cfg.enableGlobalCSS;          // 切换状态
            CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_GLOBAL_CSS,
                MF_BYCOMMAND | (g_cfg.enableGlobalCSS ? MF_CHECKED : MF_UNCHECKED));

            break;
        }
        case IDM_TOGGLE_PREPROCESS_HTML:
        {
            g_cfg.enablePreprocessHTML = !g_cfg.enablePreprocessHTML;          // 切换状态
            CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_PREPROCESS_HTML,
                MF_BYCOMMAND | (g_cfg.enablePreprocessHTML ? MF_CHECKED : MF_UNCHECKED));

            break;
        }
        case IDM_INFO_FONTS:{
            
            g_activeFonts = g_container->m_canvas->getCurrentFonts();
  
            
            ShowActiveFontsDialog(g_hWnd);
            //PrintSystemFontFamilies();
            break;
        }
        break;

        }
    }
    }
    return DefWindowProc(h, m, w, l);

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
LRESULT CALLBACK TooltipProc(HWND hwnd, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_PAINT:
    {

        if (!g_tooltip_container ||!g_tooltip_container->m_canvas)
        {
            OutputDebugStringA("[TooltipProc] self or doc null\n");
            break;
        }


        // 让 litehtml 画到 hdc
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);          // ← 必须
        g_tooltip_container->m_canvas->present(hdc, 0, 0);
        EndPaint(hwnd, &ps);
        //SaveHDCAsBmp(hdc, w, h, L"tooltip_dump.bmp");

        return 0;
    }
    case WM_DESTROY: {

        return 0;
    }
    case WM_ERASEBKGND: {
        return 1;
    }
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
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_TREEVIEW_CLASSES };
    InitCommonControlsEx(&icc);
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
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0,           // 位置和大小由 WM_SIZE 调整
        g_hWnd, nullptr, g_hInst, nullptr);

    g_hwndTV = CreateWindowExW(
        0, WC_TREEVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER |
        TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS | TVS_NOTOOLTIPS,
        0, 0, 200, 600,
        g_hWnd, (HMENU)100, g_hInst, nullptr);
    g_hView = CreateWindowExW(
        0, L"EPUBView", nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_CLIPSIBLINGS,
        0, 0, 1, 1,
        g_hWnd, (HMENU)101, g_hInst, nullptr);

    g_hTooltip = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_COMPOSITED,
        THUMB_CLASS, nullptr,
        WS_POPUP | WS_BORDER | WS_CLIPCHILDREN,
        0, 0, 300, 200,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr);

    
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
    EnableMenuItem(hMenu, IDM_TOGGLE_JS, MF_BYCOMMAND | MF_GRAYED);
    EnableMenuItem(hMenu, IDM_TOGGLE_PREPROCESS_HTML, MF_BYCOMMAND | MF_GRAYED);
    //if(!g_cfg.enableCSS)
    //{
    //    EnableMenuItem(hMenu, IDM_TOGGLE_GLOBAL_CSS, MF_BYCOMMAND | MF_GRAYED);
    //}
    //else 
    //{
    //    EnableMenuItem(hMenu, IDM_TOGGLE_GLOBAL_CSS, MF_BYCOMMAND | MF_ENABLED);
    //}
    EnableMenuItem(hMenu, IDM_TOGGLE_MENUBAR_WINDOW, MF_BYCOMMAND | MF_GRAYED);
    EnableMenuItem(hMenu, IDM_TOGGLE_SCROLLBAR_WINDOW, MF_BYCOMMAND | MF_GRAYED);
    EnableMenuItem(hMenu, IDM_TOGGLE_STATUS_WINDOW, MF_BYCOMMAND | MF_GRAYED);
    EnableMenuItem(hMenu, IDM_TOGGLE_TOC_WINDOW, MF_BYCOMMAND | MF_GRAYED);
    EnableClearType();
    // =====初始化隐藏=====
    ShowWindow(g_hStatus, SW_HIDE);
    ShowWindow(g_hwndTV, SW_HIDE);
    ShowWindow(g_hView, SW_HIDE);
    ShowWindow(g_hTooltip, SW_HIDE);
    // ====================
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


void EPUBBook::init_doc(std::string html) {
    /* 2. 加载 HTML */


    if (html.empty()) return;
    if (g_cfg.enableGlobalCSS) { html = insert_global_css(html); }
    if (g_cfg.enablePreprocessHTML) { html = PreprocessHTML(html);}
    g_container->clear();
    //g_container.reset();

    // 完整兜底 UA 样式表（litehtml 专用）
    // 在 EPUBBook::init_doc 里加诊断

    // 打印前 200 字符
    //OutputDebugStringA(std::string(html.c_str(), html.size()).c_str());
 
        g_container->m_doc =
            litehtml::document::createFromString({ html.c_str() , litehtml::encoding::utf_8}, g_container.get());

    /* 关键：DOM 刚建好，立即回填内联脚本 */
    if (g_cfg.enableJS) {
        g_bootstrap->bind_host_objects();

        g_bootstrap->run_pending_scripts(); // 立即执行
        //save_document_html(g_container->m_doc);
    }


    g_scrollY = 0;
}
// ---------- 点击目录跳转 ----------
void EPUBBook::OnTreeSelChanged(const wchar_t* href)
{
    if (!href || !*href || !g_container->m_doc) return;

    
    /* 1. 分离文件路径与锚点 */
    std::wstring whref(href);
    size_t pos = whref.find(L'#');
    std::wstring file_path = (pos == std::wstring::npos) ? whref : whref.substr(0, pos);
    std::string  id = (pos == std::wstring::npos) ? "" :
        w2a(whref.substr(pos + 1));

    if (g_last_html_path != file_path){
        std::string html = g_book->load_html(file_path.c_str());
        g_book->init_doc(html);
        g_states.needRelayout.store(true, std::memory_order_release);
        g_last_html_path = file_path;
        UpdateCache();
        SendMessage(g_hView, WM_EPUB_UPDATE_SCROLLBAR, 0, 0);
    }

    /* 3. 跳转到锚点 */
    if (!id.empty())
    {
        std::wstring cssSel = a2w(id);   // 转成宽字符
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
    return m_canvas->backend()->text_width(text, hFont);
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
        
        std::wstring cssSel = a2w(url);   // 转成宽字符
        // WM_APP + 3 约定为“跳转到锚点选择器”
        PostMessageW(g_hView, WM_EPUB_ANCHOR,
            reinterpret_cast<WPARAM>(_wcsdup(cssSel.c_str())), 0);
        return;
    }
    g_book->OnTreeSelChanged(a2w(url).c_str());

    // 外部链接：交给宿主
    //ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

bool SimpleContainer::on_element_click(const litehtml::element::ptr& el) 
{
    OutputDebugStringA(el->get_tagName());
    OutputDebugStringA("\n");
    return false;
}
void SimpleContainer::on_mouse_event(const litehtml::element::ptr& el, litehtml::mouse_event event)
{
    if (event == litehtml::mouse_event::mouse_event_enter)
    {
        auto link = find_link_in_chain(el);

        std::string html;
        if (link)
        {
            const char* href_raw = link->get_attr("href");
            if (!href_raw) {
                g_book->hide_tooltip();
                return;

            }
            std::string href = href_raw;


            std::string id = extract_anchor(href.c_str());
            if (!id.empty())
                html = html_of_anchor_paragraph(g_container->m_doc.get(), id);
        }

        if (!html.empty())
        {
            POINT pt;
            GetCursorPos(&pt);   // pt.x, pt.y 为屏幕坐标
            ScreenToClient(g_hView, &pt);   // 现在 pt.x, pt.y 是相对于窗口客户区的坐标
            g_book->show_tooltip(std::move(html), pt.x, pt.y);
        }
    }
    else
    {
        g_book->hide_tooltip();
    }
}

// 返回 p 指向的 UTF-8 字符所占字节数（1~4）
inline size_t utf8_char_len(const char* p)
{
    unsigned char c = static_cast<unsigned char>(*p);
    if (c < 0x80) return 1;
    if ((c >> 5) == 0x06) return 2;
    if ((c >> 4) == 0x0E) return 3;
    if ((c >> 3) == 0x1E) return 4;
    return 1; // 容错
}

//void SimpleContainer::split_text(const char* text,
//    const std::function<void(const char*)>& on_word,
//    const std::function<void(const char*)>& on_space)
//{
//    if (!text || !*text) return;
//
//    const char* p = text;
//    while (*p)
//    {
//        /* ---------- 空格段 ---------- */
//        if (std::isspace(static_cast<unsigned char>(*p)))
//        {
//            const char* start = p;
//            while (*p && std::isspace(static_cast<unsigned char>(*p))) ++p;
//            std::string token(start, p);
//            if (on_space) on_space(token.c_str());
//            continue;
//        }
//        /* ---------- 单词段 ---------- */
//        const char* start = p;
//        while (*p && !std::isspace(static_cast<unsigned char>(*p))) ++p;
//        std::string token(start, p);
//        if (on_word) on_word(token.c_str());
//    }
//}


void SimpleContainer::split_text(const char* text,
    const std::function<void(const char*)>& on_word,
    const std::function<void(const char*)>& on_space)
{
    if (!text || !*text) return;

    // UTF-8 → ICU UnicodeString
    icu::UnicodeString ustr = icu::UnicodeString::fromUTF8(text);

    // 创建行断行迭代器（UAX #14）
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::BreakIterator> brk(
        icu::BreakIterator::createLineInstance(icu::Locale::getDefault(), status));
    if (U_FAILURE(status)) return;

    brk->setText(ustr);

    int32_t prev = brk->first();
    for (int32_t curr = brk->next(); curr != icu::BreakIterator::DONE;
        prev = curr, curr = brk->next())
    {
        icu::UnicodeString seg(ustr, prev, curr - prev);

        // 判断这一段是不是纯空格
        bool all_space = true;
        for (int32_t i = 0; i < seg.length(); ++i) {
            if (!u_isspace(seg.char32At(i))) { all_space = false; break; }
        }

        std::string out;
        seg.toUTF8String(out);

        if (all_space) {
            if (on_space) on_space(out.c_str());
        }
        else {
            if (on_word) on_word(out.c_str());
        }
    }
}
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



litehtml::pixel_t SimpleContainer::pt_to_px(float pt) const {
    return MulDiv(pt, GetDeviceCaps(GetDC(nullptr), LOGPIXELSY), 72);
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


// ---------- 工具 ----------

static std::vector<std::string> split_ws(const std::string& s)
{
    std::vector<std::string> v;
    std::string cur;
    for (char c : s)
    {
        if (std::isspace(static_cast<unsigned char>(c)))
        {
            if (!cur.empty()) { v.push_back(cur); cur.clear(); }
        }
        else cur += c;
    }
    if (!cur.empty()) v.push_back(cur);
    return v;
}
static inline bool gumbo_tag_is_void(GumboTag tag)
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
// ---------- 递归处理 DOM ----------
static void walk(GumboNode* node, std::ostringstream& out, bool& inside_style)
{
    if (node->type == GUMBO_NODE_TEXT)
    {
        if (inside_style)
        {
            // 对 <style> 里的文本做 CSS 替换
            static std::regex re(R"rx(\[epub\|type\s*~\s*=\s*"([^"]+)"])rx",
                std::regex::icase);
            std::string txt = std::regex_replace(node->v.text.text, re, ".epub-$1");
            out << txt;
        }
        else
            out << node->v.text.text;
        return;
    }
    if (node->type != GUMBO_NODE_ELEMENT) return;

    GumboElement& el = node->v.element;
    const char* tag = gumbo_normalized_tagname(el.tag);
    out << '<' << tag;

    // 处理属性
    std::string new_class;
    for (unsigned i = 0; i < el.attributes.length; ++i)
    {
        GumboAttribute* a = static_cast<GumboAttribute*>(el.attributes.data[i]);
        std::string name = to_lower(a->name);

        if (name.rfind("epub:", 0) == 0)
        {
            if (name == "epub:type")
            {
                for (const auto& v : split_ws(a->value))
                    new_class += "epub-" + v + " ";
            }
            continue; // 丢弃 epub:*
        }

        // style 属性内也可能出现 CSS，简单替换
        if (name == "style")
        {
            static std::regex re(R"re(\[epub\|type\s*~\s*=\s*"([^"]+)"\])re",
                std::regex::icase);
                std::string fixed = std::regex_replace(a->value, re, ".epub-$1");
                out << " style=\"" << fixed << "\"";
                continue;
        }

        out << ' ' << a->name << "=\"" << a->value << "\"";
    }

    if (!new_class.empty())
    {
        new_class.pop_back(); // 去尾空格
        bool has_class = false;
        for (unsigned i = 0; i < el.attributes.length; ++i)
        {
            GumboAttribute* a = static_cast<GumboAttribute*>(el.attributes.data[i]);
            if (to_lower(a->name) == "class")
            {
                out << " class=\"" << a->value << " " << new_class << "\"";
                has_class = true;
                break;
            }
        }
        if (!has_class)
            out << " class=\"" << new_class << "\"";
    }

    if (gumbo_tag_is_void(el.tag))
    {
        out << " />";
        return;
    }

    out << '>';
    bool was_inside_style = inside_style;
    if (std::string(tag) == "style") inside_style = true;
    for (unsigned i = 0; i < el.children.length; ++i)
        walk(static_cast<GumboNode*>(el.children.data[i]), out, inside_style);
    inside_style = was_inside_style;
    out << "</" << tag << '>';
}

// ---------- 对外单一接口 ----------
std::string sanitize_epub_attr(const std::string html)
{
    GumboOutput* out = gumbo_parse(html.c_str());
    std::ostringstream oss;
    bool inside_style = false;
    walk(out->root, oss, inside_style);
    gumbo_destroy_output(&kGumboDefaultOptions, out);
    return oss.str();
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
    //html = sanitize_epub_attr(html);
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
    m_canvas->backend()->draw_text(hdc, text, hFont, color, pos);
}

void SimpleContainer::draw_image(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const std::string& url, const std::string& base_url)
{
    m_canvas->backend()->draw_image(hdc, layer, url, base_url);
}

void SimpleContainer::draw_solid_fill(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::web_color& color)
{
    m_canvas->backend()->draw_solid_fill(hdc, layer, color);
}

void SimpleContainer::draw_linear_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::background_layer::linear_gradient& gradient)
{
    m_canvas->backend()->draw_linear_gradient(hdc, layer, gradient);
}

void SimpleContainer::draw_radial_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::background_layer::radial_gradient& gradient)
{
    m_canvas->backend()->draw_radial_gradient(hdc, layer, gradient);
}

void SimpleContainer::draw_conic_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::background_layer::conic_gradient& gradient)
{
    m_canvas->backend()->draw_conic_gradient(hdc, layer, gradient);
}

void SimpleContainer::draw_borders(litehtml::uint_ptr hdc,
    const litehtml::borders& borders,
    const litehtml::position& pos,
    bool root)
{
    m_canvas->backend()->draw_borders(hdc, borders, pos, root);
}


litehtml::uint_ptr SimpleContainer::create_font(const litehtml::font_description& descr, const litehtml::document* doc, litehtml::font_metrics* fm)
{
    return m_canvas->backend()->create_font(descr, doc, fm);
}

void SimpleContainer::delete_font(litehtml::uint_ptr hFont)
{
    if (m_canvas && m_canvas->backend() && hFont)
    {
        m_canvas->backend()->delete_font(hFont);
    }
}


// ---------- 11. 列表标记 ----------------------------------------------
void SimpleContainer::draw_list_marker(litehtml::uint_ptr hdc,
    const litehtml::list_marker& marker)
{
    m_canvas->backend()->draw_list_marker(hdc, marker);
    //HDC dc = reinterpret_cast<HDC>(hdc);
    //SetBkMode(dc, TRANSPARENT);
    //SetTextColor(dc, RGB(0, 0, 0));

    //std::wstring txt;
    //if (marker.marker_type == litehtml::list_style_type_disc)
    //    txt = L"•";
    //else if (marker.marker_type == litehtml::list_style_type_decimal)
    //    txt = std::to_wstring(marker.index) + L".";

    //HFONT hOld = (HFONT)SelectObject(dc, m_hDefaultFont);
    //TextOutW(dc, marker.pos.x, marker.pos.y, txt.c_str(), (int)txt.size());
    //SelectObject(dc, hOld);
}


void SimpleContainer::set_clip(const litehtml::position& pos,
    const litehtml::border_radiuses& radius)
{
    m_canvas->backend()->set_clip(pos, radius);
    //// 取得当前绘制 HDC
    //HDC hdc = reinterpret_cast<HDC>(m_last_hdc);
    //if (!hdc) return;

    //// 1. 如果四个角半径都为 0，退化为矩形
    //if (radius.top_left_x == 0 && radius.top_right_x == 0 &&
    //    radius.bottom_left_x == 0 && radius.bottom_right_x == 0)
    //{
    //    HRGN rgn = CreateRectRgn(pos.x, pos.y,
    //        pos.x + pos.width,
    //        pos.y + pos.height);
    //    SelectClipRgn(hdc, rgn);
    //    DeleteObject(rgn);
    //    return;
    //}

    //// 2. 否则用圆角矩形
    ////    CreateRoundRectRgn 的圆角直径 = 2 * radius
    //int rx = std::max({ radius.top_left_x, radius.top_right_x,
    //                   radius.bottom_left_x, radius.bottom_right_x });
    //int ry = rx;   // 简化：保持 1:1 圆角；如需椭圆角可分别传 rx/ry
    //HRGN rgn = CreateRoundRectRgn(pos.x, pos.y,
    //    pos.x + pos.width,
    //    pos.y + pos.height,
    //    rx * 2, ry * 2);
    //SelectClipRgn(hdc, rgn);
    //DeleteObject(rgn);
}

void SimpleContainer::del_clip()
{
    m_canvas->backend()->del_clip();
    /*SelectClipRgn(reinterpret_cast<HDC>(m_last_hdc), nullptr);*/
}

// DirectWrite backend
/* ---------- 构造 ---------- */
D2DCanvas::D2DCanvas(int w, int h, HWND hwnd)
    : m_w(w), m_h(h), m_hwnd(hwnd){
    D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory1),            // 接口 GUID
        nullptr,                          // 工厂选项（可 nullptr）
        reinterpret_cast<void**>(m_d2dFactory.GetAddressOf()));
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

    RECT rc; GetClientRect(m_hwnd, &rc);
    float dpiX, dpiY;
    m_d2dFactory->GetDesktopDpi(&dpiX, &dpiY);
    float scale = dpiX / 96.0f;

    D2D1_SIZE_U size = D2D1::SizeU(
        static_cast<UINT>(rc.right * scale),
        static_cast<UINT>(rc.bottom * scale));
  
    ComPtr<ID2D1HwndRenderTarget> hwndRT;
    m_d2dFactory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(/*
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            dpiX, dpiY*/),
        D2D1::HwndRenderTargetProperties(
            m_hwnd, size),
        hwndRT.GetAddressOf());   // ← 关键：GetAddressOf()
    //ComPtr<ID2D1RenderTarget> g_d2dRT;
    hwndRT.As(&m_d2dRT);

    //// 创建完 m_d2dRT 后立即加
    //ComPtr<IDWriteFactory> dwf;
    //DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
    //    __uuidof(IDWriteFactory),
    //    reinterpret_cast<IUnknown**>(dwf.GetAddressOf()));
    //ComPtr<IDWriteRenderingParams> rp;
    //dwf->CreateCustomRenderingParams(1.0f, 1.0f, 1.0f,
    //    DWRITE_PIXEL_GEOMETRY_RGB,
    //    DWRITE_RENDERING_MODE_CLEARTYPE_GDI_CLASSIC,
    //    &rp);
    //m_d2dRT->SetTextRenderingParams(rp.Get());

    m_backend = std::make_unique<D2DBackend>(w, h, m_d2dRT);

}
/* ---------- 把缓存位图贴到窗口 ---------- */
void D2DCanvas::present(HDC hdc, int x, int y)
{
    if (!m_d2dRT) return;
    
    m_backend->m_rt->GetBitmap(&m_bmp);

    // 2. 开始绘制
    m_d2dRT->BeginDraw();
    //m_devCtx->Clear(D2D1::ColorF(D2D1::ColorF::White));
    m_d2dRT->DrawBitmap(m_bmp.Get());
    m_d2dRT->EndDraw();



}


// ---------- 辅助：UTF-8 ↔ UTF-16 ----------



// ---------- 实现 ----------
ComPtr<ID2D1SolidColorBrush> D2DBackend::getBrush(const litehtml::web_color& c)
{
    uint32_t key = (c.alpha << 24) | (c.red << 16) | (c.green << 8) | c.blue;
    auto it = m_brushPool.find(key);
    if (it != m_brushPool.end()) return it->second;

    ComPtr<ID2D1SolidColorBrush> brush;
    m_rt->CreateSolidColorBrush(
        D2D1::ColorF(c.red / 255.f, c.green / 255.f, c.blue / 255.f, c.alpha / 255.f),
        &brush);
    m_brushPool[key] = brush;
    return brush;
}

ComPtr<IDWriteTextLayout> D2DBackend::getLayout(const std::wstring& txt,
    const FontPair* fp,
    float maxW)
{
    LayoutKey k{ txt, fp->descr.hash(), maxW};
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

void D2DBackend::draw_text(litehtml::uint_ptr hdc,
    const char* text,
    litehtml::uint_ptr hFont,
    litehtml::web_color color,
    const litehtml::position& pos)
{
    if (!text || !*text || !hFont) return;
    FontPair* fp = reinterpret_cast<FontPair*>(hFont);
    if (!fp) return;

    // 1. 画刷
    auto brush = getBrush(color);
    if (!brush) return;

    // 2. 文本
    std::wstring wtxt = a2w(text);
    if (wtxt.empty()) return;

    float maxW =  8192.0f;
    auto layout = getLayout(wtxt, fp, maxW);
    if (!layout) return;

    // 3. 绘制文本
    m_rt->DrawTextLayout(D2D1::Point2F(static_cast<float>(pos.x),
        static_cast<float>(pos.y)),
        layout.Get(), brush.Get());

    // 4. 绘制装饰线（下划线 / 删除线 / 上划线）
    draw_decoration(fp, color, pos, layout.Get());
}
void D2DBackend::draw_decoration(const FontPair* fp,
    litehtml::web_color color,
    const litehtml::position& pos,
    IDWriteTextLayout* layout)
{
    if (fp->descr.decoration_line == litehtml::text_decoration_line_none)
        return;

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
    auto brush = getBrush(fp->descr.decoration_color.is_current_color
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
        m_rt->DrawLine({ x0, y }, { x1, y }, brush.Get(), thick);
    }

    /* 删除线 */
    if (fp->descr.decoration_line & litehtml::text_decoration_line_line_through)
    {
        const float y = yBase - lineMetrics[0].height * 0.35f;
        m_rt->DrawLine({ x0, y }, { x1, y }, brush.Get(), thick);
    }

    /* 上划线 */
    if (fp->descr.decoration_line & litehtml::text_decoration_line_overline)
    {
        const float y = yBase - lineMetrics[0].height;
        m_rt->DrawLine({ x0, y }, { x1, y }, brush.Get(), thick);
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

    /* ---------- 1. 取缓存位图 ---------- */
    auto it = g_container->m_img_cache.find(url);
    if (it == g_container->m_img_cache.end()) return;
    const ImageFrame& frame = it->second;
    if (frame.rgba.empty()) return;

    ComPtr<ID2D1Bitmap> bmp;
    auto d2d_it = m_d2dBitmapCache.find(url);
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
        if (SUCCEEDED(m_rt->CreateBitmap(
            D2D1::SizeU(frame.width, frame.height),
            frame.rgba.data(),
            frame.stride,
            bp,
            &bmp)))
        {
            m_d2dBitmapCache.emplace(url, bmp);
        }
    }
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
    m_rt->DrawBitmap(bmp.Get(), drawRect, 1.0f,
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
    if (!m_rt) return;

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
    m_rt->GetFactory(&factory);

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
        m_rt->CreateSolidColorBrush(clr, &brush);

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
        m_rt->DrawGeometry(sidePath.Get(), brush.Get(), side.width);
    }
}
// 工具：转小写
std::wstring  D2DBackend::toLower(std::wstring s)
{
    std::transform(s.begin(), s.end(), s.begin(), towlower);
    return s;
}

//// 1. 静态表映射
//std::optional<std::wstring> D2DBackend::mapStatic(const std::wstring& key)
//{
//    auto it = g_fontAlias.find(key);
//    return (it != g_fontAlias.end()) ? std::optional{ it->second } : std::nullopt;
//}

// 2. 动态表映射
// 声明改为静态，并把系统字体集合作为参数
//std::optional<std::wstring>
//D2DBackend::mapDynamic(const std::wstring& key)
//{
//   
//    auto it = g_fontAliasDynamic.find(key);
//    if (it == g_fontAliasDynamic.end())
//        return std::nullopt;
//
//    for (const std::wstring& face : it->second)
//    {
//        UINT32 index = 0;
//        BOOL   exists = FALSE;
//        m_sysFontColl->FindFamilyName(face.c_str(), &index, &exists);
//        if (exists)
//            return face;
//    }
//    return std::nullopt;
//}
// 3. 解析单个 token
//std::wstring D2DBackend::resolveFace(const std::wstring& raw)
//{
//    const std::wstring key = toLower(raw);
//
//    //if (auto v = mapStatic(key))   return *v;
//    //if (auto v = mapDynamic(key))  return *v;
//    return key;                    // 原样返回
//}

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
    /*----------------------------------------------------------
      2. 逐个尝试创建 IDWriteTextFormat
    ----------------------------------------------------------*/
    // 2. 自定义查找函数
    auto FindFamilyIndex = [](IDWriteFontCollection* coll,
        const std::wstring& name,
        UINT32& index,
        BOOL& exists) -> bool
        {
            exists = FALSE;
            if (!coll) return false;

            UINT32 count = coll->GetFontFamilyCount();;

            for (UINT32 i = 0; i < count; ++i)
            {
                Microsoft::WRL::ComPtr<IDWriteFontFamily> fam;
                if (FAILED(coll->GetFontFamily(i, &fam)))
                    continue;

                Microsoft::WRL::ComPtr<IDWriteLocalizedStrings> names;
                if (FAILED(fam->GetFamilyNames(&names)))
                    continue;

                UINT32 nameLen = 0;
                if (FAILED(names->GetStringLength(0, &nameLen)))
                    continue;

                std::wstring buf(nameLen + 1, L'\0');
                if (FAILED(names->GetString(0, buf.data(), nameLen + 1)))
                    continue;
                buf.resize(nameLen);            // 去掉末尾 NUL

                if (_wcsicmp(buf.c_str(), name.c_str()) == 0)
                {
                    index = i;
                    exists = TRUE;
                    return true;
                }
            }
            return true;   // 遍历完但没找到
        };



    ComPtr<IDWriteTextFormat> fmt;
    for (auto f : faces)
    {
        fmt = m_fontCache.get(f, descr, m_privateFonts.Get(), m_sysFontColl.Get());
        if (fmt) break;
    }
    //for (const auto& f : faces)
    //{
    //    std::wstring name = f;
    //    UINT32 index = 0;
    //    BOOL   exists = FALSE;
    //    FontKey exact{ f, descr.weight, descr.style == litehtml::font_style_italic };
    //    // 1) 先找完全匹配
    //    auto it = g_book->m_fontBin.find(exact);
    //    if (it != g_book->m_fontBin.end()) {
    //        name = it->second;                 // 命中
    //    }
    //    else {
    //        // 2) 退而求其次：只匹配 family
    //        for (const auto& kv : g_book->m_fontBin) {
    //            if (kv.first.family == f) {      // 忽略 weight/italic
    //                name = kv.second;
    //                break;
    //            }
    //        }
    //    }
    //    /* 1. 私有集合 */
    //    if (m_privateFonts)
    //    {
    //        FindFamilyIndex(m_privateFonts.Get(), name, index, exists);
    //        if (exists &&
    //            SUCCEEDED(m_dwrite->CreateTextFormat(
    //                name.c_str(), m_privateFonts.Get(),
    //                static_cast<DWRITE_FONT_WEIGHT>(descr.weight),
    //                descr.style == litehtml::font_style_italic ? DWRITE_FONT_STYLE_ITALIC
    //                : DWRITE_FONT_STYLE_NORMAL,
    //                DWRITE_FONT_STRETCH_NORMAL,
    //                static_cast<float>(descr.size),
    //                L"en-us",
    //                &fmt)))
    //        {
    //            //SetFontMatric(descr, m_privateFonts.Get(), name, index, fm);
    //            //m_actualFamily = f;          // 保存命中的家族名
    //            //OutputDebugStringW((L"[private] 命中：" + m_actualFamily + L"\n").c_str());
    //            break;
    //        }
    //    }

    //    /* 2. 系统集合 */
    //    index = 0;
    //    exists = FALSE;
    //    FindFamilyIndex(m_sysFontColl.Get(), name, index, exists);
    //    if (exists &&
    //        SUCCEEDED(m_dwrite->CreateTextFormat(
    //            name.c_str(), nullptr,
    //            static_cast<DWRITE_FONT_WEIGHT>(descr.weight),
    //            descr.style == litehtml::font_style_italic ? DWRITE_FONT_STYLE_ITALIC
    //            : DWRITE_FONT_STYLE_NORMAL,
    //            DWRITE_FONT_STRETCH_NORMAL,
    //            static_cast<float>(descr.size),
    //            L"en-us",
    //            &fmt)))
    //    {
    //        //SetFontMatric(descr, m_sysFontColl.Get(), name, index, fm);
    //        //m_actualFamily = f;              // 保存命中的家族名
    //        //OutputDebugStringW((L"[system] 命中：" + m_actualFamily + L"\n").c_str());
    //        break;
    //    }
    //    OutputDebugStringW((L"[DWrite] 未找到字体：" + name + L"\n").c_str());
    //}

    //if (!fmt){
    //    std::wstring name = a2w(g_cfg.default_font_name);
    //    OutputDebugStringW(L"[DWrite] 未找到字体:");
    //    OutputDebugStringW(a2w(descr.family).c_str());
    //    OutputDebugStringW(L"  使用默认字体：");
    //    OutputDebugStringW(name.c_str());
    //    OutputDebugStringW(L"\n");
    //    UINT32 index = 0;
    //    BOOL   exists = FALSE;
    //    FindFamilyIndex(m_sysFontColl.Get(), name, index, exists);
    //    if (exists &&
    //        SUCCEEDED(m_dwrite->CreateTextFormat(
    //            name.c_str(), nullptr,
    //            static_cast<DWRITE_FONT_WEIGHT>(descr.weight),
    //            descr.style == litehtml::font_style_italic ? DWRITE_FONT_STYLE_ITALIC
    //            : DWRITE_FONT_STYLE_NORMAL,
    //            DWRITE_FONT_STRETCH_NORMAL,
    //            static_cast<float>(descr.size),
    //            L"en-us",
    //            &fmt)))
    //    {
    //        //SetFontMatric(descr, m_sysFontColl.Get(), name, index, fm);
    //    }   
    //}

    if (!fmt) {
        OutputDebugStringW(L"[DWrite] 加载默认字体失败\n");
        return 0; }

 
    /*----------------------------------------------------------
     用命中的字体家族名去拿真正的字体并计算度量
   ----------------------------------------------------------*/

    ComPtr<IDWriteFontFamily>     family;
    ComPtr<IDWriteFont>           dwFont;


    UINT32 len = fmt->GetFontFamilyNameLength();


    std::wstring currentFamily;
    WCHAR buf[512];
    //UINT32 len = fmt->GetFontFamilyNameLength();   // 可见字符数
    if (len + 1 < std::size(buf))
    {
        fmt->GetFontFamilyName(buf, len + 1);      // buf[len] == '\0'
        currentFamily.assign(buf, len);        // 19 字符 + 1 NUL = 20
    }

    /* 根据命中的来源选择集合 */
    ComPtr<IDWriteFontCollection> targetCollection;
    if (!currentFamily.empty())
    {
        std::wstring cleanName(currentFamily.c_str());   // 只保留可见字符
        cleanName.erase(std::find(cleanName.begin(), cleanName.end(), L'\0'), cleanName.end());
        FontKey exact{ cleanName, descr.weight, descr.style == litehtml::font_style_italic };
        // 1) 先找完全匹配
        auto it = g_book->m_fontBin.find(exact);
        if (it != g_book->m_fontBin.end()) {
            cleanName = it->second;                 // 命中
        }
        else {
            // 2) 退而求其次：只匹配 family
            for (const auto& kv : g_book->m_fontBin) {
                if (kv.first.family == cleanName) {      // 忽略 weight/italic
                    cleanName = kv.second;
                    break;
                }
            }
        }

        UINT32 index = 0;
        BOOL   exists = FALSE;

        if (m_privateFonts &&
            FindFamilyIndex(m_privateFonts.Get(), cleanName, index, exists) && exists)
        {
            targetCollection = m_privateFonts;
        }
        else
        {
            FindFamilyIndex(m_sysFontColl.Get(), cleanName, index, exists);   // 只在系统集合里再查一次
            targetCollection = m_sysFontColl;
        }



        if (exists)
        {
            targetCollection->GetFontFamily(index, &family);
            family->GetFirstMatchingFont(
                static_cast<DWRITE_FONT_WEIGHT>(descr.weight),
                DWRITE_FONT_STRETCH_NORMAL,
                descr.style == litehtml::font_style_italic ? DWRITE_FONT_STYLE_ITALIC
                : DWRITE_FONT_STYLE_NORMAL,
                &dwFont);



            DWRITE_FONT_METRICS fm0 = {};
            dwFont->GetMetrics(&fm0);

            float dip = static_cast<float>(descr.size) / fm0.designUnitsPerEm;

            fm->font_size = descr.size;

            fm->ascent = static_cast<int>(fm0.ascent * dip + 0.5f);
            fm->descent = static_cast<int>(fm0.descent * dip + 0.5f);
            fm->height = static_cast<int>((fm0.ascent + fm0.descent + fm0.lineGap)
                * dip * g_cfg.line_height_multiplier + 0.5f);
            fm->x_height = static_cast<int>(fm0.xHeight * dip + 0.5f);
            
            // 1. 等宽数字 0 的宽度
            ComPtr<IDWriteTextLayout> tmpLayout;
            std::wstring zero = L"0";
            m_dwrite->CreateTextLayout(zero.c_str(), 1, fmt.Get(),
                65536.0f, 65536.0f, &tmpLayout);
            DWRITE_TEXT_METRICS tm = {};
            if (tmpLayout) tmpLayout->GetMetrics(&tm);
            fm->ch_width = static_cast<int>(tm.widthIncludingTrailingWhitespace + 0.5f);
            if (fm->ch_width <= 0) fm->ch_width = fm->font_size * 3 / 5; // 兜底

            // 2. 上下标偏移：简单取 x-height 的 1/2
            fm->sub_shift = fm->x_height / 2;
            fm->super_shift = fm->x_height / 2;

            // 3. 修正 baseline
            SetStatus(1, cleanName.c_str());
            //char buf[256];
            //snprintf(buf, sizeof(buf),
            //    "[DWrite] %ls: designUnitsPerEm=%u, ascent=%u, descent=%u, lineGap=%d\n",
            //    cleanName.c_str(), fm0.designUnitsPerEm, fm0.ascent, fm0.descent, fm0.lineGap);
            //OutputDebugStringA(buf);
            g_line_height = fm->height;
        }
    }
    /*----------------------------------------------------------
      6. 返回句柄
    ----------------------------------------------------------*/
    FontPair* fp = new FontPair{ fmt, descr };

    return reinterpret_cast<litehtml::uint_ptr>(fp);
}

void D2DBackend::delete_font(litehtml::uint_ptr h)
{
    if (h) delete reinterpret_cast<FontPair*>(h);
}

//void D2DBackend::draw_background(litehtml::uint_ptr hdc,
//    const std::vector<litehtml::background_paint>& bg, std::unordered_map<std::string, ImageFrame>& img_cache)
//{
//    assert(m_rt && "render target is null");
//    if (bg.empty()) return;
//
//    ComPtr<ID2D1BitmapRenderTarget> rt = m_rt;
//
//    for (const auto& b : bg)
//    {
//        //--------------------------------------------------
//        // 1. 纯色背景
//        //--------------------------------------------------
//        if (b.image.empty())
//        {
//            ComPtr<ID2D1SolidColorBrush> brush;
//            rt->CreateSolidColorBrush(
//                D2D1::ColorF(b.color.red / 255.0f,
//                    b.color.green / 255.0f,
//                    b.color.blue / 255.0f,
//                    b.color.alpha / 255.0f),
//                &brush);
//
//            D2D1_RECT_F rc = D2D1::RectF(
//                (float)b.border_box.left(), (float)b.border_box.top(),
//                (float)b.border_box.right(), (float)b.border_box.bottom());
//
//            rt->FillRectangle(rc, brush.Get());
//            continue;
//        }
//
//        //--------------------------------------------------
//        // 2. 图片背景
//        //--------------------------------------------------
//        auto it = img_cache.find(b.image);
//        if (it == img_cache.end()) continue;   // 还没加载
//
//        const ImageFrame& frame = it->second;
//        if (frame.rgba.empty()) continue;
//
//        // 如果已经缓存过 D2D 位图，直接拿；否则创建一次再缓存
//        ComPtr<ID2D1Bitmap> bmp;
//        auto d2d_it = m_d2dBitmapCache.find(b.image);
//        if (d2d_it != m_d2dBitmapCache.end())
//        {
//            bmp = d2d_it->second;
//        }
//        else
//        {
//            D2D1_BITMAP_PROPERTIES bp =
//                D2D1::BitmapProperties(
//                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
//                        D2D1_ALPHA_MODE_PREMULTIPLIED));
//
//            HRESULT hr = rt->CreateBitmap(
//                D2D1::SizeU(frame.width, frame.height),
//                frame.rgba.data(),
//                frame.stride,
//                bp,
//                &bmp);
//
//            if (SUCCEEDED(hr))
//                m_d2dBitmapCache.emplace(b.image, bmp);
//        }
//
//        if (!bmp) continue;
//
//        // 简单拉伸到 border_box；需要平铺/居中的话再算源/目标矩形
//        D2D1_RECT_F dst = D2D1::RectF(
//            (float)b.border_box.left(), (float)b.border_box.top(),
//            (float)b.border_box.right(), (float)b.border_box.bottom());
//
//        rt->DrawBitmap(bmp.Get(), dst);
//    }
//}
//

litehtml::pixel_t D2DBackend::text_width(const char* text, litehtml::uint_ptr hFont)
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
    const float maxHeight = 65536.0f;
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

static void build_rounded_rect_path(
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
    if (!m_rt) return;

    // 无圆角 → 矩形裁剪
    if (is_all_zero(bdr))
    {
        m_rt->PushAxisAlignedClip(
            D2D1::RectF(float(pos.left()), float(pos.top()),
                float(pos.right()), float(pos.bottom())),
            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        m_clipStack.emplace_back(nullptr);          // 标记为矩形
        return;
    }

    // 有圆角 → 用 PathGeometry + Layer
    ComPtr<ID2D1Factory> factory;
    m_rt->GetFactory(&factory);

    ComPtr<ID2D1PathGeometry> path;
    factory->CreatePathGeometry(&path);
    ComPtr<ID2D1GeometrySink> sink;
    path->Open(&sink);
    build_rounded_rect_path(sink, pos, bdr);        // 见下
    sink->Close();

    ComPtr<ID2D1Layer> layer;
    if (SUCCEEDED(m_rt->CreateLayer(nullptr, &layer)))
    {
        m_rt->PushLayer(
            D2D1::LayerParameters(D2D1::InfiniteRect(), path.Get()),
            layer.Get());
        m_clipStack.emplace_back(std::move(layer));
    }
}

void D2DBackend::del_clip()
{
    if (m_clipStack.empty()) return;
    if (m_clipStack.back())
        m_rt->PopLayer();           // 圆角
    else
        m_rt->PopAxisAlignedClip(); // 矩形
    m_clipStack.pop_back();
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

//litehtml::uint_ptr GdiBackend::create_font(const char* faceName,
//    int size,
//    int weight,
//    litehtml::font_style italic,
//    unsigned int decoration,
//    litehtml::font_metrics* fm)
//{
//    std::wstring wface = a2w(faceName ? faceName : "Segoe UI");
//    HFONT hFont = CreateFontW(
//        -size, 0, 0, 0,
//        weight,
//        italic == litehtml::font_style_italic ? TRUE : FALSE,
//        FALSE, FALSE, DEFAULT_CHARSET,
//        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
//        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
//        wface.c_str());
//
//    HDC dc = m_hdc;
//    HFONT old = (HFONT)SelectObject(dc, hFont);
//    GdiFont* f = new GdiFont{ hFont };
//    GetTextMetrics(dc, &f->tm);
//    SelectObject(dc, old);
//
//    if (fm) {
//        fm->ascent = f->tm.tmAscent;
//        fm->descent = f->tm.tmDescent;
//        fm->height = f->tm.tmHeight;
//        fm->x_height = f->tm.tmHeight / 2; // 近似
//    }
//    return reinterpret_cast<litehtml::uint_ptr>(f);
//}

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

void FreetypeBackend::draw_image(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const std::string& url, const std::string& base_url)
{
}

void FreetypeBackend::draw_solid_fill(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::web_color& color)
{
}

void FreetypeBackend::draw_linear_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::background_layer::linear_gradient& gradient)
{
}

void FreetypeBackend::draw_radial_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::background_layer::radial_gradient& gradient)
{
}

void FreetypeBackend::draw_conic_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::background_layer::conic_gradient& gradient)
{
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

void FreetypeBackend::draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker& marker)
{
}

litehtml::uint_ptr FreetypeBackend::create_font(const litehtml::font_description& descr, const litehtml::document* doc, litehtml::font_metrics* fm)
{
    return litehtml::uint_ptr();
}

/* ---------- 字体 ---------- */
//litehtml::uint_ptr FreetypeBackend::create_font(const char* faceName,
//    int size,
//    int weight,
//    litehtml::font_style italic,
//    unsigned int,
//    litehtml::font_metrics* fm)
//{
//    std::string path = "C:/Windows/Fonts/"; // 可扩展
//    path += faceName ? faceName : "segoeui.ttf";
//
//    FT_Face face;
//    if (FT_New_Face(m_lib, path.c_str(), 0, &face)) return 0;
//
//    auto* font = new FreetypeFont{ face, size };
//
//    if (fm) {
//        FT_Set_Pixel_Sizes(face, 0, size);
//        fm->ascent = face->size->metrics.ascender >> 6;
//        fm->descent = -(face->size->metrics.descender >> 6);
//        fm->height = (face->size->metrics.height) >> 6;
//        fm->x_height = fm->height / 2; // 近似
//    }
//    return reinterpret_cast<litehtml::uint_ptr>(font);
//}

void FreetypeBackend::delete_font(litehtml::uint_ptr h)
{
    if (h) {
        auto* f = reinterpret_cast<FreetypeFont*>(h);
        FT_Done_Face(f->face);
        delete f;
    }
}



/* ---------- 内部工具 ---------- */
void FreetypeBackend::fill_rect(const litehtml::position& rc,
    litehtml::web_color c)
{
    uint8_t* dst = m_surface + std::int16_t(rc.y) * m_stride + std::int16_t(rc.x) * 4;
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

litehtml::pixel_t FreetypeBackend::text_width(const char* text, litehtml::uint_ptr hFont)
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

void FreetypeBackend::set_clip(const litehtml::position& pos, const litehtml::border_radiuses& bdr_radius)
{
}

void FreetypeBackend::del_clip()
{
}

void EPUBBook::load_all_fonts() {
    if (g_container && g_container->m_canvas) 
    {
        auto fonts = collect_epub_fonts();
        FontKey key{ L"serif", 400, false, 0 };
        m_fontBin[key] = g_cfg.default_serif;
        key = { L"sans-serif", 400, false, 0 };
        m_fontBin[key] = g_cfg.default_sans_serif;
        key = { L"monospace", 400, false, 0 };
        m_fontBin[key] = g_cfg.default_monospace;
        build_epub_font_index(g_book->ocf_pkg_, g_book.get());

        g_container->m_canvas->backend()->load_all_fonts(fonts);
    }
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

void SimpleContainer::makeBackend(HWND hwnd)
{
    std::unique_ptr<ICanvas> canvas = nullptr;
    RECT rc;
    GetClientRect(g_hView, &rc);
    int w = rc.right;
    int h = rc.bottom;
    switch (g_cfg.fontRenderer) {
    case Renderer::GDI:{
         canvas = std::make_unique<GdiCanvas>(w, h);
         break;
    }
    case Renderer::D2D: {
        canvas = std::make_unique<D2DCanvas>(w, h, m_hwnd);
        break;
    }
    case Renderer::FreeType: {
        // 内部自己准备像素缓冲
        canvas = std::make_unique<FreetypeCanvas>(w, h, 72);
        break;
    }
    }
    m_canvas =  std::move(canvas);
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

//static std::wstring make_safe_filename(std::wstring_view src, int index)
//{
//    std::wstring out{ src };
//    const std::wstring illegal = L"<>:\"/\\|?*";
//    for (wchar_t& c : out)
//        if (illegal.find(c) != std::wstring::npos) c = L'_';
//
//    // 去掉目录分隔符，只保留纯文件名
//    size_t lastSlash = out.find_last_of(L"/\\");
//    if (lastSlash != std::wstring::npos)
//        out = out.substr(lastSlash + 1);
//
//    // 加上序号，确保唯一
//    return std::to_wstring(index) + L"_" + out;
//}

//static std::vector<std::pair<std::wstring, std::vector<uint8_t>>>
//collect_epub_fonts()
//{
//    std::vector<std::pair<std::wstring, std::vector<uint8_t>>> fonts;
//    int index = 0;   // 全局序号
//    for (const auto& item : g_book.ocf_pkg_.manifest)
//    {
//        const std::wstring& mime = item.media_type;
//        if (mime == L"application/x-font-ttf" ||
//            mime == L"application/font-sfnt" ||
//            mime == L"font/otf" ||
//            mime == L"font/ttf" ||
//            mime == L"font/woff" ||
//            mime == L"font/woff2")
//        {
//            std::wstring wpath = g_zipIndex.find(item.href);
//            EPUBBook::MemFile mf = g_book.read_zip(wpath.c_str());
//            if (!mf.data.empty())
//                fonts.emplace_back(make_safe_filename(wpath, index++), std::move(mf.data));
//        }
//    }
//    return fonts;
//}
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

std::wstring EPUBBook::get_font_family_name(const std::vector<uint8_t>& data)
{
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, data.data(), 0))
        return L"";

    struct Try {
        int platform, encoding, language, nameID;
    } tries[] = {
        // 先取 Typographic Family (ID 16)
        {STBTT_PLATFORM_ID_MICROSOFT, STBTT_MS_EID_UNICODE_BMP,
         STBTT_MS_LANG_ENGLISH, 16},
         // 再取 Family (ID 1)
         {STBTT_PLATFORM_ID_MICROSOFT, STBTT_MS_EID_UNICODE_BMP,
          STBTT_MS_LANG_ENGLISH, 1},
          // mac 平台兜底
          {STBTT_PLATFORM_ID_MAC, STBTT_MAC_EID_ROMAN, 0, 1},
    };

    for (const auto& t : tries) {
        int len = 0;
        const char* p = (const char*)stbtt_GetFontNameString(
            &info, &len, t.platform, t.encoding, t.language, t.nameID);
        if (p && len > 0) {
            std::wstring name;
            name.reserve(len / 2);
            for (int i = 0; i < len; i += 2) {
                wchar_t ch = (wchar_t(p[i]) << 8) | wchar_t(p[i + 1]);
                if (ch == 0) break;          // 保险：遇到 NUL 终止
                name.push_back(ch);
            }
            if (!name.empty())
                return name;
        }
    }
    return L"";
}
//std::wstring get_font_family_name(const std::vector<uint8_t>& data)
//{
//    stbtt_fontinfo info;
//    if (!stbtt_InitFont(&info, data.data(), 0))
//        return L"";
//
//    struct Try {
//        int platform, encoding, language, nameID;
//    } tries[] = {
//        {STBTT_PLATFORM_ID_MICROSOFT, STBTT_MS_EID_UNICODE_BMP, STBTT_MS_LANG_ENGLISH, 1},
//        {STBTT_PLATFORM_ID_MICROSOFT, STBTT_MS_EID_UNICODE_BMP, STBTT_MS_LANG_ENGLISH, 16},
//        {STBTT_PLATFORM_ID_MAC,       STBTT_MAC_EID_ROMAN,      0,                     1},
//    };
//
//    for (const auto& t : tries) {
//        int len = 0;
//        const char* p = (const char*)stbtt_GetFontNameString(
//            &info, &len, t.platform, t.encoding, t.language, t.nameID);
//        if (p && len > 0) {
//            // UTF-16BE → wchar_t
//            std::wstring name;
//            name.reserve(len / 2);
//            for (int i = 0; i < len; i += 2) {
//                wchar_t ch = (wchar_t(p[i]) << 8) | wchar_t(p[i + 1]);
//                name.push_back(ch);
//            }
//            return name;
//        }
//    }
//    return L"";
//}
// 主函数 ------------------------------------------------------------
void EPUBBook::build_epub_font_index(const OCFPackage& pkg, EPUBBook* book)
{
    if (!book) return;
   
    const std::regex rx_face(R"(@font-face\s*\{([^}]*)\})", std::regex::icase);
    const std::regex rx_fam(R"(font-family\s*:\s*['"]?([^;'"}]+)['"]?)", std::regex::icase);
    const std::regex rx_src(R"(src\s*:\s*url\s*\(\s*['"]?([^)'"]+)['"]?\s*\))", std::regex::icase);
    const std::regex rx_w(R"(font-weight\s*:\s*(\d+|bold))", std::regex::icase);
    const std::regex rx_i(R"(font-style\s*:\s*(italic|oblique))", std::regex::icase);

    for (const auto& item : pkg.manifest)
    {
        if (item.media_type != L"text/css") continue;

        // 1. 读 CSS
        std::wstring css_path = pkg.opf_dir + item.href;
        MemFile css_file = book->read_zip(book->m_zipIndex.find(css_path).c_str());
        if (css_file.data.empty()) continue;

        std::string css(css_file.data.begin(), css_file.data.end());

        // 2. 遍历 @font-face
        for (std::sregex_iterator it(css.begin(), css.end(), rx_face), end; it != end; ++it)
        {
            std::string block = it->str();
            std::smatch m;

            std::wstring family;
            std::wstring url;
            int weight = 400;
            bool italic = false;

            if (std::regex_search(block, m, rx_fam)) family = a2w(m[1].str().c_str());
            if (std::regex_search(block, m, rx_src)) url = a2w(m[1].str().c_str());
            if (std::regex_search(block, m, rx_w))   weight = (m[1] == "bold" || m[1] == "700") ? 700 : 400;
            if (std::regex_search(block, m, rx_i)) italic = true;

            if (family.empty() || url.empty()) continue;

            // 3. 读字体
            std::wstring font_path = g_book->m_zipIndex.find(url);
       


            MemFile font_file = book->read_zip(book->m_zipIndex.find(font_path).c_str());
            if (font_file.data.empty()) continue;

  
            std::wstring real_name = get_font_family_name(font_file.data);

            // 5. 填充索引
            FontKey key{ family, weight, italic , 0};
            g_book->m_fontBin[key] = std::move(real_name);
        }
    }
}


// ---------- 主函数 ----------
//void EPUBBook::build_epub_font_index(const OCFPackage& pkg, EPUBBook* book)
//{
//    if (!book) return;
//
//    // 正则：整块 @font-face
//    const std::regex rx_face(R"(@font-face\s*\{([^}]*)\})", std::regex::icase);
//
//    // 子正则
//    const std::regex rx_fam(R"(font-family\s*:\s*['"]?([^;'"}]+)['"]?)", std::regex::icase);
//    const std::regex rx_src(R"(src\s*:\s*([^;]+))", std::regex::icase);
//    const std::regex rx_url(R"(url\s*\(\s*['"]?([^)'"]+)['"]?\s*\))", std::regex::icase);
//    const std::regex rx_loc(R"(local\s*\(\s*['"]?([^)'"]+)['"]?\s*\))", std::regex::icase);
//    const std::regex rx_w(R"(font-weight\s*:\s*(normal|bold|\d+))", std::regex::icase);
//    const std::regex rx_i(R"(font-style\s*:\s*(normal|italic|oblique))", std::regex::icase);
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
//            std::vector<std::wstring> urls;
//            std::vector<std::wstring> locals;
//            bool has_weight = false, has_style = false;
//            int weight = 400;
//            bool italic = false;
//
//            if (std::regex_search(block, m, rx_fam)) family = a2w(m[1].str().c_str());
//            if (std::regex_search(block, m, rx_w)) {
//                has_weight = true;
//                weight = (m[1] == "bold" || m[1] == "700") ? 700 : 400;
//            }
//            if (std::regex_search(block, m, rx_i)) {
//                has_style = true;
//                italic = (m[1] == "italic" || m[1] == "oblique");
//            }
//
//            // 解析 src 列表
//            if (std::regex_search(block, m, rx_src)) {
//                std::string src_block = m[1];
//                for (std::sregex_iterator uit(src_block.begin(), src_block.end(), rx_url), uend; uit != uend; ++uit)
//                    urls.push_back(a2w(uit->str(1).c_str()));
//                for (std::sregex_iterator lit(src_block.begin(), src_block.end(), rx_loc), lend; lit != lend; ++lit)
//                    locals.push_back(a2w(lit->str(1).c_str()));
//            }
//
//            if (family.empty() || (urls.empty() && locals.empty())) continue;
//
//            // 3. 需要注册的所有 (weight, italic) 组合
//            std::vector<std::pair<int, bool>> combos;
//            if (has_weight && has_style) {
//                combos.emplace_back(weight, italic);
//            }
//            else if (has_weight) {
//                combos.emplace_back(weight, false);
//                combos.emplace_back(weight, true);
//            }
//            else if (has_style) {
//                combos.emplace_back(400, italic);
//                combos.emplace_back(700, italic);
//            }
//            else {
//                combos = { {400,false}, {400,true}, {700,false}, {700,true} };
//            }
//
//            // 4. 先尝试 url(...)
//            std::vector<uint8_t> font_bytes;
//            std::wstring real_name;
//            for (const auto& u : urls)
//            {
//                std::wstring font_path = pkg.opf_dir + u;
//                // 去掉 "../"
//                for (size_t pos; (pos = font_path.find(L"../")) != std::wstring::npos; ) {
//                    size_t slash = font_path.rfind(L'/', pos - 1);
//                    if (slash == std::wstring::npos) break;
//                    font_path.erase(slash + 1, pos + 3 - slash);
//                }
//
//                MemFile f = book->read_zip(book->m_zipIndex.find(font_path).c_str());
//                if (!f.data.empty()) {
//                    // 1. 长度检查
//                    if (f.data.size() < 12) {
//                        OutputDebugStringW(L"字体文件太小，跳过\n");
//                        continue;
//                    }
//
//                    // 2. 魔数检查
//                    const uint32_t head = *reinterpret_cast<const uint32_t*>(f.data.data());
//                    if (head != 0x00010000 && head != 0x4F54544F) {   // 'true' or 'OTTO'
//                        OutputDebugStringW(L"非 TTF/OTF 文件，跳过\n");
//                        continue;
//                    }
//
//                    // 3. 拷贝一份，防止后续 std::move 后原数据失效
//                    font_bytes = std::move(f.data);
//                    std::wstring real_name = get_font_family_name(font_bytes);
//                    if (real_name.empty()) {
//                        OutputDebugStringW(L"无法解析 Family Name，跳过\n");
//                        continue;
//                    }
//                
//                    break;
//
//                }
//            }
//
//            // 5. 若 url 失败，尝试 local(...) —— 这里仅记录别名，字节留空
//            if (font_bytes.empty() && !locals.empty()) {
//                real_name = locals.front();   // 先取第一个 local 名
//            }
//
//            if (real_name.empty()) continue;
//
//            // 6. 注册所有组合
//            for (const auto& [w, i] : combos) {
//                FontKey key{ family, w, i };
//                //if (!font_bytes.empty()) g_fontBin[key] = font_bytes;
//                m_fontBin[key] = real_name;   // 覆盖即可
//            }
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

                    OutputDebugStringW(std::format(L"[DWrite] 加载字体: {} " ,name).c_str());
                    OutputDebugStringW(L"\n");

                    
                }
            }
        }
    }
    //UINT32 famCount = 0;
    //famCount = m_privateFonts->GetFontFamilyCount();
    //for (UINT32 i = 0; i < famCount; ++i) {
    //    Microsoft::WRL::ComPtr<IDWriteFontFamily> fam;
    //    m_privateFonts->GetFontFamily(i, &fam);
    //    Microsoft::WRL::ComPtr<IDWriteLocalizedStrings> names;
    //    fam->GetFamilyNames(&names);
    //    UINT32 idx = 0;
    //    BOOL exists = FALSE;
    //    names->FindLocaleName(L"en-us", &idx, &exists);
    //    if (!exists) idx = 0;
    //    UINT32 len = 0;
    //    names->GetStringLength(idx, &len);
    //    std::wstring name(len + 1, 0);
    //    names->GetString(idx, name.data(), len + 1);
    //    OutputDebugStringW((L"Family: " + name + L"\n").c_str());

    //    UINT32 faceCount = 0;
    //    faceCount = fam->GetFontCount();
    //    for (UINT32 j = 0; j < faceCount; ++j) {
    //        Microsoft::WRL::ComPtr<IDWriteFont> font;
    //        fam->GetFont(j, &font);
    //        OutputDebugStringW((L"  Face " + std::to_wstring(j) + L"\n").c_str());
    //    }
    //}
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



void FreetypeBackend::load_all_fonts(std::vector<std::pair<std::wstring, std::vector<uint8_t>>>& fonts)
{
    // 1. 收集字体（文件名 → 二进制数据）
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
    //make_tooltip_backend();
    if (g_cfg.enableJS) { enableJS(); }
    if (!g_container) {
        g_container = std::make_shared<SimpleContainer>(g_hView);
    }
    if (!g_tooltip_container) {
        g_tooltip_container = std::make_shared<SimpleContainer>(g_hTooltip);
    }
    if (!g_book)
    {
        g_book = std::make_unique<EPUBBook>();   // 保险：用新实例
    }
    g_worker = std::thread(WorkerLoop);

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
    m_backend->resize(width, height);
}

void D2DCanvas::resize(int width, int height)
{
    if (width <= 0 || height <= 0) return;

    m_w = width;
    m_h = height;
    m_backend->resize(m_w, m_h);
    if (m_d2dRT) {
        D2D1_SIZE_U size{ m_w, m_h };
        m_d2dRT->Resize(size);   // HwndRenderTarget 专用
    }
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

//void GdiBackend::draw_background(litehtml::uint_ptr hdc,
//    const std::vector<litehtml::background_paint>& bg, std::unordered_map<std::string, ImageFrame>& img_cache)
//{
//    if (bg.empty()) return;
//    HDC dc = reinterpret_cast<HDC>(hdc);
//
//    for (const auto& b : bg)
//    {
//        RECT rc{ b.border_box.left(), b.border_box.top(),
//                 b.border_box.right(), b.border_box.bottom() };
//
//        //--------------------------------------------------
//        // 1. 纯色背景
//        //--------------------------------------------------
//        if (b.image.empty())
//        {
//            HBRUSH br = CreateSolidBrush(to_cr(b.color));
//            FillRect(dc, &rc, br);
//            DeleteObject(br);
//            continue;
//        }
//
//        //--------------------------------------------------
//        // 2. 图片背景
//        //--------------------------------------------------
//        auto it = img_cache.find(b.image);
//        if (it == img_cache.end()) continue;   // 未加载
//
//        const ImageFrame& frame = it->second;
//        if (frame.rgba.empty()) continue;
//
//        // 先看缓存
//        HBITMAP& hBmp = m_gdiBitmapCache[b.image];
//        if (!hBmp)
//            hBmp = create_dib_from_frame(frame);
//        if (!hBmp) continue;
//
//        // 创建内存 DC
//        HDC memDC = CreateCompatibleDC(dc);
//        HGDIOBJ oldBmp = SelectObject(memDC, hBmp);
//
//        // 拉伸到目标矩形
//        int srcW = static_cast<int>(frame.width);
//        int srcH = static_cast<int>(frame.height);
//        SetStretchBltMode(dc, HALFTONE);
//        StretchBlt(dc,
//            rc.left, rc.top,
//            rc.right - rc.left,
//            rc.bottom - rc.top,
//            memDC,
//            0, 0, srcW, srcH,
//            SRCCOPY);
//
//        SelectObject(memDC, oldBmp);
//        DeleteDC(memDC);
//    }
//}


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

//void FreetypeBackend::draw_background(litehtml::uint_ptr,
//    const std::vector<litehtml::background_paint>& bg, std::unordered_map<std::string, ImageFrame>& img_cache)
//{
//    for (const auto& b : bg)
//    {
//        //--------------------------------------------------
//        // 1. 纯色背景
//        //--------------------------------------------------
//        if (b.image.empty())
//        {
//            fill_rect(b.border_box, b.color);
//            continue;
//        }
//
//        //--------------------------------------------------
//        // 2. 图片背景
//        //--------------------------------------------------
//        auto it = img_cache.find(b.image);
//        if (it == img_cache.end()) continue;   // 未加载
//
//        const ImageFrame& frame = it->second;
//        if (frame.rgba.empty()) continue;
//
//        draw_image(frame, b.border_box);
//    }
//}

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
    m_w = width;
    m_h = height;

    
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

 
     
        g_pg.load(g_container->m_doc.get(), task.w, task.h);

        g_pg.render(g_container->m_canvas.get(), task.sy);

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

//void AppBootstrap::make_tooltip_backend()
//{
//    RECT rc;
//    GetClientRect(g_hView, &rc);
//    int w = rc.right;
//    int h = rc.bottom;
//    switch (g_cfg.fontRenderer) {
//    case Renderer::GDI: {
//        g_tooltip_canvas = std::make_unique<GdiCanvas>(w, h);
//        return;
//    }
//    case Renderer::D2D: {
//        /* 1) D2D 工厂 */
//        D2D1CreateFactory(
//            D2D1_FACTORY_TYPE_SINGLE_THREADED,
//            __uuidof(ID2D1Factory1),            // 接口 GUID
//            nullptr,                          // 工厂选项（可 nullptr）
//            reinterpret_cast<void**>(g_tooltip_d2dFactory.GetAddressOf()));
//        /* 2) DXGI 工厂 & 适配器 */
//
//        ComPtr<IDXGIFactory> dxgiFactory;
//        CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&dxgiFactory);
//        ComPtr<IDXGIAdapter> adapter;
//        dxgiFactory->EnumAdapters(0, &adapter);
//
//        /* 3) D3D11 设备 */
//        D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
//        ComPtr<ID3D11Device> d3dDevice;
//        D3D11CreateDevice(
//            adapter.Get(),
//            D3D_DRIVER_TYPE_UNKNOWN,
//            nullptr,
//            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
//            levels, 1,
//            D3D11_SDK_VERSION,
//            &d3dDevice,
//            nullptr,
//            nullptr);
//
//        RECT rc; GetClientRect(g_hTooltip, &rc);
//        ComPtr<ID2D1HwndRenderTarget> hwndRT;
//        g_tooltip_d2dFactory->CreateHwndRenderTarget(
//            D2D1::RenderTargetProperties(),
//            D2D1::HwndRenderTargetProperties(
//                g_hTooltip,
//                D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top)),
//            hwndRT.GetAddressOf());   // ← 关键：GetAddressOf()
//        //ComPtr<ID2D1RenderTarget> g_d2dRT;
//        hwndRT.As(&g_tooltip_d2dRT);
//        g_tooltip_canvas = std::make_unique<D2DCanvas>(w, h, g_tooltip_d2dRT.Get());
//        return;
//    }
//    case Renderer::FreeType: {
//        // 内部自己准备像素缓冲
//        g_tooltip_canvas = std::make_unique<FreetypeCanvas>(w, h, 72);
//        return;
//    }
//    }
//}



void EPUBBook::show_tooltip(const std::string html, int x, int y)
{
    //OutputDebugStringA(("[show_tooltip] " + std::to_string(x) + " " + std::to_string(y) + "\n").c_str());
    if (html.empty() && !g_tooltip_container) { hide_tooltip(); return; }


    bool empty = html.empty();
    const char* ptr = html.c_str();
    g_tooltip_container->m_doc = litehtml::document::createFromString(
        { html.c_str(), litehtml::encoding::utf_8 }, g_tooltip_container.get());
    int width = g_cfg.tooltip_width;
    g_tooltip_container->m_doc->render(width);
    // 3. 计算大小：让 litehtml 自己排版

    POINT pt = { x, y };
    ClientToScreen(g_hView, &pt);
   
    int height = g_tooltip_container->m_doc->height();
    int tip_x = pt.x - width/2;
    int tip_y = pt.y - height - 20;
    if (tip_y < 0) { tip_y = pt.y  + 20; }
    //OutputDebugStringA(("[render size]" + std::to_string(width) + " " + std::to_string(height) + "\n").c_str());
    // ④ 最终定位
    SetWindowPos(g_hTooltip, HWND_TOPMOST,
        tip_x, tip_y, width+2, height+2,
        SWP_SHOWWINDOW | SWP_NOACTIVATE);


    g_tooltip_container->m_canvas->resize(width, height);
    g_tooltip_container->m_canvas->BeginDraw();
    RECT rc;
    GetClientRect(g_hTooltip, &rc);
    litehtml::position clip(0, 0, rc.right, rc.bottom);
    g_tooltip_container->m_doc->draw(
        g_tooltip_container->m_canvas->getContext(),   // 强制转换
        0, 0, &clip);
    g_tooltip_container->m_canvas->EndDraw();
    InvalidateRect(g_hTooltip, nullptr, false);

}

void EPUBBook::hide_tooltip()
{
    if (g_hTooltip)
    {
        ShowWindow(g_hTooltip, SW_HIDE);
        if(g_tooltip_container && g_tooltip_container->m_doc)
        {
            g_tooltip_container->m_doc.reset();
        }
        
    }
}


//// 把 ImageFrame 转成 HBITMAP（32-bit top-down DIB）
//HBITMAP SimpleContainer::load_thumb(const char* url_utf8)
//{
//    auto it = g_img_cache.find(url_utf8);
//    if (it == g_img_cache.end()) return nullptr;
//
//    const ImageFrame& f = it->second;
//    if (f.rgba.empty()) return nullptr;
//
//    BITMAPINFO bi{};
//    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
//    bi.bmiHeader.biWidth = static_cast<LONG>(f.width);
//    bi.bmiHeader.biHeight = -static_cast<LONG>(f.height); // 负值 = top-down
//    bi.bmiHeader.biPlanes = 1;
//    bi.bmiHeader.biBitCount = 32;                         // RGBA
//    bi.bmiHeader.biCompression = BI_RGB;
//
//    void* bits = nullptr;
//    HBITMAP hbm = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS,
//        &bits, nullptr, 0);
//    if (!hbm || !bits) return nullptr;
//
//    // 直接拷贝像素（源数据已是 B,G,R,A 顺序）
//    memcpy(bits, f.rgba.data(), f.rgba.size());
//    return hbm;
//}

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
//
//std::shared_ptr<litehtml::render_item> Paginator::get_root_render_item()
//{
//    // document 里有一个私有成员：
//    //   std::shared_ptr<render_item> m_root_render;
//    // 由于我们不能直接访问，只能“反射”一下。
//    // 下面用标准布局 + offsetof 的 trick，只在 MSVC/Clang/GCC 下有效。
//    struct hack_document {
//        char _pad[sizeof(std::shared_ptr<litehtml::element>) +
//            sizeof(litehtml::document_container*) +
//            sizeof(litehtml::fonts_map) +
//            sizeof(litehtml::css_text::vector) +
//            sizeof(litehtml::css) * 3 +
//            sizeof(litehtml::web_color) +
//            sizeof(litehtml::size)];
//        std::shared_ptr<litehtml::render_item> m_root_render;
//    };
//    static_assert(sizeof(hack_document) <= sizeof(litehtml::document),
//        "hack_document layout changed!");
//
//    auto* raw = reinterpret_cast<char*>(g_doc.get()) +
//        offsetof(hack_document, m_root_render);
//    return *reinterpret_cast<std::shared_ptr<litehtml::render_item>*>(raw);
//}

// ---------- 分页 ----------
std::wstring get_href_by_id(int id)
{
    auto spine = g_book->ocf_pkg_.spine;
    if (id < spine.size() && id >= 0  ) 
    { 
        return spine[id].href;
    }
    return L"";
}
int get_id_by_href(std::wstring& href)
{
    auto spine = g_book->ocf_pkg_.spine;
    for (int i = 0; i < spine.size(); i++)
    {
        if (spine[i].href == href) {
            return i;
        }
    }
    return -1;
}

void Paginator::load(litehtml::document* doc, int w, int h)
{
    m_doc = doc;

    m_w = w;
    m_h = h;
    g_maxScroll = 0;
    if (!m_doc) return;

    g_maxScroll = m_doc->height();
}
void Paginator::render(ICanvas* canvas, int scrollY)
{
    if (!canvas || !m_doc) return;
    if (g_scrollY > g_maxScroll)
    {
        int id = get_id_by_href(g_currentHtmlPath);
        if (id >= 0) {
            id += 1;
            std::wstring html = get_href_by_id(id);
            if (!html.empty())
            {
                g_book->OnTreeSelChanged(html.c_str());
            }
        }
        return;
    }
    if (g_scrollY < 0)
    {
        int id = get_id_by_href(g_currentHtmlPath);
        if (id >= 0) {
            id -= 1;
            std::wstring html = get_href_by_id(id);
            if (!html.empty())
            {
                g_book->OnTreeSelChanged(html.c_str());
            }
        }
        return;
    }
    int render_width = g_cfg.document_width;
    /* 1) 需要排版才排 */
    if (g_states.needRelayout.exchange(false)) {
        g_container->m_doc->render(render_width, litehtml::render_all);
    }
    g_center_offset = std::max((m_w - render_width) * 0.5, 0.0);

}
void Paginator::clear() {
    m_doc = nullptr;
    m_w = m_h = 0;
    g_maxScroll = 0;
}

std::set<std::wstring> D2DBackend::getCurrentFonts() {
    std::set<std::wstring> out;


    if (m_privateFonts)
    {
        UINT32 n = m_privateFonts->GetFontFamilyCount();
        //OutputDebugStringW(L"【字体数量】 ");
        //OutputDebugStringW(std::to_wstring(n).c_str());
        //OutputDebugStringW(L"\n");

        for (UINT32 i = 0; i < n; ++i)
        {
            ComPtr<IDWriteFontFamily> f;
            m_privateFonts->GetFontFamily(i, &f);
            IDWriteLocalizedStrings* names;
            f->GetFamilyNames(&names);
            UINT32 idx; BOOL exists;
            names->FindLocaleName(L"en-us", &idx, &exists);
            UINT32 len;
            names->GetStringLength(idx, &len);
            std::wstring buf(len + 1, 0);
            names->GetString(idx, buf.data(), len + 1);
            //wprintf(L"%u: %s\n", i, buf.c_str());
            //OutputDebugStringW(buf.c_str());
            //OutputDebugStringW(L" ");
            out.emplace(std::move(buf));
        }
        //OutputDebugStringW(L"\n");
    }
    return out;
}

std::set<std::wstring> GdiCanvas::getCurrentFonts() 
{
    return m_backend->getCurrentFonts();
}

std::set<std::wstring> GdiBackend::getCurrentFonts()
{
    return {};
}


std::set<std::wstring> D2DCanvas::getCurrentFonts()
{
    return m_backend->getCurrentFonts();
}



std::set<std::wstring> FreetypeCanvas::getCurrentFonts()
{
    return m_backend->getCurrentFonts();
}

std::set<std::wstring> FreetypeBackend::getCurrentFonts()
{
    return {};
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

SimpleContainer::SimpleContainer(HWND hwnd)
    : m_hwnd(hwnd) {
    if (hwnd){ makeBackend(m_hwnd); }
}

SimpleContainer::~SimpleContainer()
   {
}

void SimpleContainer::resize(int w, int h)
{
    if (m_canvas) {
        m_canvas->resize(w, h);
    }

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
    //m_canvas->clear();
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
    SendMessage(g_hwndTV, WM_SETREDRAW, FALSE, 0);
    HTREEITEM hRoot = TreeView_GetRoot(g_hwndTV);
    TreeView_SelectItem(g_hwndTV, nullptr);   // 先取消选中
    FreeTreeData(g_hwndTV);                   // 释放 TVData*
    TreeView_DeleteAllItems(g_hwndTV);          // 此时不会再触发 TVN_SELCHANGED
    SendMessage(g_hwndTV, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(g_hwndTV, nullptr, nullptr,
        RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW);

    mz_zip_reader_end(&zip);
    cache.clear();
    m_nodes.clear();
    m_nodes.shrink_to_fit();
    m_roots.clear();
    ocf_pkg_ = {};
    m_fontBin.clear();
}

void D2DCanvas::clear()
{
    m_backend->clear();
}

void D2DBackend::clear() 
{
    m_privateFonts.Reset();
    m_d2dBitmapCache.clear();
    m_clipStack.clear();
    m_fontCache.clear();
    m_layoutCache.clear();
    m_brushPool.clear();
}

void GdiCanvas::clear(){}

void GdiBackend::clear() {}

void FreetypeCanvas::clear() {}

void FreetypeBackend::clear() {}


// ---------- 实现 ----------

D2DBackend::D2DBackend(int w, int h, ComPtr<ID2D1RenderTarget> devCtx)
    : m_w(w), m_h(h), m_devCtx(devCtx) {

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


//struct VirtualDoc {
//    int m_client_w, m_client_h;
//    int m_doc_w;
//    std::string html;
//    std::unique_ptr<SimpleContainer> m_container;
//    std::vector<OCFRef> spine;
//    litehtml::document::ptr render()
//    {
//        auto doc = litehtml::document::createFromString(html, m_container.get());
//        doc->render(m_doc_w);
//        return doc;
//    }
//    void load(EPUBBook& book)
//    {
//        spine = book.ocf_pkg_.spine;
//    }
//    int get_id_by_href(std::wstring& href)
//    {
//        for (int i = 0; i < spine.size(); i++ )
//        {
//            if (spine[i].href == href) 
//            {
//                return i;
//            }
//        }
//        return -1;
//    }
//    std::wstring get_href_by_id(int id) 
//    { 
//        return spine[id].href; 
//    }
//    void set_doc_width(int width)
//    {
//        m_doc_w = width;
//    }
//    void resize(int width, int height)
//    {
//        m_client_w = width;
//        m_client_h = height;
//    }
//    litehtml::document::ptr get_doc()
//    {
//        auto doc = render();
//        while (doc->height() < m_client_h * 2)
//        {
//            needMore();
//            render();
//        }
//        return doc;
//    }
//    void getBitmap()
//    {
//        //从get_doc获取到doc后，开始draw，然后将离屏渲染的位图拿出来返回
//    }
//    void needMore()
//    {
//        // 从当前加载的html中找出下一块拼到变量html中，若当前html拼完了就根据id加载下一个html文件继续拼接
//    }
//};


FontCache::FontCache() {
    DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(m_dw.GetAddressOf()));   // ✅
    m_defaultFamily = std::wstring(g_cfg.default_font_name.begin(),
        g_cfg.default_font_name.end());
}

/* ---------------------------------------------------------- */
Microsoft::WRL::ComPtr<IDWriteTextFormat>
FontCache::get(std::wstring& familyName, const litehtml::font_description& descr,
    IDWriteFontCollection* privateColl, IDWriteFontCollection* sysColl) {
    // 1. 构造键
    FontKey key{
        familyName.empty() ? m_defaultFamily
                             : std::wstring(familyName.begin(), familyName.end()),
        descr.weight,
        descr.style == litehtml::font_style_italic,
        descr.size
    };

    // 2. 读缓存
    {
        std::shared_lock sl(m_mtx);
        if (auto it = m_map.find(key); it != m_map.end())
            return it->second;
    }

    // 3. 未命中，创建并写入
    auto fmt = create(key, privateColl, sysColl);
    {
        std::unique_lock ul(m_mtx);
        m_map[key] = fmt;          // 若并发重复，后写覆盖，无妨
    }
    return fmt;
}

/* ---------------------------------------------------------- */
Microsoft::WRL::ComPtr<IDWriteTextFormat>
FontCache::create(const FontKey& key, IDWriteFontCollection* privateColl, IDWriteFontCollection* sysColl) {
    // 候选家族列表：精确 → 仅 family → 默认
    std::wstring tryName;
    tryName = key.family;

    // 若 g_book->m_fontBin 有映射，追加真实文件名
    if (g_book) {
        FontKey exact{ key.family, key.weight, key.italic, 0 }; // size 忽略
        if (auto it = g_book->m_fontBin.find(exact); it != g_book->m_fontBin.end())
            tryName = it->second;

        // 退而求其次：仅 family
        for (const auto& kv : g_book->m_fontBin)
            if (kv.first.family == key.family) { tryName = kv.second; break; }
    }
    

    // 逐个尝试
    
    for (auto coll : { privateColl, sysColl }) {   // 先私有，再系统
        if (!coll) continue;
        UINT32 index = 0;
        if (!findFamily(coll, tryName, index)) continue;

        Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
        if (SUCCEEDED(m_dw->CreateTextFormat(
            tryName.c_str(), coll,
            static_cast<DWRITE_FONT_WEIGHT>(key.weight),
            key.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            static_cast<float>(key.size),
            L"en-us",
            &fmt))) {
            return fmt;   // 成功
        }
    }

    //使用默认字体
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
    if (SUCCEEDED(m_dw->CreateTextFormat(
        m_defaultFamily.c_str(), sysColl,
        static_cast<DWRITE_FONT_WEIGHT>(key.weight),
        key.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        static_cast<float>(key.size),
        L"en-us",
        &fmt))) {
        return fmt;   // 成功
    }
    
    // 理论上不会走到这里，除非默认字体也失败
    return nullptr;
}

/* ---------------------------------------------------------- */
bool FontCache::findFamily(IDWriteFontCollection* coll,
                           const std::wstring&    target,
                           UINT32&                index)
{
    // 1) 快路径：DWrite 自带
    BOOL exists = FALSE;
    if (SUCCEEDED(coll->FindFamilyName(target.c_str(), &index, &exists)) && exists)
        return true;

    // 2) 慢路径：逐 family 遍历
    UINT32 count = coll->GetFontFamilyCount();
    for (UINT32 i = 0; i < count; ++i)
    {
        Microsoft::WRL::ComPtr<IDWriteFontFamily> fam;
        if (FAILED(coll->GetFontFamily(i, &fam)))
            continue;

        Microsoft::WRL::ComPtr<IDWriteLocalizedStrings> names;
        if (FAILED(fam->GetFamilyNames(&names)))
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