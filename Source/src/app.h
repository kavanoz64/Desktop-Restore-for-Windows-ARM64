// Command layer shared by main.cpp and app.cpp.
#pragma once
#include "dr.h"

namespace dr {

// One operation against the live desktop.
struct Session {
    ShellDesktopPtr desk;
    DisplayConfig dc;
    Settings settings;
    int iconSize = 0;

    bool Open(bool quiet);  // shows the original's "Couldn't find desktop ListView" on failure
    std::wstring CurrentName() const;
    std::wstring FindCurrentLayout() const;
};

int Msg(HWND owner, const std::wstring& text, UINT flags);

// Parsed command line. verb is one of: "" (none), help, save, restore, savefile, restorefile,
// custom, highlight, settings, diag, install, uninstall.
struct CommandLine {
    std::wstring verb, layout, file;
    bool quiet = false;  // /q, or DesktopCmd's /y
    bool bad = false;    // invalid: verb is set to help
};
CommandLine ParseCommandLine(const std::vector<std::wstring>& args);

// Return 0 on success, 1 on failure or cancel.
int CmdSave(bool quiet, const std::wstring& layout);     // empty layout = current resolution
int CmdRestore(bool quiet, const std::wstring& layout);  // empty layout = current resolution
int CmdSaveFile(bool quiet, const std::wstring& file);
int CmdRestoreFile(bool quiet, const std::wstring& file);
int CmdHighlight(bool quiet);
int CmdDiag(bool quiet, const std::wstring& file);  // read-only troubleshooting report
int CmdCustom();
int CmdSettings();

}  // namespace dr
