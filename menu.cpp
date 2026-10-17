#include "foobar2000.h"
#include "scan.h"
#include "dialog.h"

// ============================================================
//  Контекстное меню: Create CUE from pauses
// ============================================================

class menu_cuepause : public contextmenu_item_simple {
public:
    enum { item_id_create_cue = 0 };

    unsigned get_num_items() override { return 1; }

    void get_item_name(unsigned p_index, pfc::string_base& p_out) override {
        p_out = "Create CUE from pauses";
    }

    void get_item_default_path(unsigned p_index, pfc::string_base& p_out) override {
        p_out = "Utilities";
    }

    GUID get_item_guid(unsigned p_index) override {
        static const GUID g =
        { 0xaa11c020, 0x1111, 0x4a01, { 0x90, 0x01, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 } };
        return g;
    }

    bool get_item_description(unsigned p_index, pfc::string_base& p_out) override {
        p_out = "Scans selected tracks for silent pauses and exports a CUE sheet.";
        return true;
    }

    void context_command(unsigned p_index, metadb_handle_list_cref p_data,
        const GUID& p_caller) override
    {
        if (p_data.get_count() == 0) {
            popup_message::g_show("No tracks selected.", "CUE from Pause");
            return;
        }

        auto task = fb2k::service_new<cuepause_scan_task>(p_data);
        task->set_done_callback([](const pfc::list_t<scan_result>& results) {
            show_cuepause_dialog(results);
            });

        threaded_process::g_run_modeless(
            task,
            threaded_process::flag_show_progress
            | threaded_process::flag_show_progress_dual
            | threaded_process::flag_show_item
            | threaded_process::flag_show_abort,
            core_api::get_main_window(),
            "Scanning for pauses");
    }
};

static contextmenu_item_factory_t<menu_cuepause> g_menu_cuepause_factory;