#pragma once

#include "foobar2000.h"
#include <atltypes.h>

// ============================================================
//  Настройки компонента foo_cuepause
// ============================================================

extern cfg_bool  cfg_digital_silence_only;
extern cfg_bool  cfg_gapless_mode;
extern cfg_int   cfg_silence_threshold_db;
extern cfg_int   cfg_min_pause_ms;
extern cfg_int   cfg_pregap_ms;
extern cfg_string cfg_cue_filename_template;
extern cfg_bool  cfg_add_to_playlist;

// ---- Геометрия окна результатов ----

class cfg_window_rect : public cfg_var {
public:
    cfg_window_rect();
    void get_data_raw(stream_writer* p_stream, abort_callback& p_abort) override;
    void set_data_raw(stream_reader* p_stream, t_size size, abort_callback& p_abort) override;

    RECT m_rect;
};

extern cfg_window_rect cfg_results_window_rect;

// ---- Ширины колонок ----

static const int kCuePauseColumnCount = 5;

class cfg_column_widths : public cfg_var {
public:
    cfg_column_widths();
    void get_data_raw(stream_writer* p_stream, abort_callback& p_abort) override;
    void set_data_raw(stream_reader* p_stream, t_size size, abort_callback& p_abort) override;

    int m_widths[kCuePauseColumnCount];
};

extern cfg_column_widths cfg_results_column_widths;

// ---- Вспомогательные ----

int    get_effective_pregap_ms();
double get_silence_threshold_linear();
bool   is_digital_silence_only();
bool   is_gapless_mode();
void   enforce_pregap_rule();