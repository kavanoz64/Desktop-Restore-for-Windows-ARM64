// Desktop right-click menu, registered per user as static verbs (nothing loads into Explorer).
//   HKCU\Software\Classes\DesktopBackground\Shell\DesktopRestore            cascade "Desktop Restore"
//   HKCU\Software\Classes\DesktopBackground\Shell\DesktopRestore\shell\N... items in the cascade
//   HKCU\Software\Classes\DesktopBackground\Shell\DesktopRestore.N...       items moved to the main menu
// plus an entry in Settings > Apps so the menu can be removed from there.
#include "dr.h"

#include <shlobj.h>
#include <shlwapi.h>

#include "resource.h"

namespace dr {

namespace {

std::wstring g_shell = L"Software\\Classes\\DesktopBackground\\Shell";
std::wstring g_uninstall = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\DesktopRestorePort";
const wchar_t kVerb[] = L"DesktopRestore";

#if defined(__aarch64__) || defined(_M_ARM64)
const wchar_t kArch[] = L"ARM64";
#elif defined(__x86_64__) || defined(_M_X64)
const wchar_t kArch[] = L"x64";
#else
const wchar_t kArch[] = L"x86";
#endif

struct MenuCmd {
    const wchar_t* key;
    const wchar_t* arg;
    UINT text;
    bool Settings::*cascade;  // null: always in the cascade
    bool Settings::*confirm;  // adds "..." when the command asks first, like the original
};

const MenuCmd kCmds[] = {
    {L"1Save", L"/save", IDS_MENU_SAVE, &Settings::cascadeSave, &Settings::confirmSave},
    {L"2Restore", L"/restore", IDS_MENU_RESTORE, &Settings::cascadeRestore, &Settings::confirmRestore},
    {L"3Custom", L"/custom", IDS_MENU_CUSTOM, &Settings::cascadeCustom, nullptr},
    {L"4Highlight", L"/highlight", IDS_MENU_HILITE, &Settings::cascadeHilite, nullptr},
    {L"5Settings", L"/settings", IDS_MENU_SETTINGS, nullptr, nullptr},
};

bool SetStr(const std::wstring& sub, const wchar_t* name, const std::wstring& v) {
    HKEY h = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, sub.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &h, nullptr) !=
        ERROR_SUCCESS)
        return false;
    LONG r = RegSetValueExW(h, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()),
                            static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(h);
    return r == ERROR_SUCCESS;
}

bool SetDw(const std::wstring& sub, const wchar_t* name, DWORD v) {
    HKEY h = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, sub.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &h, nullptr) !=
        ERROR_SUCCESS)
        return false;
    LONG r = RegSetValueExW(h, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v));
    RegCloseKey(h);
    return r == ERROR_SUCCESS;
}

bool DeleteTree(const std::wstring& sub) {
    LSTATUS r = SHDeleteKeyW(HKEY_CURRENT_USER, sub.c_str());
    return r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
}

std::wstring Base() { return g_shell + L"\\" + kVerb; }

}  // namespace

void SetMenuRoots(const std::wstring& shellKey, const std::wstring& uninstallKey) {
    g_shell = shellKey;
    g_uninstall = uninstallKey;
}

bool UninstallMenu() {
    bool ok = DeleteTree(Base());
    for (const auto& c : kCmds) ok &= DeleteTree(Base() + L"." + c.key);
    ok &= DeleteTree(g_uninstall);
    return ok;
}

bool InstallMenu(const Settings& s, const std::wstring& exe) {
    UninstallMenu();
    const std::wstring quoted = L"\"" + exe + L"\"";
    const std::wstring icon = exe + L",0";
    const std::wstring base = Base();
    bool ok = SetStr(base, L"MUIVerb", LoadStr(IDS_APP_TITLE));
    ok &= SetStr(base, L"SubCommands", L"");
    ok &= SetStr(base, L"Icon", icon);
    ok &= SetStr(base, L"DesktopRestoreExe", exe);
    for (const auto& c : kCmds) {
        std::wstring text = LoadStr(c.text);
        if (c.confirm && s.*(c.confirm)) text += L"...";
        bool cascade = !c.cascade || s.*(c.cascade);
        std::wstring key = cascade ? base + L"\\shell\\" + c.key : base + L"." + c.key;
        ok &= SetStr(key, L"MUIVerb", text);
        if (!cascade) ok &= SetStr(key, L"Icon", icon);
        ok &= SetStr(key + L"\\command", nullptr, quoted + L" " + c.arg);
    }

    // Settings > Apps entry.
    std::wstring dir = exe.substr(0, exe.find_last_of(L'\\'));
    ok &= SetStr(g_uninstall, L"DisplayName", std::wstring(L"Desktop Restore (") + kArch + L")");
    ok &= SetStr(g_uninstall, L"DisplayVersion", kVersion);
    ok &= SetStr(g_uninstall, L"DisplayIcon", icon);
    ok &= SetStr(g_uninstall, L"InstallLocation", dir);
    ok &= SetStr(g_uninstall, L"UninstallString", quoted + L" /uninstall");
    ok &= SetStr(g_uninstall, L"QuietUninstallString", quoted + L" /uninstall /q");
    ok &= SetDw(g_uninstall, L"NoModify", 1);
    ok &= SetDw(g_uninstall, L"NoRepair", 1);
    return ok;
}

bool MenuInstalled(std::wstring* exePathInMenu) {
    wchar_t buf[MAX_PATH * 4];
    DWORD cb = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, Base().c_str(), L"DesktopRestoreExe", RRF_RT_REG_SZ, nullptr, buf, &cb) !=
        ERROR_SUCCESS)
        return false;
    if (exePathInMenu) *exePathInMenu = buf;
    return true;
}

}  // namespace dr
