// Entry point and command line.
#include "app.h"

#include <commctrl.h>
#include <shellapi.h>

#include "resource.h"

using namespace dr;

static std::wstring FullPath(const std::wstring& p) {
    DWORD n = GetFullPathNameW(p.c_str(), 0, nullptr, nullptr);
    if (!n) return p;
    std::wstring s(n, L'\0');
    n = GetFullPathNameW(p.c_str(), n, &s[0], nullptr);
    s.resize(n);
    return s;
}

static int Usage(bool quiet) {
    if (!quiet) Msg(nullptr, LoadStr(IDS_USAGE), MB_OK | MB_ICONINFORMATION);
    return 2;
}

// Another copy is busy (for example a dialog is open): bring its window forward instead.
static void ActivateOtherInstance() {
    const wchar_t* titles[] = {L"Custom Layout Save/Restore", L"Desktop Restore Settings", L"Desktop Save and Restore"};
    for (const wchar_t* t : titles) {
        HWND h = FindWindowW(L"#32770", t);
        if (h) {
            SetForegroundWindow(h);
            return;
        }
    }
}

static int CmdInstall(bool quiet) {
    Settings st;
    st.Load();
    if (!InstallMenu(st, ExePath())) {
        if (!quiet) Msg(nullptr, LoadStr(IDS_MENU_ERROR), MB_OK | MB_ICONWARNING);
        return 1;
    }
    if (!quiet) Msg(nullptr, LoadStr(IDS_INSTALLED), MB_OK | MB_ICONINFORMATION);
    return 0;
}

static int CmdUninstall(bool quiet) {
    bool ok = UninstallMenu();
    if (!quiet)
        Msg(nullptr, LoadStr(ok ? IDS_UNINSTALLED : IDS_UNINSTALL_ERROR), MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
    return ok ? 0 : 1;
}

// Double-click: offer to add the menu for this copy, otherwise open Custom Save/Restore.
static int RunDefault() {
    std::wstring exe = ExePath(), inMenu;
    if ((!MenuInstalled(&inMenu) || !SameText(inMenu, exe)) &&
        Msg(nullptr, Format(LoadStr(IDS_ASK_INSTALL).c_str(), exe.c_str()), MB_YESNO | MB_ICONQUESTION) == IDYES)
        return CmdInstall(false);
    return CmdCustom();
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    g_hInst = hInst;
    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    int argc = 0;
    PWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    for (int i = 1; argv && i < argc; i++) args.emplace_back(argv[i]);
    if (argv) LocalFree(argv);

    const CommandLine cl = ParseCommandLine(args);
    const std::wstring& verb = cl.verb;
    const bool quiet = cl.quiet;

    // One desktop operation at a time, so two menu clicks cannot move icons concurrently.
    const bool touchesDesktop = verb.empty() || verb == L"save" || verb == L"restore" || verb == L"custom" ||
                                verb == L"highlight" || verb == L"savefile" || verb == L"restorefile";
    HANDLE mutex = nullptr;
    DWORD wait = WAIT_FAILED;
    if (touchesDesktop) {
        mutex = CreateMutexW(nullptr, FALSE, L"Local\\DesktopRestorePort");
        wait = mutex ? WaitForSingleObject(mutex, 0) : WAIT_FAILED;
        if (wait == WAIT_TIMEOUT) {
            if (!quiet) ActivateOtherInstance();
            CloseHandle(mutex);
            if (SUCCEEDED(hrInit)) CoUninitialize();
            return 3;
        }
    }

    int rc = 2;
    if (verb == L"help") {
        Usage(quiet);
        rc = cl.bad ? 2 : 0;
    } else if (verb.empty())
        rc = RunDefault();
    else if (verb == L"save")
        rc = CmdSave(quiet, cl.layout);
    else if (verb == L"restore")
        rc = CmdRestore(quiet, cl.layout);
    else if (verb == L"savefile")
        rc = CmdSaveFile(quiet, FullPath(cl.file));
    else if (verb == L"restorefile")
        rc = CmdRestoreFile(quiet, FullPath(cl.file));
    else if (verb == L"custom")
        rc = CmdCustom();
    else if (verb == L"highlight")
        rc = CmdHighlight(quiet);
    else if (verb == L"settings")
        rc = CmdSettings();
    else if (verb == L"diag")
        rc = CmdDiag(quiet, cl.file.empty() ? cl.file : FullPath(cl.file));
    else if (verb == L"install")
        rc = CmdInstall(quiet);
    else if (verb == L"uninstall")
        rc = CmdUninstall(quiet);

    if (mutex) {
        if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    if (SUCCEEDED(hrInit)) CoUninitialize();
    return rc;
}
