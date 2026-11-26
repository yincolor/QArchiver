// QArchiver / 小Q解压
// Explorer context-menu extension: dynamic "添加到 <name>.zip" labels and a
// "压缩到..." format submenu.  Implemented as a small in-process COM server so
// the menu text can include the selected file name and archive files can be
// excluded dynamically.
// 资源管理器右键菜单扩展：动态“添加到 <名称>.zip”文案与“压缩到...”格式子菜单。
// 以进程内 COM 服务器实现，因此菜单文案可以包含所选文件名，并能动态排除
// 已经是压缩包的文件。

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <initializer_list>
#include <new>
#include <string>
#include <vector>

namespace {

const CLSID kClsid = {0x5f3b9c2a, 0x7e4d, 0x4a1b,
                      {0x9c, 0x6d, 0x2b, 0x8e, 0x1f, 0x4a, 0x7c, 0x30}};
const wchar_t *kClsidString = L"{5F3B9C2A-7E4D-4A1B-9C6D-2B8E1F4A7C30}";
const wchar_t *kHandlerName = L"QArchiver";
const wchar_t *kApprovedName = L"QArchiver (小Q解压)";

HINSTANCE g_instance = nullptr;
volatile LONG g_objectCount = 0;

std::wstring modulePath()
{
    wchar_t buffer[MAX_PATH * 4] = {};
    const DWORD length = GetModuleFileNameW(g_instance, buffer, _countof(buffer));
    if (length == 0 || length >= _countof(buffer)) {
        return std::wstring();
    }
    return std::wstring(buffer, length);
}

std::wstring registryAppPath(HKEY root)
{
    HKEY key = nullptr;
    const wchar_t *subkey =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\QArchiver.exe";
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return std::wstring();
    }
    wchar_t value[MAX_PATH * 4] = {};
    DWORD size = sizeof(value);
    DWORD type = 0;
    const LONG status = RegQueryValueExW(key, nullptr, nullptr, &type,
                                         reinterpret_cast<LPBYTE>(value), &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_SZ) {
        return std::wstring();
    }
    return std::wstring(value);
}

std::wstring executablePath()
{
    std::wstring path = modulePath();
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        path.erase(slash + 1);
    }
    path += L"QArchiver.exe";
    if (PathFileExistsW(path.c_str())) {
        return path;
    }

    // A stale per-user registration may point at an old build directory. Fall
    // back to the installed App Paths entry / Program Files so the context
    // menu keeps working after an in-place upgrade.
    // 旧的用户级注册可能仍指向旧构建目录；回退到安装版 App Paths /
    // Program Files，保证覆盖升级后右键菜单仍可用。
    for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
        const std::wstring registered = registryAppPath(root);
        if (!registered.empty() && PathFileExistsW(registered.c_str())) {
            return registered;
        }
    }
    wchar_t programFiles[MAX_PATH * 4] = {};
    if (GetEnvironmentVariableW(L"ProgramFiles", programFiles,
                                _countof(programFiles)) > 0) {
        std::wstring installed = programFiles;
        installed += L"\\QArchiver\\QArchiver.exe";
        if (PathFileExistsW(installed.c_str())) {
            return installed;
        }
    }
    return path;
}

std::wstring quote(const std::wstring &value)
{
    return L"\"" + value + L"\"";
}

bool hasArchiveSuffix(const std::wstring &path)
{
    static const wchar_t *suffixes[] = {
        L".zip", L".7z", L".rar", L".tar", L".gz", L".bz2", L".xz",
        L".tgz", L".txz", L".tbz2", L".tar.gz", L".tar.xz", L".tar.bz2",
        L".tar.zst", L".tzst", L".zst", L".cab", L".iso"
    };
    std::wstring lower = path;
    CharLowerBuffW(lower.data(), static_cast<DWORD>(lower.size()));
    for (const wchar_t *suffix : suffixes) {
        const size_t length = wcslen(suffix);
        if (lower.size() >= length
            && lower.compare(lower.size() - length, length, suffix) == 0) {
            return true;
        }
    }
    return false;
}

std::wstring fileName(const std::wstring &path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

// Build a small menu bitmap with a real transparent mask. The shell does not
// provide icons for dynamically inserted context-menu items, so load the
// registered file-type icon and attach it explicitly.
// 为动态插入的右键菜单项构造带透明掩码的小图标；资源管理器不会自动提供图标。
struct MenuBitmap {
    HBITMAP color = nullptr;
    HBITMAP mask = nullptr;
};

HICON loadFileTypeIcon(const wchar_t *name, DWORD attributes, bool appIcon)
{
    if (appIcon) {
        HICON smallIcon = nullptr;
        const std::wstring exe = executablePath();
        if (!exe.empty()
            && ExtractIconExW(exe.c_str(), 0, nullptr, &smallIcon, 1) > 0) {
            return smallIcon;
        }
    }

    SHFILEINFOW info = {};
    const UINT flags = SHGFI_ICON | SHGFI_SMALLICON
        | (appIcon ? 0 : SHGFI_USEFILEATTRIBUTES);
    if (SHGetFileInfoW(name, attributes, &info, sizeof(info), flags)) {
        return info.hIcon;
    }
    return nullptr;
}

MenuBitmap makeMenuBitmap(const wchar_t *name, DWORD attributes, bool appIcon)
{
    MenuBitmap result;
    HICON icon = loadFileTypeIcon(name, attributes, appIcon);
    if (!icon) {
        return result;
    }

    const int size = GetSystemMetrics(SM_CXSMICON);
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -size;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void *pixels = nullptr;
    result.color = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &pixels,
                                    nullptr, 0);
    if (result.color && pixels) {
        auto *bytes = static_cast<BYTE *>(pixels);
        const COLORREF menuColor = GetSysColor(COLOR_MENU);
        for (int i = 0; i < size * size; ++i) {
            bytes[i * 4 + 0] = GetBValue(menuColor);
            bytes[i * 4 + 1] = GetGValue(menuColor);
            bytes[i * 4 + 2] = GetRValue(menuColor);
            bytes[i * 4 + 3] = 255;
        }
        HGDIOBJ old = SelectObject(dc, result.color);
        DrawIconEx(dc, 0, 0, icon, size, size, 0, nullptr, DI_NORMAL);
        SelectObject(dc, old);

        bool hasAlpha = false;
        for (int i = 0; i < size * size; ++i) {
            if (bytes[i * 4 + 3] != 255) {
                hasAlpha = true;
                break;
            }
        }
        const int maskStride = ((size + 15) / 16) * 2;
        std::vector<BYTE> maskBits(static_cast<size_t>(maskStride) * size, 0);
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const int i = y * size + x;
                const BYTE a = bytes[i * 4 + 3];
                const bool transparent = hasAlpha
                    ? a == 0
                    : bytes[i * 4 + 0] == GetBValue(menuColor)
                        && bytes[i * 4 + 1] == GetGValue(menuColor)
                        && bytes[i * 4 + 2] == GetRValue(menuColor);
                if (transparent) {
                    maskBits[static_cast<size_t>(y) * maskStride + x / 8]
                        |= static_cast<BYTE>(0x80 >> (x % 8));
                }
            }
        }
        result.mask = CreateBitmap(size, size, 1, 1, maskBits.data());
    }
    DestroyIcon(icon);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    if (!result.mask) {
        DeleteObject(result.color);
        result.color = nullptr;
    }
    return result;
}

std::wstring baseName(const std::wstring &path, bool isDirectory)
{
    std::wstring name = fileName(path);
    if (isDirectory) {
        return name.empty() ? L"archive" : name;
    }
    // Remove compound archive suffixes first, then one extension.
    std::wstring lower = name;
    CharLowerBuffW(lower.data(), static_cast<DWORD>(lower.size()));
    static const wchar_t *compound[] = {L".tar.gz", L".tar.xz", L".tar.bz2",
                                        L".tar.zst"};
    for (const wchar_t *suffix : compound) {
        const size_t length = wcslen(suffix);
        if (lower.size() > length
            && lower.compare(lower.size() - length, length, suffix) == 0) {
            name.erase(name.size() - length);
            lower.erase(lower.size() - length);
            break;
        }
    }
    const size_t dot = lower.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) {
        name.erase(dot);
    }
    return name.empty() ? L"archive" : name;
}

bool launch(const std::wstring &arguments)
{
    const std::wstring exe = executablePath();
    if (exe.empty() || !PathFileExistsW(exe.c_str())) {
        return false;
    }
    std::wstring commandLine = quote(exe) + L" " + arguments;
    std::vector<wchar_t> buffer(commandLine.begin(), commandLine.end());
    buffer.push_back(L'\0');
    const size_t slash = exe.find_last_of(L"\\/");
    const std::wstring workingDir =
        slash == std::wstring::npos ? std::wstring() : exe.substr(0, slash);

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    const BOOL ok = CreateProcessW(
        nullptr, buffer.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, workingDir.empty() ? nullptr : workingDir.c_str(),
        &startup, &process);
    if (ok) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    return ok == TRUE;
}

void runCompression(const std::vector<std::wstring> &paths,
                    const wchar_t *format, bool customDialog)
{
    if (paths.empty()) {
        return;
    }
    if (customDialog) {
        std::wstring arguments = L"--compress-dialog --open-after";
        for (const std::wstring &path : paths) {
            arguments += L" " + quote(path);
        }
        launch(arguments);
        return;
    }
    // One process per item keeps the "name.zip next to each file" semantics
    // for multi-selection.  Only a single item asks for the result to be
    // revealed: N concurrent processes each opening a main window would flood
    // the desktop with windows the user never asked for.
    // 多选时每项单独启动一个进程，保持“每个文件旁生成同名 zip”的语义；
    // 仅单文件时附带 --open-after：N 个进程各开一个主窗口会把桌面刷屏。
    const bool openAfter = paths.size() == 1;
    for (const std::wstring &path : paths) {
        std::wstring arguments = std::wstring(L"--compress ") + format;
        if (openAfter) {
            arguments += L" --open-after";
        }
        arguments += L" " + quote(path);
        launch(arguments);
    }
}

class ContextMenu final : public IShellExtInit, public IContextMenu {
public:
    ContextMenu() { InterlockedIncrement(&g_objectCount); }
    ~ContextMenu()
    {
        for (const MenuBitmap &bitmap : m_bitmaps) {
            if (bitmap.color) {
                DeleteObject(bitmap.color);
            }
            if (bitmap.mask) {
                DeleteObject(bitmap.mask);
            }
        }
        InterlockedDecrement(&g_objectCount);
    }

    // IUnknown / IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override
    {
        if (!object) {
            return E_POINTER;
        }
        *object = nullptr;
        if (riid == IID_IUnknown || riid == IID_IShellExtInit) {
            *object = static_cast<IShellExtInit *>(this);
        } else if (riid == IID_IContextMenu) {
            *object = static_cast<IContextMenu *>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return static_cast<ULONG>(InterlockedIncrement(&m_reference));
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const LONG reference = InterlockedDecrement(&m_reference);
        if (reference == 0) {
            delete this;
        }
        return static_cast<ULONG>(reference);
    }

    // IShellExtInit / IShellExtInit
    HRESULT STDMETHODCALLTYPE Initialize(PCIDLIST_ABSOLUTE folderPidl,
                                         IDataObject *dataObject,
                                         HKEY) override
    {
        m_paths.clear();
        m_folder.clear();
        m_background = false;

        if (folderPidl) {
            wchar_t folder[MAX_PATH * 4] = {};
            if (SHGetPathFromIDListW(folderPidl, folder)) {
                m_folder = folder;
            }
        }
        if (!dataObject) {
            return E_INVALIDARG;
        }

        FORMATETC format = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1,
                            TYMED_HGLOBAL};
        STGMEDIUM medium = {};
        if (SUCCEEDED(dataObject->GetData(&format, &medium))) {
            if (HDROP drop = static_cast<HDROP>(GlobalLock(medium.hGlobal))) {
                const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
                for (UINT i = 0; i < count; ++i) {
                    wchar_t path[MAX_PATH * 4] = {};
                    if (DragQueryFileW(drop, i, path, _countof(path)) > 0) {
                        m_paths.emplace_back(path);
                    }
                }
                GlobalUnlock(medium.hGlobal);
            }
            ReleaseStgMedium(&medium);
        }

        m_background = m_paths.empty() && !m_folder.empty();
        return S_OK;
    }

    // IContextMenu / IContextMenu
    HRESULT STDMETHODCALLTYPE QueryContextMenu(HMENU menu, UINT index,
                                               UINT firstCommand,
                                               UINT lastCommand,
                                               UINT flags) override
    {
        if (flags & CMF_DEFAULTONLY) {
            return 0;
        }
        std::vector<std::wstring> targets = m_paths;
        if (targets.empty() && m_background && !m_folder.empty()) {
            targets.push_back(m_folder);
        }
        if (targets.empty()) {
            return 0;
        }
        // Archive files already are compressed: no "add to archive" menu.
        // 压缩包本身不再显示“添加到压缩包”。
        const DWORD attributes = GetFileAttributesW(targets.front().c_str());
        const bool isDirectory =
            attributes != INVALID_FILE_ATTRIBUTES
            && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (!isDirectory && hasArchiveSuffix(targets.front())) {
            return 0;
        }
        if (lastCommand < firstCommand + 8) {
            return 0;
        }

        const std::wstring zipName =
            baseName(targets.front(), isDirectory) + L".zip";
        const std::wstring label = L"添加到 " + quote(zipName);
        InsertMenuW(menu, index, MF_BYPOSITION | MF_STRING, firstCommand,
                    label.c_str());
        attachBitmap(menu, index, L"file.zip", FILE_ATTRIBUTE_NORMAL);

        HMENU submenu = CreatePopupMenu();
        AppendMenuW(submenu, MF_STRING, firstCommand + 1, L"7z 压缩包 (.7z)");
        AppendMenuW(submenu, MF_STRING, firstCommand + 2, L"ZIP 压缩包 (.zip)");
        AppendMenuW(submenu, MF_STRING, firstCommand + 3, L"Gzip tar (.tar.gz)");
        AppendMenuW(submenu, MF_STRING, firstCommand + 4, L"Xz tar (.tar.xz)");
        AppendMenuW(submenu, MF_STRING, firstCommand + 5,
                    L"Bzip2 tar (.tar.bz2)");
        AppendMenuW(submenu, MF_STRING, firstCommand + 6, L"Tar (.tar)");
        AppendMenuW(submenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(submenu, MF_STRING, firstCommand + 7,
                    L"自定义压缩选项...");
        attachBitmap(submenu, 0, L"file.7z", FILE_ATTRIBUTE_NORMAL);
        attachBitmap(submenu, 1, L"file.zip", FILE_ATTRIBUTE_NORMAL);
        attachBitmap(submenu, 2, L"file.tar.gz", FILE_ATTRIBUTE_NORMAL);
        attachBitmap(submenu, 3, L"file.tar.xz", FILE_ATTRIBUTE_NORMAL);
        attachBitmap(submenu, 4, L"file.tar.bz2", FILE_ATTRIBUTE_NORMAL);
        attachBitmap(submenu, 5, L"file.tar", FILE_ATTRIBUTE_NORMAL);
        attachBitmap(submenu, 7, L"", FILE_ATTRIBUTE_NORMAL, true);
        InsertMenuW(menu, index + 1, MF_BYPOSITION | MF_POPUP,
                    reinterpret_cast<UINT_PTR>(submenu), L"压缩到...");
        attachBitmap(menu, index + 1, L"", FILE_ATTRIBUTE_NORMAL, true);

        m_firstCommand = firstCommand;
        return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, 8);
    }

    HRESULT STDMETHODCALLTYPE InvokeCommand(LPCMINVOKECOMMANDINFO info) override
    {
        if (!info) {
            return E_INVALIDARG;
        }
        UINT command = LOWORD(info->lpVerb);
        // Command ids are carried in lpVerb's low word. CMINVOKECOMMANDINFOEX's
        // lpVerbW is a Unicode *verb string* (often null for menu ids); using
        // it here makes every submenu item look like command 0, i.e. "zip".
        // 命令 ID 在 lpVerb 低 16 位；CMINVOKECOMMANDINFOEX 的 lpVerbW 是
        // Unicode 字符串 verb（菜单 ID 时通常为空），误用会让所有子菜单
        // 都变成命令 0（zip）。
        // Explorer normally passes the idCmdFirst-relative offset. Some
        // callers pass the absolute command id; accept that too when the
        // offset is unambiguous.
        // 资源管理器通常传相对 idCmdFirst 的偏移；部分调用方传绝对命令 ID，
        // 在不会产生歧义时一并兼容。
        if (m_firstCommand > 7 && command >= m_firstCommand) {
            command -= m_firstCommand;
        }
        if (command > 7) {
            return E_INVALIDARG;
        }
        std::vector<std::wstring> targets = m_paths;
        if (targets.empty() && m_background && !m_folder.empty()) {
            targets.push_back(m_folder);
        }
        if (targets.empty()) {
            return E_INVALIDARG;
        }

        switch (command) {
        case 0: runCompression(targets, L"zip", false); break;
        case 1: runCompression(targets, L"7z", false); break;
        case 2: runCompression(targets, L"zip", false); break;
        case 3: runCompression(targets, L"tar.gz", false); break;
        case 4: runCompression(targets, L"tar.xz", false); break;
        case 5: runCompression(targets, L"tar.bz2", false); break;
        case 6: runCompression(targets, L"tar", false); break;
        case 7: runCompression(targets, L"", true); break;
        default: return E_INVALIDARG;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetCommandString(UINT_PTR, UINT, UINT *,
                                               CHAR *, UINT) override
    {
        return E_NOTIMPL;
    }

private:
    void attachBitmap(HMENU menu, UINT position, const wchar_t *name,
                      DWORD attributes, bool appIcon = false)
    {
        MenuBitmap bitmap = makeMenuBitmap(name, attributes, appIcon);
        if (bitmap.color && bitmap.mask) {
            SetMenuItemBitmaps(menu, position, MF_BYPOSITION, bitmap.color,
                               bitmap.mask);
            m_bitmaps.push_back(bitmap);
        }
    }

    LONG m_reference = 1;
    std::vector<std::wstring> m_paths;
    std::wstring m_folder;
    bool m_background = false;
    UINT m_firstCommand = 0;
    std::vector<MenuBitmap> m_bitmaps;
};

class ClassFactory final : public IClassFactory {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override
    {
        if (!object) {
            return E_POINTER;
        }
        *object = nullptr;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *object = static_cast<IClassFactory *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return static_cast<ULONG>(InterlockedIncrement(&m_reference));
    }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const LONG reference = InterlockedDecrement(&m_reference);
        if (reference == 0) {
            delete this;
        }
        return static_cast<ULONG>(reference);
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown *outer, REFIID riid,
                                             void **object) override
    {
        if (outer) {
            return CLASS_E_NOAGGREGATION;
        }
        auto *menu = new (std::nothrow) ContextMenu();
        if (!menu) {
            return E_OUTOFMEMORY;
        }
        const HRESULT result = menu->QueryInterface(riid, object);
        menu->Release();
        return result;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override
    {
        if (lock) {
            InterlockedIncrement(&g_objectCount);
        } else {
            InterlockedDecrement(&g_objectCount);
        }
        return S_OK;
    }

private:
    LONG m_reference = 1;
};

// ---------------------------------------------------------------------------
// Registration helpers / 注册辅助
// ---------------------------------------------------------------------------

HKEY classesRoot()
{
    return IsUserAnAdmin() ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
}

bool setString(HKEY root, const std::wstring &key, const wchar_t *name,
               const std::wstring &value)
{
    HKEY handle = nullptr;
    if (RegCreateKeyExW(root, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr,
                        &handle, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const DWORD bytes =
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const LONG result = RegSetValueExW(
        handle, name, 0, REG_SZ,
        reinterpret_cast<const BYTE *>(value.c_str()), bytes);
    RegCloseKey(handle);
    return result == ERROR_SUCCESS;
}

bool deleteTree(HKEY root, const std::wstring &key)
{
    const LONG result = RegDeleteTreeW(root, key.c_str());
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}

void registerHandler(HKEY root, const std::wstring &base)
{
    setString(root, base + L"\\shellex\\ContextMenuHandlers\\" + kHandlerName,
              nullptr, kClsidString);
}

void unregisterHandler(HKEY root, const std::wstring &base)
{
    deleteTree(root,
               base + L"\\shellex\\ContextMenuHandlers\\" + kHandlerName);
}

void notifyShell()
{
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

} // namespace

// ---------------------------------------------------------------------------
// COM exports / COM 导出
// ---------------------------------------------------------------------------

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **object)
{
    if (!IsEqualCLSID(clsid, kClsid)) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    auto *factory = new (std::nothrow) ClassFactory();
    if (!factory) {
        return E_OUTOFMEMORY;
    }
    const HRESULT result = factory->QueryInterface(riid, object);
    factory->Release();
    return result;
}

STDAPI DllCanUnloadNow()
{
    return g_objectCount == 0 ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer()
{
    const std::wstring dll = modulePath();
    if (dll.empty()) {
        return E_FAIL;
    }
    HKEY root = classesRoot();
    const std::wstring clsidKey =
        std::wstring(L"Software\\Classes\\CLSID\\") + kClsidString;
    if (!setString(root, clsidKey, nullptr, kApprovedName)
        || !setString(root, clsidKey + L"\\InprocServer32", nullptr, dll)
        || !setString(root, clsidKey + L"\\InprocServer32",
                      L"ThreadingModel", L"Apartment")) {
        return E_FAIL;
    }
    registerHandler(root, L"Software\\Classes\\*");
    registerHandler(root, L"Software\\Classes\\Directory");
    registerHandler(root, L"Software\\Classes\\Directory\\Background");

    // Best effort: the Approved list is not required by modern Explorer but
    // some older shells use it to filter extensions.
    // 尽力写入 Approved 列表；现代资源管理器不强制要求。
    setString(HKEY_LOCAL_MACHINE,
              L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell "
              L"Extensions\\Approved",
              kClsidString, kApprovedName);
    notifyShell();
    return S_OK;
}

STDAPI DllUnregisterServer()
{
    HKEY root = classesRoot();
    unregisterHandler(root, L"Software\\Classes\\*");
    unregisterHandler(root, L"Software\\Classes\\Directory");
    unregisterHandler(root, L"Software\\Classes\\Directory\\Background");
    const std::wstring clsidKey =
        std::wstring(L"Software\\Classes\\CLSID\\") + kClsidString;
    deleteTree(root, clsidKey);
    RegDeleteKeyValueW(HKEY_LOCAL_MACHINE,
                       L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell "
                       L"Extensions\\Approved",
                       kClsidString);
    notifyShell();
    return S_OK;
}

BOOL APIENTRY DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_instance = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
