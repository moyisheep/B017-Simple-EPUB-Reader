# Simple EPUB Reader

[一个简单的 EPUB 阅读器]

## 开发说明
使用AI辅助开发（当贝AI，Kimi K2, DeepSeek，问小白等），人工调试加优化功能，估计可能存在许多问题，暂时懒优化了

## 📌 功能
- EPUB 阅读器
- 可指定字体、调整字体大小、行高、页面宽度和页面缩放
- 支持SVG渲染
- 支持MathML渲染（有点慢）
- 点击预览（点击图片弹出窗口查看大图， 右键取消）
- 悬浮预览（鼠标悬停某些页内跳转链接时，弹出窗口预览）


## ⌨️ 快捷键

### 文档操作
| 快捷键               | 功能                |
|----------------------|---------------------|
| `F5`                 | 重新加载文档        |
| `Ctrl + O`           | 打开文件            |
| `Tab`                | 切换目录窗口        |

### 字体调整
| 快捷键               | 功能                |
|----------------------|---------------------|
| `Ctrl + +`/`Ctrl + =`| 增大字体            |
| `Ctrl + -`           | 减小字体            |
| `Ctrl + Backspace`   | 重置字体大小        |

### 行高调整
| 快捷键                     | 功能                |
|----------------------------|---------------------|
| `Ctrl + Shift + +`         | 增加行高            |
| `Ctrl + Shift + -`         | 减少行高            |
| `Ctrl + Shift + Backspace` | 重置行高            |

### 页面宽度
| 快捷键               | 功能                |
|----------------------|---------------------|
| `Alt + +`            | 增加页面宽度        |
| `Alt + -`            | 减少页面宽度        |
| `Alt + Backspace`    | 重置页面宽度        |

### 视图缩放
| 操作方式               | 功能                |
|-----------------------|---------------------|
| `Ctrl + 鼠标滚轮上`   | 放大文档            |
| `Ctrl + 鼠标滚轮下`   | 缩小文档            |
| `Ctrl + 鼠标中键点击`        | 重置缩放比例        |

### 编辑
| 快捷键       | 功能        |
|--------------|-------------|
| `Ctrl + C`   | 复制文本    |


### 使用到的第三方库
| 库名称       | 许可证            | 简介                  | 源码地址 |
|---------------|--------------------|------------------------------|--------|
| [miniz](https://github.com/richgel999/miniz) | MIT | ZIP compression | [GitHub](https://github.com/richgel999/miniz) |
| [tinyxml2](https://github.com/leethomason/tinyxml2) | MIT | XML parsing | [GitHub](https://github.com/leethomason/tinyxml2) |
| [LunaSVG](https://github.com/sammycage/lunasvg) | MIT | SVG rendering | [GitHub](https://github.com/sammycage/lunasvg) |
| [litehtml](https://github.com/litehtml/litehtml) | BSD 3-Clause | HTML rendering | [GitHub](https://github.com/litehtml/litehtml) |
| [Gumbo](https://github.com/google/gumbo-parser) | Apache 2.0 | HTML5 parsing | [GitHub](https://github.com/google/gumbo-parser) |
| [BLAKE3](https://github.com/BLAKE3-team/BLAKE3) | CC0 1.0 or Apache 2.0 | Cryptographic hashing | [GitHub](https://github.com/BLAKE3-team/BLAKE3) |
| [stb_image](https://github.com/nothings/stb) | Public Domain/MIT | Image loading | [GitHub](https://github.com/nothings/stb) |
| [FreeType](https://www.freetype.org/) | FTL (FreeType License) | Font rendering | [Website](https://www.freetype.org/) |
| [SQLite](https://www.sqlite.org/) | Public Domain | Embedded database | [Website](https://www.sqlite.org/) |
| [Boost.Algorithm](https://www.boost.org/) | Boost Software License 1.0 | String utilities | [Website](https://www.boost.org/) |

### 使用到的Windows库
| 库名称              | 许可证       | 简介                  |
|------------------------|---------------|------------------------------|
| Windows SDK            | Proprietary   | Core Windows API             |
| DirectX 11/Direct2D    | Proprietary   | Graphics rendering           |
| GDI+                   | Proprietary   | Legacy graphics              |
| WIC (Windows Imaging)  | Proprietary   | Image codecs                 |
| MSVC Runtime           | Proprietary   | C++ standard library         |


## 其他
- 阅读统计以及设置保存在 文档/Simple EPUB Reader/data/*.db
- 若启用epub自带字体，会把epub字体临时解压在 AppData/Local/Temp/epub_book/


## 许可证
The core project is licensed under **MIT License**.  
See [LICENSE](LICENSE) for details.

Third-party library licenses are listed in [NOTICE](NOTICE).