#pragma once

#include "foobar2000.h"
#include "scan.h"

// ============================================================
//  Экспорт CUE
// ============================================================

// Формат MM:SS:FF (75 fps)
pfc::string8 format_cue_time(double seconds);

// Формат MM:SS.mmm — для отображения длительности трека в UI
pfc::string8 format_ms_time(double seconds);

// Строит текст CUE по результатам сканирования.
// Выбранные метки помечены в bitmap'е: selected[i] == true.
// track_bitmap — по одному bit_array на файл, отмечает выбранные метки.
pfc::string8 build_cue_text(
    const pfc::list_t<scan_result>& results,
    const pfc::list_t<pfc::bit_array_bittable>& selected_per_file
);

// Записывает CUE рядом с первым аудиофайлом.
// Возвращает полный путь к записанному файлу.
// Если файл существует — добавляет суффикс " (01)", " (02)", ...
pfc::string8 export_cue_file(
    const pfc::list_t<scan_result>& results,
    const pfc::list_t<pfc::bit_array_bittable>& selected_per_file,
    const char* filename_template
);

// Добавляет CUE в текущий активный плейлист.
void add_cue_to_active_playlist(const char* cue_path);