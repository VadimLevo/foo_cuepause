#include "scan.h"
#include "cfg.h"

#include <cmath>
#include <vector>

static const double kMinEdgeSec = 0.2;
static const double kGaplessRmsWindowSec = 0.050;
static const double kGaplessMinMarkerGapSec = 2.0;       // для оставшихся меток
static const double kGaplessMinDistanceToNext = 30.0;    // для gapless-меток
static const double kGaplessGrowthRatio = 1.5;

struct rms_point {
    t_uint64 sample_pos;
    double   rms;
    bool     inside_pause;
};

struct pause_span {
    t_uint64 start;
    t_uint64 end;
};

// Вспомогательная структура: метка + флаг «gapless».
struct tagged_marker {
    pause_marker m;
    bool is_gapless;
};

cuepause_scan_task::cuepause_scan_task(metadb_handle_list_cref tracks)
    : m_tracks(tracks) {
}

void cuepause_scan_task::run(threaded_process_status& p_status, abort_callback& p_abort) {
    const t_size total = m_tracks.get_count();
    m_results.remove_all();

    for (t_size i = 0; i < total; ++i) {
        p_abort.check();
        metadb_handle_ptr track = m_tracks[i];
        scan_result r;
        r.path = track->get_path();
        p_status.set_item_path(r.path);
        p_status.set_progress(i, total);
        scan_one_track(track, r, p_status, p_abort);
        m_results.add_item(r);
    }
    p_status.set_progress(total, total);
}

void cuepause_scan_task::on_done(HWND /*p_wnd*/, bool p_was_aborted) {
    if (p_was_aborted) return;
    if (m_done_cb) m_done_cb(m_results);
}

void cuepause_scan_task::scan_one_track(
    metadb_handle_ptr track, scan_result& out,
    threaded_process_status& p_status, abort_callback& p_abort)
{
    if (is_gapless_mode()) {
        scan_one_track_gapless(track, out, p_status, p_abort);
    }
    else {
        scan_one_track_normal(track, out, p_status, p_abort);
    }
}

// ============================================================
//  Обычный режим
// ============================================================

void cuepause_scan_task::scan_one_track_normal(
    metadb_handle_ptr track, scan_result& out,
    threaded_process_status& p_status, abort_callback& p_abort)
{
    const bool   digital_only = is_digital_silence_only();
    const double threshold = get_silence_threshold_linear();
    const double min_pause_ms = (double)cfg_min_pause_ms.get();
    const double pregap_ms = (double)get_effective_pregap_ms();

    double meta_length = 0.0;
    {
        file_info_impl info;
        if (track->get_info(info)) {
            meta_length = info.get_length();
            out.length_seconds = meta_length;
        }
    }

    try {
        input_decoder::ptr decoder;
        input_entry::g_open_for_decoding(decoder, nullptr, out.path, p_abort, false);
        decoder->initialize(0, input_flag_no_seeking, p_abort);

        audio_chunk_impl_t<> chunk;
        t_uint64 sample_pos = 0;
        bool in_pause = false;
        t_uint64 pause_start = 0;
        bool seen_loud = false;
        bool first_marker_done = false;
        double sample_rate = 0.0;
        t_size channels = 0;
        t_uint64 min_pause_samples = 0;
        t_uint64 pregap_samples = 0;
        double decoded_seconds = 0.0;

        while (decoder->run(chunk, p_abort)) {
            p_abort.check();
            if (meta_length > 0.0) {
                decoded_seconds += chunk.get_duration();
                if (decoded_seconds > meta_length) decoded_seconds = meta_length;
                p_status.set_progress_secondary_float(decoded_seconds / meta_length);
            }

            const audio_sample* data = chunk.get_data();
            const t_size nch = chunk.get_channels();
            const t_size nsamp = chunk.get_sample_count();
            const double srate = chunk.get_srate();

            if (srate != sample_rate) {
                sample_rate = srate;
                channels = nch;
                min_pause_samples = (t_uint64)(min_pause_ms * sample_rate / 1000.0);
                pregap_samples = (t_uint64)(pregap_ms * sample_rate / 1000.0);
                out.sample_rate = sample_rate;
                out.channels = channels;
            }

            for (t_size s = 0; s < nsamp; ++s) {
                const audio_sample* sp = data + (s * nch);
                bool silent = true;
                if (digital_only) {
                    for (t_size ch = 0; ch < nch; ++ch) {
                        if (sp[ch] != 0.0f) { silent = false; break; }
                    }
                }
                else {
                    for (t_size ch = 0; ch < nch; ++ch) {
                        if (std::fabs((double)sp[ch]) > threshold) { silent = false; break; }
                    }
                }

                if (silent) {
                    if (!in_pause) { in_pause = true; pause_start = sample_pos; }
                    ++sample_pos;
                    continue;
                }

                if (!seen_loud) {
                    seen_loud = true;
                    if (in_pause && pause_start == 0) {
                        const double t0 = (double)sample_pos / sample_rate;
                        pause_marker m;
                        m.manual = false;
                        m.pause_start_seconds = 0.0;
                        m.pause_end_seconds = t0;
                        if (t0 < kMinEdgeSec) {
                            m.marker_seconds = 0.0;
                        }
                        else {
                            double effective = (double)pregap_samples / sample_rate;
                            if (effective < kMinEdgeSec) effective = kMinEdgeSec;
                            if (t0 >= effective) m.marker_seconds = t0 - effective;
                            else m.marker_seconds = 0.0;
                        }
                        out.markers.add_item(m);
                        first_marker_done = true;
                    }
                    else {
                        pause_marker m;
                        m.manual = false;
                        m.pause_start_seconds = 0.0;
                        m.pause_end_seconds = 0.0;
                        m.marker_seconds = 0.0;
                        out.markers.add_item(m);
                        first_marker_done = true;
                    }
                    in_pause = false;
                    ++sample_pos;
                    continue;
                }

                if (in_pause) {
                    const t_uint64 pause_end = sample_pos;
                    const t_uint64 pause_len = pause_end - pause_start;
                    if (!first_marker_done) first_marker_done = true;
                    if (pause_len >= min_pause_samples) {
                        pause_marker m;
                        m.manual = false;
                        m.pause_start_seconds = (double)pause_start / sample_rate;
                        m.pause_end_seconds = (double)pause_end / sample_rate;
                        t_uint64 marker_pos = 0;
                        if (pause_end >= pregap_samples) marker_pos = pause_end - pregap_samples;
                        if (marker_pos < pause_start) marker_pos = pause_start;
                        m.marker_seconds = (double)marker_pos / sample_rate;
                        out.markers.add_item(m);
                    }
                    in_pause = false;
                }
                ++sample_pos;
            }
        }
        out.ok = true;
    }
    catch (const exception_aborted&) { throw; }
    catch (const std::exception& e) {
        out.ok = false;
        pfc::string8 err = e.what();
        out.error_text = err;
        console::print(pfc::format("foo_cuepause: error decoding ", out.path, ": ", err));
    }
}

// ============================================================
//  Gapless mode
// ============================================================

void cuepause_scan_task::scan_one_track_gapless(
    metadb_handle_ptr track, scan_result& out,
    threaded_process_status& p_status, abort_callback& p_abort)
{
    const double threshold = get_silence_threshold_linear();
    const double min_pause_ms = (double)cfg_min_pause_ms.get();
    const double pregap_ms = (double)get_effective_pregap_ms();

    double meta_length = 0.0;
    {
        file_info_impl info;
        if (track->get_info(info)) {
            meta_length = info.get_length();
            out.length_seconds = meta_length;
        }
    }

    try {
        input_decoder::ptr decoder;
        input_entry::g_open_for_decoding(decoder, nullptr, out.path, p_abort, false);
        decoder->initialize(0, input_flag_no_seeking, p_abort);

        audio_chunk_impl_t<> chunk;

        double sample_rate = 0.0;
        t_size channels = 0;
        t_uint64 min_pause_samples = 0;
        t_uint64 pregap_samples = 0;
        t_uint64 window_samples = 0;

        pfc::list_t<rms_point> env;
        double acc_sum_sq = 0.0;
        t_uint64 acc_count = 0;
        t_uint64 acc_start = 0;

        bool in_pause = false;
        t_uint64 pause_start = 0;
        pfc::list_t<pause_span> pause_spans;

        bool seen_loud_first = false;
        double first_loud_sec = 0.0;

        t_uint64 sample_pos = 0;
        double decoded_seconds = 0.0;

        while (decoder->run(chunk, p_abort)) {
            p_abort.check();
            if (meta_length > 0.0) {
                decoded_seconds += chunk.get_duration();
                if (decoded_seconds > meta_length) decoded_seconds = meta_length;
                p_status.set_progress_secondary_float(decoded_seconds / meta_length);
            }

            const audio_sample* data = chunk.get_data();
            const t_size nch = chunk.get_channels();
            const t_size nsamp = chunk.get_sample_count();
            const double srate = chunk.get_srate();

            if (srate != sample_rate) {
                sample_rate = srate;
                channels = nch;
                min_pause_samples = (t_uint64)(min_pause_ms * sample_rate / 1000.0);
                pregap_samples = (t_uint64)(pregap_ms * sample_rate / 1000.0);
                window_samples = (t_uint64)(kGaplessRmsWindowSec * sample_rate);
                if (window_samples < 1) window_samples = 1;
                out.sample_rate = sample_rate;
                out.channels = channels;
            }

            for (t_size s = 0; s < nsamp; ++s) {
                const audio_sample* sp = data + (s * nch);
                bool silent = true;
                for (t_size ch = 0; ch < nch; ++ch) {
                    if (std::fabs((double)sp[ch]) > threshold) { silent = false; break; }
                }

                if (!seen_loud_first && !silent) {
                    seen_loud_first = true;
                    first_loud_sec = (double)sample_pos / sample_rate;
                }

                double sum_sq = 0.0;
                for (t_size ch = 0; ch < nch; ++ch) {
                    double v = (double)sp[ch];
                    sum_sq += v * v;
                }
                sum_sq /= (double)nch;

                if (acc_count == 0) acc_start = sample_pos;
                acc_sum_sq += sum_sq;
                ++acc_count;

                if (acc_count >= window_samples) {
                    rms_point p;
                    p.sample_pos = acc_start;
                    p.rms = std::sqrt(acc_sum_sq / (double)acc_count);
                    p.inside_pause = false;
                    env.add_item(p);
                    acc_sum_sq = 0.0;
                    acc_count = 0;
                }

                if (silent) {
                    if (!in_pause) { in_pause = true; pause_start = sample_pos; }
                }
                else {
                    if (in_pause) {
                        t_uint64 pause_end = sample_pos;
                        t_uint64 pause_len = pause_end - pause_start;
                        if (pause_len >= min_pause_samples) {
                            pause_span ps;
                            ps.start = pause_start;
                            ps.end = pause_end;
                            pause_spans.add_item(ps);
                        }
                        in_pause = false;
                    }
                }
                ++sample_pos;
            }
        }

        if (acc_count > 0) {
            rms_point p;
            p.sample_pos = acc_start;
            p.rms = std::sqrt(acc_sum_sq / (double)acc_count);
            p.inside_pause = false;
            env.add_item(p);
        }

        if (in_pause) {
            t_uint64 pause_len = sample_pos - pause_start;
            if (pause_len >= min_pause_samples) {
                pause_span ps;
                ps.start = pause_start;
                ps.end = sample_pos;
                pause_spans.add_item(ps);
            }
        }

        // Пометка окон, пересекающихся с паузой.
        for (t_size i = 0; i < env.get_count(); ++i) {
            t_uint64 win_start = env[i].sample_pos;
            t_uint64 win_end = win_start + window_samples;
            for (t_size k = 0; k < pause_spans.get_count(); ++k) {
                if (win_start < pause_spans[k].end && win_end > pause_spans[k].start) {
                    env[i].inside_pause = true;
                    break;
                }
            }
        }

        pfc::list_t<tagged_marker> tagged;

        // Первая метка файла.
        {
            const double t0 = first_loud_sec;
            pause_marker m;
            m.manual = false;
            m.pause_start_seconds = 0.0;
            m.pause_end_seconds = t0;
            if (t0 < kMinEdgeSec) {
                m.marker_seconds = 0.0;
            }
            else {
                double effective = pregap_ms / 1000.0;
                if (effective < kMinEdgeSec) effective = kMinEdgeSec;
                if (t0 >= effective) m.marker_seconds = t0 - effective;
                else m.marker_seconds = 0.0;
            }
            tagged_marker t;
            t.m = m;
            t.is_gapless = false;
            tagged.add_item(t);
        }

        // Метки из пауз.
        for (t_size i = 0; i < pause_spans.get_count(); ++i) {
            t_uint64 pause_start_pos = pause_spans[i].start;
            t_uint64 pause_end_pos = pause_spans[i].end;
            if (pause_start_pos == 0) continue;
            t_uint64 marker_pos = 0;
            if (pause_end_pos >= pregap_samples) marker_pos = pause_end_pos - pregap_samples;
            if (marker_pos < pause_start_pos) marker_pos = pause_start_pos;
            pause_marker m;
            m.manual = false;
            m.marker_seconds = (double)marker_pos / sample_rate;
            m.pause_start_seconds = (double)pause_start_pos / sample_rate;
            m.pause_end_seconds = (double)pause_end_pos / sample_rate;
            tagged_marker t;
            t.m = m;
            t.is_gapless = false;
            tagged.add_item(t);
        }

        // Gapless-кандидаты.
        if (env.get_count() >= 3 && sample_rate > 0.0) {
            for (t_size i = 1; i + 1 < env.get_count(); ++i) {
                if (env[i].inside_pause) continue;
                if (env[i - 1].inside_pause && env[i + 1].inside_pause) continue;
                const double r_prev = env[i - 1].rms;
                const double r_cur = env[i].rms;
                const double r_next = env[i + 1].rms;
                if (r_cur >= threshold) continue;
                if (!(r_cur <= r_prev && r_cur <= r_next)) continue;
                if (r_next < r_cur * kGaplessGrowthRatio) continue;
                t_uint64 marker_pos = env[i].sample_pos;
                if (marker_pos >= pregap_samples) marker_pos -= pregap_samples;
                else marker_pos = 0;
                pause_marker m;
                m.manual = false;
                m.marker_seconds = (double)marker_pos / sample_rate;
                m.pause_start_seconds = m.marker_seconds;
                m.pause_end_seconds = m.marker_seconds;
                tagged_marker t;
                t.m = m;
                t.is_gapless = true;
                tagged.add_item(t);
            }
        }

        // Сортировка по времени.
        const t_size n = tagged.get_count();
        for (t_size a = 1; a < n; ++a) {
            tagged_marker key = tagged[a];
            t_size b = a;
            while (b > 0 && tagged[b - 1].m.marker_seconds > key.m.marker_seconds) {
                tagged[b] = tagged[b - 1];
                --b;
            }
            tagged[b] = key;
        }

        // Фильтр 1: gapless-метка не может быть ближе 30 сек к следующей.
        if (tagged.get_count() > 1) {
            pfc::list_t<tagged_marker> filtered;
            for (t_size i = 0; i < tagged.get_count(); ++i) {
                if (tagged[i].is_gapless && i + 1 < tagged.get_count()) {
                    const double gap = tagged[i + 1].m.marker_seconds
                        - tagged[i].m.marker_seconds;
                    if (gap < kGaplessMinDistanceToNext) {
                        continue;
                    }
                }
                filtered.add_item(tagged[i]);
            }
            tagged.remove_all();
            for (t_size k = 0; k < filtered.get_count(); ++k) {
                tagged.add_item(filtered[k]);
            }
        }

        // Фильтр 2: удалить, если до предыдущей < 2 сек.
        if (tagged.get_count() > 1) {
            pfc::list_t<tagged_marker> filtered;
            filtered.add_item(tagged[0]);
            for (t_size i = 1; i < tagged.get_count(); ++i) {
                const double prev = filtered[filtered.get_count() - 1].m.marker_seconds;
                const double cur = tagged[i].m.marker_seconds;
                if (cur - prev < kGaplessMinMarkerGapSec) continue;
                filtered.add_item(tagged[i]);
            }
            tagged.remove_all();
            for (t_size k = 0; k < filtered.get_count(); ++k) {
                tagged.add_item(filtered[k]);
            }
        }

        // Фильтр 3: отбросить метку, если до конца файла < min_pause.
        if (tagged.get_count() > 0 && out.length_seconds > 0.0) {
            while (tagged.get_count() > 0) {
                double last = tagged[tagged.get_count() - 1].m.marker_seconds;
                if (out.length_seconds - last < min_pause_ms / 1000.0) {
                    tagged.remove_by_idx(tagged.get_count() - 1);
                }
                else {
                    break;
                }
            }
        }

        for (t_size i = 0; i < tagged.get_count(); ++i) {
            pause_marker m = tagged[i].m;
            out.markers.add_item(m);
        }

        out.ok = true;
    }
    catch (const exception_aborted&) { throw; }
    catch (const std::exception& e) {
        out.ok = false;
        pfc::string8 err = e.what();
        out.error_text = err;
        console::print(pfc::format("foo_cuepause: error decoding ", out.path, ": ", err));
    }
}