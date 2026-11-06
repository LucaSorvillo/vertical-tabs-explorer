#include "Settings.h"

#include "Common.h"

namespace xpx
{
    Settings Settings::Load()
    {
        const std::wstring file = SettingsPath();
        Settings s;
        s.restoreOnOpen = GetPrivateProfileIntW(L"Session", L"RestoreOnOpen", s.restoreOnOpen ? 1 : 0, file.c_str()) != 0;
        s.backToTop = GetPrivateProfileIntW(L"BackToTop", L"Enabled", s.backToTop ? 1 : 0, file.c_str()) != 0;
        s.reopenTab = GetPrivateProfileIntW(L"ReopenTab", L"Enabled", s.reopenTab ? 1 : 0, file.c_str()) != 0;
        s.openInTabs = GetPrivateProfileIntW(L"Tabs", L"OpenInTabs", s.openInTabs ? 1 : 0, file.c_str()) != 0;
        s.verticalTabs = GetPrivateProfileIntW(L"VerticalTabs", L"Enabled", s.verticalTabs ? 1 : 0, file.c_str()) != 0;
        s.verticalTabsSuspended = GetPrivateProfileIntW(L"VerticalTabs", L"Suspended", s.verticalTabsSuspended ? 1 : 0, file.c_str()) != 0;
        s.skipShellProcess = GetPrivateProfileIntW(L"VerticalTabs", L"SkipShellProcess", s.skipShellProcess ? 1 : 0, file.c_str()) != 0;
        s.navBar = GetPrivateProfileIntW(L"NavBar", L"Enabled", s.navBar ? 1 : 0, file.c_str()) != 0;
        s.verboseLog = GetPrivateProfileIntW(L"Debug", L"VerboseLog", s.verboseLog ? 1 : 0, file.c_str()) != 0;
        return s;
    }

    bool Settings::Save() const
    {
        const std::wstring file = SettingsPath();
        bool ok = WritePrivateProfileStringW(L"Session", L"RestoreOnOpen", restoreOnOpen ? L"1" : L"0", file.c_str()) != FALSE;
        ok &= WritePrivateProfileStringW(L"BackToTop", L"Enabled", backToTop ? L"1" : L"0", file.c_str()) != FALSE;
        ok &= WritePrivateProfileStringW(L"ReopenTab", L"Enabled", reopenTab ? L"1" : L"0", file.c_str()) != FALSE;
        ok &= WritePrivateProfileStringW(L"Tabs", L"OpenInTabs", openInTabs ? L"1" : L"0", file.c_str()) != FALSE;
        ok &= WritePrivateProfileStringW(L"VerticalTabs", L"Enabled", verticalTabs ? L"1" : L"0", file.c_str()) != FALSE;
        ok &= WritePrivateProfileStringW(L"VerticalTabs", L"Suspended", verticalTabsSuspended ? L"1" : L"0", file.c_str()) != FALSE;
        ok &= WritePrivateProfileStringW(L"NavBar", L"Enabled", navBar ? L"1" : L"0", file.c_str()) != FALSE;
        ok &= WritePrivateProfileStringW(L"Debug", L"VerboseLog", verboseLog ? L"1" : L"0", file.c_str()) != FALSE;
        return ok;
    }
}
