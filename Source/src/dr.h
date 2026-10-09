// Desktop Restore, Windows ARM64 port.
// Clean re-implementation of Desktop Restore 1.7 (dkticnsr.dll) by Jamie O'Connell,
// built as a standalone executable that works with Explorer from outside its process.
// Layouts stay compatible with the original: HKCU\Software\JOConnell\DeskTop and .dtr files.
#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace dr {

// "DR.3" format marker written by Desktop Restore 1.6 and later.
constexpr DWORD kFormatVersion = 0x332E5244;
constexpr int kMaxPasses = 100;  // as in 1.7.2; the loop stops as soon as nothing moves

extern HINSTANCE g_hInst;
extern const wchar_t kVersion[];

// One desktop icon in a stored layout.
struct IconRecord {
    std::wstring path;      // parsing name ("C:\...\x.lnk") or "::{CLSID}"
    std::wstring name;      // label as shown on the desktop
    int x = 0, y = 0;       // stored: relative to monitor; after ToViewCoords: desktop view coords
    int monitor = -1;       // EnumDisplayMonitors index, -1 = absolute view coordinates
    int index = 0;          // number used in the "Icon%04d" key/section name
    bool legacy = false;    // name-only record (very old format)
    bool oldFormat = false; // 1.5.x record: path holds a file name, not a full path
    bool floating = false;  // its monitor no longer exists; aimed at the primary, may need a free spot
};

struct Monitor {
    RECT rcMonitor{};   // screen coordinates (physical pixels)
    RECT rcWork{};
    bool primary = false;
    RECT rcView{};      // rcMonitor in desktop view coordinates
    RECT rcWorkView{};  // rcWork in desktop view coordinates
};

struct DisplayConfig {
    std::vector<Monitor> monitors;  // EnumDisplayMonitors order, like the original
    int systemMonitorCount = 1;     // GetSystemMetrics(SM_CMONITORS)
    int screenCx = 0, screenCy = 0; // primary screen size
    POINT viewOrigin{};             // screen position of desktop view coordinate (0,0)

    static DisplayConfig Query(HWND hwndView);  // hwndView may be null
    void SetViewOrigin(POINT origin);
    int MonitorAt(POINT viewPt) const;          // -1 when the point is on no monitor
    int PrimaryIndex() const;
    RECT PrimaryWorkView() const;
    POINT PrimaryCenterView() const;
};

// Layout name exactly as the original builds it, e.g. "Layout 1920 x 1080 (48)"
// or "Multi-Monitor *[1920 x 1080][2560 x 1440] (48)".
std::wstring LayoutName(const DisplayConfig& dc, int iconSize, bool withIconSize);

struct Settings {
    bool cascadeSave = true, cascadeRestore = true, cascadeCustom = true, cascadeHilite = true;
    bool confirmSave = true, confirmRestore = false, forceVisible = false;
    void Load();
    bool Save() const;
};

// Registry layouts: HKCU\<root>\<layout>\Icon0001 {XCoord, YCoord, Monitor, Name, Path} + Version.
void SetRegistryRoot(const std::wstring& subkey);  // tests only
const std::wstring& RegistryRoot();
std::vector<std::wstring> ListLayouts();
bool IsReservedLayoutName(const std::wstring& name);  // "Icon####" and the temporary save key
bool LayoutExists(const std::wstring& name);
bool SaveLayoutReg(const std::wstring& name, const std::vector<IconRecord>& recs);
bool LoadLayoutReg(const std::wstring& name, std::vector<IconRecord>& recs);
bool DeleteLayoutReg(const std::wstring& name);

// .dtr layout files (UTF-16 INI format).
bool SaveLayoutFile(const std::wstring& path, const std::vector<IconRecord>& recs);
bool LoadLayoutFile(const std::wstring& path, std::vector<IconRecord>& recs);

// Converts monitor-relative records to absolute desktop view coordinates.
void ToViewCoords(std::vector<IconRecord>& recs, const DisplayConfig& dc);

// Desktop abstraction, so the algorithms can be unit tested with a mock.
struct DesktopItem {
    std::wstring path, name;
    POINT pos{};
};

class IDesktop {
public:
    virtual ~IDesktop() = default;
    virtual int Count() = 0;
    virtual bool Item(int i, DesktopItem& out) = 0;  // false when the item is unusable
    virtual bool GetPos(int i, POINT& pt) = 0;
    virtual bool SetPos(int i, POINT pt) = 0;
    virtual SIZE Spacing() = 0;
    virtual bool AutoArrange() = 0;
    virtual void Select(const std::vector<int>& items) = 0;
    virtual bool BeginRestore() { return true; }  // false: Auto arrange is on and could not be turned off
    virtual void EndRestore() {}
};

std::vector<IconRecord> CaptureLayout(IDesktop& d, const DisplayConfig& dc);
// (record index, item index) pairs, sorted by record index.
std::vector<std::pair<int, int>> MatchRecords(const std::vector<IconRecord>& recs,
                                              const std::vector<DesktopItem>& items);
struct RestoreResult {
    int matched = 0, placed = 0, displaced = 0, madeVisible = 0, passes = 0;
    bool blocked = false;  // Auto arrange stayed on, nothing was moved
};
RestoreResult RestoreLayout(IDesktop& d, const DisplayConfig& dc,
                            const std::vector<IconRecord>& viewRecs, bool forceVisible);
std::vector<int> UnmatchedItems(IDesktop& d, const std::vector<IconRecord>& recs);

// The live desktop in Explorer, reached through COM (IShellWindows / IFolderView).
class ShellDesktop;
struct ShellDesktopDeleter {
    void operator()(ShellDesktop* p) const;
};
using ShellDesktopPtr = std::unique_ptr<ShellDesktop, ShellDesktopDeleter>;
ShellDesktopPtr OpenShellDesktop(HRESULT* phr);
HWND ShellDesktopListView(ShellDesktop& d);
int ShellDesktopIconSize(ShellDesktop& d);
IDesktop& AsDesktop(ShellDesktop& d);

// Desktop right-click menu (per user, no admin rights).
void SetMenuRoots(const std::wstring& shellKey, const std::wstring& uninstallKey);  // tests only
bool InstallMenu(const Settings& s, const std::wstring& exePath);
bool UninstallMenu();
bool MenuInstalled(std::wstring* exePathInMenu);

// Helpers.
std::wstring Format(const wchar_t* fmt, ...);
std::wstring LoadStr(UINT id);
std::wstring ExePath();
bool SameText(const std::wstring& a, const std::wstring& b);  // ordinal, case-insensitive

}  // namespace dr
