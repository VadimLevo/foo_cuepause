#include "cue.h"
#include "cfg.h"

#include <cmath>

// ============================================================
//  Форматирование времени
// ============================================================

pfc::string8 format_cue_time(double seconds) {
    if (seconds < 0.0) seconds = 0.0;

    t_uint64 total_frames = (t_uint64)(seconds * 75.0 + 0.5);
    t_uint64 frames = total_frames % 75;
    t_uint64 total_sec = total_frames / 75;
    t_uint64 sec = total_sec % 60;
    t_uint64 min = total_sec / 60;

    pfc::string8 out;
    out << pfc::format_uint(min, 2) << ":"
        << pfc::format_uint(sec, 2) << ":"
        << pfc::format_uint(frames, 2);
    return out;
}

pfc::string8 format_ms_time(double seconds) {
    if (seconds < 0.0) seconds = 0.0;

    t_uint64 total_ms = (t_uint64)(seconds * 1000.0 + 0.5);
    t_uint64 ms = total_ms % 1000;
    t_uint64 total_sec = total_ms / 1000;
    t_uint64 sec = total_sec % 60;
    t_uint64 min = total_sec / 60;

    pfc::string8 out;
    out << pfc::format_uint(min, 2) << ":"
        << pfc::format_uint(sec, 2) << "."
        << pfc::format_uint(ms, 3);
    return out;
}

// ============================================================
//  Утилиты для путей
// ============================================================

static pfc::string8 extract_filename(const pfc::string8& path) {
    t_size last_sep = pfc_infinite;
    const t_size len = path.length();
    for (t_size i = 0; i < len; ++i) {
        char c = path[i];
        if (c == '\\' || c == '/') last_sep = i;
    }
    if (last_sep == pfc_infinite || last_sep + 1 >= len) return path;
    return pfc::string8(path.get_ptr() + last_sep + 1);
}

static pfc::string8 strip_extension(const pfc::string8& name) {
    t_size dot = pfc_infinite;
    const t_size len = name.length();
    for (t_size i = len; i > 0; --i) {
        if (name[i - 1] == '.') { dot = i - 1; break; }
        if (name[i - 1] == '\\' || name[i - 1] == '/') break;
    }
    if (dot == pfc_infinite) return name;
    return pfc::string8(name.get_ptr(), dot);
}

static pfc::string8 extract_dir(const pfc::string8& path) {
    t_size last_sep = pfc_infinite;
    const t_size len = path.length();
    for (t_size i = 0; i < len; ++i) {
        char c = path[i];
        if (c == '\\' || c == '/') last_sep = i;
    }
    if (last_sep == pfc_infinite) return pfc::string8("");
    return pfc::string8(path.get_ptr(), last_sep + 1);
}

// ============================================================
//  Подстановка шаблона имени CUE
// ============================================================

static pfc::string8 apply_template(
    const char* tpl,
    const pfc::string8& first_path)
{
    pfc::string8 result;
    if (tpl == nullptr || tpl[0] == 0) tpl = "%filename%";

    pfc::string8 filename_noext = strip_extension(extract_filename(first_path));

    pfc::string8 album, artist;
    {
        metadb_handle_ptr h;
        static_api_ptr_t<metadb> mdb;
        try {
            mdb->handle_create(h, make_playable_location(first_path, 0));
            file_info_impl info;
            // Синхронное получение тегов — get_info_async может вернуть пусто,
            // если метаданные ещё не в кэше.
            if (h->get_info(info)) {
                const char* a = info.meta_get("ALBUM", 0);
                const char* r = info.meta_get("ARTIST", 0);
                if (a) album = a;
                if (r) artist = r;
            }
        }
        catch (...) {}
    }

    const char* p = tpl;
    while (*p) {
        if (p[0] == '%') {
            if (strncmp(p, "%filename%", 10) == 0) {
                result << filename_noext;
                p += 10;
                continue;
            }
            if (strncmp(p, "%album%", 7) == 0) {
                result << album;
                p += 7;
                continue;
            }
            if (strncmp(p, "%artist%", 8) == 0) {
                result << artist;
                p += 8;
                continue;
            }
        }
        result << *p;
        ++p;
    }

    if (result.is_empty()) result << filename_noext;
    return result;
}

// ============================================================
//  Сборка CUE
// ============================================================

pfc::string8 build_cue_text(
    const pfc::list_t<scan_result>& results,
    const pfc::list_t<pfc::bit_array_bittable>& selected_per_file)
{
    pfc::string8 out;

    if (results.get_count() > 0) {
        metadb_handle_ptr h;
        static_api_ptr_t<metadb> mdb;
        try {
            mdb->handle_create(h, make_playable_location(results[0].path, 0));
            file_info_impl info;
            // Синхронное получение тегов.
            if (h->get_info(info)) {
                const char* artist = info.meta_get("ARTIST", 0);
                const char* album = info.meta_get("ALBUM", 0);
                const char* date = info.meta_get("DATE", 0);
                const char* genre = info.meta_get("GENRE", 0);

                if (artist) out << "PERFORMER \"" << artist << "\"\n";
                if (album)  out << "TITLE \"" << album << "\"\n";
                if (date)   out << "REM DATE " << date << "\n";
                if (genre)  out << "REM GENRE " << genre << "\n";
            }
        }
        catch (...) {}
    }

    int global_track = 1;

    for (t_size f = 0; f < results.get_count(); ++f) {
        const scan_result& r = results[f];
        if (!r.ok) continue;
        if (r.markers.get_count() == 0) continue;

        pfc::string8 fname = extract_filename(r.path);

        out << "FILE \"" << fname << "\" WAVE\n";

        pfc::list_t<t_size> selected_idx;
        const pfc::bit_array_bittable& sel = selected_per_file[f];
        for (t_size i = 0; i < r.markers.get_count(); ++i) {
            if (sel.get(i)) selected_idx.add_item(i);
        }

        if (selected_idx.get_count() == 0) {
            continue;
        }

        {
            const t_size idx = selected_idx[0];
            const pause_marker& m = r.markers[idx];

            out << "  TRACK " << pfc::format_uint((t_uint64)global_track, 2) << " AUDIO\n";

            if (m.pause_start_seconds > 0.0) {
                out << "    INDEX 00 " << format_cue_time(m.pause_start_seconds) << "\n";
            }
            out << "    INDEX 01 " << format_cue_time(m.marker_seconds) << "\n";

            ++global_track;
        }

        for (t_size k = 1; k < selected_idx.get_count(); ++k) {
            const t_size idx = selected_idx[k];
            const pause_marker& m = r.markers[idx];

            out << "  TRACK " << pfc::format_uint((t_uint64)global_track, 2) << " AUDIO\n";
            out << "    INDEX 01 " << format_cue_time(m.marker_seconds) << "\n";

            ++global_track;
        }
    }

    return out;
}

// ============================================================
//  Запись CUE на диск
// ============================================================

static bool file_exists(const pfc::string8& path) {
    try {
        abort_callback_dummy abort;
        return filesystem::g_exists(path, abort);
    }
    catch (...) {
        return false;
    }
}

static pfc::string8 make_unique_cue_path(const pfc::string8& base_path) {
    if (!file_exists(base_path)) return base_path;

    pfc::string8 dir = extract_dir(base_path);
    pfc::string8 fname = extract_filename(base_path);
    pfc::string8 stem = strip_extension(fname);

    for (int n = 1; n < 1000; ++n) {
        pfc::string8 candidate;
        candidate << dir << stem << " (" << pfc::format_uint((t_uint64)n, 2) << ").cue";
        if (!file_exists(candidate)) return candidate;
    }
    return base_path;
}

pfc::string8 export_cue_file(
    const pfc::list_t<scan_result>& results,
    const pfc::list_t<pfc::bit_array_bittable>& selected_per_file,
    const char* filename_template)
{
    if (results.get_count() == 0) return pfc::string8("");

    pfc::string8 dir = extract_dir(results[0].path);
    pfc::string8 base_name = apply_template(filename_template, results[0].path);
    if (base_name.is_empty()) base_name = strip_extension(extract_filename(results[0].path));

    t_size len = base_name.length();
    if (len < 4 ||
        pfc::stricmp_ascii(base_name.get_ptr() + len - 4, ".cue") != 0)
    {
        base_name << ".cue";
    }

    pfc::string8 full_path;
    full_path << dir << base_name;

    full_path = make_unique_cue_path(full_path);

    pfc::string8 text = build_cue_text(results, selected_per_file);

    try {
        abort_callback_dummy abort;
        service_ptr_t<file> f;
        filesystem::g_open_write_new(f, full_path, abort);
        f->write(text.get_ptr(), text.length(), abort);
    }
    catch (const std::exception& e) {
        pfc::string8 err_msg = e.what();
        console::print(pfc::format("foo_cuepause: cannot write CUE: ", err_msg));
        return pfc::string8("");
    }

    return full_path;
}

// ============================================================
//  Добавление в плейлист
// ============================================================

void add_cue_to_active_playlist(const char* cue_path) {
    if (!cue_path || !cue_path[0]) {
        console::print("foo_cuepause: add_cue: empty path");
        return;
    }

    static_api_ptr_t<playlist_manager> plm;
    t_size active = plm->get_active_playlist();
    if (active == pfc_infinite) {
        if (plm->get_playlist_count() == 0) {
            console::print("foo_cuepause: add_cue: no playlists");
            return;
        }
        active = 0;
    }

    pfc::list_t<const char*> paths;
    paths.add_item(cue_path);

    metadb_handle_list handles;

    static_api_ptr_t<playlist_incoming_item_filter> pif;
    bool ok = pif->process_locations(
        paths,
        handles,
        false,
        nullptr,
        nullptr,
        core_api::get_main_window()
    );

    if (!ok) {
        console::print("foo_cuepause: add_cue: process_locations aborted or failed");
        return;
    }

    if (handles.get_count() == 0) {
        console::print("foo_cuepause: add_cue: no handles returned");
        return;
    }

    t_size base = plm->playlist_get_item_count(active);
    plm->playlist_insert_items(active, base, handles, bit_array_true());

    console::print(pfc::format("foo_cuepause: added ", (t_uint32)handles.get_count(),
        " items to playlist ", (t_uint32)active));
}