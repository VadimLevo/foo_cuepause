#include "cfg.h"

#include <cmath>

// ============================================================
//  GUID'ы
// ============================================================

static const GUID guid_cfg_digital_silence_only =
{ 0xaa11c001, 0x1111, 0x4a01, { 0x90, 0x01, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 } };

static const GUID guid_cfg_gapless_mode =
{ 0xaa11c009, 0x1111, 0x4a01, { 0x90, 0x01, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 } };

static const GUID guid_cfg_silence_threshold_db =
{ 0xaa11c002, 0x1111, 0x4a01, { 0x90, 0x01, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 } };

static const GUID guid_cfg_min_pause_ms =
{ 0xaa11c003, 0x1111, 0x4a01, { 0x90, 0x01, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 } };

static const GUID guid_cfg_pregap_ms =
{ 0xaa11c004, 0x1111, 0x4a01, { 0x90, 0x01, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 } };

static const GUID guid_cfg_cue_filename_template =
{ 0xaa11c005, 0x1111, 0x4a01, { 0x90, 0x01, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 } };

static const GUID guid_cfg_add_to_playlist =
{ 0xaa11c006, 0x1111, 0x4a01, { 0x90, 0x01, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 } };

static const GUID guid_cfg_window_rect =
{ 0xaa11c007, 0x1111, 0x4a01, { 0x90, 0x01, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 } };

static const GUID guid_cfg_column_widths =
{ 0xaa11c008, 0x1111, 0x4a01, { 0x90, 0x01, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 } };

// ============================================================
//  Хранилища
// ============================================================

cfg_bool   cfg_digital_silence_only(guid_cfg_digital_silence_only, true);
cfg_bool   cfg_gapless_mode(guid_cfg_gapless_mode, false);
cfg_int    cfg_silence_threshold_db(guid_cfg_silence_threshold_db, -60);
cfg_int    cfg_min_pause_ms(guid_cfg_min_pause_ms, 500);
cfg_int    cfg_pregap_ms(guid_cfg_pregap_ms, 200);
cfg_string cfg_cue_filename_template(guid_cfg_cue_filename_template, "%filename%");
cfg_bool   cfg_add_to_playlist(guid_cfg_add_to_playlist, true);

// ============================================================
//  Вспомогательные функции
// ============================================================

int get_effective_pregap_ms() {
    int pregap = (int)cfg_pregap_ms.get();
    int min_pause = (int)cfg_min_pause_ms.get();
    if (pregap > min_pause) pregap = min_pause;
    if (pregap < 0) pregap = 0;
    return pregap;
}

bool is_digital_silence_only() {
    return cfg_digital_silence_only.get();
}

bool is_gapless_mode() {
    return cfg_gapless_mode.get();
}

double get_silence_threshold_linear() {
    if (is_digital_silence_only() && !is_gapless_mode()) {
        return 0.0;
    }

    int db = (int)cfg_silence_threshold_db.get();
    if (db > 0) db = 0;
    if (db < -120) db = -120;

    return pow(10.0, (double)db / 20.0);
}

void enforce_pregap_rule() {
    int pregap = (int)cfg_pregap_ms.get();
    int min_pause = (int)cfg_min_pause_ms.get();

    if (min_pause < 1) min_pause = 1;
    if (min_pause > 60000) min_pause = 60000;

    if (pregap < 0) pregap = 0;
    if (pregap > 60000) pregap = 60000;

    if (pregap > min_pause) {
        pregap = min_pause;
    }

    if ((int)cfg_pregap_ms.get() != pregap) {
        cfg_pregap_ms = pregap;
    }
    if ((int)cfg_min_pause_ms.get() != min_pause) {
        cfg_min_pause_ms = min_pause;
    }
}

// ============================================================
//  cfg_window_rect
// ============================================================

cfg_window_rect::cfg_window_rect() : cfg_var(guid_cfg_window_rect) {
    m_rect.left = 100;
    m_rect.top = 100;
    m_rect.right = 720;
    m_rect.bottom = 480;
}

void cfg_window_rect::get_data_raw(stream_writer* p_stream, abort_callback& p_abort) {
    p_stream->write_lendian_t((t_int32)m_rect.left, p_abort);
    p_stream->write_lendian_t((t_int32)m_rect.top, p_abort);
    p_stream->write_lendian_t((t_int32)m_rect.right, p_abort);
    p_stream->write_lendian_t((t_int32)m_rect.bottom, p_abort);
}

void cfg_window_rect::set_data_raw(stream_reader* p_stream, t_size /*size*/, abort_callback& p_abort) {
    t_int32 l = 100, t = 100, r = 720, b = 480;
    p_stream->read_lendian_t(l, p_abort);
    p_stream->read_lendian_t(t, p_abort);
    p_stream->read_lendian_t(r, p_abort);
    p_stream->read_lendian_t(b, p_abort);
    m_rect.left = l;
    m_rect.top = t;
    m_rect.right = r;
    m_rect.bottom = b;
}

cfg_window_rect cfg_results_window_rect;

// ============================================================
//  cfg_column_widths
// ============================================================

cfg_column_widths::cfg_column_widths() : cfg_var(guid_cfg_column_widths) {
    m_widths[0] = 35;
    m_widths[1] = 260;
    m_widths[2] = 110;
    m_widths[3] = 110;
    m_widths[4] = 80;
}

void cfg_column_widths::get_data_raw(stream_writer* p_stream, abort_callback& p_abort) {
    for (int i = 0; i < kCuePauseColumnCount; ++i) {
        p_stream->write_lendian_t((t_int32)m_widths[i], p_abort);
    }
}

void cfg_column_widths::set_data_raw(stream_reader* p_stream, t_size /*size*/, abort_callback& p_abort) {
    t_int32 w[kCuePauseColumnCount];
    for (int i = 0; i < kCuePauseColumnCount; ++i) {
        w[i] = m_widths[i];
        p_stream->read_lendian_t(w[i], p_abort);
    }
    for (int i = 0; i < kCuePauseColumnCount; ++i) {
        m_widths[i] = w[i];
    }
}

cfg_column_widths cfg_results_column_widths;