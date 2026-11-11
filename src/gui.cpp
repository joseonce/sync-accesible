#include "gui.h"
#include "models.h"
#include "json_helper.h"
#include "sync_engine.h"
#include "scheduler.h"
#include "speech.h"
#include "dialogs.h"
#include "logger.h"

#include <commctrl.h>
#include <shellapi.h>
#include <thread>
#include <atomic>
#include <sstream>
#include <iomanip>
#include <vector>
#include <filesystem>
#include <algorithm>

#define WM_TRAYICON (WM_USER + 1)
#define WM_SYNC_DONE (WM_USER + 2)
#define WM_COMPARE_DONE (WM_USER + 3)
#define WM_SYNC_PROGRESS (WM_USER + 4)
#define WM_SEQ_STEP_DONE (WM_USER + 5)
#define WM_SEQ_ALL_DONE  (WM_USER + 6)

#define ID_TRAY_RESTORE 3001
#define ID_TRAY_SYNC_ALL 3002
#define ID_TRAY_PAUSE 3003
#define ID_TRAY_EXIT 3004

// Command IDs
#define IDM_PROJECT_NEW       2001
#define IDM_PROJECT_OPEN      2002
#define IDM_PROJECT_SAVE      2003
#define IDM_PROJECT_SAVEAS    2004
#define IDM_PROJECT_EXIT      2005
#define IDM_PROJECT_EXPORT_TXT 2006

#define IDM_SYNC_COMPARE      2010
#define IDM_SYNC_ONE          2011
#define IDM_SYNC_SELECTED_SEQ 2012
#define IDM_SYNC_ALL_SEQ      2013
#define IDM_SYNC_BATCH_QUEUE  2014

#define IDM_EDIT_ADD_PAIR     2020
#define IDM_EDIT_EDIT_PAIR    2021
#define IDM_EDIT_DEL_PAIR     2022
#define IDM_EDIT_INVERT_PAIR  2024
#define IDM_TOOLS_SETTINGS    2023
#define IDM_TOOLS_VIEW_LOG    2025

#define IDM_PAIR_OPEN_LEFT_EXPLORER   2030
#define IDM_PAIR_OPEN_RIGHT_EXPLORER  2031
#define IDM_PREVIEW_OPEN_LEFT         2032
#define IDM_PREVIEW_OPEN_RIGHT        2033
#define IDM_PREVIEW_OPEN_LEFT_DIR     2034
#define IDM_PREVIEW_OPEN_RIGHT_DIR    2035
#define IDM_PREVIEW_COPY_PATH         2036

#define IDM_VIEW_TAB_PAIRS            2040
#define IDM_VIEW_TAB_PREVIEW          2041
#define IDM_VIEW_TAB_NEXT             2042
#define IDM_VIEW_FILTER_CHANGES       2043
#define IDM_VIEW_SORT_ACTION          2044
#define IDM_VIEW_SORT_NAME            2045
#define IDM_VIEW_SORT_SIZE            2046

#define IDM_HELP_MANUAL               2050
#define IDM_HELP_ABOUT                2051
#define IDM_HELP_SHORTCUTS            2052

#define IDC_BTN_COMPARE       1001
#define IDC_BTN_SYNC          1002
#define IDC_BTN_SYNC_SEQ      1003
#define IDC_BTN_SYNC_ALL      1004
#define IDC_BTN_BATCH_QUEUE   1005
#define IDC_BTN_OPEN_PRJ      1006
#define IDC_BTN_SAVE_PRJ      1007
#define IDC_BTN_ADD_PAIR      1008
#define IDC_BTN_EDIT_PAIR     1009
#define IDC_BTN_DEL_PAIR      1010
#define IDC_BTN_SETTINGS      1011

#define IDC_TAB               1020
#define IDC_LIST_PAIRS        1021
#define IDC_LIST_PREVIEW      1022
#define IDC_STATUSBAR         1023
#define IDC_PROGRESSBAR       1024

namespace Gui {

enum class PreviewSortOrder {
    ByAction,   // Conflictos, copias, eliminaciones primero, idénticos al final
    ByName,     // Alfabético por ruta
    BySize      // Por tamaño
};

static HWND g_hwndMain = nullptr;
static HWND g_hTab = nullptr;
static HWND g_hListPairs = nullptr;
static HWND g_hListPreview = nullptr;
static HWND g_hStatusBar = nullptr;
static HWND g_hProgressBar = nullptr;
static int g_lastProgressMilestone = 0;
static HFONT g_hFont = nullptr;
static NOTIFYICONDATAW g_nid = { sizeof(NOTIFYICONDATAW) };
static UINT g_uTaskbarRestartMsg = 0;

static PreviewSortOrder g_previewSortOrder = PreviewSortOrder::ByAction;
static bool g_filterShowOnlyChanges = true;
static std::vector<size_t> g_previewDisplayedIndices;

static AppSettings g_settings;
static SyncProject g_currentProject;
static ComparisonSummary g_currentSummary;
static std::wstring g_configPath;
static std::atomic<bool> g_isBusy(false);
static std::atomic<bool> g_cancelRequested(false);

static HFONT CreateAppFont() {
    NONCLIENTMETRICSW ncm = { sizeof(NONCLIENTMETRICSW) };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    return CreateFontIndirectW(&ncm.lfMessageFont);
}

static std::wstring FormatFileSize(uint64_t bytes) {
    if (bytes < 1024) return std::to_wstring(bytes) + L" B";
    if (bytes < 1024 * 1024) {
        wchar_t buf[32];
        swprintf_s(buf, L"%.1f KB", (double)bytes / 1024.0);
        return buf;
    }
    if (bytes < 1024ULL * 1024 * 1024) {
        wchar_t buf[32];
        swprintf_s(buf, L"%.1f MB", (double)bytes / (1024.0 * 1024.0));
        return buf;
    }
    wchar_t buf[32];
    swprintf_s(buf, L"%.2f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    return buf;
}

// -------------------------------------------------------------
// VIRTUAL COLUMN NAVIGATION (ACCESSIBILITY: LEFT/RIGHT ARROWS)
// -------------------------------------------------------------
struct VirtualListState {
    int currentCol = 0;
    int lastFocusedRow = -1;
};

static void AnnounceVirtualColumn(HWND hList, int rowIndex, int colIndex, bool isBoundary, bool isStart) {
    if (rowIndex < 0 || colIndex < 0) return;

    HWND hHeader = ListView_GetHeader(hList);
    int colCount = hHeader ? Header_GetItemCount(hHeader) : 0;
    if (colCount <= 0) {
        LVCOLUMNW testCol = {0};
        testCol.mask = LVCF_WIDTH;
        while (ListView_GetColumn(hList, colCount, &testCol)) {
            colCount++;
        }
    }
    if (colCount <= 0 || colIndex >= colCount) return;

    wchar_t headerTitle[256] = {0};
    if (hHeader) {
        HDITEMW hdi = {0};
        hdi.mask = HDI_TEXT;
        hdi.pszText = headerTitle;
        hdi.cchTextMax = 256;
        Header_GetItem(hHeader, colIndex, &hdi);
    }
    if (headerTitle[0] == L'\0') {
        LVCOLUMNW lvc = {0};
        lvc.mask = LVCF_TEXT;
        lvc.pszText = headerTitle;
        lvc.cchTextMax = 256;
        ListView_GetColumn(hList, colIndex, &lvc);
    }
    if (headerTitle[0] == L'\0') {
        swprintf_s(headerTitle, L"Columna %d", colIndex + 1);
    }

    wchar_t cellValue[1024] = {0};
    ListView_GetItemText(hList, rowIndex, colIndex, cellValue, 1024);

    std::wstring valStr = cellValue;
    if (valStr.empty()) {
        valStr = L"vacío";
    }

    std::wstring speech;
    if (isBoundary) {
        if (isStart) {
            speech = L"Inicio: " + std::wstring(headerTitle) + L": " + valStr;
        } else {
            speech = L"Fin: " + std::wstring(headerTitle) + L": " + valStr;
        }
    } else {
        speech = std::wstring(headerTitle) + L": " + valStr;
    }

    // Ensure cell is visible horizontally if listview is wide
    RECT rcSub;
    if (ListView_GetSubItemRect(hList, rowIndex, colIndex, LVIR_BOUNDS, &rcSub)) {
        RECT rcClient;
        GetClientRect(hList, &rcClient);
        if (rcSub.left < 0) {
            ListView_Scroll(hList, rcSub.left, 0);
        } else if (rcSub.right > rcClient.right) {
            ListView_Scroll(hList, rcSub.right - rcClient.right + 20, 0);
        }
    }

    if (g_hStatusBar) {
        SendMessageW(g_hStatusBar, SB_SETTEXTW, 0, (LPARAM)speech.c_str());
    }

    Speech::Speak(speech, true);
}

static LRESULT CALLBACK VirtualColumnSubclassProc(
    HWND hWnd,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR uIdSubclass,
    DWORD_PTR dwRefData
) {
    VirtualListState* pState = (VirtualListState*)dwRefData;

    switch (uMsg) {
        case WM_GETDLGCODE: {
            LRESULT res = DefSubclassProc(hWnd, uMsg, wParam, lParam);
            res &= ~(DLGC_WANTTAB | DLGC_WANTALLKEYS);
            return res | DLGC_WANTARROWS;
        }

        case WM_KEYDOWN: {
            bool ctrlDown  = (GetKeyState(VK_CONTROL) < 0);
            bool altDown   = (GetKeyState(VK_MENU) < 0);
            bool shiftDown = (GetKeyState(VK_SHIFT) < 0);

            if (!ctrlDown && !altDown && !shiftDown) {
                if (wParam == VK_LEFT || wParam == VK_RIGHT) {
                    int focusedRow = ListView_GetNextItem(hWnd, -1, LVNI_FOCUSED);
                    if (focusedRow == -1) {
                        focusedRow = ListView_GetNextItem(hWnd, -1, LVNI_SELECTED);
                    }
                    if (focusedRow == -1 && ListView_GetItemCount(hWnd) > 0) {
                        focusedRow = 0;
                        ListView_SetItemState(hWnd, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                    }

                    if (focusedRow != -1) {
                        HWND hHeader = ListView_GetHeader(hWnd);
                        int colCount = hHeader ? Header_GetItemCount(hHeader) : 0;
                        if (colCount <= 0) {
                            LVCOLUMNW testCol = {0};
                            testCol.mask = LVCF_WIDTH;
                            while (ListView_GetColumn(hWnd, colCount, &testCol)) {
                                colCount++;
                            }
                        }

                        if (colCount > 0) {
                            if (focusedRow != pState->lastFocusedRow) {
                                pState->currentCol = 0;
                                pState->lastFocusedRow = focusedRow;
                            }

                            if (wParam == VK_RIGHT) {
                                if (pState->currentCol < colCount - 1) {
                                    pState->currentCol++;
                                    AnnounceVirtualColumn(hWnd, focusedRow, pState->currentCol, false, false);
                                } else {
                                    AnnounceVirtualColumn(hWnd, focusedRow, pState->currentCol, true, false);
                                }
                                return 0; // Handled: row focus does not move
                            } else if (wParam == VK_LEFT) {
                                if (pState->currentCol > 0) {
                                    pState->currentCol--;
                                    AnnounceVirtualColumn(hWnd, focusedRow, pState->currentCol, false, true);
                                } else {
                                    AnnounceVirtualColumn(hWnd, focusedRow, pState->currentCol, true, true);
                                }
                                return 0; // Handled: row focus does not move
                            }
                        }
                    }
                } else if (wParam == VK_UP || wParam == VK_DOWN || wParam == VK_PRIOR || wParam == VK_NEXT || wParam == VK_HOME || wParam == VK_END) {
                    pState->currentCol = 0;
                }
            }
            break;
        }

        case WM_NCDESTROY: {
            delete pState;
            RemoveWindowSubclass(hWnd, VirtualColumnSubclassProc, uIdSubclass);
            break;
        }
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static void EnableVirtualColumnNavigation(HWND hList, UINT_PTR uIdSubclass) {
    VirtualListState* pState = new VirtualListState();
    SetWindowSubclass(hList, VirtualColumnSubclassProc, uIdSubclass, (DWORD_PTR)pState);
}

static void UpdateWindowTitle() {
    std::wstring title = L"SyncAccesible (C++) - [";
    title += g_currentProject.name;
    if (!g_currentProject.file_path.empty()) {
        title += L" - " + g_currentProject.file_path;
    }
    title += L"]";
    SetWindowTextW(g_hwndMain, title.c_str());
}

static void SaveAppConfig() {
    JsonHelper::SaveToFile(g_configPath, g_settings, g_currentProject);
    Scheduler::NotifyPairsUpdated();
}

static void UpdatePairsList() {
    ListView_DeleteAllItems(g_hListPairs);

    for (size_t i = 0; i < g_currentProject.pairs.size(); ++i) {
        const auto& p = g_currentProject.pairs[i];
        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = (int)i;
        lvi.iSubItem = 0;
        lvi.pszText = (LPWSTR)p.name.c_str();
        ListView_InsertItem(g_hListPairs, &lvi);

        std::wstring enStr = p.enabled ? L"Activa" : L"Desactivada";
        ListView_SetItemText(g_hListPairs, (int)i, 1, (LPWSTR)enStr.c_str());
        ListView_SetItemText(g_hListPairs, (int)i, 2, (LPWSTR)SyncModeToString(p.mode));
        ListView_SetItemText(g_hListPairs, (int)i, 3, (LPWSTR)ScheduleTypeToString(p.schedule_type));
        ListView_SetItemText(g_hListPairs, (int)i, 4, (LPWSTR)p.last_sync_status.c_str());
        ListView_SetItemText(g_hListPairs, (int)i, 5, (LPWSTR)p.last_sync_time.c_str());
        ListView_SetItemText(g_hListPairs, (int)i, 6, (LPWSTR)p.left_folder.c_str());
        ListView_SetItemText(g_hListPairs, (int)i, 7, (LPWSTR)p.right_folder.c_str());
    }

    if (!g_currentProject.pairs.empty() && ListView_GetSelectedCount(g_hListPairs) == 0) {
        ListView_SetItemState(g_hListPairs, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
}

static void UpdatePreviewList() {
    SendMessageW(g_hListPreview, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_hListPreview);

    g_previewDisplayedIndices.clear();

    for (size_t i = 0; i < g_currentSummary.items.size(); ++i) {
        const auto& item = g_currentSummary.items[i];
        if (g_filterShowOnlyChanges && item.action == ActionType::Identical) {
            continue;
        }
        g_previewDisplayedIndices.push_back(i);
    }

    // Sort displayed indices
    std::sort(g_previewDisplayedIndices.begin(), g_previewDisplayedIndices.end(), [&](size_t a, size_t b) {
        const auto& itemA = g_currentSummary.items[a];
        const auto& itemB = g_currentSummary.items[b];

        // Archivos con errores siempre se sitúan arriba del todo
        bool errA = (!itemA.error_msg.empty() || itemA.status.rfind(L"Error", 0) == 0);
        bool errB = (!itemB.error_msg.empty() || itemB.status.rfind(L"Error", 0) == 0);
        if (errA != errB) return errA;

        if (g_previewSortOrder == PreviewSortOrder::ByAction) {
            auto actionPriority = [](ActionType at) {
                switch (at) {
                    case ActionType::Conflict:    return 0;
                    case ActionType::CopyToRight: return 1;
                    case ActionType::CopyToLeft:  return 2;
                    case ActionType::DeleteRight: return 3;
                    case ActionType::DeleteLeft:  return 4;
                    case ActionType::Identical:   return 5;
                    default: return 6;
                }
            };
            int prioA = actionPriority(itemA.action);
            int prioB = actionPriority(itemB.action);
            if (prioA != prioB) return prioA < prioB;
            return itemA.rel_path < itemB.rel_path;
        } else if (g_previewSortOrder == PreviewSortOrder::ByName) {
            return itemA.rel_path < itemB.rel_path;
        } else if (g_previewSortOrder == PreviewSortOrder::BySize) {
            uint64_t sizeA = (itemA.left_size > 0) ? itemA.left_size : itemA.right_size;
            uint64_t sizeB = (itemB.left_size > 0) ? itemB.left_size : itemB.right_size;
            if (sizeA != sizeB) return sizeA > sizeB;
            return itemA.rel_path < itemB.rel_path;
        }
        return a < b;
    });

    ListView_SetItemCount(g_hListPreview, (int)g_previewDisplayedIndices.size());

    for (size_t row = 0; row < g_previewDisplayedIndices.size(); ++row) {
        size_t actualIdx = g_previewDisplayedIndices[row];
        const auto& item = g_currentSummary.items[actualIdx];

        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = (int)row;
        lvi.iSubItem = 0;
        lvi.pszText = (LPWSTR)item.rel_path.c_str();
        ListView_InsertItem(g_hListPreview, &lvi);

        std::wstring leftSizeStr = item.left_exists ? (item.is_dir ? L"<Carpeta>" : FormatFileSize(item.left_size)) : L"No existe";
        std::wstring rightSizeStr = item.right_exists ? (item.is_dir ? L"<Carpeta>" : FormatFileSize(item.right_size)) : L"No existe";
        ListView_SetItemText(g_hListPreview, (int)row, 1, (LPWSTR)ActionTypeToString(item.action));
        ListView_SetItemText(g_hListPreview, (int)row, 2, (LPWSTR)leftSizeStr.c_str());
        ListView_SetItemText(g_hListPreview, (int)row, 3, (LPWSTR)rightSizeStr.c_str());
        ListView_SetItemText(g_hListPreview, (int)row, 4, (LPWSTR)item.status.c_str());
        if (!item.error_msg.empty()) {
            ListView_SetItemText(g_hListPreview, (int)row, 4, (LPWSTR)item.error_msg.c_str());
        }
    }

    if (!g_previewDisplayedIndices.empty() && ListView_GetSelectedCount(g_hListPreview) == 0) {
        ListView_SetItemState(g_hListPreview, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }

    SendMessageW(g_hListPreview, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hListPreview, nullptr, TRUE);
}

static std::vector<int> GetSelectedPairIndices();

static void ExportSummaryReport() {
    if (g_currentSummary.items.empty()) {
        Speech::Speak(L"No hay datos de comparación o sincronización para exportar.");
        MessageBoxW(g_hwndMain, L"No hay datos de comparación o sincronización en la vista previa para exportar.", L"Exportar Informe", MB_OK | MB_ICONINFORMATION);
        return;
    }

    time_t t = time(nullptr);
    tm tmNow;
    localtime_s(&tmNow, &t);
    wchar_t defaultName[MAX_PATH];
    swprintf_s(defaultName, L"informe_sync_%04d-%02d-%02d_%02d%02d%02d.txt",
               tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday,
               tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec);

    OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
    wchar_t szFile[MAX_PATH];
    wcscpy_s(szFile, defaultName);
    ofn.hwndOwner = g_hwndMain;
    ofn.lpstrFilter = L"Archivos de texto (*.txt)\0*.txt\0Todos los archivos (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"txt";

    if (!GetSaveFileNameW(&ofn)) return;

    FILE* f = nullptr;
    _wfopen_s(&f, szFile, L"wt, ccs=UTF-8");
    if (!f) {
        LOG_ERROR(L"Error al crear archivo de informe: " + std::wstring(szFile));
        MessageBoxW(g_hwndMain, L"Error al guardar el archivo de informe.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    auto selP = GetSelectedPairIndices();
    FolderPair currentPair;
    if (!selP.empty()) {
        currentPair = g_currentProject.pairs[selP[0]];
    }

    fwprintf_s(f, L"================================================================================\n");
    fwprintf_s(f, L"INFORME DE SINCRONIZACIÓN - SYNCACCESIBLE (Versión 2.0 C++ Nativo)\n");
    fwprintf_s(f, L"Autor y Desarrollador: José Manuel Fernández Cortés\n");
    fwprintf_s(f, L"================================================================================\n\n");

    fwprintf_s(f, L"DATOS GENERALES:\n");
    fwprintf_s(f, L"- Fecha y Hora:        %04d-%02d-%02d %02d:%02d:%02d\n",
               tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday,
               tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec);
    fwprintf_s(f, L"- Proyecto:            %ls\n", g_currentProject.name.c_str());
    fwprintf_s(f, L"- Par de Carpetas:     %ls\n", g_currentSummary.pair_name.c_str());
    if (!currentPair.left_folder.empty()) {
        fwprintf_s(f, L"- Carpeta Origen:      %ls\n", currentPair.left_folder.c_str());
        fwprintf_s(f, L"- Carpeta Destino:     %ls\n", currentPair.right_folder.c_str());
        fwprintf_s(f, L"- Modo:                %ls\n", SyncModeToString(currentPair.mode));
    }
    fwprintf_s(f, L"\n");

    fwprintf_s(f, L"RESUMEN ESTADÍSTICO:\n");
    fwprintf_s(f, L"- Total de archivos analizados:   %zu\n", g_currentSummary.items.size());
    fwprintf_s(f, L"- Total archivos por sincronizar: %d (%ls)\n",
               g_currentSummary.total_files_to_sync, FormatFileSize(g_currentSummary.total_bytes_to_sync).c_str());
    fwprintf_s(f, L"  * Copiar a Destino:             %d\n", g_currentSummary.files_to_copy_right);
    fwprintf_s(f, L"  * Copiar a Origen:              %d\n", g_currentSummary.files_to_copy_left);
    fwprintf_s(f, L"  * Eliminar en Destino:          %d\n", g_currentSummary.files_to_delete_right);
    fwprintf_s(f, L"  * Eliminar en Origen:           %d\n", g_currentSummary.files_to_delete_left);
    fwprintf_s(f, L"  * Archivos idénticos:           %d\n", g_currentSummary.identical_count);
    fwprintf_s(f, L"  * Conflictos:                   %d\n", g_currentSummary.conflict_count);
    fwprintf_s(f, L"\n");

    fwprintf_s(f, L"DETALLE DE ARCHIVOS (%s):\n", g_filterShowOnlyChanges ? L"Solo cambios / acciones pendientes" : L"Todos los archivos");
    fwprintf_s(f, L"--------------------------------------------------------------------------------\n");
    fwprintf_s(f, L"%-20ls | %-12ls | %-12ls | %-14ls | %ls\n", L"ACCIÓN", L"TAM. ORIGEN", L"TAM. DESTINO", L"ESTADO", L"RUTA DEL ARCHIVO");
    fwprintf_s(f, L"--------------------------------------------------------------------------------\n");

    for (size_t idx : g_previewDisplayedIndices) {
        if (idx >= g_currentSummary.items.size()) continue;
        const auto& it = g_currentSummary.items[idx];
        std::wstring lSize = it.left_exists ? (it.is_dir ? L"<Carpeta>" : FormatFileSize(it.left_size)) : L"No existe";
        std::wstring rSize = it.right_exists ? (it.is_dir ? L"<Carpeta>" : FormatFileSize(it.right_size)) : L"No existe";
        std::wstring statusStr = it.status;
        if (!it.error_msg.empty()) statusStr += L" (" + it.error_msg + L")";

        fwprintf_s(f, L"%-20ls | %-12ls | %-12ls | %-14ls | %ls\n",
                   ActionTypeToString(it.action),
                   lSize.c_str(),
                   rSize.c_str(),
                   statusStr.c_str(),
                   it.rel_path.c_str());
    }

    fwprintf_s(f, L"--------------------------------------------------------------------------------\n");
    fwprintf_s(f, L"Fin del informe. Generado por SyncAccesible - Autor: José Manuel Fernández Cortés\n");
    fclose(f);

    LOG_INFO(L"Informe exportado con éxito a: " + std::wstring(szFile));
    Speech::Speak(L"Informe guardado correctamente en archivo de texto.");

    int ask = MessageBoxW(g_hwndMain,
        (L"Informe guardado correctamente en:\n\n" + std::wstring(szFile) + L"\n\n¿Desea abrirlo ahora en el Bloc de notas?").c_str(),
        L"Informe Exportado", MB_YESNO | MB_ICONQUESTION);
    if (ask == IDYES) {
        ShellExecuteW(g_hwndMain, L"open", szFile, nullptr, nullptr, SW_SHOWNORMAL);
    }
}

static std::vector<int> GetSelectedPairIndices() {
    std::vector<int> selList;
    int pos = -1;
    while ((pos = ListView_GetNextItem(g_hListPairs, pos, LVNI_SELECTED)) != -1) {
        selList.push_back(pos);
    }
    return selList;
}

static void SwitchTab(int tabIndex) {
    TabCtrl_SetCurSel(g_hTab, tabIndex);
    if (tabIndex == 0) {
        ShowWindow(g_hListPreview, SW_HIDE);
        ShowWindow(g_hListPairs, SW_SHOW);
        SetFocus(g_hListPairs);
        Speech::Speak(L"Pestaña 1: Pares de Carpetas");
    } else {
        ShowWindow(g_hListPairs, SW_HIDE);
        ShowWindow(g_hListPreview, SW_SHOW);
        SetFocus(g_hListPreview);
        Speech::Speak(L"Pestaña 2: Vista Previa de Sincronización");
    }
}

// -------------------------------------------------------------
// PROJECT ACTIONS: New, Open, Save, Save As
// -------------------------------------------------------------
static void ProjectNew() {
    int res = MessageBoxW(g_hwndMain, L"¿Desea crear un nuevo proyecto vacío? Asegúrese de guardar los cambios del proyecto actual si los necesita.", L"Nuevo Proyecto", MB_YESNO | MB_ICONQUESTION);
    if (res != IDYES) return;

    g_currentProject.name = L"Proyecto Nuevo";
    g_currentProject.file_path.clear();
    g_currentProject.pairs.clear();
    g_currentSummary.items.clear();

    UpdatePairsList();
    UpdatePreviewList();
    UpdateWindowTitle();
    SaveAppConfig();

    Speech::Speak(L"Nuevo proyecto creado.");
}

static void ProjectOpen() {
    std::wstring path;
    if (Dialogs::PromptOpenProject(g_hwndMain, path)) {
        SyncProject loaded;
        if (JsonHelper::LoadProjectFromFile(path, loaded)) {
            g_currentProject = loaded;
            g_settings.current_project_path = path;

            // Add to recent projects
            auto it = std::find(g_settings.recent_projects.begin(), g_settings.recent_projects.end(), path);
            if (it != g_settings.recent_projects.end()) {
                g_settings.recent_projects.erase(it);
            }
            g_settings.recent_projects.insert(g_settings.recent_projects.begin(), path);
            if (g_settings.recent_projects.size() > 10) g_settings.recent_projects.resize(10);

            UpdatePairsList();
            UpdateWindowTitle();
            SaveAppConfig();

            Speech::Speak(L"Proyecto " + g_currentProject.name + L" abierto con " + std::to_wstring(g_currentProject.pairs.size()) + L" pares de carpetas.");
        } else {
            MessageBoxW(g_hwndMain, L"No se pudo cargar el archivo del proyecto.", L"Error", MB_OK | MB_ICONERROR);
        }
    }
}

static void ProjectSave(bool saveAs) {
    if (saveAs || g_currentProject.file_path.empty()) {
        std::wstring path = g_currentProject.file_path;
        if (path.empty()) {
            path = g_currentProject.name + L".syncproj";
        }
        if (Dialogs::PromptSaveProject(g_hwndMain, path)) {
            g_currentProject.file_path = path;
            size_t lastSlash = path.find_last_of(L"\\/");
            std::wstring fname = (lastSlash == std::wstring::npos) ? path : path.substr(lastSlash + 1);
            size_t dot = fname.find_last_of(L'.');
            if (dot != std::wstring::npos) fname = fname.substr(0, dot);
            g_currentProject.name = fname;
        } else {
            return;
        }
    }

    if (JsonHelper::SaveProjectToFile(g_currentProject.file_path, g_currentProject)) {
        g_settings.current_project_path = g_currentProject.file_path;
        SaveAppConfig();
        UpdateWindowTitle();
        Speech::Speak(L"Proyecto guardado como " + g_currentProject.name);
    } else {
        MessageBoxW(g_hwndMain, L"Error al guardar el archivo del proyecto.", L"Error", MB_OK | MB_ICONERROR);
    }
}

// -------------------------------------------------------------
// SYNC ACTIONS: Compare, Single Sync, Sequential Sync
// -------------------------------------------------------------
static void StartCompareSelected() {
    auto sel = GetSelectedPairIndices();
    if (sel.empty()) {
        Speech::Speak(L"Por favor, seleccione un par de carpetas de la lista.");
        return;
    }

    if (g_isBusy.load()) {
        Speech::Speak(L"Ya hay una operacion en curso.");
        return;
    }

    g_isBusy.store(true);
    g_lastProgressMilestone = 0;
    SendMessageW(g_hProgressBar, PBM_SETPOS, 0, 0);
    Speech::StartWaitingBeep();

    FolderPair pair = g_currentProject.pairs[sel[0]];
    SendMessageW(g_hStatusBar, SB_SETTEXTW, 0, (LPARAM)L"Comparando carpetas...");
    Speech::Speak(L"Iniciando comparacion de " + pair.name);

    std::thread([pair]() {
        try {
            auto progressCb = [](int pct, const std::wstring& st) {
                PostMessageW(g_hwndMain, WM_SYNC_PROGRESS, (WPARAM)pct, (LPARAM)new std::wstring(st));
            };
            ComparisonSummary summary = SyncEngine::CompareFolders(pair, nullptr, progressCb);
            g_currentSummary = summary;
            PostMessageW(g_hwndMain, WM_COMPARE_DONE, 0, 0);
        } catch (const std::exception& ex) {
            LOG_ERROR(std::string("Excepción en hilo de comparación: ") + ex.what());
            PostMessageW(g_hwndMain, WM_COMPARE_DONE, 0, 0);
        } catch (...) {
            LOG_ERROR("Excepción desconocida en hilo de comparación");
            PostMessageW(g_hwndMain, WM_COMPARE_DONE, 0, 0);
        }
    }).detach();
}

static void StartSyncSingle() {
    auto sel = GetSelectedPairIndices();
    if (sel.empty()) {
        Speech::Speak(L"Por favor, seleccione un par de carpetas de la lista.");
        return;
    }

    if (g_isBusy.load()) {
        Speech::Speak(L"Ya hay una operacion en curso.");
        return;
    }

    int idx = sel[0];
    FolderPair pair = g_currentProject.pairs[idx];

    // If summary is for this pair and not empty, execute directly
    if (g_currentSummary.pair_id == pair.id && !g_currentSummary.items.empty()) {
        if (g_currentSummary.total_files_to_sync == 0) {
            Speech::Speak(L"Las carpetas ya están al día. No hay cambios por sincronizar.");
            return;
        }

        g_isBusy.store(true);
        g_lastProgressMilestone = 0;
        SendMessageW(g_hProgressBar, PBM_SETPOS, 0, 0);
        Speech::StartWaitingBeep();

        SendMessageW(g_hStatusBar, SB_SETTEXTW, 0, (LPARAM)L"Sincronizando archivos...");
        Speech::Speak(L"Iniciando sincronización de " + std::to_wstring(g_currentSummary.total_files_to_sync) + L" archivos.");

        std::thread([pair, idx]() {
            try {
                auto syncProgressCb = [](int pct, int cur, int tot, const std::wstring& rp) {
                    std::wstring text = L"Sincronizando (" + std::to_wstring(cur) + L"/" + std::to_wstring(tot) + L"): " + rp;
                    PostMessageW(g_hwndMain, WM_SYNC_PROGRESS, (WPARAM)pct, (LPARAM)new std::wstring(text));
                };
                bool ok = SyncEngine::ExecuteSync(pair, g_currentSummary, nullptr, syncProgressCb);
                PostMessageW(g_hwndMain, WM_SYNC_DONE, ok ? 1 : 0, (LPARAM)idx);
            } catch (const std::exception& ex) {
                LOG_ERROR(std::string("Excepción en sincronización: ") + ex.what());
                PostMessageW(g_hwndMain, WM_SYNC_DONE, 0, (LPARAM)idx);
            } catch (...) {
                LOG_ERROR("Excepción desconocida en sincronización");
                PostMessageW(g_hwndMain, WM_SYNC_DONE, 0, (LPARAM)idx);
            }
        }).detach();
        return;
    }

    // Otherwise compare first then sync
    g_isBusy.store(true);
    g_lastProgressMilestone = 0;
    SendMessageW(g_hProgressBar, PBM_SETPOS, 0, 0);
    Speech::StartWaitingBeep();

    SendMessageW(g_hStatusBar, SB_SETTEXTW, 0, (LPARAM)L"Comparando antes de sincronizar...");

    std::thread([pair, idx]() {
        try {
            auto progressCb = [](int pct, const std::wstring& st) {
                PostMessageW(g_hwndMain, WM_SYNC_PROGRESS, (WPARAM)pct, (LPARAM)new std::wstring(st));
            };
            ComparisonSummary summary = SyncEngine::CompareFolders(pair, nullptr, progressCb);
            g_currentSummary = summary;

            if (summary.total_files_to_sync == 0) {
                PostMessageW(g_hwndMain, WM_SYNC_DONE, 1, (LPARAM)idx);
                return;
            }

            g_lastProgressMilestone = 0;
            auto syncProgressCb = [](int pct, int cur, int tot, const std::wstring& rp) {
                std::wstring text = L"Sincronizando (" + std::to_wstring(cur) + L"/" + std::to_wstring(tot) + L"): " + rp;
                PostMessageW(g_hwndMain, WM_SYNC_PROGRESS, (WPARAM)pct, (LPARAM)new std::wstring(text));
            };
            bool ok = SyncEngine::ExecuteSync(pair, g_currentSummary, nullptr, syncProgressCb);
            PostMessageW(g_hwndMain, WM_SYNC_DONE, ok ? 1 : 0, (LPARAM)idx);
        } catch (const std::exception& ex) {
            LOG_ERROR(std::string("Excepción en comparar/sincronizar: ") + ex.what());
            PostMessageW(g_hwndMain, WM_SYNC_DONE, 0, (LPARAM)idx);
        } catch (...) {
            LOG_ERROR("Excepción desconocida en comparar/sincronizar");
            PostMessageW(g_hwndMain, WM_SYNC_DONE, 0, (LPARAM)idx);
        }
    }).detach();
}

// Sequential Execution: syncs folder pairs one after another in exact order
static void StartSyncSequential(const std::vector<int>& indices) {
    if (indices.empty()) {
        Speech::Speak(L"No hay pares de carpetas seleccionados para sincronizar en serie.");
        return;
    }

    if (g_isBusy.load()) {
        Speech::Speak(L"Ya hay una operacion en curso.");
        return;
    }

    g_isBusy.store(true);
    g_cancelRequested.store(false);
    g_lastProgressMilestone = 0;
    SendMessageW(g_hProgressBar, PBM_SETPOS, 0, 0);
    Speech::StartWaitingBeep();

    std::wstring announce = L"Iniciando ejecución en serie de " + std::to_wstring(indices.size()) + L" pares de carpetas.";
    SendMessageW(g_hStatusBar, SB_SETTEXTW, 0, (LPARAM)announce.c_str());
    Speech::Speak(announce);

    std::thread([indices]() {
        int successCount = 0;
        int errorCount = 0;

        try {
            for (size_t step = 0; step < indices.size(); ++step) {
                if (g_cancelRequested.load()) break;

                int idx = indices[step];
                if (idx < 0 || idx >= (int)g_currentProject.pairs.size()) continue;

                FolderPair pair = g_currentProject.pairs[idx];
                std::wstring stepMsg = L"Ejecutando " + std::to_wstring(step + 1) + L" de " +
                                       std::to_wstring(indices.size()) + L": " + pair.name;
                Speech::Speak(stepMsg);

                g_lastProgressMilestone = 0;
                auto progressCb = [](int pct, const std::wstring& st) {
                    PostMessageW(g_hwndMain, WM_SYNC_PROGRESS, (WPARAM)pct, (LPARAM)new std::wstring(st));
                };
                ComparisonSummary summary = SyncEngine::CompareFolders(pair, &g_cancelRequested, progressCb);

                if (g_cancelRequested.load()) break;

                bool ok = true;
                if (summary.total_files_to_sync > 0) {
                    g_lastProgressMilestone = 0;
                    auto syncCb = [](int pct, int cur, int tot, const std::wstring& rp) {
                        std::wstring text = L"Sincronizando (" + std::to_wstring(cur) + L"/" + std::to_wstring(tot) + L"): " + rp;
                        PostMessageW(g_hwndMain, WM_SYNC_PROGRESS, (WPARAM)pct, (LPARAM)new std::wstring(text));
                    };
                    ok = SyncEngine::ExecuteSync(pair, summary, &g_cancelRequested, syncCb);
                }

                if (ok) {
                    successCount++;
                    g_currentProject.pairs[idx].last_sync_status = L"Completado (" + std::to_wstring(summary.total_files_to_sync) + L" archivos)";
                } else {
                    errorCount++;
                    g_currentProject.pairs[idx].last_sync_status = L"Error";
                }

                // Get current timestamp
                time_t t = time(nullptr);
                tm tmNow;
                localtime_s(&tmNow, &t);
                wchar_t tbuf[32];
                swprintf_s(tbuf, L"%02d:%02d:%02d", tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec);
                g_currentProject.pairs[idx].last_sync_time = tbuf;

                PostMessageW(g_hwndMain, WM_SEQ_STEP_DONE, (WPARAM)idx, 0);
            }
        } catch (const std::exception& ex) {
            LOG_ERROR(std::string("Excepción en ejecución en serie: ") + ex.what());
        } catch (...) {
            LOG_ERROR("Excepción desconocida en ejecución en serie");
        }

        PostMessageW(g_hwndMain, WM_SEQ_ALL_DONE, (WPARAM)successCount, (LPARAM)errorCount);
    }).detach();
}

static void StartSyncSelectedSequential() {
    auto sel = GetSelectedPairIndices();
    if (sel.empty()) {
        Speech::Speak(L"Seleccione uno o varios pares de carpetas de la lista para sincronizarlos en serie.");
        return;
    }
    StartSyncSequential(sel);
}

static void StartSyncAllSequential() {
    std::vector<int> all;
    for (size_t i = 0; i < g_currentProject.pairs.size(); ++i) {
        if (g_currentProject.pairs[i].enabled) {
            all.push_back((int)i);
        }
    }
    if (all.empty()) {
        Speech::Speak(L"No hay pares de carpetas activados en el proyecto.");
        return;
    }
    StartSyncSequential(all);
}

static void AddPair() {
    FolderPair newPair;
    newPair.id = L"pair_" + std::to_wstring(GetTickCount64());
    newPair.name = L"Nuevo par de carpetas " + std::to_wstring(g_currentProject.pairs.size() + 1);

    if (Dialogs::ShowPairDialog(g_hwndMain, newPair, true)) {
        g_currentProject.pairs.push_back(newPair);
        SaveAppConfig();
        UpdatePairsList();
        Speech::Speak(L"Par de carpetas " + newPair.name + L" añadido al proyecto.");
    }
}

static void EditPair() {
    auto sel = GetSelectedPairIndices();
    if (sel.empty()) {
        Speech::Speak(L"Seleccione el par de carpetas que desea editar.");
        return;
    }

    int idx = sel[0];
    FolderPair copy = g_currentProject.pairs[idx];
    if (Dialogs::ShowPairDialog(g_hwndMain, copy, false)) {
        g_currentProject.pairs[idx] = copy;
        SaveAppConfig();
        UpdatePairsList();
        Speech::Speak(L"Cambios en " + copy.name + L" guardados.");
    }
}

static void DeletePair() {
    auto sel = GetSelectedPairIndices();
    if (sel.empty()) {
        Speech::Speak(L"Seleccione el par de carpetas que desea eliminar.");
        return;
    }

    int idx = sel[0];
    std::wstring name = g_currentProject.pairs[idx].name;
    int res = MessageBoxW(g_hwndMain, (L"¿Esta seguro de que desea eliminar el par de carpetas '" + name + L"'?").c_str(), L"Confirmar eliminacion", MB_YESNO | MB_ICONQUESTION);
    if (res == IDYES) {
        g_currentProject.pairs.erase(g_currentProject.pairs.begin() + idx);
        SaveAppConfig();
        UpdatePairsList();
        Speech::Speak(L"Par de carpetas " + name + L" eliminado.");
    }
}

static void InvertPair() {
    auto sel = GetSelectedPairIndices();
    if (sel.empty()) {
        Speech::Speak(L"Seleccione el par de carpetas que desea invertir.");
        return;
    }

    int idx = sel[0];
    FolderPair& p = g_currentProject.pairs[idx];
    std::swap(p.left_folder, p.right_folder);
    SaveAppConfig();
    UpdatePairsList();

    std::wstring msg = L"Par de carpetas '" + p.name + L"' invertido. Origen y destino intercambiados.";
    SendMessageW(g_hStatusBar, SB_SETTEXTW, 0, (LPARAM)msg.c_str());
    Speech::Speak(msg);
}

static void ShowBatchQueueDialog() {
    Dialogs::ShowBatchRunnerDialog(g_hwndMain, g_settings, g_currentProject);
    UpdatePairsList();
}

static void ShowSettings() {
    if (Dialogs::ShowSettingsDialog(g_hwndMain, g_settings)) {
        Speech::SetEnabled(g_settings.speech_enabled);
        SaveAppConfig();
        Speech::Speak(L"Configuracion actualizada.");
    }
}

static void CopyToClipboard(const std::wstring& text) {
    if (!OpenClipboard(g_hwndMain)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hGlob) {
        memcpy(GlobalLock(hGlob), text.c_str(), bytes);
        GlobalUnlock(hGlob);
        SetClipboardData(CF_UNICODETEXT, hGlob);
    }
    CloseClipboard();
    Speech::Speak(L"Copiado al portapapeles: " + text);
}

static void ShowPairsContextMenu(HWND hwnd, int x, int y) {
    auto sel = GetSelectedPairIndices();
    if (sel.empty()) return;
    int idx = sel[0];
    const FolderPair& p = g_currentProject.pairs[idx];

    // If opened via keyboard (Apps key or Shift+F10), x and y are -1
    if (x == -1 && y == -1) {
        RECT rcItem;
        if (ListView_GetItemRect(g_hListPairs, idx, &rcItem, LVIR_LABEL)) {
            POINT pt = { rcItem.left + 20, rcItem.bottom };
            ClientToScreen(g_hListPairs, &pt);
            x = pt.x;
            y = pt.y;
        } else {
            RECT rcList;
            GetWindowRect(g_hListPairs, &rcList);
            x = rcList.left + 50;
            y = rcList.top + 50;
        }
    }

    HMENU hMenu = CreatePopupMenu();
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_SYNC_COMPARE, L"&Comparar Par de Carpetas\tF5");
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_SYNC_ONE, L"&Sincronizar Par de Carpetas\tF6");
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_EDIT_INVERT_PAIR, L"&Invertir Par de Carpetas (Origen <-> Destino)\tCtrl+I");
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_EDIT_EDIT_PAIR, L"&Editar Par de Carpetas...\tCtrl+E");
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_EDIT_ADD_PAIR, L"&Añadir Nuevo Par de Carpetas...\tCtrl+N");
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_EDIT_DEL_PAIR, L"&Eliminar Par de Carpetas\tSupr");
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_PAIR_OPEN_LEFT_EXPLORER, L"Abrir Carpeta &Origen en Explorador");
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_PAIR_OPEN_RIGHT_EXPLORER, L"Abrir Carpeta &Destino en Explorador");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN, x, y, 0, hwnd, nullptr);
    DestroyMenu(hMenu);
}

static void ShowPreviewContextMenu(HWND hwnd, int x, int y) {
    int selRow = ListView_GetNextItem(g_hListPreview, -1, LVNI_SELECTED);
    if (selRow < 0 || selRow >= (int)g_previewDisplayedIndices.size()) return;

    size_t actualIdx = g_previewDisplayedIndices[selRow];
    if (actualIdx >= g_currentSummary.items.size()) return;
    const SyncItem& item = g_currentSummary.items[actualIdx];

    auto selPairs = GetSelectedPairIndices();
    std::wstring leftRoot, rightRoot;
    if (!selPairs.empty()) {
        const FolderPair& p = g_currentProject.pairs[selPairs[0]];
        leftRoot = p.left_folder;
        rightRoot = p.right_folder;
    }

    if (x == -1 && y == -1) {
        RECT rcItem;
        if (ListView_GetItemRect(g_hListPreview, selRow, &rcItem, LVIR_LABEL)) {
            POINT pt = { rcItem.left + 20, rcItem.bottom };
            ClientToScreen(g_hListPreview, &pt);
            x = pt.x;
            y = pt.y;
        } else {
            RECT rcList;
            GetWindowRect(g_hListPreview, &rcList);
            x = rcList.left + 50;
            y = rcList.top + 50;
        }
    }

    HMENU hMenu = CreatePopupMenu();
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_PREVIEW_COPY_PATH, L"&Copiar Ruta Relativa al Portapapeles");
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);

    if (item.left_exists && !leftRoot.empty()) {
        InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_PREVIEW_OPEN_LEFT, L"Abrir Archivo en &Origen");
        InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_PREVIEW_OPEN_LEFT_DIR, L"Ver en Carpeta Contenedora de O&rigen");
    }
    if (item.right_exists && !rightRoot.empty()) {
        InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_PREVIEW_OPEN_RIGHT, L"Abrir Archivo en &Destino");
        InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_PREVIEW_OPEN_RIGHT_DIR, L"Ver en Carpeta Contenedora de &Destino");
    }

    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING | (g_filterShowOnlyChanges ? MF_CHECKED : MF_UNCHECKED), IDM_VIEW_FILTER_CHANGES, L"&Mostrar solo archivos con cambios (Ocultar idénticos)\tCtrl+H");
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_VIEW_SORT_ACTION, L"&Ordenar por Acción (Copias y eliminaciones primero)\tAlt+O");
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_PROJECT_EXPORT_TXT, L"&Exportar Informe de Resultados a TXT...\tCtrl+P");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN, x, y, 0, hwnd, nullptr);
    DestroyMenu(hMenu);
}

static void ShowTrayContextMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    InsertMenuW(hMenu, 0, MF_BYPOSITION | MF_STRING, ID_TRAY_RESTORE, L"&Mostrar SyncAccesible");
    InsertMenuW(hMenu, 1, MF_BYPOSITION | MF_STRING, ID_TRAY_SYNC_ALL, L"Sincronizar &todas en serie ahora");
    InsertMenuW(hMenu, 2, MF_BYPOSITION | MF_STRING | (Scheduler::IsPaused() ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_PAUSE, L"&Pausar programacion");
    InsertMenuW(hMenu, 3, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    InsertMenuW(hMenu, 4, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT, L"&Salir");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(hMenu);
}

static HMENU CreateAppMenuBar() {
    HMENU hMenuBar = CreateMenu();

    // Menu Archivo
    HMENU hMenuPrj = CreatePopupMenu();
    AppendMenuW(hMenuPrj, MF_STRING, IDM_PROJECT_NEW, L"&Nuevo Proyecto\tCtrl+Shift+N");
    AppendMenuW(hMenuPrj, MF_STRING, IDM_PROJECT_OPEN, L"&Abrir Proyecto...\tCtrl+O");
    AppendMenuW(hMenuPrj, MF_STRING, IDM_PROJECT_SAVE, L"&Guardar Proyecto\tCtrl+S");
    AppendMenuW(hMenuPrj, MF_STRING, IDM_PROJECT_SAVEAS, L"Guardar Proyecto &Como...\tCtrl+Shift+S");
    AppendMenuW(hMenuPrj, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenuPrj, MF_STRING, IDM_PROJECT_EXPORT_TXT, L"&Exportar Informe de Resultados a TXT...\tCtrl+P");
    AppendMenuW(hMenuPrj, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenuPrj, MF_STRING, IDM_PROJECT_EXIT, L"&Cerrar y Salir por Completo\tAlt+F4");
    AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hMenuPrj, L"&Archivo");

    // Menu Sincronización
    HMENU hMenuSync = CreatePopupMenu();
    AppendMenuW(hMenuSync, MF_STRING, IDM_SYNC_COMPARE, L"&Comparar Seleccionada\tF5");
    AppendMenuW(hMenuSync, MF_STRING, IDM_SYNC_ONE, L"&Sincronizar Seleccionada\tF6");
    AppendMenuW(hMenuSync, MF_STRING, IDM_SYNC_SELECTED_SEQ, L"Sincronizar Selección en &Serie (Una tras otra)\tF7");
    AppendMenuW(hMenuSync, MF_STRING, IDM_SYNC_ALL_SEQ, L"Sincronizar &Todos los Pares en Serie\tCtrl+F6");
    AppendMenuW(hMenuSync, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenuSync, MF_STRING, IDM_SYNC_BATCH_QUEUE, L"&Cola de Lotes (Varios Proyectos)... \tAlt+L");
    AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hMenuSync, L"&Sincronización");

    // Menu Pares de Carpetas
    HMENU hMenuEdit = CreatePopupMenu();
    AppendMenuW(hMenuEdit, MF_STRING, IDM_EDIT_ADD_PAIR, L"&Añadir Par de Carpetas...\tCtrl+N");
    AppendMenuW(hMenuEdit, MF_STRING, IDM_EDIT_EDIT_PAIR, L"&Editar Par de Carpetas Seleccionado...\tCtrl+E");
    AppendMenuW(hMenuEdit, MF_STRING, IDM_EDIT_INVERT_PAIR, L"&Invertir Par de Carpetas Seleccionado (Origen <-> Destino)\tCtrl+I");
    AppendMenuW(hMenuEdit, MF_STRING, IDM_EDIT_DEL_PAIR, L"&Eliminar Par de Carpetas Seleccionado\tSupr");
    AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hMenuEdit, L"&Pares de Carpetas");

    // Menu Ver
    HMENU hMenuVer = CreatePopupMenu();
    AppendMenuW(hMenuVer, MF_STRING, IDM_VIEW_TAB_PAIRS, L"Pestaña &1: Pares de Carpetas\tAlt+1");
    AppendMenuW(hMenuVer, MF_STRING, IDM_VIEW_TAB_PREVIEW, L"Pestaña &2: Vista Previa de Sincronización\tAlt+2");
    AppendMenuW(hMenuVer, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenuVer, MF_STRING, IDM_VIEW_TAB_NEXT, L"&Cambiar de Pestaña\tCtrl+Tab");
    AppendMenuW(hMenuVer, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenuVer, MF_STRING | (g_filterShowOnlyChanges ? MF_CHECKED : MF_UNCHECKED), IDM_VIEW_FILTER_CHANGES, L"&Mostrar solo archivos con cambios (Ocultar idénticos)\tCtrl+H");

    HMENU hMenuSort = CreatePopupMenu();
    AppendMenuW(hMenuSort, MF_STRING, IDM_VIEW_SORT_ACTION, L"Por &Acción (Copias y eliminaciones primero)\tAlt+O");
    AppendMenuW(hMenuSort, MF_STRING, IDM_VIEW_SORT_NAME, L"Por &Nombre de archivo");
    AppendMenuW(hMenuSort, MF_STRING, IDM_VIEW_SORT_SIZE, L"Por &Tamaño");
    AppendMenuW(hMenuVer, MF_POPUP, (UINT_PTR)hMenuSort, L"&Ordenar Vista Previa");

    AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hMenuVer, L"&Ver");

    // Menu Herramientas
    HMENU hMenuTools = CreatePopupMenu();
    AppendMenuW(hMenuTools, MF_STRING, IDM_TOOLS_SETTINGS, L"&Configuración General...");
    AppendMenuW(hMenuTools, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenuTools, MF_STRING, IDM_TOOLS_VIEW_LOG, L"&Ver Archivo de Registro (.log)...");
    AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hMenuTools, L"&Herramientas");

    // Menu Ayuda
    HMENU hMenuHelp = CreatePopupMenu();
    AppendMenuW(hMenuHelp, MF_STRING, IDM_HELP_SHORTCUTS, L"&Atajos de Teclado...\tShift+F1");
    AppendMenuW(hMenuHelp, MF_STRING, IDM_HELP_MANUAL, L"&Manual de Usuario (HTML)...\tF1");
    AppendMenuW(hMenuHelp, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenuHelp, MF_STRING, IDM_HELP_ABOUT, L"&Acerca de SyncAccesible...");
    AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hMenuHelp, L"A&yuda");

    return hMenuBar;
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_uTaskbarRestartMsg != 0 && msg == g_uTaskbarRestartMsg) {
        Shell_NotifyIconW(NIM_ADD, &g_nid);
        return 0;
    }

    switch (msg) {
        case WM_CREATE: {
            g_hwndMain = hwnd;
            g_hFont = CreateAppFont();
            g_uTaskbarRestartMsg = RegisterWindowMessageW(L"TaskbarCreated");

            // Menu Bar
            HMENU hMenu = CreateAppMenuBar();
            SetMenu(hwnd, hMenu);

            // Tray Icon
            g_nid.hWnd = hwnd;
            g_nid.uID = 1;
            g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            g_nid.uCallbackMessage = WM_TRAYICON;
            g_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
            wcscpy_s(g_nid.szTip, L"SyncAccesible (C++)");
            Shell_NotifyIconW(NIM_ADD, &g_nid);

            // Button bar
            auto makeBtn = [&](const wchar_t* text, int x, int y, int w, int h, int id) -> HWND {
                HWND hBtn = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, x, y, w, h, hwnd, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
                SendMessageW(hBtn, WM_SETFONT, (WPARAM)g_hFont, TRUE);
                return hBtn;
            };

            int bx = 10;
            makeBtn(L"&Comparar (F5)", bx, 8, 110, 30, IDC_BTN_COMPARE); bx += 114;
            makeBtn(L"&Sincronizar (F6)", bx, 8, 118, 30, IDC_BTN_SYNC); bx += 122;
            makeBtn(L"En &Serie (F7)", bx, 8, 115, 30, IDC_BTN_SYNC_SEQ); bx += 119;
            makeBtn(L"&Todas en Serie", bx, 8, 118, 30, IDC_BTN_SYNC_ALL); bx += 122;
            makeBtn(L"Cola de &Lotes...", bx, 8, 125, 30, IDC_BTN_BATCH_QUEUE); bx += 129;
            makeBtn(L"&Abrir Prj", bx, 8, 85, 30, IDC_BTN_OPEN_PRJ); bx += 89;
            makeBtn(L"&Guardar Prj", bx, 8, 95, 30, IDC_BTN_SAVE_PRJ);

            // Tab Control
            g_hTab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP, 10, 46, 860, 480, hwnd, (HMENU)IDC_TAB, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(g_hTab, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            TCITEMW tie = {0};
            tie.mask = TCIF_TEXT;
            tie.pszText = (LPWSTR)L"1. Pares de Carpetas";
            TabCtrl_InsertItem(g_hTab, 0, &tie);
            tie.pszText = (LPWSTR)L"2. Vista Previa de Comparacion / Acciones";
            TabCtrl_InsertItem(g_hTab, 1, &tie);

            // List 1: Pairs List (Multi-selection enabled)
            g_hListPairs = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS, 14, 76, 852, 445, hwnd, (HMENU)IDC_LIST_PAIRS, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(g_hListPairs, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            ListView_SetExtendedListViewStyle(g_hListPairs, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

            auto addCol = [&](HWND hList, int idx, const wchar_t* name, int w) {
                LVCOLUMNW lvc = {0};
                lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
                lvc.cx = w;
                lvc.pszText = (LPWSTR)name;
                ListView_InsertColumn(hList, idx, &lvc);
            };

            addCol(g_hListPairs, 0, L"Nombre", 160);
            addCol(g_hListPairs, 1, L"Estado", 80);
            addCol(g_hListPairs, 2, L"Modo", 130);
            addCol(g_hListPairs, 3, L"Programación", 110);
            addCol(g_hListPairs, 4, L"Último Resultado", 140);
            addCol(g_hListPairs, 5, L"Hora Última Sincronización", 120);
            addCol(g_hListPairs, 6, L"Origen", 220);
            addCol(g_hListPairs, 7, L"Destino", 220);
            EnableVirtualColumnNavigation(g_hListPairs, 101);

            // List 2: Preview List
            g_hListPreview = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 14, 76, 852, 445, hwnd, (HMENU)IDC_LIST_PREVIEW, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(g_hListPreview, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            ListView_SetExtendedListViewStyle(g_hListPreview, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

            addCol(g_hListPreview, 0, L"Archivo", 360);
            addCol(g_hListPreview, 1, L"Acción Prevista", 160);
            addCol(g_hListPreview, 2, L"Tamaño Origen", 110);
            addCol(g_hListPreview, 3, L"Tamaño Destino", 110);
            addCol(g_hListPreview, 4, L"Estado / Detalle", 180);
            EnableVirtualColumnNavigation(g_hListPreview, 102);

            // Progress Bar
            g_hProgressBar = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 10, 480, 860, 16, hwnd, (HMENU)IDC_PROGRESSBAR, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(g_hProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            SendMessageW(g_hProgressBar, PBM_SETPOS, 0, 0);

            // Status Bar
            g_hStatusBar = CreateStatusWindowW(WS_CHILD | WS_VISIBLE, L"Listo.", hwnd, IDC_STATUSBAR);
            SendMessageW(g_hStatusBar, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            UpdatePairsList();
            UpdateWindowTitle();

            if (g_settings.auto_sync_on_startup) {
                StartSyncAllSequential();
            }

            return 0;
        }

        case WM_SIZE: {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);

            SendMessageW(g_hStatusBar, WM_SIZE, wParam, lParam);

            RECT rcStatus;
            GetWindowRect(g_hStatusBar, &rcStatus);
            int statusH = rcStatus.bottom - rcStatus.top;

            int progressH = 16;
            int progressY = h - statusH - progressH - 3;
            SetWindowPos(g_hProgressBar, nullptr, 10, progressY, w - 20, progressH, SWP_NOZORDER);

            int tabY = 46;
            int tabH = progressY - tabY - 6;
            if (tabH > 50) {
                SetWindowPos(g_hTab, nullptr, 10, tabY, w - 20, tabH, SWP_NOZORDER);
                SetWindowPos(g_hListPairs, nullptr, 14, tabY + 30, w - 28, tabH - 35, SWP_NOZORDER);
                SetWindowPos(g_hListPreview, nullptr, 14, tabY + 30, w - 28, tabH - 35, SWP_NOZORDER);
            }
            return 0;
        }

        case WM_NOTIFY: {
            NMHDR* pnm = (NMHDR*)lParam;
            if (pnm->idFrom == IDC_TAB && pnm->code == TCN_SELCHANGE) {
                int cur = TabCtrl_GetCurSel(g_hTab);
                SwitchTab(cur);
            } else if (pnm->idFrom == IDC_LIST_PAIRS && (pnm->code == NM_DBLCLK || pnm->code == NM_RETURN)) {
                EditPair();
            } else if (pnm->idFrom == IDC_LIST_PREVIEW && pnm->code == LVN_COLUMNCLICK) {
                NMLISTVIEW* pnmv = (NMLISTVIEW*)lParam;
                if (pnmv->iSubItem == 0) {
                    g_previewSortOrder = PreviewSortOrder::ByName;
                    Speech::Speak(L"Lista ordenada por nombre de archivo");
                } else if (pnmv->iSubItem == 1) {
                    g_previewSortOrder = PreviewSortOrder::ByAction;
                    Speech::Speak(L"Lista ordenada por acción prevista");
                } else if (pnmv->iSubItem == 2 || pnmv->iSubItem == 3) {
                    g_previewSortOrder = PreviewSortOrder::BySize;
                    Speech::Speak(L"Lista ordenada por tamaño");
                }
                UpdatePreviewList();
            }
            return 0;
        }

        case WM_CONTEXTMENU: {
            HWND hFrom = (HWND)wParam;
            int x = (short)LOWORD(lParam);
            int y = (short)HIWORD(lParam);

            if (hFrom == g_hListPairs) {
                ShowPairsContextMenu(hwnd, x, y);
                return 0;
            } else if (hFrom == g_hListPreview) {
                ShowPreviewContextMenu(hwnd, x, y);
                return 0;
            }
            break;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            switch (id) {
                // Toolbar buttons & Menus
                case IDC_BTN_COMPARE:
                case IDM_SYNC_COMPARE:
                    StartCompareSelected();
                    break;
                case IDC_BTN_SYNC:
                case IDM_SYNC_ONE:
                    StartSyncSingle();
                    break;
                case IDC_BTN_SYNC_SEQ:
                case IDM_SYNC_SELECTED_SEQ:
                    StartSyncSelectedSequential();
                    break;
                case IDC_BTN_SYNC_ALL:
                case IDM_SYNC_ALL_SEQ:
                    StartSyncAllSequential();
                    break;
                case IDC_BTN_BATCH_QUEUE:
                case IDM_SYNC_BATCH_QUEUE:
                    ShowBatchQueueDialog();
                    break;

                case IDC_BTN_OPEN_PRJ:
                case IDM_PROJECT_OPEN:
                    ProjectOpen();
                    break;
                case IDC_BTN_SAVE_PRJ:
                case IDM_PROJECT_SAVE:
                    ProjectSave(false);
                    break;
                case IDM_PROJECT_NEW:
                    ProjectNew();
                    break;
                case IDM_PROJECT_SAVEAS:
                    ProjectSave(true);
                    break;
                case IDM_PROJECT_EXIT:
                    DestroyWindow(hwnd);
                    break;

                case IDC_BTN_ADD_PAIR:
                case IDM_EDIT_ADD_PAIR:
                    AddPair();
                    break;
                case IDC_BTN_EDIT_PAIR:
                case IDM_EDIT_EDIT_PAIR:
                    EditPair();
                    break;
                case IDC_BTN_DEL_PAIR:
                case IDM_EDIT_DEL_PAIR:
                    DeletePair();
                    break;
                case IDM_EDIT_INVERT_PAIR:
                    InvertPair();
                    break;

                case IDM_PAIR_OPEN_LEFT_EXPLORER: {
                    auto sel = GetSelectedPairIndices();
                    if (!sel.empty()) {
                        ShellExecuteW(hwnd, L"open", g_currentProject.pairs[sel[0]].left_folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    }
                    break;
                }
                case IDM_PAIR_OPEN_RIGHT_EXPLORER: {
                    auto sel = GetSelectedPairIndices();
                    if (!sel.empty()) {
                        ShellExecuteW(hwnd, L"open", g_currentProject.pairs[sel[0]].right_folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    }
                    break;
                }
                case IDM_PREVIEW_COPY_PATH: {
                    int selRow = ListView_GetNextItem(g_hListPreview, -1, LVNI_SELECTED);
                    if (selRow >= 0 && selRow < (int)g_previewDisplayedIndices.size()) {
                        size_t actualIdx = g_previewDisplayedIndices[selRow];
                        if (actualIdx < g_currentSummary.items.size()) {
                            CopyToClipboard(g_currentSummary.items[actualIdx].rel_path);
                        }
                    }
                    break;
                }
                case IDM_PREVIEW_OPEN_LEFT: {
                    int selRow = ListView_GetNextItem(g_hListPreview, -1, LVNI_SELECTED);
                    auto selP = GetSelectedPairIndices();
                    if (selRow >= 0 && selRow < (int)g_previewDisplayedIndices.size() && !selP.empty()) {
                        size_t actualIdx = g_previewDisplayedIndices[selRow];
                        if (actualIdx < g_currentSummary.items.size()) {
                            std::wstring fullPath = (std::filesystem::path(g_currentProject.pairs[selP[0]].left_folder) / g_currentSummary.items[actualIdx].rel_path).wstring();
                            ShellExecuteW(hwnd, L"open", fullPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                        }
                    }
                    break;
                }
                case IDM_PREVIEW_OPEN_RIGHT: {
                    int selRow = ListView_GetNextItem(g_hListPreview, -1, LVNI_SELECTED);
                    auto selP = GetSelectedPairIndices();
                    if (selRow >= 0 && selRow < (int)g_previewDisplayedIndices.size() && !selP.empty()) {
                        size_t actualIdx = g_previewDisplayedIndices[selRow];
                        if (actualIdx < g_currentSummary.items.size()) {
                            std::wstring fullPath = (std::filesystem::path(g_currentProject.pairs[selP[0]].right_folder) / g_currentSummary.items[actualIdx].rel_path).wstring();
                            ShellExecuteW(hwnd, L"open", fullPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                        }
                    }
                    break;
                }
                case IDM_PREVIEW_OPEN_LEFT_DIR: {
                    int selRow = ListView_GetNextItem(g_hListPreview, -1, LVNI_SELECTED);
                    auto selP = GetSelectedPairIndices();
                    if (selRow >= 0 && selRow < (int)g_previewDisplayedIndices.size() && !selP.empty()) {
                        size_t actualIdx = g_previewDisplayedIndices[selRow];
                        if (actualIdx < g_currentSummary.items.size()) {
                            std::wstring fullPath = (std::filesystem::path(g_currentProject.pairs[selP[0]].left_folder) / g_currentSummary.items[actualIdx].rel_path).wstring();
                            std::wstring args = L"/select,\"" + fullPath + L"\"";
                            ShellExecuteW(hwnd, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
                        }
                    }
                    break;
                }
                case IDM_PREVIEW_OPEN_RIGHT_DIR: {
                    int selRow = ListView_GetNextItem(g_hListPreview, -1, LVNI_SELECTED);
                    auto selP = GetSelectedPairIndices();
                    if (selRow >= 0 && selRow < (int)g_previewDisplayedIndices.size() && !selP.empty()) {
                        size_t actualIdx = g_previewDisplayedIndices[selRow];
                        if (actualIdx < g_currentSummary.items.size()) {
                            std::wstring fullPath = (std::filesystem::path(g_currentProject.pairs[selP[0]].right_folder) / g_currentSummary.items[actualIdx].rel_path).wstring();
                            std::wstring args = L"/select,\"" + fullPath + L"\"";
                            ShellExecuteW(hwnd, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
                        }
                    }
                    break;
                }
                case IDM_VIEW_TAB_PAIRS:
                    SwitchTab(0);
                    break;
                case IDM_VIEW_TAB_PREVIEW:
                    SwitchTab(1);
                    break;
                case IDM_VIEW_TAB_NEXT: {
                    int cur = TabCtrl_GetCurSel(g_hTab);
                    SwitchTab((cur == 0) ? 1 : 0);
                    break;
                }
                case IDM_VIEW_FILTER_CHANGES:
                    g_filterShowOnlyChanges = !g_filterShowOnlyChanges;
                    UpdatePreviewList();
                    if (g_filterShowOnlyChanges) {
                        Speech::Speak(L"Filtro activado: Mostrando solo archivos con cambios. Total: " + std::to_wstring(g_previewDisplayedIndices.size()) + L" elementos.");
                    } else {
                        Speech::Speak(L"Filtro desactivado: Mostrando todos los archivos incluyendo idénticos. Total: " + std::to_wstring(g_previewDisplayedIndices.size()) + L" elementos.");
                    }
                    break;
                case IDM_VIEW_SORT_ACTION:
                    g_previewSortOrder = PreviewSortOrder::ByAction;
                    UpdatePreviewList();
                    Speech::Speak(L"Vista previa ordenada por acción prevista");
                    break;
                case IDM_VIEW_SORT_NAME:
                    g_previewSortOrder = PreviewSortOrder::ByName;
                    UpdatePreviewList();
                    Speech::Speak(L"Vista previa ordenada por nombre de archivo");
                    break;
                case IDM_VIEW_SORT_SIZE:
                    g_previewSortOrder = PreviewSortOrder::BySize;
                    UpdatePreviewList();
                    Speech::Speak(L"Vista previa ordenada por tamaño");
                    break;

                case IDM_PROJECT_EXPORT_TXT:
                    ExportSummaryReport();
                    break;

                case IDC_BTN_SETTINGS:
                case IDM_TOOLS_SETTINGS:
                    ShowSettings();
                    break;

                case IDM_TOOLS_VIEW_LOG:
                    Speech::Speak(L"Abriendo archivo de registro sync_accesible.log");
                    Logger::OpenLogInDefaultEditor(hwnd);
                    break;

                case IDM_HELP_SHORTCUTS:
                    Dialogs::ShowShortcutsDialog(hwnd);
                    break;

                case IDM_HELP_MANUAL:
                    Dialogs::OpenManualHtml(hwnd);
                    break;
                case IDM_HELP_ABOUT:
                    Dialogs::ShowAboutDialog(hwnd);
                    break;

                case ID_TRAY_RESTORE:
                    ShowWindow(hwnd, SW_RESTORE);
                    SetForegroundWindow(hwnd);
                    break;
                case ID_TRAY_SYNC_ALL:
                    StartSyncAllSequential();
                    break;
                case ID_TRAY_PAUSE:
                    Scheduler::Pause(!Scheduler::IsPaused());
                    break;
                case ID_TRAY_EXIT:
                    DestroyWindow(hwnd);
                    break;
            }
            return 0;
        }

        case WM_SYNC_PROGRESS: {
            int percent = (int)wParam;
            std::wstring* pText = (std::wstring*)lParam;
            if (percent < 0) percent = 0;
            if (percent > 100) percent = 100;

            SendMessageW(g_hProgressBar, PBM_SETPOS, (WPARAM)percent, 0);

            if (pText) {
                std::wstring statusMsg = L"[" + std::to_wstring(percent) + L"%] " + *pText;
                SendMessageW(g_hStatusBar, SB_SETTEXTW, 0, (LPARAM)statusMsg.c_str());
                delete pText;
            }

            // Anuncio por voz cada 25% (25%, 50%, 75%)
            int milestone = percent / 25;
            if (milestone > g_lastProgressMilestone && milestone < 4) {
                g_lastProgressMilestone = milestone;
                Speech::Speak(std::to_wstring(milestone * 25) + L" por ciento", false);
            }
            return 0;
        }

        case WM_COMPARE_DONE: {
            Speech::StopWaitingBeep();
            g_lastProgressMilestone = 0;
            SendMessageW(g_hProgressBar, PBM_SETPOS, 100, 0);
            g_isBusy.store(false);
            UpdatePreviewList();
            SwitchTab(1);

            int toDelete = g_currentSummary.files_to_delete_right + g_currentSummary.files_to_delete_left;
            std::wstring msg = L"Comparacion completada: " + std::to_wstring(g_currentSummary.total_files_to_sync) +
                               L" archivos por sincronizar (" + FormatFileSize(g_currentSummary.total_bytes_to_sync) + L")";
            if (toDelete > 0) {
                msg += L", " + std::to_wstring(toDelete) + L" para eliminar";
            }
            msg += L".";
            SendMessageW(g_hStatusBar, SB_SETTEXTW, 0, (LPARAM)msg.c_str());

            Speech::Speak(msg);
            if (g_settings.sound_alerts) Speech::PlayAlertSound();
            return 0;
        }

        case WM_SYNC_DONE: {
            Speech::StopWaitingBeep();
            g_lastProgressMilestone = 0;
            SendMessageW(g_hProgressBar, PBM_SETPOS, 100, 0);
            g_isBusy.store(false);
            bool ok = (wParam != 0);
            int idx = (int)lParam;

            if (idx >= 0 && idx < (int)g_currentProject.pairs.size()) {
                g_currentProject.pairs[idx].last_sync_status = ok ? L"Sincronizado" : L"Error";
                time_t t = time(nullptr);
                tm tmNow;
                localtime_s(&tmNow, &t);
                wchar_t tbuf[32];
                swprintf_s(tbuf, L"%02d:%02d:%02d", tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec);
                g_currentProject.pairs[idx].last_sync_time = tbuf;
                SaveAppConfig();
            }

            UpdatePairsList();
            UpdatePreviewList();

            if (ok) {
                std::wstring msg = L"Sincronización completada con éxito.";
                SendMessageW(g_hStatusBar, SB_SETTEXTW, 0, (LPARAM)msg.c_str());
                Speech::Speak(msg);
                if (g_settings.sound_alerts) Speech::PlayAlertSound();
            } else {
                std::vector<std::pair<std::wstring, std::wstring>> failedItems;
                for (const auto& itm : g_currentSummary.items) {
                    if (!itm.error_msg.empty() || itm.status.rfind(L"Error", 0) == 0) {
                        failedItems.push_back({itm.rel_path, itm.error_msg.empty() ? itm.status : itm.error_msg});
                    }
                }

                std::wstring spokenMsg;
                if (!failedItems.empty()) {
                    if (failedItems.size() == 1) {
                        spokenMsg = L"Error al sincronizar el archivo: " + failedItems[0].first + L". " + failedItems[0].second;
                    } else {
                        spokenMsg = L"Sincronización finalizada con " + std::to_wstring(failedItems.size()) + L" errores. Primer fallo en: " + failedItems[0].first + L". " + failedItems[0].second;
                    }
                } else {
                    spokenMsg = L"Sincronización finalizada con errores.";
                }

                SendMessageW(g_hStatusBar, SB_SETTEXTW, 0, (LPARAM)spokenMsg.c_str());
                Speech::Speak(spokenMsg);
                if (g_settings.sound_alerts) MessageBeep(MB_ICONHAND);

                // Switch to Vista Previa and focus first error item
                SwitchTab(1);
                if (!g_previewDisplayedIndices.empty()) {
                    ListView_SetItemState(g_hListPreview, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
                    ListView_SetItemState(g_hListPreview, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                    ListView_EnsureVisible(g_hListPreview, 0, FALSE);
                    SetFocus(g_hListPreview);
                }

                // Show accessible message dialog detailing the exact failed files
                std::wstring detailMsg = L"Se han producido errores al sincronizar los siguientes archivos:\n\n";
                for (size_t i = 0; i < failedItems.size() && i < 10; ++i) {
                    detailMsg += L"• Archivo: " + failedItems[i].first + L"\n  Causa: " + failedItems[i].second + L"\n\n";
                }
                if (failedItems.size() > 10) {
                    detailMsg += L"... y " + std::to_wstring(failedItems.size() - 10) + L" archivos más con error.\n\n";
                }
                detailMsg += L"Sugerencias:\n";
                detailMsg += L"1. Si el archivo está en uso por otra aplicación (como OneDrive o FreeFileSync), ciérrela o espere a que termine.\n";
                detailMsg += L"2. Si es un archivo que no necesita sincronizar (por ejemplo, sync.ffs_db), agréguelo a las Exclusiones del par (Ctrl + E).\n";
                detailMsg += L"3. Puede consultar todos los detalles en el menú Herramientas -> Ver Archivo de Registro (.log).";

                MessageBoxW(hwnd, detailMsg.c_str(), L"SyncAccesible - Errores en la Sincronización", MB_OK | MB_ICONWARNING);
            }
            return 0;
        }

        case WM_SEQ_STEP_DONE: {
            int idx = (int)wParam;
            if (idx >= 0 && idx < (int)g_currentProject.pairs.size()) {
                ListView_SetItemText(g_hListPairs, idx, 4, (LPWSTR)g_currentProject.pairs[idx].last_sync_status.c_str());
                ListView_SetItemText(g_hListPairs, idx, 5, (LPWSTR)g_currentProject.pairs[idx].last_sync_time.c_str());
            }
            return 0;
        }

        case WM_SEQ_ALL_DONE: {
            Speech::StopWaitingBeep();
            g_lastProgressMilestone = 0;
            SendMessageW(g_hProgressBar, PBM_SETPOS, 100, 0);
            g_isBusy.store(false);
            int successCount = (int)wParam;
            int errorCount = (int)lParam;

            SaveAppConfig();
            UpdatePairsList();

            std::wstring msg = L"Sincronización en serie finalizada: " + std::to_wstring(successCount) + L" completadas";
            if (errorCount > 0) {
                msg += L", " + std::to_wstring(errorCount) + L" con errores. Consulte la lista de vista previa y el archivo de registro.";
            } else {
                msg += L" con éxito.";
            }

            SendMessageW(g_hStatusBar, SB_SETTEXTW, 0, (LPARAM)msg.c_str());
            Speech::Speak(msg);
            if (g_settings.sound_alerts) {
                if (errorCount > 0) MessageBeep(MB_ICONHAND);
                else Speech::PlayAlertSound();
            }
            return 0;
        }

        case WM_TRAYICON: {
            if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
                ShowWindow(hwnd, SW_RESTORE);
                SetForegroundWindow(hwnd);
                Speech::Speak(L"SyncAccesible restaurado.");
            } else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
                ShowTrayContextMenu(hwnd);
            }
            return 0;
        }

        case WM_CLOSE: {
            if (g_settings.minimize_to_tray_on_close) {
                ShowWindow(hwnd, SW_HIDE);
                Speech::Speak(L"SyncAccesible sigue ejecutandose en la bandeja del sistema.");
            } else {
                DestroyWindow(hwnd);
            }
            return 0;
        }

        case WM_DESTROY: {
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            if (g_hFont) DeleteObject(g_hFont);
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int Run(HINSTANCE hInstance, int nCmdShow) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring dir = exePath;
    size_t lastSlash = dir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) dir = dir.substr(0, lastSlash);
    g_configPath = dir + L"\\config.json";

    // Load configuration & current project
    JsonHelper::LoadFromFile(g_configPath, g_settings, g_currentProject);

    // Initialize speech & screen reader
    Speech::Initialize();
    Speech::SetEnabled(g_settings.speech_enabled);

    // Start background scheduler on current project pairs
    Scheduler::Start(&g_currentProject.pairs, [](const std::wstring& id, bool success, const std::wstring& msg) {
        if (g_hwndMain) {
            PostMessageW(g_hwndMain, WM_SYNC_DONE, success ? 1 : 0, -1);
        }
    });

    // Register Main Window Class
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"SyncAccesibleMainWindowClass";
    RegisterClassExW(&wc);

    // Create Main Window
    HWND hwnd = CreateWindowExW(
        0,
        L"SyncAccesibleMainWindowClass",
        L"SyncAccesible (C++) - Sincronizador Grafico Accesible",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 960, 620,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!hwnd) return 1;

    bool startMin = g_settings.start_minimized;
    if (wcsstr(GetCommandLineW(), L"--minimized") != nullptr ||
        wcsstr(GetCommandLineW(), L"-minimized") != nullptr ||
        wcsstr(GetCommandLineW(), L"/minimized") != nullptr) {
        startMin = true;
    }

    int showCmd = startMin ? SW_HIDE : nCmdShow;
    ShowWindow(hwnd, showCmd);
    if (!startMin) {
        UpdateWindow(hwnd);
    }

    // Keyboard Accelerators table
    ACCEL accels[] = {
        { FSHIFT | FVIRTKEY, VK_F1, IDM_HELP_SHORTCUTS },
        { FVIRTKEY, VK_F1, IDM_HELP_MANUAL },
        { FVIRTKEY, VK_F5, IDM_SYNC_COMPARE },
        { FVIRTKEY, VK_F6, IDM_SYNC_ONE },
        { FVIRTKEY, VK_F7, IDM_SYNC_SELECTED_SEQ },
        { FCONTROL | FVIRTKEY, VK_F6, IDM_SYNC_ALL_SEQ },
        { FALT | FVIRTKEY, 'L', IDM_SYNC_BATCH_QUEUE },
        { FCONTROL | FVIRTKEY, 'O', IDM_PROJECT_OPEN },
        { FCONTROL | FVIRTKEY, 'S', IDM_PROJECT_SAVE },
        { FCONTROL | FSHIFT | FVIRTKEY, 'S', IDM_PROJECT_SAVEAS },
        { FCONTROL | FSHIFT | FVIRTKEY, 'N', IDM_PROJECT_NEW },
        { FCONTROL | FVIRTKEY, 'N', IDM_EDIT_ADD_PAIR },
        { FCONTROL | FVIRTKEY, 'E', IDM_EDIT_EDIT_PAIR },
        { FCONTROL | FVIRTKEY, 'I', IDM_EDIT_INVERT_PAIR },
        { FVIRTKEY, VK_DELETE, IDM_EDIT_DEL_PAIR },
        { FCONTROL | FVIRTKEY, VK_TAB, IDM_VIEW_TAB_NEXT },
        { FCONTROL | FSHIFT | FVIRTKEY, VK_TAB, IDM_VIEW_TAB_NEXT },
        { FALT | FVIRTKEY, '1', IDM_VIEW_TAB_PAIRS },
        { FALT | FVIRTKEY, '2', IDM_VIEW_TAB_PREVIEW },
        { FCONTROL | FVIRTKEY, '1', IDM_VIEW_TAB_PAIRS },
        { FCONTROL | FVIRTKEY, '2', IDM_VIEW_TAB_PREVIEW },
        { FCONTROL | FVIRTKEY, 'P', IDM_PROJECT_EXPORT_TXT },
        { FCONTROL | FVIRTKEY, 'H', IDM_VIEW_FILTER_CHANGES },
        { FALT | FVIRTKEY, 'O', IDM_VIEW_SORT_ACTION }
    };
    HACCEL hAccel = CreateAcceleratorTableW(accels, sizeof(accels) / sizeof(ACCEL));

    // Message Loop
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) {
            bool ctrlDown  = (GetKeyState(VK_CONTROL) < 0);
            bool altDown   = (GetKeyState(VK_MENU) < 0);
            bool shiftDown = (GetKeyState(VK_SHIFT) < 0);

            if (msg.message == WM_KEYDOWN && msg.wParam == VK_F1 && shiftDown && !ctrlDown && !altDown) {
                Dialogs::ShowShortcutsDialog(hwnd);
                continue;
            } else if (msg.message == WM_KEYDOWN && msg.wParam == VK_F1 && !ctrlDown && !altDown && !shiftDown) {
                Dialogs::OpenManualHtml(hwnd);
                continue;
            } else if (ctrlDown && !altDown && !shiftDown && (msg.wParam == 'P' || msg.wParam == 'p')) {
                ExportSummaryReport();
                continue;
            } else if (ctrlDown && !altDown && !shiftDown && (msg.wParam == 'H' || msg.wParam == 'h')) {
                g_filterShowOnlyChanges = !g_filterShowOnlyChanges;
                UpdatePreviewList();
                if (g_filterShowOnlyChanges) {
                    Speech::Speak(L"Filtro activado: Mostrando solo archivos con cambios. Total: " + std::to_wstring(g_previewDisplayedIndices.size()) + L" elementos.");
                } else {
                    Speech::Speak(L"Filtro desactivado: Mostrando todos los archivos incluyendo idénticos. Total: " + std::to_wstring(g_previewDisplayedIndices.size()) + L" elementos.");
                }
                continue;
            } else if (altDown && !ctrlDown && !shiftDown && (msg.wParam == 'O' || msg.wParam == 'o')) {
                g_previewSortOrder = PreviewSortOrder::ByAction;
                UpdatePreviewList();
                Speech::Speak(L"Vista previa ordenada por acción prevista");
                continue;
            } else if (ctrlDown && !altDown && msg.wParam == VK_TAB) {
                int cur = TabCtrl_GetCurSel(g_hTab);
                SwitchTab((cur == 0) ? 1 : 0);
                continue;
            } else if (altDown && !ctrlDown && !shiftDown && (msg.wParam == '1' || msg.wParam == VK_NUMPAD1)) {
                SwitchTab(0);
                continue;
            } else if (altDown && !ctrlDown && !shiftDown && (msg.wParam == '2' || msg.wParam == VK_NUMPAD2)) {
                SwitchTab(1);
                continue;
            } else if (ctrlDown && !altDown && !shiftDown && (msg.wParam == '1' || msg.wParam == VK_NUMPAD1)) {
                SwitchTab(0);
                continue;
            } else if (ctrlDown && !altDown && !shiftDown && (msg.wParam == '2' || msg.wParam == VK_NUMPAD2)) {
                SwitchTab(1);
                continue;
            } else if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE && !ctrlDown && !altDown) {
                HWND hFocus = GetFocus();
                if (hFocus == g_hListPairs || hFocus == g_hListPreview || msg.hwnd == hwnd) {
                    ShowWindow(hwnd, SW_HIDE);
                    Speech::Speak(L"SyncAccesible minimizado a la bandeja del sistema.");
                    continue;
                }
            }
        }

        if (!TranslateAcceleratorW(hwnd, hAccel, &msg)) {
            bool bypassDialog = false;
            if (msg.message == WM_KEYDOWN || msg.message == WM_KEYUP) {
                if (msg.wParam == VK_LEFT || msg.wParam == VK_RIGHT) {
                    HWND hFocus = GetFocus();
                    if (hFocus == g_hListPairs || hFocus == g_hListPreview || msg.hwnd == g_hListPairs || msg.hwnd == g_hListPreview) {
                        bypassDialog = true;
                    }
                }
            }

            if (bypassDialog || !IsDialogMessageW(hwnd, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }

    // Cleanup
    DestroyAcceleratorTable(hAccel);
    Scheduler::Stop();
    Speech::Shutdown();

    return (int)msg.wParam;
}

} // namespace Gui
