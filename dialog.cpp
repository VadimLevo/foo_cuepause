#include "dialog.h"
#include "cue.h"
#include "cfg.h"
#include "resource.h"
#include "listview_helper.h"

#include <atlbase.h>
#include <atlapp.h>
#include <atlctrls.h>
#include <atltypes.h>

#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")

// ============================================================
//  Тёмная тема
// ============================================================

static HBRUSH g_hbrDarkBackground = NULL;
static HBRUSH g_hbrDarkEdit = NULL;
static HBRUSH g_hbrDarkList = NULL;

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

static HBRUSH GetDarkListBrush() {
    if (!g_hbrDarkList) g_hbrDarkList = CreateSolidBrush(RGB(30, 30, 30));
    return g_hbrDarkList;
}

void cuepause_cleanup_dark_brushes() {
    if (g_hbrDarkBackground) { DeleteObject(g_hbrDarkBackground); g_hbrDarkBackground = NULL; }
    if (g_hbrDarkEdit) { DeleteObject(g_hbrDarkEdit);       g_hbrDarkEdit = NULL; }
    if (g_hbrDarkList) { DeleteObject(g_hbrDarkList);       g_hbrDarkList = NULL; }
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

static void ApplyDarkThemeToListView(HWND lv) {
    if (!lv) return;
    SetWindowTheme(lv, L"DarkMode_Explorer", NULL);
    ListView_SetBkColor(lv, RGB(30, 30, 30));
    ListView_SetTextBkColor(lv, RGB(30, 30, 30));
    ListView_SetTextColor(lv, RGB(220, 220, 220));
    InvalidateRect(lv, NULL, TRUE);
}

// ============================================================
//  Состояние диалога
// ============================================================

struct marker_row {
    t_size file_index;
    t_size marker_index;
    double marker_seconds;
    double track_length_seconds;
    bool   manual;
    int    display_number;
};

static pfc::list_t<scan_result>              g_results;
static pfc::list_t<marker_row>               g_rows;
static pfc::list_t<pfc::bit_array_bittable>  g_selected;
static HWND                                   g_dlg = nullptr;

static bool g_updating_list = false;

// ============================================================
//  Вспомогательные
// ============================================================

static pfc::string8 row_time_text(const marker_row& r) {
    return format_cue_time(r.marker_seconds);
}

static pfc::string8 row_length_text(const marker_row& r) {
    if (r.track_length_seconds <= 0.0) return "";
    return format_ms_time(r.track_length_seconds);
}

static pfc::string8 row_source_text(const marker_row& r) {
    return r.manual ? "manual" : "auto";
}

static pfc::string8 extract_filename_only(const pfc::string8& path) {
    t_size last_sep = pfc_infinite;
    const t_size len = path.length();
    for (t_size i = 0; i < len; ++i) {
        char c = path[i];
        if (c == '\\' || c == '/') last_sep = i;
    }
    if (last_sep == pfc_infinite || last_sep + 1 >= len) return path;
    return pfc::string8(path.get_ptr() + last_sep + 1);
}

static pfc::string8 row_file_text(const marker_row& r) {
    if (r.file_index >= g_results.get_count()) return "";
    return extract_filename_only(g_results[r.file_index].path);
}

static pfc::string8 row_number_text(const marker_row& r) {
    if (r.display_number <= 0) return "";
    pfc::string8 s;
    s << r.display_number;
    return s;
}

static bool is_row_selected(const marker_row& r) {
    if (r.file_index >= g_selected.get_count()) return false;
    if (r.marker_index >= g_results[r.file_index].markers.get_count()) return false;
    return g_selected[r.file_index].get(r.marker_index);
}

static void rebuild_rows() {
    g_rows.remove_all();

    for (t_size f = 0; f < g_results.get_count(); ++f) {
        const scan_result& res = g_results[f];
        const t_size n = res.markers.get_count();

        for (t_size i = 0; i < n; ++i) {
            marker_row row;
            row.file_index = f;
            row.marker_index = i;
            row.marker_seconds = res.markers[i].marker_seconds;
            row.manual = res.markers[i].manual;
            row.display_number = 0;
            row.track_length_seconds = 0.0;
            g_rows.add_item(row);
        }
    }
}

static void rebuild_selected() {
    g_selected.remove_all();
    for (t_size f = 0; f < g_results.get_count(); ++f) {
        const t_size n = g_results[f].markers.get_count();
        pfc::bit_array_bittable ba(n);
        for (t_size i = 0; i < n; ++i) {
            ba.set(i, true);
        }
        g_selected.add_item(ba);
    }
}

static void update_display_numbers() {
    int counter = 1;
    for (t_size i = 0; i < g_rows.get_count(); ++i) {
        marker_row& r = g_rows[i];
        r.display_number = is_row_selected(r) ? counter++ : 0;
    }
}

// Пересчитывает длины треков с учётом снятых галочек.
static void recalculate_track_lengths() {
    for (t_size f = 0; f < g_results.get_count(); ++f) {
        const scan_result& res = g_results[f];
        const t_size n = res.markers.get_count();

        for (t_size i = 0; i < n; ++i) {
            t_size row_idx = pfc_infinite;
            for (t_size k = 0; k < g_rows.get_count(); ++k) {
                if (g_rows[k].file_index == f && g_rows[k].marker_index == i) {
                    row_idx = k;
                    break;
                }
            }
            if (row_idx == pfc_infinite) continue;

            marker_row& r = g_rows[row_idx];

            if (!is_row_selected(r)) {
                r.track_length_seconds = 0.0;
                continue;
            }

            double end_sec = res.length_seconds;
            for (t_size j = i + 1; j < n; ++j) {
                if (g_selected[f].get(j)) {
                    end_sec = res.markers[j].marker_seconds;
                    break;
                }
            }
            r.track_length_seconds = end_sec - r.marker_seconds;
            if (r.track_length_seconds < 0.0) r.track_length_seconds = 0.0;
        }
    }
}

static t_size count_selected() {
    t_size n = 0;
    for (t_size f = 0; f < g_selected.get_count(); ++f) {
        const auto& ba = g_selected[f];
        for (t_size i = 0; i < g_results[f].markers.get_count(); ++i) {
            if (ba.get(i)) ++n;
        }
    }
    return n;
}

static void update_summary(HWND hDlg) {
    pfc::string8 summary;
    summary << "Files: " << (t_uint32)g_results.get_count()
        << "   Markers: " << (t_uint32)g_rows.get_count()
        << "   Selected: " << (t_uint32)count_selected();
    SetDlgItemTextA(hDlg, IDC_LABEL_SUMMARY, summary);
}

static void refresh_list(HWND hDlg) {
    HWND lv = GetDlgItem(hDlg, IDC_LISTVIEW);

    g_updating_list = true;

    ListView_DeleteAllItems(lv);

    update_display_numbers();
    recalculate_track_lengths();

    const t_size n = g_rows.get_count();
    for (t_size i = 0; i < n; ++i) {
        const marker_row& r = g_rows[i];

        pfc::string8 num_text = row_number_text(r);
        pfc::string8 file_text = row_file_text(r);
        pfc::string8 time_text = row_time_text(r);
        pfc::string8 len_text = row_length_text(r);
        pfc::string8 src_text = row_source_text(r);

        unsigned idx = listview_helper::insert_item(lv, ~0u, num_text, 0);
        if (idx != ~0u) {
            listview_helper::set_item_text(lv, idx, 1, file_text);
            listview_helper::set_item_text(lv, idx, 2, time_text);
            listview_helper::set_item_text(lv, idx, 3, len_text);
            listview_helper::set_item_text(lv, idx, 4, src_text);

            bool checked = is_row_selected(r);
            ListView_SetCheckState(lv, idx, checked ? TRUE : FALSE);
        }
    }

    g_updating_list = false;

    update_summary(hDlg);
}

// ============================================================
//  Ширины колонок
// ============================================================

static void save_column_widths(HWND lv) {
    if (!lv) return;
    for (int i = 0; i < kCuePauseColumnCount; ++i) {
        cfg_results_column_widths.m_widths[i] = ListView_GetColumnWidth(lv, i);
    }
}

static void apply_column_widths(HWND lv) {
    if (!lv) return;
    for (int i = 0; i < kCuePauseColumnCount; ++i) {
        ListView_SetColumnWidth(lv, i, cfg_results_column_widths.m_widths[i]);
    }
}

// ============================================================
//  Layout
// ============================================================

static void layout_children(HWND hDlg) {
    RECT rc;
    GetClientRect(hDlg, &rc);

    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    const int margin = 7;

    int lineH = 12;
    {
        HDC hdc = GetDC(hDlg);
        if (hdc) {
            TEXTMETRIC tm;
            if (GetTextMetrics(hdc, &tm)) {
                lineH = tm.tmHeight + tm.tmExternalLeading + 2;
                if (lineH < 12) lineH = 12;
            }
            ReleaseDC(hDlg, hdc);
        }
    }

    HWND lv = GetDlgItem(hDlg, IDC_LISTVIEW);
    HWND summary = GetDlgItem(hDlg, IDC_LABEL_SUMMARY);
    HWND add = GetDlgItem(hDlg, IDC_BTN_ADD_MARKER);
    HWND exp = GetDlgItem(hDlg, IDC_BTN_EXPORT);
    HWND cls = GetDlgItem(hDlg, IDC_BTN_CLOSE);

    RECT rcAdd, rcExp, rcCls;
    GetWindowRect(add, &rcAdd);
    GetWindowRect(exp, &rcExp);
    GetWindowRect(cls, &rcCls);

    int addW = rcAdd.right - rcAdd.left;
    int expW = rcExp.right - rcExp.left;
    int clsW = rcCls.right - rcCls.left;
    int btnH = rcCls.bottom - rcCls.top;

    int btnY = height - margin - btnH;

    int clsX = width - margin - clsW;
    int expX = clsX - margin - expW;
    int addX = expX - margin - addW;

    int summaryW = addX - margin * 2;
    if (summaryW < 50) summaryW = 50;

    int lvW = width - margin * 2;
    int lvH = btnY - margin * 2;
    if (lvH < 50) lvH = 50;

    HDWP hdwp = BeginDeferWindowPos(5);
    if (hdwp) {
        hdwp = DeferWindowPos(hdwp, lv, nullptr, margin, margin, lvW, lvH,
            SWP_NOZORDER | SWP_NOACTIVATE);
        hdwp = DeferWindowPos(hdwp, summary, nullptr, margin,
            btnY + (btnH - lineH) / 2,
            summaryW, lineH,
            SWP_NOZORDER | SWP_NOACTIVATE);
        hdwp = DeferWindowPos(hdwp, add, nullptr, addX, btnY, 0, 0,
            SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
        hdwp = DeferWindowPos(hdwp, exp, nullptr, expX, btnY, 0, 0,
            SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
        hdwp = DeferWindowPos(hdwp, cls, nullptr, clsX, btnY, 0, 0,
            SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
        EndDeferWindowPos(hdwp);
    }
}

// ============================================================
//  Ручное добавление метки
// ============================================================

static void add_manual_marker_at_playback_position() {
    static_api_ptr_t<playback_control> pc;
    if (!pc->is_playing() && !pc->is_paused()) {
        popup_message::g_show(
            "Playback is not active. Start playing a track, then press Add marker.",
            "CUE from Pause");
        return;
    }

    double pos = pc->playback_get_position();
    if (pos < 0.0) pos = 0.0;

    metadb_handle_ptr playing;
    if (!pc->get_now_playing(playing) || !playing.is_valid()) {
        popup_message::g_show("Cannot determine currently playing track.", "CUE from Pause");
        return;
    }

    pfc::string8 playing_path = playing->get_path();

    t_size file_idx = pfc_infinite;
    for (t_size f = 0; f < g_results.get_count(); ++f) {
        if (g_results[f].path == playing_path) {
            file_idx = f;
            break;
        }
    }

    if (file_idx == pfc_infinite) {
        popup_message::g_show(
            "Currently playing track is not among scanned files.",
            "CUE from Pause");
        return;
    }

    scan_result& res = g_results[file_idx];

    pause_marker m;
    m.manual = true;
    m.marker_seconds = pos;
    m.pause_start_seconds = pos;
    m.pause_end_seconds = pos;

    t_size insert_at = res.markers.get_count();
    for (t_size i = 0; i < res.markers.get_count(); ++i) {
        if (res.markers[i].marker_seconds > pos) {
            insert_at = i;
            break;
        }
    }
    res.markers.insert_item(m, insert_at);

    pfc::list_t<pfc::bit_array_bittable> old_selected = g_selected;
    rebuild_rows();
    rebuild_selected();

    if (file_idx < old_selected.get_count()) {
        const pfc::bit_array_bittable& old_ba = old_selected[file_idx];
        pfc::bit_array_bittable& new_ba = g_selected[file_idx];
        const t_size n_old = old_ba.size();
        for (t_size i = 0; i < n_old; ++i) {
            bool v = old_ba.get(i);
            if (i < insert_at) new_ba.set(i, v);
            else new_ba.set(i + 1, v);
        }
        new_ba.set(insert_at, true);
    }

    update_display_numbers();
    if (g_dlg) refresh_list(g_dlg);
}

// ============================================================
//  Экспорт
// ============================================================

static void do_export(HWND hDlg) {
    if (g_results.get_count() == 0) {
        popup_message::g_show("Nothing to export.", "CUE from Pause");
        return;
    }

    if (count_selected() == 0) {
        popup_message::g_show("No markers selected for export.", "CUE from Pause");
        return;
    }

    pfc::string8 tpl = cfg_cue_filename_template.get();
    pfc::string8 path = export_cue_file(g_results, g_selected, tpl.get_ptr());

    if (path.is_empty()) {
        popup_message::g_show("Failed to write CUE file.", "CUE from Pause");
        return;
    }

    if (cfg_add_to_playlist.get()) {
        add_cue_to_active_playlist(path);
    }

    pfc::string8 msg;
    msg << "CUE exported:\n" << path;
    popup_message::g_show(msg, "CUE from Pause");
}

// ============================================================
//  Процедура диалога
// ============================================================

static void close_dialog(HWND hDlg) {
    HWND lv = GetDlgItem(hDlg, IDC_LISTVIEW);
    if (lv) save_column_widths(lv);
    GetWindowRect(hDlg, &cfg_results_window_rect.m_rect);
    DestroyWindow(hDlg);
}

static INT_PTR CALLBACK DialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        g_dlg = hDlg;

        {
            HICON hIcon = LoadIcon(core_api::get_my_instance(),
                MAKEINTRESOURCE(IDI_CUEPAUSE));
            if (hIcon) {
                SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
                SendMessage(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            }
        }

        SetWindowPos(hDlg, nullptr,
            cfg_results_window_rect.m_rect.left,
            cfg_results_window_rect.m_rect.top,
            cfg_results_window_rect.m_rect.right - cfg_results_window_rect.m_rect.left,
            cfg_results_window_rect.m_rect.bottom - cfg_results_window_rect.m_rect.top,
            SWP_NOZORDER);

        HWND lv = GetDlgItem(hDlg, IDC_LISTVIEW);
        ListView_SetExtendedListViewStyle(lv,
            LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES | LVS_EX_GRIDLINES);

        listview_helper::insert_column(lv, 0, "#", cfg_results_column_widths.m_widths[0]);
        listview_helper::insert_column(lv, 1, "File", cfg_results_column_widths.m_widths[1]);
        listview_helper::insert_column(lv, 2, "Marker", cfg_results_column_widths.m_widths[2]);
        listview_helper::insert_column(lv, 3, "Length", cfg_results_column_widths.m_widths[3]);
        listview_helper::insert_column(lv, 4, "Source", cfg_results_column_widths.m_widths[4]);

        if (IsDarkMode()) {
            ApplyDarkThemeToDialog(hDlg);
            ApplyDarkThemeToListView(lv);
        }

        refresh_list(hDlg);
        apply_column_widths(lv);
        layout_children(hDlg);
        return TRUE;
    }
    case WM_SIZE: {
        if (wParam == SIZE_MINIMIZED) return TRUE;
        layout_children(hDlg);
        return TRUE;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = 600;
        mmi->ptMinTrackSize.y = 300;
        return TRUE;
    }
    case WM_NOTIFY: {
        NMHDR* hdr = (NMHDR*)lParam;
        if (hdr->idFrom == IDC_LISTVIEW) {
            HWND lv = GetDlgItem(hDlg, IDC_LISTVIEW);

            if (hdr->code == LVN_ITEMCHANGED) {
                if (g_updating_list) return TRUE;

                NMLISTVIEW* nmlv = (NMLISTVIEW*)lParam;
                if (nmlv->iItem < 0) return TRUE;
                if (!(nmlv->uChanged & LVIF_STATE)) return TRUE;

                UINT oldState = (nmlv->uOldState & LVIS_STATEIMAGEMASK) >> 12;
                UINT newState = (nmlv->uNewState & LVIS_STATEIMAGEMASK) >> 12;
                if (oldState == newState) return TRUE;

                bool checked = (newState == 2);

                int idx = nmlv->iItem;
                if (idx < 0 || idx >= (int)g_rows.get_count()) return TRUE;

                const marker_row& r = g_rows[idx];
                if (r.file_index >= g_selected.get_count()) return TRUE;

                g_selected[r.file_index].set(r.marker_index, checked);

                update_display_numbers();
                recalculate_track_lengths();

                g_updating_list = true;
                for (t_size i = 0; i < g_rows.get_count(); ++i) {
                    pfc::string8 num = row_number_text(g_rows[i]);
                    pfc::string8 len = row_length_text(g_rows[i]);
                    listview_helper::set_item_text(lv, (unsigned)i, 0, num);
                    listview_helper::set_item_text(lv, (unsigned)i, 3, len);
                }
                g_updating_list = false;

                update_summary(hDlg);
                return TRUE;
            }
        }
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_BTN_CLOSE) {
            close_dialog(hDlg);
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_BTN_EXPORT) {
            {
                HWND lv = GetDlgItem(hDlg, IDC_LISTVIEW);
                if (lv) save_column_widths(lv);
            }
            do_export(hDlg);
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_BTN_ADD_MARKER) {
            add_manual_marker_at_playback_position();
            return TRUE;
        }
        break;
    case WM_CLOSE:
        close_dialog(hDlg);
        return TRUE;

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
            return (INT_PTR)GetDarkListBrush();
        }
        break;

    case WM_DESTROY:
        if (g_dlg == hDlg) {
            g_dlg = nullptr;
            // Данные здесь не чистим — они управляются из show_cuepause_dialog.
        }
        return TRUE;
    }
    return FALSE;
}

// ============================================================
//  Публичная точка входа
// ============================================================

void show_cuepause_dialog(const pfc::list_t<scan_result>& results) {
    // 1. Сначала закрываем старый диалог (если есть).
    //    Это гарантирует, что WM_DESTROY не тронет g_results,
    //    которые мы сейчас заполним.
    if (g_dlg) {
        HWND lv = GetDlgItem(g_dlg, IDC_LISTVIEW);
        if (lv) save_column_widths(lv);
        DestroyWindow(g_dlg);
        g_dlg = nullptr;
    }

    // 2. Теперь заполняем данные для нового диалога.
    g_results.remove_all();
    g_results = results;

    rebuild_rows();
    rebuild_selected();

    // 3. Создаём новый диалог.
    HWND hDlg = CreateDialogParam(
        core_api::get_my_instance(),
        MAKEINTRESOURCE(IDD_CUEPAUSE_DIALOG),
        core_api::get_main_window(),
        DialogProc,
        0);

    if (hDlg) {
        ShowWindow(hDlg, SW_SHOW);
        SetForegroundWindow(hDlg);
    }
}