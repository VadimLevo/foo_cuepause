#pragma once

#include "foobar2000.h"
#include "scan.h"
#include <atlbase.h>
#include <atlapp.h>
#include <atlctrls.h>
#include <atltypes.h>

// Показывает диалог с результатами сканирования.
// results — результаты скана, должен жить до закрытия диалога
// (кладём копию внутрь).
void show_cuepause_dialog(const pfc::list_t<scan_result>& results);