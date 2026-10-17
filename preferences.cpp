#include "preferences.h"
#include "cfg.h"
#include "resource.h"

#include <atlbase.h>
#include <atlapp.h>
#include <atlctrls.h>
#include <atltypes.h>

#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")

static INT_PTR CALLBACK PrefsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

// ============================================================
//  Тёмная тема
// ============================================================

static HBRUSH g_hbrDarkBackground = NULL;
static HBRUSH g_hbrDarkEdit = NULL;

static bool IsDarkMode() {
    return ui_config_manager::g_is_dark_mode();
}

static HBRUSH GetDarkBackgroundBrush() {
    if (!g_hbrDarkBackground) g_hbrDarkBackground = CreateSolidBrush(RGB(45, 45, 45));
    return g_hbrDarkBackground;
}

static HBRUSH GetDarkEditBrush() {
    if (!g_hbrDarkEdit) g_hbrDarkEdit = CreateSolidBrush(RGB(30, 30, 30));
    return g_hbrDarkEdit;
}

void cuepause_prefs_cleanup_dark_brushes() {
    if (g_hbrDarkBackground) { DeleteObject(g_hbrDarkBackground); g_hbrDarkBackground = NULL; }
    if (g_hbrDarkEdit) { DeleteObject(g_hbrDarkEdit);       g_hbrDarkEdit = NULL; }
}

static BOOL CALLBACK EnumChildApplyDarkTheme(HWND hChild, LPARAM lParam) {
    (void)lParam;
    wchar_t cls[64] = { 0 };
    GetClassNameW(hChild, cls, 64);

    if (_wcsicmp(cls, L"Button") == 0) {
        LONG style = GetWindowLong(hChild, GWL_STYLE);
        LONG type = style & 0x0F;

        if (type == BS_AUTOCHECKBOX || type == BS_CHECKBOX ||
            type == BS_AUTO3STATE || type == BS_3STATE ||
            type == BS_GROUPBOX) {
            SetWindowTheme(hChild, L"", L"");
        }
        else {
            SetWindowTheme(hChild, L"DarkMode_Explorer", NULL);
        }
    }
    else if (_wcsicmp(cls, L"Edit") == 0 ||
        _wcsicmp(cls, L"ComboBox") == 0) {
        SetWindowTheme(hChild, L"DarkMode_CFD", NULL);
    }
    else if (_wcsicmp(cls, L"SysListView32") == 0) {
        SetWindowTheme(hChild, L"DarkMode_Explorer", NULL);
    }
    else if (_wcsicmp(cls, L"msctls_updown32") == 0 ||
        _wcsicmp(cls, L"msctls_trackbar32") == 0) {
        SetWindowTheme(hChild, L"DarkMode_Explorer", NULL);
    }
    else if (_wcsicmp(cls, L"Static") == 0) {
        SetWindowTheme(hChild, L"", L"");
    }
    return TRUE;
}

static void ApplyDarkThemeToDialog(HWND hDlg) {
    if (!hDlg) return;
    SetWindowTheme(hDlg, L"DarkMode_Explorer", NULL);
    EnumChildWindows(hDlg, EnumChildApplyDarkTheme, 0);
    RedrawWindow(hDlg, NULL, NULL,
        RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

// ============================================================
//  Инстанс страницы
// ============================================================

class cuepause_prefs_instance : public preferences_page_instance {
public:
    cuepause_prefs_instance(HWND parent, preferences_page_callback::ptr callback)
        : m_callback(callback), m_hWnd(NULL)
    {
        m_hWnd = CreateDialogParam(
            core_api::get_my_instance(),
            MAKEINTRESOURCE(IDD_PREFS_DIALOG),
            parent,
            PrefsDlgProc,
            (LPARAM)this);
    }

    HWND get_wnd() override { return m_hWnd; }

    t_uint32 get_state() override {
        t_uint32 state = preferences_state::dark_mode_supported;
        state |= preferences_state::resettable;
        if (m_hWnd && has_changes()) {
            state |= preferences_state::changed;
        }
        return state;
    }

    void apply() override {
        if (!m_hWnd) return;

        cfg_digital_silence_only =
            (IsDlgButtonChecked(m_hWnd, IDC_CHK_DIGITAL_ONLY) == BST_CHECKED);

        cfg_gapless_mode =
            (IsDlgButtonChecked(m_hWnd, IDC_CHK_GAPLESS) == BST_CHECKED);

        cfg_silence_threshold_db =
            (int)GetDlgItemInt(m_hWnd, IDC_EDIT_THRESHOLD, nullptr, TRUE);

        cfg_min_pause_ms =
            (int)GetDlgItemInt(m_hWnd, IDC_EDIT_MINPAUSE, nullptr, FALSE);

        cfg_pregap_ms =
            (int)GetDlgItemInt(m_hWnd, IDC_EDIT_PREGAP, nullptr, FALSE);

        char buf[512] = { 0 };
        GetDlgItemTextA(m_hWnd, IDC_EDIT_TEMPLATE, buf, sizeof(buf));
        cfg_cue_filename_template = buf;

        cfg_add_to_playlist =
            (IsDlgButtonChecked(m_hWnd, IDC_CHK_ADD_TO_PLAYLIST) == BST_CHECKED);

        enforce_pregap_rule();

        if (m_callback.is_valid()) m_callback->on_state_changed();
    }

    void reset() override {
        if (!m_hWnd) return;

        cfg_digital_silence_only = true;
        cfg_gapless_mode = false;
        cfg_silence_threshold_db = -60;
        cfg_min_pause_ms = 500;
        cfg_pregap_ms = 200;
        cfg_cue_filename_template = "%filename%";
        cfg_add_to_playlist = true;

        CheckDlgButton(m_hWnd, IDC_CHK_DIGITAL_ONLY, BST_CHECKED);
        CheckDlgButton(m_hWnd, IDC_CHK_GAPLESS, BST_UNCHECKED);
        SetDlgItemInt(m_hWnd, IDC_EDIT_THRESHOLD, (UINT)-60, TRUE);
        SetDlgItemInt(m_hWnd, IDC_EDIT_MINPAUSE, 500, FALSE);
        SetDlgItemInt(m_hWnd, IDC_EDIT_PREGAP, 200, FALSE);
        SetDlgItemTextA(m_hWnd, IDC_EDIT_TEMPLATE, "%filename%");
        CheckDlgButton(m_hWnd, IDC_CHK_ADD_TO_PLAYLIST, BST_CHECKED);

        update_control_state(m_hWnd);

        if (m_callback.is_valid()) m_callback->on_state_changed();
    }

    void notify_changed() {
        if (m_callback.is_valid()) m_callback->on_state_changed();
    }

    // Обновить доступность контролов в зависимости от gapless/digital.
    // Принимает HWND явно, потому что в WM_INITDIALOG m_hWnd ещё NULL.
    void update_control_state(HWND hDlg) {
        HWND wnd = hDlg ? hDlg : m_hWnd;
        if (!wnd) return;

        BOOL gapless = (IsDlgButtonChecked(wnd, IDC_CHK_GAPLESS) == BST_CHECKED);
        BOOL digital = (IsDlgButtonChecked(wnd, IDC_CHK_DIGITAL_ONLY) == BST_CHECKED);

        // DSO отключается в gapless-режиме (галочка сохраняется).
        EnableWindow(GetDlgItem(wnd, IDC_CHK_DIGITAL_ONLY), !gapless);

        // Threshold активен, когда НЕ (digital_only && !gapless).
        BOOL threshold_enabled = (!digital) || gapless;
        EnableWindow(GetDlgItem(wnd, IDC_EDIT_THRESHOLD), threshold_enabled);
        EnableWindow(GetDlgItem(wnd, IDC_SPIN_THRESHOLD), threshold_enabled);

        // Minimum pause всегда активен.
        EnableWindow(GetDlgItem(wnd, IDC_EDIT_MINPAUSE), TRUE);
        EnableWindow(GetDlgItem(wnd, IDC_SPIN_MINPAUSE), TRUE);
    }

private:
    bool has_changes() {
        if (!m_hWnd) return false;

        bool cur_digital = (IsDlgButtonChecked(m_hWnd, IDC_CHK_DIGITAL_ONLY) == BST_CHECKED);
        if (cur_digital != cfg_digital_silence_only.get()) return true;

        bool cur_gapless = (IsDlgButtonChecked(m_hWnd, IDC_CHK_GAPLESS) == BST_CHECKED);
        if (cur_gapless != cfg_gapless_mode.get()) return true;

        int cur_threshold = (int)GetDlgItemInt(m_hWnd, IDC_EDIT_THRESHOLD, nullptr, TRUE);
        if (cur_threshold != (int)cfg_silence_threshold_db.get()) return true;

        int cur_minpause = (int)GetDlgItemInt(m_hWnd, IDC_EDIT_MINPAUSE, nullptr, FALSE);
        if (cur_minpause != (int)cfg_min_pause_ms.get()) return true;

        int cur_pregap = (int)GetDlgItemInt(m_hWnd, IDC_EDIT_PREGAP, nullptr, FALSE);
        if (cur_pregap != (int)cfg_pregap_ms.get()) return true;

        bool cur_add = (IsDlgButtonChecked(m_hWnd, IDC_CHK_ADD_TO_PLAYLIST) == BST_CHECKED);
        if (cur_add != cfg_add_to_playlist.get()) return true;

        char buf[512] = { 0 };
        GetDlgItemTextA(m_hWnd, IDC_EDIT_TEMPLATE, buf, sizeof(buf));
        if (pfc::string8(buf) != cfg_cue_filename_template.get()) return true;

        return false;
    }

public:
    static cuepause_prefs_instance* from_hwnd(HWND hDlg) {
        return (cuepause_prefs_instance*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
    }

    // Обработка переключения Gapless. Pre-gap не трогаем.
    void on_gapless_toggled() {
        if (!m_hWnd) return;
        update_control_state(m_hWnd);
        notify_changed();
    }

private:
    HWND m_hWnd;
    preferences_page_callback::ptr m_callback;
};

// ============================================================
//  preferences_page
// ============================================================

class cuepause_prefs_page : public preferences_page_v3 {
public:
    const char* get_name() override { return "CUE from Pause"; }

    GUID get_guid() override {
        static const GUID g =
        { 0xaa11c010, 0x1111, 0x4a01, { 0x90, 0x01, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 } };
        return g;
    }

    GUID get_parent_guid() override { return preferences_page::guid_tools; }

    preferences_page_instance::ptr instantiate(
        HWND parent,
        preferences_page_callback::ptr callback) override
    {
        return fb2k::service_new<cuepause_prefs_instance>(parent, callback);
    }
};

static preferences_page_factory_t<cuepause_prefs_page> g_prefs_factory;

// ============================================================
//  Процедура
// ============================================================

static void enforce_pregap_pair(HWND hDlg) {
    BOOL ok1 = FALSE, ok2 = FALSE;
    int minpause = (int)GetDlgItemInt(hDlg, IDC_EDIT_MINPAUSE, &ok1, FALSE);
    int pregap = (int)GetDlgItemInt(hDlg, IDC_EDIT_PREGAP, &ok2, FALSE);

    if (ok1 && ok2 && minpause >= 1 && pregap >= 0) {
        if (pregap > minpause) {
            SetDlgItemInt(hDlg, IDC_EDIT_PREGAP, (UINT)minpause, FALSE);
        }
    }
}

static INT_PTR CALLBACK PrefsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)lParam);

        CheckDlgButton(hDlg, IDC_CHK_DIGITAL_ONLY,
            cfg_digital_silence_only.get() ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHK_GAPLESS,
            cfg_gapless_mode.get() ? BST_CHECKED : BST_UNCHECKED);

        SetDlgItemInt(hDlg, IDC_EDIT_THRESHOLD,
            (UINT)(int)cfg_silence_threshold_db.get(), TRUE);
        SendDlgItemMessage(hDlg, IDC_SPIN_THRESHOLD, UDM_SETRANGE, 0,
            MAKELPARAM((short)-20, (short)-120));

        SetDlgItemInt(hDlg, IDC_EDIT_MINPAUSE,
            (UINT)(int)cfg_min_pause_ms.get(), FALSE);
        SendDlgItemMessage(hDlg, IDC_SPIN_MINPAUSE, UDM_SETRANGE, 0,
            MAKELPARAM((short)60000, (short)1));

        SetDlgItemInt(hDlg, IDC_EDIT_PREGAP,
            (UINT)(int)cfg_pregap_ms.get(), FALSE);
        SendDlgItemMessage(hDlg, IDC_SPIN_PREGAP, UDM_SETRANGE, 0,
            MAKELPARAM((short)60000, (short)0));

        SetDlgItemTextA(hDlg, IDC_EDIT_TEMPLATE,
            cfg_cue_filename_template.get());

        CheckDlgButton(hDlg, IDC_CHK_ADD_TO_PLAYLIST,
            cfg_add_to_playlist.get() ? BST_CHECKED : BST_UNCHECKED);

        cuepause_prefs_instance* self = (cuepause_prefs_instance*)lParam;
        if (self) self->update_control_state(hDlg);

        if (IsDarkMode()) {
            ApplyDarkThemeToDialog(hDlg);
        }

        return TRUE;
    }

    case WM_COMMAND: {
        const WORD id = LOWORD(wParam);
        const WORD code = HIWORD(wParam);

        cuepause_prefs_instance* self = cuepause_prefs_instance::from_hwnd(hDlg);

        if (id == IDC_CHK_GAPLESS && code == BN_CLICKED) {
            if (self) self->on_gapless_toggled();
            return TRUE;
        }

        if (id == IDC_CHK_DIGITAL_ONLY && code == BN_CLICKED) {
            if (self) self->update_control_state(hDlg);
            if (self) self->notify_changed();
            return TRUE;
        }

        if (id == IDC_BTN_TEMPLATE_MENU && code == BN_CLICKED) {
            HMENU menu = CreatePopupMenu();
            AppendMenuA(menu, MF_STRING, 1, "%filename%");
            AppendMenuA(menu, MF_STRING, 2, "%album%");
            AppendMenuA(menu, MF_STRING, 3, "%artist%");

            RECT rc;
            HWND btn = GetDlgItem(hDlg, IDC_BTN_TEMPLATE_MENU);
            GetWindowRect(btn, &rc);

            int cmd = TrackPopupMenu(menu,
                TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                rc.left, rc.bottom, 0, hDlg, nullptr);
            DestroyMenu(menu);

            const char* insert = nullptr;
            if (cmd == 1) insert = "%filename%";
            if (cmd == 2) insert = "%album%";
            if (cmd == 3) insert = "%artist%";

            if (insert) {
                HWND edit = GetDlgItem(hDlg, IDC_EDIT_TEMPLATE);
                SendMessageA(edit, EM_REPLACESEL, TRUE, (LPARAM)insert);
                if (self) self->notify_changed();
            }
            return TRUE;
        }

        if ((id == IDC_EDIT_MINPAUSE || id == IDC_EDIT_PREGAP) && code == EN_KILLFOCUS) {
            enforce_pregap_pair(hDlg);
            if (self) self->notify_changed();
            return TRUE;
        }

        if (code == EN_CHANGE || code == BN_CLICKED || code == CBN_SELCHANGE) {
            if (self) self->notify_changed();
        }
        break;
    }

    case WM_NOTIFY: {
        NMHDR* hdr = (NMHDR*)lParam;
        if (hdr->code == PSN_APPLY) {
            SetWindowLongPtr(hDlg, DWLP_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        break;
    }

    case WM_CTLCOLORDLG:
        if (IsDarkMode()) return (INT_PTR)GetDarkBackgroundBrush();
        break;

    case WM_CTLCOLORBTN:
        if (IsDarkMode()) {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(220, 220, 220));
            SetBkMode(hdc, TRANSPARENT);
            return (INT_PTR)GetDarkBackgroundBrush();
        }
        break;

    case WM_CTLCOLORSTATIC:
        if (IsDarkMode()) {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(220, 220, 220));
            SetBkMode(hdc, TRANSPARENT);
            return (INT_PTR)GetDarkBackgroundBrush();
        }
        break;

    case WM_CTLCOLOREDIT:
        if (IsDarkMode()) {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(220, 220, 220));
            SetBkColor(hdc, RGB(30, 30, 30));
            return (INT_PTR)GetDarkEditBrush();
        }
        break;

    case WM_CTLCOLORLISTBOX:
        if (IsDarkMode()) {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(220, 220, 220));
            SetBkColor(hdc, RGB(30, 30, 30));
            return (INT_PTR)GetDarkEditBrush();
        }
        break;
    }
    return FALSE;
}