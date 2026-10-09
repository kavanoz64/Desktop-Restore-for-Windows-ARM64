// Commands and dialogs: Save, Restore, Custom Save/Restore, Highlight, Settings.
// Behaviour follows Desktop Restore 1.7 (prompts, defaults, layout names, fallbacks).
#include "app.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include "resource.h"

namespace dr {

int Msg(HWND owner, const std::wstring& text, UINT flags) {
    return MessageBoxW(owner, text.c_str(), LoadStr(IDS_CAPTION).c_str(), flags | MB_SETFOREGROUND);
}

static void SetAppIcon(HWND dlg) {
    HICON big = static_cast<HICON>(LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                              GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0));
    HICON small = static_cast<HICON>(LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                                GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    if (big) SendMessageW(dlg, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big));
    if (small) SendMessageW(dlg, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small));
}

// ---------------------------------------------------------------- session

bool Session::Open(bool quiet) {
    HRESULT hr = S_OK;
    desk = OpenShellDesktop(&hr);
    if (!desk) {
        if (!quiet) Msg(nullptr, LoadStr(IDS_NO_LISTVIEW), MB_OK | MB_ICONEXCLAMATION);
        return false;
    }
    dc = DisplayConfig::Query(ShellDesktopListView(*desk));
    iconSize = ShellDesktopIconSize(*desk);
    settings.Load();
    return true;
}

std::wstring Session::CurrentName() const { return LayoutName(dc, iconSize, true); }

// The original looks for the name with the icon size first, then without it (pre-1.6.4 layouts).
std::wstring Session::FindCurrentLayout() const {
    std::wstring n = LayoutName(dc, iconSize, true);
    if (LayoutExists(n)) return n;
    n = LayoutName(dc, iconSize, false);
    return LayoutExists(n) ? n : std::wstring();
}

static int DoRestore(Session& s, std::vector<IconRecord> recs, bool quiet) {
    // Restoring switches Auto arrange off (see ShellDesktop::BeginRestore); ask first.
    if (AsDesktop(*s.desk).AutoArrange() && !quiet) {
        if (Msg(nullptr, LoadStr(IDS_AUTO_ARRANGE), MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return 1;
        if (!s.Open(quiet)) return 1;  // fresh view: the desktop may have changed while asking
    }
    ToViewCoords(recs, s.dc);
    RestoreResult r = RestoreLayout(AsDesktop(*s.desk), s.dc, recs, s.settings.forceVisible);
    if (r.blocked) {
        if (!quiet) Msg(nullptr, LoadStr(IDS_AUTO_ARRANGE_ON), MB_OK | MB_ICONWARNING);
        return 1;
    }
    return r.matched > 0 ? 0 : 1;
}

// Never replace a layout with an empty capture (for example when Explorer restarted meanwhile).
static bool Capture(Session& s, std::vector<IconRecord>& recs, bool quiet) {
    recs = CaptureLayout(AsDesktop(*s.desk), s.dc);
    if (recs.empty() && !quiet) Msg(nullptr, LoadStr(IDS_NOTHING_SAVED), MB_OK | MB_ICONWARNING);
    return !recs.empty();
}

static int DoSave(Session& s, const std::wstring& name, bool quiet) {
    std::vector<IconRecord> recs;
    if (!Capture(s, recs, quiet)) return 1;
    if (!SaveLayoutReg(name, recs)) {
        if (!quiet) Msg(nullptr, Format(LoadStr(IDS_SAVE_ERROR).c_str(), name.c_str()), MB_OK | MB_ICONWARNING);
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------- commands

int CmdSave(bool quiet, const std::wstring& layout) {
    Session s;
    if (!s.Open(quiet)) return 1;
    std::wstring name = layout;
    if (name.empty()) {
        name = s.CurrentName();
        if (s.settings.confirmSave && !quiet) {
            if (Msg(nullptr, Format(LoadStr(IDS_CONFIRM_SAVE).c_str(), name.c_str()),
                    MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
                return 1;
            if (!s.Open(quiet)) return 1;  // fresh view: the desktop may have changed while asking
        }
    }
    return DoSave(s, name, quiet);
}

int CmdRestore(bool quiet, const std::wstring& layout) {
    Session s;
    if (!s.Open(quiet)) return 1;
    std::wstring name = layout;
    if (name.empty()) {
        name = s.FindCurrentLayout();
        if (name.empty()) {  // nothing saved for this resolution: the original opens Custom
            if (quiet) return 1;
            s.desk.reset();
            return CmdCustom();
        }
        if (s.settings.confirmRestore && !quiet) {
            if (Msg(nullptr, Format(LoadStr(IDS_CONFIRM_RESTORE).c_str(), name.c_str()),
                    MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
                return 1;
            if (!s.Open(quiet)) return 1;
        }
    }
    std::vector<IconRecord> recs;
    if (!LoadLayoutReg(name, recs)) {
        if (!quiet) Msg(nullptr, Format(LoadStr(IDS_NO_ICON_INFO).c_str(), name.c_str()), MB_OK | MB_ICONWARNING);
        return 1;
    }
    return DoRestore(s, recs, quiet);
}

int CmdSaveFile(bool quiet, const std::wstring& file) {
    Session s;
    if (!s.Open(quiet)) return 1;
    std::vector<IconRecord> recs;
    if (!Capture(s, recs, quiet)) return 1;
    if (!SaveLayoutFile(file, recs)) {
        if (!quiet) Msg(nullptr, Format(LoadStr(IDS_FILE_ERROR).c_str(), file.c_str()), MB_OK | MB_ICONWARNING);
        return 1;
    }
    return 0;
}

int CmdRestoreFile(bool quiet, const std::wstring& file) {
    Session s;
    if (!s.Open(quiet)) return 1;
    std::vector<IconRecord> recs;
    if (!LoadLayoutFile(file, recs)) {
        if (!quiet) Msg(nullptr, Format(LoadStr(IDS_NO_ICON_INFO).c_str(), file.c_str()), MB_OK | MB_ICONWARNING);
        return 1;
    }
    return DoRestore(s, recs, quiet);
}

int CmdHighlight(bool quiet) {
    Session s;
    if (!s.Open(quiet)) return 1;
    std::wstring name = s.FindCurrentLayout();
    std::vector<IconRecord> recs;
    if (name.empty() || !LoadLayoutReg(name, recs)) {
        if (!quiet)
            Msg(nullptr, Format(LoadStr(IDS_NO_ICON_INFO).c_str(), (name.empty() ? s.CurrentName() : name).c_str()),
                MB_OK | MB_ICONWARNING);
        return 1;
    }
    IDesktop& d = AsDesktop(*s.desk);
    std::vector<int> unmatched = UnmatchedItems(d, recs);
    if (unmatched.empty()) {
        if (!quiet) Msg(nullptr, Format(LoadStr(IDS_ALL_MATCHED).c_str(), name.c_str()), MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    d.Select(unmatched);
    return 0;
}

// ------------------------------------------------------------- diagnostics

static std::wstring LayoutFolder(bool create);

// Read-only report of what the program sees, for troubleshooting on a new machine.
int CmdDiag(bool quiet, const std::wstring& fileIn) {
    std::wstring out;
    auto line = [&out](const std::wstring& s) { out += s + L"\r\n"; };
#if defined(__aarch64__) || defined(_M_ARM64)
    line(Format(L"Desktop Restore %ls (ARM64 build) diagnostics", kVersion));
#else
    line(Format(L"Desktop Restore %ls (x64 build) diagnostics", kVersion));
#endif
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    using IsWow64Process2Fn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
    OSVERSIONINFOW vi{};
    vi.dwOSVersionInfoSize = sizeof(vi);
    auto rtlGetVersion =
        reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    if (rtlGetVersion && rtlGetVersion(&vi) == 0)
        line(Format(L"Windows %lu.%lu.%lu", vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber));
    auto isWow64Process2 =
        reinterpret_cast<IsWow64Process2Fn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2"));
    USHORT procMachine = 0, nativeMachine = 0;
    if (isWow64Process2 && isWow64Process2(GetCurrentProcess(), &procMachine, &nativeMachine))
        line(Format(L"Native machine 0x%04X (0xAA64 = ARM64, 0x8664 = x64)", nativeMachine));
    using GetCtxFn = DPI_AWARENESS_CONTEXT(WINAPI*)();
    using GetAwFn = DPI_AWARENESS(WINAPI*)(DPI_AWARENESS_CONTEXT);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    auto getCtx = reinterpret_cast<GetCtxFn>(GetProcAddress(user32, "GetThreadDpiAwarenessContext"));
    auto getAw = reinterpret_cast<GetAwFn>(GetProcAddress(user32, "GetAwarenessFromDpiAwarenessContext"));
    if (getCtx && getAw) line(Format(L"DPI awareness %d (2 = per monitor)", static_cast<int>(getAw(getCtx()))));

    Session s;
    bool ok = s.Open(true);
    line(ok ? L"Desktop view: found" : L"Desktop view: NOT FOUND");
    if (ok) {
        IDesktop& d = AsDesktop(*s.desk);
        SIZE sp = d.Spacing();
        line(Format(L"List view window %p, view origin %ld,%ld", static_cast<void*>(ShellDesktopListView(*s.desk)),
                    s.dc.viewOrigin.x, s.dc.viewOrigin.y));
        for (size_t i = 0; i < s.dc.monitors.size(); i++) {
            const Monitor& m = s.dc.monitors[i];
            line(Format(L"Monitor %d%ls: %ld,%ld to %ld,%ld, work area %ld,%ld to %ld,%ld", static_cast<int>(i),
                        m.primary ? L" (primary)" : L"", m.rcMonitor.left, m.rcMonitor.top, m.rcMonitor.right,
                        m.rcMonitor.bottom, m.rcWork.left, m.rcWork.top, m.rcWork.right, m.rcWork.bottom));
        }
        line(Format(L"Icon size %d, spacing %ldx%ld, auto arrange %ls", s.iconSize, sp.cx, sp.cy,
                    d.AutoArrange() ? L"ON" : L"off"));
        std::wstring found = s.FindCurrentLayout();
        line(L"Current layout name: " + s.CurrentName() + L" (saved: " + (found.empty() ? L"no" : found) + L")");
        int n = d.Count();
        line(Format(L"Icons: %d", n));
        for (int i = 0; i < n; i++) {
            DesktopItem it;
            bool valid = d.Item(i, it);
            line(Format(L"%3d %ls pos %ld,%ld monitor %d  %ls  |  %ls", i, valid ? L"ok " : L"BAD", it.pos.x, it.pos.y,
                        s.dc.MonitorAt(it.pos), it.name.c_str(), it.path.c_str()));
        }
    }
    line(L"Saved layouts:");
    for (const auto& name : ListLayouts()) line(L"  " + name);

    std::wstring file = fileIn;
    if (file.empty()) {
        std::wstring dir = LayoutFolder(true);
        if (dir.empty()) {
            wchar_t tmp[MAX_PATH + 1] = L"";
            GetTempPathW(MAX_PATH + 1, tmp);
            dir = tmp;
            if (!dir.empty() && dir.back() == L'\\') dir.pop_back();
        }
        file = dir + L"\\diagnostics.txt";
    }
    std::wstring data = L"\xFEFF" + out;
    HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD done = 0;
    bool written = h != INVALID_HANDLE_VALUE &&
                   WriteFile(h, data.data(), static_cast<DWORD>(data.size() * sizeof(wchar_t)), &done, nullptr);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    if (!written) {
        if (!quiet) Msg(nullptr, Format(LoadStr(IDS_FILE_ERROR).c_str(), file.c_str()), MB_OK | MB_ICONWARNING);
        return 1;
    }
    if (!quiet) ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return ok ? 0 : 1;
}

// ---------------------------------------------------- custom save/restore

struct CustomData {
    std::wstring current;
    int action = 0;
    std::wstring value;
};

static std::wstring WindowText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(static_cast<size_t>(n) + 1, L'\0');
    n = GetWindowTextW(h, &s[0], n + 1);
    s.resize(static_cast<size_t>(n > 0 ? n : 0));
    return s;
}

static std::wstring Trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t"), b = s.find_last_not_of(L" \t");
    return a == std::wstring::npos ? std::wstring() : s.substr(a, b - a + 1);
}

static std::wstring SelectedLayout(HWND dlg) {
    HWND cb = GetDlgItem(dlg, IDC_LAYOUTS);
    LRESULT sel = SendMessageW(cb, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR) return std::wstring();
    LRESULT len = SendMessageW(cb, CB_GETLBTEXTLEN, static_cast<WPARAM>(sel), 0);
    if (len == CB_ERR || len <= 0) return std::wstring();
    std::wstring s(static_cast<size_t>(len) + 1, L'\0');
    SendMessageW(cb, CB_GETLBTEXT, static_cast<WPARAM>(sel), reinterpret_cast<LPARAM>(&s[0]));
    s.resize(wcsnlen(s.c_str(), s.size()));
    return s;
}

// Selects `select` when it exists; otherwise selects nothing and shows `select` as the new name
// (or, after a delete, selects the entry at fallbackIndex).
static void FillLayouts(HWND dlg, const std::wstring& select, int fallbackIndex) {
    HWND cb = GetDlgItem(dlg, IDC_LAYOUTS);
    SendMessageW(cb, CB_RESETCONTENT, 0, 0);
    for (const auto& n : ListLayouts()) SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(n.c_str()));
    int count = static_cast<int>(SendMessageW(cb, CB_GETCOUNT, 0, 0));
    LRESULT sel = select.empty() ? CB_ERR
                                 : SendMessageW(cb, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
                                                reinterpret_cast<LPARAM>(select.c_str()));
    if (sel == CB_ERR && select.empty() && count > 0 && fallbackIndex >= 0)
        sel = fallbackIndex < count ? fallbackIndex : count - 1;
    SendMessageW(cb, CB_SETCURSEL, static_cast<WPARAM>(sel), 0);
    if (sel == CB_ERR) SetWindowTextW(cb, select.c_str());
}

static void UpdateButtons(HWND dlg) {
    BOOL sel = SendDlgItemMessageW(dlg, IDC_LAYOUTS, CB_GETCURSEL, 0, 0) != CB_ERR;
    EnableWindow(GetDlgItem(dlg, IDC_RESTORE), sel);
    EnableWindow(GetDlgItem(dlg, IDC_DELETE), sel);
}

static std::wstring LayoutFolder(bool create) {
    PWSTR docs = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)) && docs) {
        dir = std::wstring(docs) + L"\\DesktopRestore";
        if (create) CreateDirectoryW(dir.c_str(), nullptr);
    }
    CoTaskMemFree(docs);
    return dir;
}

static bool PickFile(HWND owner, bool save, const std::wstring& defName, std::wstring& path) {
    std::vector<wchar_t> buf(32768, L'\0');
    if (save) {
        std::wstring n = defName;  // layout names may hold characters not allowed in file names
        for (auto& c : n)
            if (wcschr(L"\\/:*?\"<>|", c)) c = L'~';
        wcsncpy(buf.data(), n.c_str(), buf.size() - 1);
    }
    std::wstring dir = LayoutFolder(save);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Desktop Restore (*.dtr)\0*.dtr\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = static_cast<DWORD>(buf.size());
    ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    ofn.lpstrDefExt = L"dtr";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (!(save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn))) return false;
    path = buf.data();
    return true;
}

static INT_PTR CALLBACK CustomProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = reinterpret_cast<CustomData*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        d = reinterpret_cast<CustomData*>(lp);
        SetAppIcon(dlg);
        SetDlgItemTextW(dlg, IDC_VERSION, Format(LoadStr(IDS_VERSION_LINE).c_str(), kVersion).c_str());
        SetDlgItemTextW(dlg, IDC_CURRENT, d->current.c_str());
        FillLayouts(dlg, d->current, -1);
        UpdateButtons(dlg);
        SetForegroundWindow(dlg);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        case IDC_RESTORE: {
            std::wstring n = SelectedLayout(dlg);
            if (n.empty()) return TRUE;
            d->action = IDC_RESTORE;
            d->value = n;
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        case IDC_SAVE: {
            std::wstring n = Trim(WindowText(GetDlgItem(dlg, IDC_LAYOUTS)));
            if (n.empty() || n.size() > 255 || n.find(L'\\') != std::wstring::npos || IsReservedLayoutName(n)) {
                MessageBeep(MB_ICONWARNING);
                return TRUE;
            }
            // Overwriting a layout other than the current resolution's deserves a question.
            if (!SameText(n, d->current) && LayoutExists(n) &&
                MessageBoxW(dlg, Format(LoadStr(IDS_CONFIRM_SAVE).c_str(), n.c_str()).c_str(),
                            LoadStr(IDS_CAPTION).c_str(), MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
                return TRUE;
            d->action = IDC_SAVE;
            d->value = n;
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        case IDC_SAVEFILE:
        case IDC_RESTOREFILE: {
            bool save = LOWORD(wp) == IDC_SAVEFILE;
            std::wstring p;
            if (!PickFile(dlg, save, d->current, p)) return TRUE;
            d->action = LOWORD(wp);
            d->value = p;
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        case IDC_DELETE: {
            std::wstring n = SelectedLayout(dlg);
            if (n.empty()) return TRUE;
            int sel = static_cast<int>(SendDlgItemMessageW(dlg, IDC_LAYOUTS, CB_GETCURSEL, 0, 0));
            if (MessageBoxW(dlg, Format(LoadStr(IDS_CONFIRM_DELETE).c_str(), n.c_str()).c_str(),
                            LoadStr(IDS_CAPTION).c_str(), MB_OKCANCEL | MB_ICONQUESTION) == IDOK) {
                DeleteLayoutReg(n);
                FillLayouts(dlg, std::wstring(), sel);
                if (SendDlgItemMessageW(dlg, IDC_LAYOUTS, CB_GETCURSEL, 0, 0) == CB_ERR)
                    SetDlgItemTextW(dlg, IDC_LAYOUTS, d->current.c_str());
                UpdateButtons(dlg);
            }
            return TRUE;
        }
        case IDC_LAYOUTS:
            if (HIWORD(wp) == CBN_SELCHANGE || HIWORD(wp) == CBN_EDITCHANGE || HIWORD(wp) == CBN_KILLFOCUS)
                UpdateButtons(dlg);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

int CmdCustom() {
    CustomData data;
    {
        Session s;  // only needed for the current layout name
        if (!s.Open(false)) return 1;
        data.current = s.CurrentName();
    }
    if (DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(IDD_CUSTOM), nullptr, CustomProc,
                        reinterpret_cast<LPARAM>(&data)) != IDOK)
        return 1;
    switch (data.action) {
    case IDC_SAVE: {
        Session s;
        return s.Open(false) ? DoSave(s, data.value, false) : 1;
    }
    case IDC_RESTORE:
        return CmdRestore(false, data.value);
    case IDC_SAVEFILE:
        return CmdSaveFile(false, data.value);
    case IDC_RESTOREFILE:
        return CmdRestoreFile(false, data.value);
    }
    return 1;
}

// ---------------------------------------------------------------- settings

static INT_PTR CALLBACK SettingsProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* s = reinterpret_cast<Settings*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        s = reinterpret_cast<Settings*>(lp);
        SetAppIcon(dlg);
        CheckRadioButton(dlg, IDC_SAVE_CASCADE, IDC_SAVE_MAIN, s->cascadeSave ? IDC_SAVE_CASCADE : IDC_SAVE_MAIN);
        CheckRadioButton(dlg, IDC_REST_CASCADE, IDC_REST_MAIN, s->cascadeRestore ? IDC_REST_CASCADE : IDC_REST_MAIN);
        CheckRadioButton(dlg, IDC_CUST_CASCADE, IDC_CUST_MAIN, s->cascadeCustom ? IDC_CUST_CASCADE : IDC_CUST_MAIN);
        CheckRadioButton(dlg, IDC_HILI_CASCADE, IDC_HILI_MAIN, s->cascadeHilite ? IDC_HILI_CASCADE : IDC_HILI_MAIN);
        CheckDlgButton(dlg, IDC_CONFIRM_SAVE, s->confirmSave ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_CONFIRM_REST, s->confirmRestore ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_FORCE_VISIBLE, s->forceVisible ? BST_CHECKED : BST_UNCHECKED);
        SetForegroundWindow(dlg);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            s->cascadeSave = IsDlgButtonChecked(dlg, IDC_SAVE_CASCADE) == BST_CHECKED;
            s->cascadeRestore = IsDlgButtonChecked(dlg, IDC_REST_CASCADE) == BST_CHECKED;
            s->cascadeCustom = IsDlgButtonChecked(dlg, IDC_CUST_CASCADE) == BST_CHECKED;
            s->cascadeHilite = IsDlgButtonChecked(dlg, IDC_HILI_CASCADE) == BST_CHECKED;
            s->confirmSave = IsDlgButtonChecked(dlg, IDC_CONFIRM_SAVE) == BST_CHECKED;
            s->confirmRestore = IsDlgButtonChecked(dlg, IDC_CONFIRM_REST) == BST_CHECKED;
            s->forceVisible = IsDlgButtonChecked(dlg, IDC_FORCE_VISIBLE) == BST_CHECKED;
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

int CmdSettings() {
    Settings st;
    st.Load();
    if (DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(IDD_SETTINGS), nullptr, SettingsProc,
                        reinterpret_cast<LPARAM>(&st)) != IDOK)
        return 1;
    if (!st.Save()) return 1;
    std::wstring exe;
    if (MenuInstalled(&exe)) InstallMenu(st, exe);  // menu text and placement follow the settings
    return 0;
}

}  // namespace dr
