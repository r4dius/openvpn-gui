/*
 *  OpenVPN-GUI -- A Windows GUI for OpenVPN.
 *
 *  Copyright (C) 2009 Heiko Hund <heikoh@users.sf.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program (see the file COPYING included with this
 *  distribution); if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <prsht.h>
#include <tchar.h>
#include <stdio.h>
#include <stdarg.h>
#include <malloc.h>

#include "main.h"
#include "localization.h"
#include "openvpn-gui-res.h"
#include "options.h"
#include "registry.h"
#include "misc.h"

extern options_t o;

static const LANGID fallbackLangId = MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT);
static LANGID gui_language;

static HRSRC
FindResourceLang(PTSTR resType, PTSTR resId, LANGID langId)
{
    HRSRC res;

    /* try to find the resource in requested language */
    res = FindResourceEx(o.hInstance, resType, resId, langId);
    if (res)
    {
        return res;
    }

    /* try to find the resource in the default sublanguage */
    LANGID defLangId = MAKELANGID(PRIMARYLANGID(langId), SUBLANG_DEFAULT);
    res = FindResourceEx(o.hInstance, resType, resId, defLangId);
    if (res)
    {
        return res;
    }

    /* try to find the resource in the default language */
    res = FindResourceEx(o.hInstance, resType, resId, fallbackLangId);
    if (res)
    {
        return res;
    }

    /* try to find the resource in any language */
    return FindResource(o.hInstance, resId, resType);
}

/*
 * Return value: 0 for LTR, 1 for RTL, 2 or 3 for vertical
 */
int
LangFlowDirection(void)
{
    int res = 0; /* LTR by default */
    wchar_t lcname[LOCALE_NAME_MAX_LENGTH];
    wchar_t data[2];
    if (LCIDToLocaleName(MAKELCID(GetGUILanguage(), SORT_DEFAULT), lcname, _countof(lcname), 0) != 0
        && GetLocaleInfoEx(lcname, LOCALE_IREADINGLAYOUT, data, 2) != 0)
    {
        res = _wtoi(data);
    }
    return res;
}

LANGID
GetGUILanguage(void)
{
    if (gui_language != 0)
    {
        return gui_language;
    }

    HKEY regkey;
    DWORD value = 0;

    LONG status = RegOpenKeyEx(HKEY_CURRENT_USER, GUI_REGKEY_HKCU, 0, KEY_READ, &regkey);
    if (status == ERROR_SUCCESS)
    {
        GetRegistryValueNumeric(regkey, _T("ui_language"), &value);
        RegCloseKey(regkey);
    }

    gui_language = ( value != 0 ? value : GetUserDefaultUILanguage() );
    InitMUILanguage(gui_language);
    return gui_language;
}


static void
SetGUILanguage(LANGID langId)
{
    HKEY regkey;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, GUI_REGKEY_HKCU, 0, NULL, 0,
                       KEY_WRITE, NULL, &regkey, NULL) != ERROR_SUCCESS)
    {
        ShowLocalizedMsg(IDS_ERR_CREATE_REG_HKCU_KEY, GUI_REGKEY_HKCU);
    }

    SetRegistryValueNumeric(regkey, _T("ui_language"), langId);
    InitMUILanguage(langId);
    gui_language = langId;
}

static int
LocalizedSystemTime(const SYSTEMTIME *st, wchar_t *buf, size_t size)
{
    int date_size = 0, time_size = 0;
    LCID locale = MAKELCID(GetGUILanguage(), SORT_DEFAULT);

    if (size == 0 || buf == NULL)
    {
        date_size = GetDateFormat(locale, DATE_SHORTDATE, st, NULL, NULL, 0);
        time_size = GetTimeFormat(locale, TIME_NOSECONDS, st, NULL, NULL, 0);
        return date_size + time_size;
    }

    date_size = GetDateFormat(locale, DATE_SHORTDATE, st, NULL,
                              buf, size);
    if (size > (size_t) date_size)
    {
        time_size = GetTimeFormat(locale, TIME_NOSECONDS, st, NULL,
                                  buf + date_size, size - date_size);
    }
    if (date_size > 0 && time_size > 0)
    {
        buf[date_size - 1] = L' '; /* replaces the NUL char in the middle */
    }
    return date_size + time_size;
}

/*
 * Convert filetime to a wide character string -- caller must free the
 * result after use.
 */
wchar_t *
LocalizedFileTime(const FILETIME *ft)
{
    FILETIME lft;
    SYSTEMTIME st;
    FileTimeToLocalFileTime(ft, &lft);
    FileTimeToSystemTime(&lft, &st);
    wchar_t *buf = NULL;

    int size = LocalizedSystemTime(&st, NULL, 0);
    if (size > 0)
    {
        buf = calloc(1, size*sizeof(wchar_t));
        if (buf)
        {
            LocalizedSystemTime(&st, buf, size);
        }
    }
    return buf;
}

int
LocalizedTime(const time_t t, LPTSTR buf, size_t size)
{
    /* Convert Unix timestamp to Win32 SYSTEMTIME */
    FILETIME lft;
    SYSTEMTIME st;
    LONGLONG tmp = (t * 10000000LL) + 116444736000000000LL;
    FILETIME ft = { .dwLowDateTime = (DWORD) tmp, .dwHighDateTime = tmp >> 32};
    FileTimeToLocalFileTime(&ft, &lft);
    FileTimeToSystemTime(&lft, &st);

    return LocalizedSystemTime(&st, buf, size);
}

static int
LoadStringLang(UINT stringId, LANGID langId, PTSTR buffer, int bufferSize, va_list args)
{
    PWCH entry;
    PTSTR resBlockId = MAKEINTRESOURCE(stringId / 16 + 1);
    int resIndex = stringId & 15;

    /* find resource block for string */
    HRSRC res = FindResourceLang(RT_STRING, resBlockId, langId);
    if (res == NULL)
    {
        goto err;
    }

    /* get pointer to first entry in resource block */
    entry = (PWCH) LoadResource(o.hInstance, res);
    if (entry == NULL)
    {
        goto err;
    }

    /* search for string in block */
    for (int i = 0; i < 16; i++)
    {
        /* skip over this entry */
        if (i != resIndex)
        {
            entry += ((*entry) + 1);
            continue;
        }

        /* string does not exist */
        if (i == resIndex && *entry == 0)
        {
            break;
        }

        /* string found, copy it */
        PTSTR formatStr = (PTSTR) malloc((*entry + 1) * sizeof(TCHAR));
        if (formatStr == NULL)
        {
            break;
        }
        formatStr[*entry] = 0;

        wcsncpy(formatStr, entry + 1, *entry);
        _vsntprintf(buffer, bufferSize, formatStr, args);
        buffer[bufferSize - 1] = 0;
        free(formatStr);
        return _tcslen(buffer);
    }

err:
    /* not found, try again with the default language */
    if (langId != fallbackLangId)
    {
        return LoadStringLang(stringId, fallbackLangId, buffer, bufferSize, args);
    }

    return 0;
}


static PTSTR
__LoadLocalizedString(const UINT stringId, va_list args)
{
    static TCHAR msg[512];
    msg[0] = 0;
    LoadStringLang(stringId, GetGUILanguage(), msg, _countof(msg), args);
    return msg;
}


PTSTR
LoadLocalizedString(const UINT stringId, ...)
{
    va_list args;
    va_start(args, stringId);
    PTSTR str = __LoadLocalizedString(stringId, args);
    va_end(args);
    return str;
}


int
LoadLocalizedStringBuf(PTSTR buffer, int bufferSize, const UINT stringId, ...)
{
    va_list args;
    va_start(args, stringId);
    int len = LoadStringLang(stringId, GetGUILanguage(), buffer, bufferSize, args);
    va_end(args);
    return len;
}


static int
__ShowLocalizedMsgEx(const UINT type, HANDLE parent, LPCTSTR caption, const UINT stringId, va_list args)
{
    return MessageBoxEx(parent, __LoadLocalizedString(stringId, args), caption,
                        type | MB_SETFOREGROUND | MBOX_RTL_FLAGS, GetGUILanguage());
}

int
ShowLocalizedMsgEx(const UINT type, HANDLE parent, LPCTSTR caption, const UINT stringId, ...)
{
    va_list args;
    va_start(args, stringId);
    int result = __ShowLocalizedMsgEx(type, parent, caption, stringId, args);
    va_end(args);
    return result;
}


void
ShowLocalizedMsg(const UINT stringId, ...)
{
    va_list args;
    va_start(args, stringId);
    __ShowLocalizedMsgEx(MB_OK, NULL, _T(PACKAGE_NAME), stringId, args);
    va_end(args);
}

HICON
LoadLocalizedIconEx(const UINT iconId, int cxDesired, int cyDesired)
{
    LANGID langId = GetGUILanguage();

    HICON hIcon =
        (HICON) LoadImage(o.hInstance, MAKEINTRESOURCE(iconId),
                          IMAGE_ICON, cxDesired, cyDesired, LR_DEFAULTSIZE|LR_SHARED);
    if (hIcon)
    {
        return hIcon;
    }
    else
    {
        PrintDebug(L"Loading icon using LoadImage failed.");
    }

    /* Fallback to CreateIconFromResource which always scales
     * from the first image in the resource
     */
    /* find group icon resource */
    HRSRC res = FindResourceLang(RT_GROUP_ICON, MAKEINTRESOURCE(iconId), langId);
    if (res == NULL)
    {
        return NULL;
    }

    HGLOBAL resInfo = LoadResource(o.hInstance, res);
    if (resInfo == NULL)
    {
        return NULL;
    }

    int id = LookupIconIdFromDirectory(resInfo, TRUE);
    if (id == 0)
    {
        return NULL;
    }

    /* find the actual icon */
    res = FindResourceLang(RT_ICON, MAKEINTRESOURCE(id), langId);
    if (res == NULL)
    {
        return NULL;
    }

    resInfo = LoadResource(o.hInstance, res);
    if (resInfo == NULL)
    {
        return NULL;
    }

    DWORD resSize = SizeofResource(o.hInstance, res);
    if (resSize == 0)
    {
        return NULL;
    }

    /* Note: this uses the first icon in the resource and scales it */
    hIcon = CreateIconFromResourceEx(resInfo, resSize, TRUE, 0x30000,
                                     cxDesired, cyDesired, LR_DEFAULTSIZE|LR_SHARED);
    return hIcon;
}

HICON
LoadLocalizedIcon(const UINT iconId)
{
    /* get the required normal icon size (e.g., taskbar icon) */
    int cx = GetSystemMetrics(SM_CXICON);
    int cy = GetSystemMetrics(SM_CYICON);
    return LoadLocalizedIconEx(iconId, cx, cy);
}

HICON
LoadLocalizedSmallIcon(const UINT iconId)
{
    /* get the required small icon size (e.g., tray icon) */
    int cx = GetSystemMetrics(SM_CXSMICON);
    int cy = GetSystemMetrics(SM_CYSMICON);
    return LoadLocalizedIconEx(iconId, cx, cy);
}

LPCDLGTEMPLATE
LocalizedDialogResource(const UINT dialogId)
{
    /* find dialog resource */
    HRSRC res = FindResourceLang(RT_DIALOG, MAKEINTRESOURCE(dialogId), GetGUILanguage());
    if (res == NULL)
    {
        return NULL;
    }

    return LoadResource(o.hInstance, res);
}


INT_PTR
LocalizedDialogBoxParam(const UINT dialogId, DLGPROC dialogFunc, const LPARAM param)
{
    return LocalizedDialogBoxParamEx(dialogId, o.hWnd, dialogFunc, param);
}

INT_PTR
LocalizedDialogBoxParamEx(const UINT dialogId, HWND owner, DLGPROC dialogFunc, const LPARAM param)
{
    LPCDLGTEMPLATE resInfo = LocalizedDialogResource(dialogId);
    if (resInfo == NULL)
    {
        return -1;
    }

    return DialogBoxIndirectParam(o.hInstance, resInfo, owner, dialogFunc, param);
}

HWND
CreateLocalizedDialogParam(const UINT dialogId, DLGPROC dialogFunc, const LPARAM param)
{
    /* find dialog resource */
    HRSRC res = FindResourceLang(RT_DIALOG, MAKEINTRESOURCE(dialogId), GetGUILanguage());
    if (res == NULL)
    {
        return NULL;
    }

    HGLOBAL resInfo = LoadResource(o.hInstance, res);
    if (resInfo == NULL)
    {
        return NULL;
    }

    return CreateDialogIndirectParam(o.hInstance, resInfo, o.hWnd, dialogFunc, param);
}


HWND
CreateLocalizedDialog(const UINT dialogId, DLGPROC dialogFunc)
{
    return CreateLocalizedDialogParam(dialogId, dialogFunc, 0);
}


static PTSTR
LangListEntry(const UINT stringId, const LANGID langId, ...)
{
    static TCHAR str[128];
    va_list args;

    va_start(args, langId);
    LoadStringLang(stringId, langId, str, _countof(str), args);
    va_end(args);
    return str;
}


typedef struct {
    HWND languages;
    LANGID language;
} langProcData;


static BOOL
FillLangListProc(UNUSED HANDLE module, UNUSED PTSTR type, UNUSED PTSTR stringId, WORD langId, LONG_PTR lParam)
{
    langProcData *data = (langProcData *) lParam;

    int index = ComboBox_AddString(data->languages, LangListEntry(IDS_LANGUAGE_NAME, langId));
    ComboBox_SetItemData(data->languages, index, langId);

    /* Select this item if it is the currently displayed language */
    if (langId == data->language
        ||  (PRIMARYLANGID(langId) == PRIMARYLANGID(data->language)
             && ComboBox_GetCurSel(data->languages) == CB_ERR) )
    {
        ComboBox_SetCurSel(data->languages, index);
    }

    return TRUE;
}

static BOOL
GetLaunchOnStartup()
{

    WCHAR regPath[MAX_PATH], exePath[MAX_PATH];
    BOOL result = FALSE;
    HKEY regkey;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &regkey) == ERROR_SUCCESS)
    {

        if (GetRegistryValue(regkey, L"OpenVPN-GUI", regPath, MAX_PATH)
            && GetModuleFileNameW(NULL, exePath, MAX_PATH))
        {
            if (_wcsicmp(regPath, exePath) == 0)
            {
                result = TRUE;
            }
        }

        RegCloseKey(regkey);

    }

    return result;

}

static void
SetLaunchOnStartup(BOOL value)
{

    WCHAR exePath[MAX_PATH];
    HKEY regkey;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &regkey) == ERROR_SUCCESS)
    {

        if (value)
        {
            if (GetModuleFileNameW(NULL, exePath, MAX_PATH))
            {
                SetRegistryValue(regkey, L"OpenVPN-GUI", exePath);
            }
        }
        else
        {
            RegDeleteValue(regkey, L"OpenVPN-GUI");
        }

        RegCloseKey(regkey);

    }

}

INT_PTR CALLBACK
GeneralSettingsDlgProc(HWND hwndDlg, UINT msg, UNUSED WPARAM wParam, LPARAM lParam)
{
    LPPSHNOTIFY psn;
    langProcData langData = {
        .languages = GetDlgItem(hwndDlg, ID_CMB_LANGUAGE),
        .language = GetGUILanguage()
    };

    switch (msg)
    {

        case WM_INITDIALOG:
            RECT rect;
            GetWindowRect(hwndDlg, &rect);
            /* Loosing time moving stuff */
            MoveWindow(GetDlgItem(hwndDlg, ID_GROUPBOX1), DPI_SCALE(7), DPI_SCALE(3), DPI_SCALE(362), DPI_SCALE(50), TRUE);
            MoveWindow(GetDlgItem(hwndDlg, ID_TXT_LANGUAGE), DPI_SCALE(23), DPI_SCALE(24), DPI_SCALE(60), DPI_SCALE(15), TRUE);
            MoveWindow(GetDlgItem(hwndDlg, ID_CMB_LANGUAGE), DPI_SCALE(88), DPI_SCALE(20), DPI_SCALE(270), DPI_SCALE(50), TRUE);
            MoveWindow(GetDlgItem(hwndDlg, ID_GROUPBOX2), DPI_SCALE(7), DPI_SCALE(56), DPI_SCALE(362), DPI_SCALE(50), TRUE);
            MoveWindow(GetDlgItem(hwndDlg, ID_CHK_STARTUP), DPI_SCALE(23), DPI_SCALE(76), DPI_SCALE(330), DPI_SCALE(15), TRUE);
            MoveWindow(GetDlgItem(hwndDlg, ID_GROUPBOX3), DPI_SCALE(7), DPI_SCALE(109), DPI_SCALE(362), DPI_SCALE(325), TRUE);
            MoveWindow(GetDlgItem(hwndDlg, ID_CHK_LOG_APPEND), DPI_SCALE(23), DPI_SCALE(129), DPI_SCALE(330), DPI_SCALE(15), TRUE);
            MoveWindow(GetDlgItem(hwndDlg, ID_CHK_SHOW_SCRIPT_WIN), DPI_SCALE(23), DPI_SCALE(154), DPI_SCALE(330), DPI_SCALE(15), TRUE);
            MoveWindow(GetDlgItem(hwndDlg, ID_CHK_SILENT), DPI_SCALE(23), DPI_SCALE(179), DPI_SCALE(330), DPI_SCALE(15), TRUE);
            MoveWindow(GetDlgItem(hwndDlg, ID_CHK_ALWAYS_USE_ISERVICE), DPI_SCALE(23), DPI_SCALE(204), DPI_SCALE(330), DPI_SCALE(15), TRUE);
            MoveWindow(GetDlgItem(hwndDlg, ID_TXT_BALLOON), DPI_SCALE(23), DPI_SCALE(230), DPI_SCALE(330), DPI_SCALE(15), TRUE);
            MoveWindow(GetDlgItem(hwndDlg, ID_RB_BALLOON1), DPI_SCALE(36), DPI_SCALE(254), DPI_SCALE(100), DPI_SCALE(15), TRUE);
            MoveWindow(GetDlgItem(hwndDlg, ID_RB_BALLOON2), DPI_SCALE(146), DPI_SCALE(254), DPI_SCALE(100), DPI_SCALE(15), TRUE);
            MoveWindow(GetDlgItem(hwndDlg, ID_RB_BALLOON0), DPI_SCALE(256), DPI_SCALE(254), DPI_SCALE(100), DPI_SCALE(15), TRUE);
            MoveWindow(GetDlgItem(hwndDlg, ID_TXT_PERSISTENT), DPI_SCALE(23), DPI_SCALE(280), DPI_SCALE(330), DPI_SCALE(15), TRUE);
            MoveWindow(GetDlgItem(hwndDlg, ID_RB_BALLOON3), DPI_SCALE(36), DPI_SCALE(304), DPI_SCALE(100), DPI_SCALE(15), TRUE);
            MoveWindow(GetDlgItem(hwndDlg, ID_RB_BALLOON4), DPI_SCALE(146), DPI_SCALE(304), DPI_SCALE(100), DPI_SCALE(15), TRUE);
            MoveWindow(GetDlgItem(hwndDlg, ID_RB_BALLOON5), DPI_SCALE(256), DPI_SCALE(304), DPI_SCALE(100), DPI_SCALE(15), TRUE);
            MoveWindow(GetDlgItem(hwndDlg, ID_CHK_PLAP_REG), DPI_SCALE(23), DPI_SCALE(329), DPI_SCALE(330), DPI_SCALE(15), TRUE);
            MoveWindow(GetDlgItem(hwndDlg, ID_CHK_AUTO_RESTART), DPI_SCALE(23), DPI_SCALE(354), DPI_SCALE(330), DPI_SCALE(15), TRUE);
            MoveWindow(GetDlgItem(hwndDlg, ID_TXT_CONCAT_OTP), DPI_SCALE(23), DPI_SCALE(380), DPI_SCALE(330), DPI_SCALE(15), TRUE);
            MoveWindow(GetDlgItem(hwndDlg, ID_RB_APPEND_OTP), DPI_SCALE(36), DPI_SCALE(404), DPI_SCALE(100), DPI_SCALE(15), TRUE);
            MoveWindow(GetDlgItem(hwndDlg, ID_RB_PREPEND_OTP), DPI_SCALE(146), DPI_SCALE(404), DPI_SCALE(100), DPI_SCALE(15), TRUE);
            MoveWindow(GetDlgItem(hwndDlg, ID_RB_DISABLE_OTP), DPI_SCALE(256), DPI_SCALE(404), DPI_SCALE(100), DPI_SCALE(15), TRUE);

            /* Populate UI language selection combo box */
            EnumResourceLanguages( NULL, RT_STRING, MAKEINTRESOURCE(IDS_LANGUAGE_NAME / 16 + 1),
                                   (ENUMRESLANGPROC) FillLangListProc, (LONG_PTR) &langData );

            /* If none of the available languages matched, select the fallback */
            if (ComboBox_GetCurSel(langData.languages) == CB_ERR)
            {
                ComboBox_SelectString(langData.languages, -1,
                                      LangListEntry(IDS_LANGUAGE_NAME, fallbackLangId));
            }

            /* Clear language id data for the selected item */
            ComboBox_SetItemData(langData.languages, ComboBox_GetCurSel(langData.languages), 0);

            if (GetLaunchOnStartup())
            {
                Button_SetCheck(GetDlgItem(hwndDlg, ID_CHK_STARTUP), BST_CHECKED);
            }

            if (o.log_append)
            {
                Button_SetCheck(GetDlgItem(hwndDlg, ID_CHK_LOG_APPEND), BST_CHECKED);
            }
            if (o.silent_connection)
            {
                Button_SetCheck(GetDlgItem(hwndDlg, ID_CHK_SILENT), BST_CHECKED);
            }
            if (o.iservice_admin)
            {
                Button_SetCheck(GetDlgItem(hwndDlg, ID_CHK_ALWAYS_USE_ISERVICE), BST_CHECKED);
            }
            if (o.show_balloon == 0)
            {
                CheckRadioButton(hwndDlg, ID_RB_BALLOON0, ID_RB_BALLOON2, ID_RB_BALLOON0);
            }
            else if (o.show_balloon == 1)
            {
                CheckRadioButton(hwndDlg, ID_RB_BALLOON0, ID_RB_BALLOON2, ID_RB_BALLOON1);
            }
            else if (o.show_balloon == 2)
            {
                CheckRadioButton(hwndDlg, ID_RB_BALLOON0, ID_RB_BALLOON2, ID_RB_BALLOON2);
            }
            if (o.show_script_window)
            {
                Button_SetCheck(GetDlgItem(hwndDlg, ID_CHK_SHOW_SCRIPT_WIN), BST_CHECKED);
            }
            if (o.enable_persistent == 0) /* Never */
            {
                CheckRadioButton(hwndDlg, ID_RB_BALLOON3, ID_RB_BALLOON5, ID_RB_BALLOON5);
            }
            else if (o.enable_persistent == 1) /* Enabled, but no auto-attach */
            {
                CheckRadioButton(hwndDlg, ID_RB_BALLOON3, ID_RB_BALLOON5, ID_RB_BALLOON4);
            }
            else if (o.enable_persistent == 2) /* Enabled and auto-attach */
            {
                CheckRadioButton(hwndDlg, ID_RB_BALLOON3, ID_RB_BALLOON5, ID_RB_BALLOON3);
            }
            if (o.show_balloon == 0)
            {
                CheckRadioButton(hwndDlg, ID_RB_BALLOON0, ID_RB_BALLOON2, ID_RB_BALLOON0);
            }
            else if (o.show_balloon == 1)
            {
                CheckRadioButton(hwndDlg, ID_RB_BALLOON0, ID_RB_BALLOON2, ID_RB_BALLOON1);
            }
            else if (o.show_balloon == 2)
            {
                CheckRadioButton(hwndDlg, ID_RB_BALLOON0, ID_RB_BALLOON2, ID_RB_BALLOON2);
            }

            int plap_status = GetPLAPRegistrationStatus();
            if (plap_status == -1) /* PLAP not supported in this version */
            {
                ShowWindow(GetDlgItem(hwndDlg, ID_CHK_PLAP_REG), SW_HIDE);
            }
            else if (plap_status != 0)
            {
                Button_SetCheck(GetDlgItem(hwndDlg, ID_CHK_PLAP_REG), BST_CHECKED);
            }
            if (o.enable_auto_restart)
            {
                Button_SetCheck(GetDlgItem(hwndDlg, ID_CHK_AUTO_RESTART), BST_CHECKED);
            }
            if (o.auth_pass_concat_otp == 0)
            {
                CheckRadioButton(hwndDlg, ID_RB_APPEND_OTP, ID_RB_DISABLE_OTP, ID_RB_DISABLE_OTP);
            }
            else if (o.auth_pass_concat_otp == 1)
            {
                CheckRadioButton(hwndDlg, ID_RB_APPEND_OTP, ID_RB_DISABLE_OTP, ID_RB_APPEND_OTP);
            }
            else if (o.auth_pass_concat_otp == 2)
            {
                CheckRadioButton(hwndDlg, ID_RB_APPEND_OTP, ID_RB_DISABLE_OTP, ID_RB_PREPEND_OTP);
            }

            break;

        case WM_COMMAND:
            if (LOWORD(wParam) == ID_CHK_PLAP_REG && HIWORD(wParam) == BN_CLICKED)
            {
                /* change PLAPRegistration state */
                HWND h = GetDlgItem(hwndDlg, ID_CHK_PLAP_REG);
                BOOL newstate = Button_GetCheck(h) == BST_CHECKED ?  TRUE : FALSE;
                if (SetPLAPRegistration(newstate) != 0) /* failed or user cancelled -- reset checkmark */
                {
                    Button_SetCheck(h, newstate  ? BST_UNCHECKED : BST_CHECKED);
                }
            }
            break;

        case WM_NOTIFY:
            psn = (LPPSHNOTIFY) lParam;
            if (psn->hdr.code == (UINT) PSN_APPLY)
            {
                LANGID langId = (LANGID) ComboBox_GetItemData(langData.languages,
                                                              ComboBox_GetCurSel(langData.languages));

                if (langId != 0)
                {
                    SetGUILanguage(langId);
                }

                SetLaunchOnStartup(Button_GetCheck(GetDlgItem(hwndDlg, ID_CHK_STARTUP)) == BST_CHECKED);

                o.log_append =
                    (Button_GetCheck(GetDlgItem(hwndDlg, ID_CHK_LOG_APPEND)) == BST_CHECKED);
                o.silent_connection =
                    (Button_GetCheck(GetDlgItem(hwndDlg, ID_CHK_SILENT)) == BST_CHECKED);
                o.iservice_admin =
                    (Button_GetCheck(GetDlgItem(hwndDlg, ID_CHK_ALWAYS_USE_ISERVICE)) == BST_CHECKED);
                if (IsDlgButtonChecked(hwndDlg, ID_RB_BALLOON0))
                {
                    o.show_balloon = 0;
                }
                else if (IsDlgButtonChecked(hwndDlg, ID_RB_BALLOON2))
                {
                    o.show_balloon = 2;
                }
                else
                {
                    o.show_balloon = 1;
                }
                if (IsDlgButtonChecked(hwndDlg, ID_RB_BALLOON3))
                {
                    o.enable_persistent = 2;
                }
                else if (IsDlgButtonChecked(hwndDlg, ID_RB_BALLOON4))
                {
                    o.enable_persistent = 1;
                }
                else
                {
                    o.enable_persistent = 0;
                }
                o.show_script_window =
                    (Button_GetCheck(GetDlgItem(hwndDlg, ID_CHK_SHOW_SCRIPT_WIN)) == BST_CHECKED);
                o.enable_auto_restart =
                    (Button_GetCheck(GetDlgItem(hwndDlg, ID_CHK_AUTO_RESTART)) == BST_CHECKED);
                if (IsDlgButtonChecked(hwndDlg, ID_RB_APPEND_OTP))
                {
                    o.auth_pass_concat_otp = 1;
                }
                else if (IsDlgButtonChecked(hwndDlg, ID_RB_PREPEND_OTP))
                {
                    o.auth_pass_concat_otp = 2;
                }
                else
                {
                    o.auth_pass_concat_otp = 0;
                }

                SaveRegistryKeys();

                SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, PSNRET_NOERROR);
                return TRUE;
            }
            break;
    }

    return FALSE;
}
