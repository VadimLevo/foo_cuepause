#include "foobar2000.h"
#include "cfg.h"

DECLARE_COMPONENT_VERSION(
    "CUE from Pause",
    "1.0",
    "Exports CUE sheets based on audio pauses."
);

// Очистка GDI-кистей тёмной темы — реализованы в dialog.cpp и preferences.cpp.
void cuepause_cleanup_dark_brushes();
void cuepause_prefs_cleanup_dark_brushes();

class my_initquit : public initquit {
public:
    void on_init() override {
        console::print("foo_cuepause: loaded");
    }
    void on_quit() override {
        cuepause_cleanup_dark_brushes();
        cuepause_prefs_cleanup_dark_brushes();
    }
};

static initquit_factory_t<my_initquit> g_my_initquit_factory;