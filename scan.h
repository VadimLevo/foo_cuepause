#pragma once

#include "foobar2000.h"

struct pause_marker {
    double marker_seconds;
    double pause_start_seconds;
    double pause_end_seconds;
    bool   manual;
};

struct scan_result {
    pfc::string8 path;
    double length_seconds;
    double sample_rate;
    t_size channels;
    pfc::list_t<pause_marker> markers;
    bool ok;
    pfc::string8 error_text;

    scan_result() : length_seconds(0), sample_rate(0), channels(0), ok(true) {}
};

class cuepause_scan_task : public threaded_process_callback {
public:
    cuepause_scan_task(metadb_handle_list_cref tracks);

    void run(threaded_process_status& p_status, abort_callback& p_abort) override;
    void on_done(HWND p_wnd, bool p_was_aborted) override;

    const pfc::list_t<scan_result>& get_results() const { return m_results; }

    typedef void (*done_callback_t)(const pfc::list_t<scan_result>& results);
    void set_done_callback(done_callback_t cb) { m_done_cb = cb; }

private:
    void scan_one_track(
        metadb_handle_ptr track,
        scan_result& out,
        threaded_process_status& p_status,
        abort_callback& p_abort);

    void scan_one_track_normal(
        metadb_handle_ptr track,
        scan_result& out,
        threaded_process_status& p_status,
        abort_callback& p_abort);

    void scan_one_track_gapless(
        metadb_handle_ptr track,
        scan_result& out,
        threaded_process_status& p_status,
        abort_callback& p_abort);

    metadb_handle_list m_tracks;
    pfc::list_t<scan_result> m_results;
    done_callback_t m_done_cb = nullptr;
};