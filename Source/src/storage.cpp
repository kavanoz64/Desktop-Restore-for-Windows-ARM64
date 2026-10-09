// Layout storage, layout naming, settings and display geometry.
// Formats follow Desktop Restore 1.7 so layouts written by either program work in the other.
#include "dr.h"

#include <shlwapi.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cwctype>

namespace dr {

HINSTANCE g_hInst = nullptr;
const wchar_t kVersion[] = L"1.7.2 port r2";  // port of 1.7.2.083; r1 was "1.7.1"

static std::wstring g_root = L"Software\\JOConnell\\DeskTop";
static const wchar_t kTempLayout[] = L"_DesktopRestore_Saving_";

void SetRegistryRoot(const std::wstring& subkey) { g_root = subkey; }
const std::wstring& RegistryRoot() { return g_root; }

// ---------------------------------------------------------------- helpers

std::wstring Format(const wchar_t* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = _vscwprintf(fmt, ap);
    std::wstring s;
    if (n > 0) {
        s.resize(static_cast<size_t>(n) + 1);
        vswprintf(&s[0], s.size(), fmt, ap2);
        s.resize(static_cast<size_t>(n));
    }
    va_end(ap2);
    va_end(ap);
    return s;
}

std::wstring LoadStr(UINT id) {
    const wchar_t* p = nullptr;
    int n = LoadStringW(g_hInst, id, reinterpret_cast<LPWSTR>(&p), 0);
    return (n > 0 && p) ? std::wstring(p, static_cast<size_t>(n)) : std::wstring();
}

std::wstring ExePath() {
    std::wstring s(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, &s[0], static_cast<DWORD>(s.size()));
        if (n == 0) return std::wstring();
        if (n < s.size()) {
            s.resize(n);
            return s;
        }
        s.resize(s.size() * 2);
    }
}

bool SameText(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(),
                                static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

static DWORD GetDword(HKEY h, const wchar_t* name, DWORD def) {
    DWORD type = 0, v = 0, cb = sizeof(v);
    if (RegQueryValueExW(h, name, nullptr, &type, reinterpret_cast<BYTE*>(&v), &cb) == ERROR_SUCCESS &&
        type == REG_DWORD && cb == sizeof(v))
        return v;
    return def;
}

static bool SetDword(HKEY h, const wchar_t* name, DWORD v) {
    return RegSetValueExW(h, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v)) ==
           ERROR_SUCCESS;
}

static std::wstring GetSz(HKEY h, const wchar_t* name, const wchar_t* def) {
    DWORD type = 0, cb = 0;
    if (RegQueryValueExW(h, name, nullptr, &type, nullptr, &cb) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ))
        return def;
    std::wstring s(cb / sizeof(wchar_t) + 1, L'\0');
    DWORD cb2 = static_cast<DWORD>(s.size() * sizeof(wchar_t));
    if (RegQueryValueExW(h, name, nullptr, &type, reinterpret_cast<BYTE*>(&s[0]), &cb2) != ERROR_SUCCESS)
        return def;
    s.resize(wcsnlen(s.c_str(), s.size()));
    return s;
}

static bool SetSz(HKEY h, const wchar_t* name, const std::wstring& v) {
    return RegSetValueExW(h, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()),
                          static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

class Key {
public:
    Key() = default;
    ~Key() { Close(); }
    Key(const Key&) = delete;
    Key& operator=(const Key&) = delete;
    bool Open(HKEY parent, const std::wstring& sub, REGSAM sam) {
        Close();
        return RegOpenKeyExW(parent, sub.c_str(), 0, sam, &h_) == ERROR_SUCCESS;
    }
    bool Create(HKEY parent, const std::wstring& sub, REGSAM sam) {
        Close();
        return RegCreateKeyExW(parent, sub.c_str(), 0, nullptr, 0, sam, nullptr, &h_, nullptr) ==
               ERROR_SUCCESS;
    }
    void Close() {
        if (h_) RegCloseKey(h_);
        h_ = nullptr;
    }
    HKEY get() const { return h_; }

private:
    HKEY h_ = nullptr;
};

// ------------------------------------------------------- display geometry

static BOOL CALLBACK EnumMonitorProc(HMONITOR hmon, HDC, LPRECT, LPARAM lp) {
    auto* list = reinterpret_cast<std::vector<Monitor>*>(lp);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hmon, &mi)) {
        Monitor m;
        m.rcMonitor = mi.rcMonitor;
        m.rcWork = mi.rcWork;
        m.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
        list->push_back(m);
    }
    return TRUE;
}

DisplayConfig DisplayConfig::Query(HWND hwndView) {
    DisplayConfig dc;
    dc.systemMonitorCount = GetSystemMetrics(SM_CMONITORS);
    dc.screenCx = GetSystemMetrics(SM_CXSCREEN);
    dc.screenCy = GetSystemMetrics(SM_CYSCREEN);
    EnumDisplayMonitors(nullptr, nullptr, EnumMonitorProc, reinterpret_cast<LPARAM>(&dc.monitors));
    // The desktop list view spans the virtual screen; its client (0,0) is our origin.
    POINT origin{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN)};
    if (hwndView) {
        POINT p{0, 0};
        if (ClientToScreen(hwndView, &p)) origin = p;
    }
    dc.SetViewOrigin(origin);
    return dc;
}

void DisplayConfig::SetViewOrigin(POINT origin) {
    viewOrigin = origin;
    for (auto& m : monitors) {
        m.rcView = m.rcMonitor;
        OffsetRect(&m.rcView, -origin.x, -origin.y);
        m.rcWorkView = m.rcWork;
        OffsetRect(&m.rcWorkView, -origin.x, -origin.y);
    }
}

int DisplayConfig::MonitorAt(POINT pt) const {
    for (size_t i = 0; i < monitors.size(); i++)
        if (PtInRect(&monitors[i].rcView, pt)) return static_cast<int>(i);
    return -1;
}

int DisplayConfig::PrimaryIndex() const {
    for (size_t i = 0; i < monitors.size(); i++)
        if (monitors[i].primary) return static_cast<int>(i);
    return monitors.empty() ? -1 : 0;
}

RECT DisplayConfig::PrimaryWorkView() const {
    int p = PrimaryIndex();
    if (p >= 0) return monitors[static_cast<size_t>(p)].rcWorkView;
    RECT r{0, 0, screenCx, screenCy};
    return r;
}

POINT DisplayConfig::PrimaryCenterView() const {
    int p = PrimaryIndex();
    RECT r{0, 0, screenCx, screenCy};
    if (p >= 0) r = monitors[static_cast<size_t>(p)].rcView;
    POINT c{r.left + (r.right - r.left) / 2, r.top + (r.bottom - r.top) / 2};
    return c;
}

std::wstring LayoutName(const DisplayConfig& dc, int iconSize, bool withIconSize) {
    std::wstring s;
    if (dc.systemMonitorCount <= 1) {
        s = Format(L"Layout %d x %d", dc.screenCx, dc.screenCy);
    } else {
        s = L"Multi-Monitor ";
        for (const auto& m : dc.monitors) {
            if (m.primary) s += L"*";
            s += Format(L"[%d x %d]", static_cast<int>(m.rcMonitor.right - m.rcMonitor.left),
                        static_cast<int>(m.rcMonitor.bottom - m.rcMonitor.top));
        }
    }
    if (withIconSize && iconSize != 0) s += Format(L" (%lu)", static_cast<unsigned long>(iconSize));
    return s;
}

void ToViewCoords(std::vector<IconRecord>& recs, const DisplayConfig& dc) {
    for (auto& r : recs) {
        if (r.legacy || r.monitor < 0) continue;  // already absolute
        int m = r.monitor;
        if (m >= static_cast<int>(dc.monitors.size())) {  // that monitor is gone: aim for the primary
            m = dc.PrimaryIndex();
            r.floating = true;
        }
        if (m < 0) continue;
        r.x += dc.monitors[static_cast<size_t>(m)].rcView.left;
        r.y += dc.monitors[static_cast<size_t>(m)].rcView.top;
        r.monitor = -1;
    }
}

// --------------------------------------------------------------- settings

void Settings::Load() {
    *this = Settings();
    Key k;
    if (!k.Open(HKEY_CURRENT_USER, g_root, KEY_READ)) return;
    cascadeSave = GetDword(k.get(), L"Cascade Save", 1) != 0;
    cascadeRestore = GetDword(k.get(), L"Cascade Restore", 1) != 0;
    cascadeCustom = GetDword(k.get(), L"Cascade Custom", 1) != 0;
    cascadeHilite = GetDword(k.get(), L"Cascade Hilite", 1) != 0;
    confirmSave = GetDword(k.get(), L"Confirm Save", 1) != 0;
    confirmRestore = GetDword(k.get(), L"Confirm Restore", 0) != 0;
    forceVisible = GetDword(k.get(), L"Force Visible", 0) != 0;
}

bool Settings::Save() const {
    Key k;
    if (!k.Create(HKEY_CURRENT_USER, g_root, KEY_READ | KEY_WRITE)) return false;
    bool ok = SetDword(k.get(), L"Cascade Save", cascadeSave);
    ok &= SetDword(k.get(), L"Cascade Restore", cascadeRestore);
    ok &= SetDword(k.get(), L"Cascade Custom", cascadeCustom);
    ok &= SetDword(k.get(), L"Cascade Hilite", cascadeHilite);
    ok &= SetDword(k.get(), L"Confirm Save", confirmSave);
    ok &= SetDword(k.get(), L"Confirm Restore", confirmRestore);
    ok &= SetDword(k.get(), L"Force Visible", forceVisible);
    return ok;
}

// ------------------------------------------------------- registry layouts

// The original skips (and deletes) stray "Icon####" keys at the top level.
static bool IsIconKeyName(const wchar_t* s) {
    if (wcsncmp(s, L"Icon", 4) != 0) return false;
    for (int i = 4; i < 8; i++)
        if (!iswdigit(s[i])) return false;
    return true;
}

bool IsReservedLayoutName(const std::wstring& name) {
    return IsIconKeyName(name.c_str()) || SameText(name, kTempLayout);
}

std::vector<std::wstring> ListLayouts() {
    std::vector<std::wstring> v;
    Key k;
    if (!k.Open(HKEY_CURRENT_USER, g_root, KEY_READ)) return v;
    for (DWORD i = 0;; i++) {
        wchar_t name[256];
        DWORD cch = 256;
        LONG r = RegEnumKeyExW(k.get(), i, name, &cch, nullptr, nullptr, nullptr, nullptr);
        if (r == ERROR_MORE_DATA) continue;
        if (r != ERROR_SUCCESS) break;
        if (!IsReservedLayoutName(name)) v.push_back(name);
    }
    return v;
}

bool LayoutExists(const std::wstring& name) {
    if (name.empty()) return false;
    Key k;
    return k.Open(HKEY_CURRENT_USER, g_root + L"\\" + name, KEY_READ);
}

bool SaveLayoutReg(const std::wstring& name, const std::vector<IconRecord>& recs) {
    if (recs.empty() || name.empty() || name.size() > 255 || name.find(L'\\') != std::wstring::npos ||
        IsReservedLayoutName(name))
        return false;
    Key root;
    if (!root.Create(HKEY_CURRENT_USER, g_root, KEY_READ | KEY_WRITE | DELETE)) return false;
    // Write everything into a temporary key first, so a failed save never loses the old layout.
    SHDeleteKeyW(root.get(), kTempLayout);
    bool ok = true;
    {
        Key lay;
        if (!lay.Create(root.get(), kTempLayout, KEY_READ | KEY_WRITE)) return false;
        for (const auto& r : recs) {
            Key ik;
            if (!ik.Create(lay.get(), Format(L"Icon%04d", r.index), KEY_WRITE)) {
                ok = false;
                break;
            }
            ok &= SetDword(ik.get(), L"XCoord", static_cast<DWORD>(r.x));
            ok &= SetDword(ik.get(), L"YCoord", static_cast<DWORD>(r.y));
            ok &= SetDword(ik.get(), L"Monitor", static_cast<DWORD>(r.monitor));
            ok &= SetSz(ik.get(), L"Name", r.name);
            ok &= SetSz(ik.get(), L"Path", r.path);
            if (!ok) break;
        }
        ok = ok && SetDword(lay.get(), L"Version", kFormatVersion);
    }
    if (ok) {
        SHDeleteKeyW(root.get(), name.c_str());
        ok = RegRenameKey(root.get(), kTempLayout, name.c_str()) == ERROR_SUCCESS;
        if (!ok) {  // fall back to copying
            Key dst;
            ok = dst.Create(root.get(), name, KEY_READ | KEY_WRITE) &&
                 SHCopyKeyW(root.get(), kTempLayout, dst.get(), 0) == ERROR_SUCCESS;
        }
    }
    SHDeleteKeyW(root.get(), kTempLayout);
    return ok;
}

bool LoadLayoutReg(const std::wstring& name, std::vector<IconRecord>& recs) {
    recs.clear();
    Key lay;
    if (name.empty() || !lay.Open(HKEY_CURRENT_USER, g_root + L"\\" + name, KEY_READ)) return false;
    if (GetDword(lay.get(), L"Version", 0xFFFFFFFF) == kFormatVersion) {
        for (DWORD i = 0;; i++) {
            wchar_t kn[256];
            DWORD cch = 256;
            LONG r = RegEnumKeyExW(lay.get(), i, kn, &cch, nullptr, nullptr, nullptr, nullptr);
            if (r == ERROR_MORE_DATA) continue;
            if (r != ERROR_SUCCESS) break;
            Key ik;
            if (!ik.Open(lay.get(), kn, KEY_READ)) continue;
            IconRecord rec;
            rec.x = static_cast<int>(GetDword(ik.get(), L"XCoord", 0xFFFFFFFF));
            rec.y = static_cast<int>(GetDword(ik.get(), L"YCoord", 0xFFFFFFFF));
            rec.monitor = static_cast<int>(GetDword(ik.get(), L"Monitor", 0xFFFFFFFF));
            rec.name = GetSz(ik.get(), L"Name", L"Empty");
            rec.path = GetSz(ik.get(), L"Path", L"Empty");
            rec.index = static_cast<int>(i) + 1;
            recs.push_back(rec);
        }
    } else {
        // Pre-1.6 layouts keep one value per icon:
        //   REG_DWORD  name -> MAKELONG(x, y)              (oldest, name only)
        //   REG_BINARY file -> POINT + label (UTF-16)       (1.5.2 to 1.5.6)
        std::vector<wchar_t> vn(16384);
        std::vector<BYTE> data(4096);
        for (DWORD i = 0;; i++) {
            DWORD cch = static_cast<DWORD>(vn.size()), type = 0, cb = static_cast<DWORD>(data.size());
            LONG r = RegEnumValueW(lay.get(), i, vn.data(), &cch, nullptr, &type, data.data(), &cb);
            if (r == ERROR_MORE_DATA) continue;
            if (r != ERROR_SUCCESS) break;
            IconRecord rec;
            rec.index = static_cast<int>(i) + 1;
            if (type == REG_DWORD && cb == 4) {
                DWORD v;
                memcpy(&v, data.data(), 4);
                rec.name = rec.path = std::wstring(vn.data(), cch);
                rec.x = LOWORD(v);
                rec.y = HIWORD(v);
                rec.legacy = true;
            } else if (type == REG_BINARY && cb >= 8) {
                LONG xy[2];
                memcpy(xy, data.data(), 8);
                size_t chars = (cb - 8) / sizeof(wchar_t);
                std::wstring label(chars, L'\0');
                if (chars) memcpy(&label[0], data.data() + 8, chars * sizeof(wchar_t));
                label.resize(wcsnlen(label.c_str(), label.size()));
                rec.path = std::wstring(vn.data(), cch);
                rec.name = label;
                rec.x = xy[0];
                rec.y = xy[1];
                rec.oldFormat = true;
            } else {
                continue;
            }
            recs.push_back(rec);
        }
    }
    return !recs.empty();
}

bool DeleteLayoutReg(const std::wstring& name) {
    if (name.empty()) return false;
    Key root;
    if (!root.Open(HKEY_CURRENT_USER, g_root, KEY_READ | KEY_WRITE | DELETE)) return false;
    return SHDeleteKeyW(root.get(), name.c_str()) == ERROR_SUCCESS;
}

// ------------------------------------------------------------- .dtr files

bool SaveLayoutFile(const std::wstring& path, const std::vector<IconRecord>& recs) {
    std::wstring s;
    s += L'\xFEFF';  // UTF-16LE BOM, like _wfopen(..., L"w, ccs=UNICODE")
    s += L";\r\n; Desktop Restore: ";
    s += kVersion;
    s += L". UNICODE Layout File.\r\n;\r\n\r\n";
    for (const auto& r : recs) {
        s += Format(L"[Icon%04d]\r\n", r.index);
        s += Format(L"Legacy=%x\r\n", r.legacy ? 1u : 0u);
        s += Format(L"XCoord=%d\r\n", r.x);
        s += Format(L"YCoord=%d\r\n", r.y);
        s += L"Name=" + r.name + L"\r\n";
        s += L"Path=" + r.path + L"\r\n";
        if (!r.legacy) s += Format(L"Monitor=%d\r\n", r.monitor);
    }
    if (!recs.empty()) s += Format(L"[_DesktopRestore_]\r\nVersion=%d\r\n", static_cast<int>(kFormatVersion));

    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD bytes = static_cast<DWORD>(s.size() * sizeof(wchar_t)), done = 0;
    BOOL ok = WriteFile(h, s.data(), bytes, &done, nullptr);
    CloseHandle(h);
    return ok && done == bytes;
}

static std::wstring IniStr(const wchar_t* sec, const wchar_t* key, const std::wstring& file) {
    std::vector<wchar_t> buf(8192);
    DWORD n = GetPrivateProfileStringW(sec, key, L"", buf.data(), static_cast<DWORD>(buf.size()),
                                       file.c_str());
    return std::wstring(buf.data(), n);
}

static long IniInt(const wchar_t* sec, const wchar_t* key, long def, const std::wstring& file,
                   int base = 10) {
    std::wstring s = IniStr(sec, key, file);
    if (s.empty()) return def;
    wchar_t* end = nullptr;
    long v = wcstol(s.c_str(), &end, base);
    return end == s.c_str() ? def : v;
}

bool LoadLayoutFile(const std::wstring& file, std::vector<IconRecord>& recs) {
    recs.clear();
    if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    std::vector<wchar_t> names(0x20000);
    DWORD n = GetPrivateProfileSectionNamesW(names.data(), static_cast<DWORD>(names.size()), file.c_str());
    if (n == 0 || n >= names.size() - 2) return false;
    names[n] = L'\0';
    std::wstring verStr = IniStr(L"_DesktopRestore_", L"Version", file);
    if (verStr.empty()) verStr = IniStr(L"", L"Version", file);
    bool newFormat = wcstoul(verStr.c_str(), nullptr, 10) == kFormatVersion;
    int idx = 1;
    for (const wchar_t* sec = names.data(); *sec; sec += wcslen(sec) + 1, idx++) {
        std::wstring name = IniStr(sec, L"Name", file);
        if (name.empty()) continue;
        IconRecord r;
        r.name = name;
        r.index = idx;
        r.x = static_cast<int>(IniInt(sec, L"XCoord", 0, file));
        r.y = static_cast<int>(IniInt(sec, L"YCoord", 0, file));
        bool legacy = IniInt(sec, L"Legacy", 0, file, 16) != 0;
        if (legacy) {
            r.path = name;
            r.legacy = true;
        } else if (newFormat) {
            r.path = IniStr(sec, L"Path", file);
            r.monitor = static_cast<int>(IniInt(sec, L"Monitor", -1, file));
        } else {
            r.path = sec;  // 1.5.x files: the section name is the file name
            r.oldFormat = true;
        }
        recs.push_back(r);
    }
    return !recs.empty();
}

}  // namespace dr
