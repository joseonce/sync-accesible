#include "dialogs.h"
#include "json_helper.h"
#include "sync_engine.h"
#include "speech.h"
#include "logger.h"
#include <commctrl.h>
#include <shlobj.h>
#include <commdlg.h>
#include <sstream>
#include <thread>
#include <atomic>
#include <vector>

namespace Dialogs {

static HFONT GetAppFont() {
    NONCLIENTMETRICSW ncm = { sizeof(NONCLIENTMETRICSW) };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    return CreateFontIndirectW(&ncm.lfMessageFont);
}

static std::wstring BrowseForFolder(HWND hwndParent, const std::wstring& title) {
    std::wstring result;
    BROWSEINFOW bi = {0};
    bi.hwndOwner = hwndParent;
    bi.lpszTitle = title.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        wchar_t path[MAX_PATH];
        if (SHGetPathFromIDListW(pidl, path)) {
            result = path;
        }
        CoTaskMemFree(pidl);
    }
    return result;
}

bool PromptOpenProject(HWND hwndParent, std::wstring& outPath) {
    OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
    wchar_t szFile[MAX_PATH] = L"";
    ofn.hwndOwner = hwndParent;
    ofn.lpstrFilter = L"Proyectos Sync (*.syncproj;*.json)\0*.syncproj;*.json\0Todos los archivos (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    ofn.lpstrDefExt = L"syncproj";
    if (GetOpenFileNameW(&ofn)) {
        outPath = szFile;
        return true;
    }
    return false;
}

bool PromptSaveProject(HWND hwndParent, std::wstring& inOutPath) {
    OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
    wchar_t szFile[MAX_PATH] = L"";
    if (!inOutPath.empty()) {
        wcsncpy_s(szFile, inOutPath.c_str(), MAX_PATH - 1);
    }
    ofn.hwndOwner = hwndParent;
    ofn.lpstrFilter = L"Proyectos Sync (*.syncproj)\0*.syncproj\0Archivos JSON (*.json)\0*.json\0Todos los archivos (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = L"syncproj";
    if (GetSaveFileNameW(&ofn)) {
        inOutPath = szFile;
        return true;
    }
    return false;
}

// -------------------------------------------------------------
// PAIR DIALOG (Add / Edit Folder Pair)
// -------------------------------------------------------------
struct PairDlgData {
    FolderPair* pPair;
    bool isNew;
    bool saved = false;
    HFONT hFont = nullptr;
    HWND hEditName = nullptr;
    HWND hEditLeft = nullptr;
    HWND hBtnBrowseLeft = nullptr;
    HWND hEditRight = nullptr;
    HWND hBtnBrowseRight = nullptr;
    HWND hComboMode = nullptr;
    HWND hComboSched = nullptr;
    HWND hEditInterval = nullptr;
    HWND hEditDaily = nullptr;
    HWND hEditExclusions = nullptr;
    HWND hCheckEnabled = nullptr;
};

static LRESULT CALLBACK PairDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PairDlgData* data = (PairDlgData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
            data = (PairDlgData*)cs->lpCreateParams;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)data);

            data->hFont = GetAppFont();
            auto makeCtrl = [&](DWORD exStyle, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) -> HWND {
                HWND hCtrl = CreateWindowExW(exStyle, cls, text, style | WS_CHILD | WS_VISIBLE, x, y, w, h, hwnd, (HMENU)(INT_PTR)id, cs->hInstance, nullptr);
                SendMessageW(hCtrl, WM_SETFONT, (WPARAM)data->hFont, TRUE);
                return hCtrl;
            };

            int y = 15;
            // Name
            makeCtrl(0, L"STATIC", L"&Nombre del par de carpetas:", WS_CHILD, 15, y, 150, 20, 100);
            data->hEditName = makeCtrl(WS_EX_CLIENTEDGE, L"EDIT", data->pPair->name.c_str(), WS_TABSTOP | ES_AUTOHSCROLL, 170, y - 2, 290, 24, 101);
            y += 35;

            // Left Folder
            makeCtrl(0, L"STATIC", L"Carpeta &Origen (Izquierda):", WS_CHILD, 15, y, 150, 20, 102);
            data->hEditLeft = makeCtrl(WS_EX_CLIENTEDGE, L"EDIT", data->pPair->left_folder.c_str(), WS_TABSTOP | ES_AUTOHSCROLL, 170, y - 2, 205, 24, 103);
            data->hBtnBrowseLeft = makeCtrl(0, L"BUTTON", L"E&xaminar...", WS_TABSTOP | BS_PUSHBUTTON, 385, y - 2, 75, 24, 104);
            y += 35;

            // Right Folder
            makeCtrl(0, L"STATIC", L"Carpeta &Destino (Derecha):", WS_CHILD, 15, y, 150, 20, 105);
            data->hEditRight = makeCtrl(WS_EX_CLIENTEDGE, L"EDIT", data->pPair->right_folder.c_str(), WS_TABSTOP | ES_AUTOHSCROLL, 170, y - 2, 205, 24, 106);
            data->hBtnBrowseRight = makeCtrl(0, L"BUTTON", L"Ex&aminar...", WS_TABSTOP | BS_PUSHBUTTON, 385, y - 2, 75, 24, 107);
            y += 35;

            // Invert / Swap Folders Button
            makeCtrl(0, L"BUTTON", L"&Invertir Carpetas (Origen <-> Destino)", WS_TABSTOP | BS_PUSHBUTTON, 170, y - 2, 290, 26, 119);
            y += 35;

            // Sync Mode
            makeCtrl(0, L"STATIC", L"&Modo de sincronizacion:", WS_CHILD, 15, y, 150, 20, 108);
            data->hComboMode = makeCtrl(0, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, 170, y - 2, 290, 150, 109);
            SendMessageW(data->hComboMode, CB_ADDSTRING, 0, (LPARAM)L"Espejo (Mirror - Destino identico a Origen)");
            SendMessageW(data->hComboMode, CB_ADDSTRING, 0, (LPARAM)L"Bidireccional (Two-Way - Mas recientes en ambos)");
            SendMessageW(data->hComboMode, CB_ADDSTRING, 0, (LPARAM)L"Actualizar (Update - Copia nuevos/modificados)");
            SendMessageW(data->hComboMode, CB_SETCURSEL, (WPARAM)data->pPair->mode, 0);
            y += 35;

            // Schedule Type
            makeCtrl(0, L"STATIC", L"&Programacion automatica:", WS_CHILD, 15, y, 150, 20, 110);
            data->hComboSched = makeCtrl(0, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, 170, y - 2, 290, 150, 111);
            SendMessageW(data->hComboSched, CB_ADDSTRING, 0, (LPARAM)L"Manual (Solo cuando se solicite)");
            SendMessageW(data->hComboSched, CB_ADDSTRING, 0, (LPARAM)L"Intervalo de minutos");
            SendMessageW(data->hComboSched, CB_ADDSTRING, 0, (LPARAM)L"Diaria a una hora fija");
            SendMessageW(data->hComboSched, CB_ADDSTRING, 0, (LPARAM)L"Tiempo real (Watchdog al haber cambios)");
            SendMessageW(data->hComboSched, CB_SETCURSEL, (WPARAM)data->pPair->schedule_type, 0);
            y += 35;

            // Interval & Daily parameters
            makeCtrl(0, L"STATIC", L"Cada &minutos:", WS_CHILD, 15, y, 90, 20, 112);
            data->hEditInterval = makeCtrl(WS_EX_CLIENTEDGE, L"EDIT", std::to_wstring(data->pPair->interval_minutes).c_str(), WS_TABSTOP | ES_NUMBER, 110, y - 2, 50, 24, 113);

            makeCtrl(0, L"STATIC", L"&Hora diaria (HH:MM):", WS_CHILD, 185, y, 140, 20, 114);
            data->hEditDaily = makeCtrl(WS_EX_CLIENTEDGE, L"EDIT", data->pPair->daily_time.c_str(), WS_TABSTOP, 330, y - 2, 60, 24, 115);
            y += 35;

            // Exclusions
            makeCtrl(0, L"STATIC", L"Patrones a &excluir (separados por coma):", WS_CHILD, 15, y, 400, 20, 116);
            y += 22;
            std::wstring excStr;
            for (size_t i = 0; i < data->pPair->exclusions.size(); ++i) {
                excStr += data->pPair->exclusions[i];
                if (i + 1 < data->pPair->exclusions.size()) excStr += L", ";
            }
            data->hEditExclusions = makeCtrl(WS_EX_CLIENTEDGE, L"EDIT", excStr.c_str(), WS_TABSTOP | ES_AUTOHSCROLL, 15, y, 445, 24, 117);
            y += 35;

            // Enabled Checkbox
            data->hCheckEnabled = makeCtrl(0, L"BUTTON", L"Par de carpetas &activado", WS_TABSTOP | BS_AUTOCHECKBOX, 15, y, 200, 22, 118);
            SendMessageW(data->hCheckEnabled, BM_SETCHECK, data->pPair->enabled ? BST_CHECKED : BST_UNCHECKED, 0);
            y += 35;

            // Buttons: Save & Cancel
            makeCtrl(0, L"BUTTON", L"&Guardar", WS_TABSTOP | BS_DEFPUSHBUTTON, 280, y, 85, 28, IDOK);
            makeCtrl(0, L"BUTTON", L"&Cancelar", WS_TABSTOP | BS_PUSHBUTTON, 375, y, 85, 28, IDCANCEL);

            SetFocus(data->hEditName);
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == 104) { // Browse Left
                std::wstring dir = BrowseForFolder(hwnd, L"Seleccione la carpeta Origen (Izquierda)");
                if (!dir.empty()) SetWindowTextW(data->hEditLeft, dir.c_str());
            } else if (id == 107) { // Browse Right
                std::wstring dir = BrowseForFolder(hwnd, L"Seleccione la carpeta Destino (Derecha)");
                if (!dir.empty()) SetWindowTextW(data->hEditRight, dir.c_str());
            } else if (id == 119) { // Invert Left and Right folders
                wchar_t bufL[1024] = {0};
                wchar_t bufR[1024] = {0};
                GetWindowTextW(data->hEditLeft, bufL, 1024);
                GetWindowTextW(data->hEditRight, bufR, 1024);
                SetWindowTextW(data->hEditLeft, bufR);
                SetWindowTextW(data->hEditRight, bufL);
                Speech::Speak(L"Carpetas invertidas. Origen y destino intercambiados.");
                SetFocus(data->hEditLeft);
            } else if (id == IDOK) {
                wchar_t buf[1024];

                GetWindowTextW(data->hEditName, buf, 1024);
                data->pPair->name = buf;
                if (data->pPair->name.empty()) data->pPair->name = L"Sin nombre";

                GetWindowTextW(data->hEditLeft, buf, 1024);
                data->pPair->left_folder = buf;

                GetWindowTextW(data->hEditRight, buf, 1024);
                data->pPair->right_folder = buf;

                data->pPair->mode = (SyncMode)SendMessageW(data->hComboMode, CB_GETCURSEL, 0, 0);
                data->pPair->schedule_type = (ScheduleType)SendMessageW(data->hComboSched, CB_GETCURSEL, 0, 0);

                GetWindowTextW(data->hEditInterval, buf, 1024);
                try { data->pPair->interval_minutes = std::stoi(buf); } catch (...) { data->pPair->interval_minutes = 30; }

                GetWindowTextW(data->hEditDaily, buf, 1024);
                data->pPair->daily_time = buf;

                data->pPair->enabled = (SendMessageW(data->hCheckEnabled, BM_GETCHECK, 0, 0) == BST_CHECKED);

                // Parse exclusions
                GetWindowTextW(data->hEditExclusions, buf, 1024);
                std::wstring rawExc = buf;
                data->pPair->exclusions.clear();
                std::wstringstream wss(rawExc);
                std::wstring token;
                while (std::getline(wss, token, L',')) {
                    while (!token.empty() && token.front() == L' ') token.erase(0, 1);
                    while (!token.empty() && token.back() == L' ') token.pop_back();
                    if (!token.empty()) data->pPair->exclusions.push_back(token);
                }

                data->saved = true;
                DestroyWindow(hwnd);
            } else if (id == IDCANCEL) {
                data->saved = false;
                DestroyWindow(hwnd);
            }
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (data->hFont) DeleteObject(data->hFont);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool ShowPairDialog(HWND hwndParent, FolderPair& pair, bool isNew) {
    static bool registered = false;
    HINSTANCE hInst = GetModuleHandleW(nullptr);

    if (!registered) {
        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = PairDlgProc;
        wc.hInstance = hInst;
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"SyncPairDialogClass";
        RegisterClassExW(&wc);
        registered = true;
    }

    PairDlgData data;
    data.pPair = &pair;
    data.isNew = isNew;

    std::wstring title = isNew ? L"Añadir Nuevo Par de Carpetas" : L"Editar Par de Carpetas";

    RECT rcParent;
    GetWindowRect(hwndParent, &rcParent);
    int w = 490, h = 415;
    int x = rcParent.left + ((rcParent.right - rcParent.left) - w) / 2;
    int y = rcParent.top + ((rcParent.bottom - rcParent.top) - h) / 2;

    HWND hwndDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"SyncPairDialogClass",
        title.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, w, h,
        hwndParent,
        nullptr,
        hInst,
        &data
    );

    EnableWindow(hwndParent, FALSE);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hwndDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(hwndParent, TRUE);
    SetForegroundWindow(hwndParent);

    return data.saved;
}

// -------------------------------------------------------------
// SETTINGS DIALOG
// -------------------------------------------------------------
static bool IsAutoStartEnabled() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t buffer[MAX_PATH];
        DWORD bytes = sizeof(buffer);
        DWORD type = 0;
        LONG res = RegQueryValueExW(hKey, L"SyncAccesible", nullptr, &type, (LPBYTE)buffer, &bytes);
        RegCloseKey(hKey);
        return (res == ERROR_SUCCESS);
    }
    return false;
}

static void SetAutoStartEnabled(bool enable) {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        if (enable) {
            wchar_t exePath[MAX_PATH];
            GetModuleFileNameW(nullptr, exePath, MAX_PATH);
            std::wstring cmd = L"\"" + std::wstring(exePath) + L"\" --minimized";
            RegSetValueExW(hKey, L"SyncAccesible", 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)(cmd.size() + 1) * sizeof(wchar_t));
        } else {
            RegDeleteValueW(hKey, L"SyncAccesible");
        }
        RegCloseKey(hKey);
    }
}

struct SettingsDlgData {
    AppSettings* pSettings;
    bool saved = false;
    HFONT hFont = nullptr;
    HWND hCheckStartWithWin = nullptr;
    HWND hCheckStartMin = nullptr;
    HWND hCheckMinClose = nullptr;
    HWND hCheckSpeech = nullptr;
    HWND hCheckSound = nullptr;
    HWND hCheckAutoSync = nullptr;
};

static LRESULT CALLBACK SettingsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SettingsDlgData* data = (SettingsDlgData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
            data = (SettingsDlgData*)cs->lpCreateParams;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)data);

            data->hFont = GetAppFont();
            auto makeCheck = [&](const wchar_t* text, bool checked, int y, int id) -> HWND {
                HWND hCtrl = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 20, y, 360, 24, hwnd, (HMENU)(INT_PTR)id, cs->hInstance, nullptr);
                SendMessageW(hCtrl, WM_SETFONT, (WPARAM)data->hFont, TRUE);
                SendMessageW(hCtrl, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
                return hCtrl;
            };

            data->pSettings->start_with_windows = IsAutoStartEnabled();

            int y = 16;
            data->hCheckStartWithWin = makeCheck(L"&Iniciar con Windows", data->pSettings->start_with_windows, y, 200); y += 30;
            data->hCheckStartMin = makeCheck(L"&Iniciar minimizado en la bandeja del sistema", data->pSettings->start_minimized, y, 201); y += 30;
            data->hCheckMinClose = makeCheck(L"&Minimizar a la bandeja al pulsar cerrar (X)", data->pSettings->minimize_to_tray_on_close, y, 202); y += 30;
            data->hCheckSpeech = makeCheck(L"&Activar voz para lectores de pantalla (NVDA/JAWS/SAPI)", data->pSettings->speech_enabled, y, 203); y += 30;
            data->hCheckSound = makeCheck(L"Alertas &sonoras de finalizacion", data->pSettings->sound_alerts, y, 204); y += 30;
            data->hCheckAutoSync = makeCheck(L"Sincronizar &automaticamente al arrancar", data->pSettings->auto_sync_on_startup, y, 205); y += 36;

            HWND hBtnOk = CreateWindowExW(0, L"BUTTON", L"&Aceptar", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 200, y, 85, 28, hwnd, (HMENU)IDOK, cs->hInstance, nullptr);
            SendMessageW(hBtnOk, WM_SETFONT, (WPARAM)data->hFont, TRUE);

            HWND hBtnCancel = CreateWindowExW(0, L"BUTTON", L"&Cancelar", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 295, y, 85, 28, hwnd, (HMENU)IDCANCEL, cs->hInstance, nullptr);
            SendMessageW(hBtnCancel, WM_SETFONT, (WPARAM)data->hFont, TRUE);

            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == IDOK) {
                data->pSettings->start_with_windows = (SendMessageW(data->hCheckStartWithWin, BM_GETCHECK, 0, 0) == BST_CHECKED);
                SetAutoStartEnabled(data->pSettings->start_with_windows);

                data->pSettings->start_minimized = (SendMessageW(data->hCheckStartMin, BM_GETCHECK, 0, 0) == BST_CHECKED);
                data->pSettings->minimize_to_tray_on_close = (SendMessageW(data->hCheckMinClose, BM_GETCHECK, 0, 0) == BST_CHECKED);
                data->pSettings->speech_enabled = (SendMessageW(data->hCheckSpeech, BM_GETCHECK, 0, 0) == BST_CHECKED);
                data->pSettings->sound_alerts = (SendMessageW(data->hCheckSound, BM_GETCHECK, 0, 0) == BST_CHECKED);
                data->pSettings->auto_sync_on_startup = (SendMessageW(data->hCheckAutoSync, BM_GETCHECK, 0, 0) == BST_CHECKED);

                data->saved = true;
                DestroyWindow(hwnd);
            } else if (id == IDCANCEL) {
                data->saved = false;
                DestroyWindow(hwnd);
            }
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (data->hFont) DeleteObject(data->hFont);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool ShowSettingsDialog(HWND hwndParent, AppSettings& settings) {
    static bool registered = false;
    HINSTANCE hInst = GetModuleHandleW(nullptr);

    if (!registered) {
        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = SettingsDlgProc;
        wc.hInstance = hInst;
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"SyncSettingsDialogClass";
        RegisterClassExW(&wc);
        registered = true;
    }

    SettingsDlgData data;
    data.pSettings = &settings;

    RECT rcParent;
    GetWindowRect(hwndParent, &rcParent);
    int w = 420, h = 295;
    int x = rcParent.left + ((rcParent.right - rcParent.left) - w) / 2;
    int y = rcParent.top + ((rcParent.bottom - rcParent.top) - h) / 2;

    HWND hwndDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"SyncSettingsDialogClass",
        L"Configuracion General - SyncAccesible",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, w, h,
        hwndParent,
        nullptr,
        hInst,
        &data
    );

    EnableWindow(hwndParent, FALSE);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hwndDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(hwndParent, TRUE);
    SetForegroundWindow(hwndParent);

    return data.saved;
}

// -------------------------------------------------------------
// BATCH RUNNER DIALOG (Ejecución de múltiples proyectos en cola)
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
                                return 0;
                            } else if (wParam == VK_LEFT) {
                                if (pState->currentCol > 0) {
                                    pState->currentCol--;
                                    AnnounceVirtualColumn(hWnd, focusedRow, pState->currentCol, false, true);
                                } else {
                                    AnnounceVirtualColumn(hWnd, focusedRow, pState->currentCol, true, true);
                                }
                                return 0;
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

#define WM_BATCH_ITEM_UPDATE (WM_USER + 50)
#define WM_BATCH_FINISHED    (WM_USER + 51)

#define IDC_BATCH_LIST        501
#define IDC_BATCH_ADD_FILE    502
#define IDC_BATCH_ADD_CURRENT 503
#define IDC_BATCH_REMOVE      504
#define IDC_BATCH_MOVE_UP     505
#define IDC_BATCH_MOVE_DOWN   506
#define IDC_BATCH_START       507
#define IDC_BATCH_STOP        508

struct BatchDlgData {
    HFONT hFont = nullptr;
    HWND hList = nullptr;
    HWND hBtnStart = nullptr;
    HWND hBtnStop = nullptr;
    HWND hStatusStatic = nullptr;
    const AppSettings* pSettings = nullptr;
    const SyncProject* pCurrentProject = nullptr;

    std::vector<BatchTaskItem> queue;
    std::atomic<bool> isRunning{false};
    std::atomic<bool> cancelRequested{false};
};

static void RefreshBatchList(HWND hList, const std::vector<BatchTaskItem>& queue) {
    ListView_DeleteAllItems(hList);
    for (size_t i = 0; i < queue.size(); ++i) {
        const auto& item = queue[i];
        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = (int)i;
        lvi.iSubItem = 0;
        std::wstring numStr = std::to_wstring(i + 1);
        lvi.pszText = (LPWSTR)numStr.c_str();
        ListView_InsertItem(hList, &lvi);

        ListView_SetItemText(hList, (int)i, 1, (LPWSTR)item.project_name.c_str());
        ListView_SetItemText(hList, (int)i, 2, (LPWSTR)item.pair.name.c_str());
        ListView_SetItemText(hList, (int)i, 3, (LPWSTR)SyncModeToString(item.pair.mode));
        ListView_SetItemText(hList, (int)i, 4, (LPWSTR)item.status.c_str());
        ListView_SetItemText(hList, (int)i, 5, (LPWSTR)item.pair.left_folder.c_str());
        ListView_SetItemText(hList, (int)i, 6, (LPWSTR)item.pair.right_folder.c_str());
    }
}

static LRESULT CALLBACK BatchDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    BatchDlgData* data = (BatchDlgData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
            data = (BatchDlgData*)cs->lpCreateParams;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)data);

            data->hFont = GetAppFont();
            auto makeBtn = [&](const wchar_t* text, int x, int y, int w, int h, int id) -> HWND {
                HWND hCtrl = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, x, y, w, h, hwnd, (HMENU)(INT_PTR)id, cs->hInstance, nullptr);
                SendMessageW(hCtrl, WM_SETFONT, (WPARAM)data->hFont, TRUE);
                return hCtrl;
            };

            // Top action buttons
            int bx = 15;
            makeBtn(L"&Añadir Proyecto...", bx, 12, 140, 28, IDC_BATCH_ADD_FILE); bx += 148;
            makeBtn(L"Añadir Pares &Actuales", bx, 12, 160, 28, IDC_BATCH_ADD_CURRENT); bx += 168;
            makeBtn(L"&Quitar de la Cola", bx, 12, 130, 28, IDC_BATCH_REMOVE); bx += 138;
            makeBtn(L"&Subir", bx, 12, 70, 28, IDC_BATCH_MOVE_UP); bx += 78;
            makeBtn(L"&Bajar", bx, 12, 70, 28, IDC_BATCH_MOVE_DOWN);

            // Queue List View
            data->hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 15, 48, 810, 340, hwnd, (HMENU)IDC_BATCH_LIST, cs->hInstance, nullptr);
            SendMessageW(data->hList, WM_SETFONT, (WPARAM)data->hFont, TRUE);
            ListView_SetExtendedListViewStyle(data->hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

            auto addCol = [&](int idx, const wchar_t* name, int w) {
                LVCOLUMNW lvc = {0};
                lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
                lvc.cx = w;
                lvc.pszText = (LPWSTR)name;
                ListView_InsertColumn(data->hList, idx, &lvc);
            };

            addCol(0, L"#", 40);
            addCol(1, L"Proyecto", 140);
            addCol(2, L"Par de Carpetas", 150);
            addCol(3, L"Modo", 110);
            addCol(4, L"Estado", 140);
            addCol(5, L"Origen", 160);
            addCol(6, L"Destino", 160);
            EnableVirtualColumnNavigation(data->hList, 103);

            // Populate initially with current project's pairs if available
            if (data->pCurrentProject && !data->pCurrentProject->pairs.empty()) {
                for (const auto& p : data->pCurrentProject->pairs) {
                    BatchTaskItem bi;
                    bi.project_name = data->pCurrentProject->name;
                    bi.pair = p;
                    bi.status = L"En cola";
                    data->queue.push_back(bi);
                }
                RefreshBatchList(data->hList, data->queue);
            }

            // Bottom controls
            data->hStatusStatic = CreateWindowExW(0, L"STATIC", L"Listo para ejecutar en serie.", WS_CHILD | WS_VISIBLE, 15, 400, 480, 24, hwnd, nullptr, cs->hInstance, nullptr);
            SendMessageW(data->hStatusStatic, WM_SETFONT, (WPARAM)data->hFont, TRUE);

            data->hBtnStart = makeBtn(L"&Iniciar Ejecución en Secuencia", 500, 396, 220, 32, IDC_BATCH_START);
            data->hBtnStop = makeBtn(L"&Detener", 728, 396, 95, 32, IDC_BATCH_STOP);
            EnableWindow(data->hBtnStop, FALSE);

            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == IDC_BATCH_ADD_FILE) {
                std::wstring prjPath;
                if (PromptOpenProject(hwnd, prjPath)) {
                    SyncProject extProject;
                    if (JsonHelper::LoadProjectFromFile(prjPath, extProject)) {
                        for (const auto& p : extProject.pairs) {
                            BatchTaskItem bi;
                            bi.project_name = extProject.name;
                            bi.pair = p;
                            bi.status = L"En cola";
                            data->queue.push_back(bi);
                        }
                        RefreshBatchList(data->hList, data->queue);
                        Speech::Speak(L"Añadidos " + std::to_wstring(extProject.pairs.size()) + L" pares de carpetas del proyecto " + extProject.name);
                    }
                }
            } else if (id == IDC_BATCH_ADD_CURRENT) {
                if (data->pCurrentProject) {
                    for (const auto& p : data->pCurrentProject->pairs) {
                        BatchTaskItem bi;
                        bi.project_name = data->pCurrentProject->name;
                        bi.pair = p;
                        bi.status = L"En cola";
                        data->queue.push_back(bi);
                    }
                    RefreshBatchList(data->hList, data->queue);
                    Speech::Speak(L"Pares de carpetas del proyecto actual añadidos a la cola.");
                }
            } else if (id == IDC_BATCH_REMOVE) {
                int sel = ListView_GetNextItem(data->hList, -1, LVNI_SELECTED);
                if (sel >= 0 && sel < (int)data->queue.size()) {
                    data->queue.erase(data->queue.begin() + sel);
                    RefreshBatchList(data->hList, data->queue);
                    if (!data->queue.empty()) {
                        int nextSel = (sel >= (int)data->queue.size()) ? (int)data->queue.size() - 1 : sel;
                        ListView_SetItemState(data->hList, nextSel, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                    }
                    Speech::Speak(L"Elemento quitado de la cola.");
                }
            } else if (id == IDC_BATCH_MOVE_UP) {
                int sel = ListView_GetNextItem(data->hList, -1, LVNI_SELECTED);
                if (sel > 0 && sel < (int)data->queue.size()) {
                    std::swap(data->queue[sel], data->queue[sel - 1]);
                    RefreshBatchList(data->hList, data->queue);
                    ListView_SetItemState(data->hList, sel - 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                }
            } else if (id == IDC_BATCH_MOVE_DOWN) {
                int sel = ListView_GetNextItem(data->hList, -1, LVNI_SELECTED);
                if (sel >= 0 && sel + 1 < (int)data->queue.size()) {
                    std::swap(data->queue[sel], data->queue[sel + 1]);
                    RefreshBatchList(data->hList, data->queue);
                    ListView_SetItemState(data->hList, sel + 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                }
            } else if (id == IDC_BATCH_START) {
                if (data->queue.empty()) {
                    Speech::Speak(L"La cola de sincronizacion esta vacia.");
                    return 0;
                }
                if (data->isRunning.load()) return 0;

                data->isRunning.store(true);
                data->cancelRequested.store(false);
                EnableWindow(data->hBtnStart, FALSE);
                EnableWindow(data->hBtnStop, TRUE);

                Speech::Speak(L"Iniciando ejecucion en secuencia de " + std::to_wstring(data->queue.size()) + L" elementos.");
                Speech::StartWaitingBeep();

                // Launch worker thread
                std::thread([hwnd, data]() {
                    int totalSuccess = 0;
                    int totalErrors = 0;

                    try {
                        for (size_t i = 0; i < data->queue.size(); ++i) {
                            if (data->cancelRequested.load()) break;

                            auto& item = data->queue[i];
                            item.status = L"Comparando...";
                            PostMessageW(hwnd, WM_BATCH_ITEM_UPDATE, (WPARAM)i, 0);

                            std::wstring announceStart = L"Elemento " + std::to_wstring(i + 1) + L" de " +
                                                         std::to_wstring(data->queue.size()) + L": " + item.pair.name;
                            Speech::Speak(announceStart);

                            ComparisonSummary summary = SyncEngine::CompareFolders(item.pair, &data->cancelRequested, nullptr);

                            if (data->cancelRequested.load()) {
                                item.status = L"Cancelado";
                                PostMessageW(hwnd, WM_BATCH_ITEM_UPDATE, (WPARAM)i, 0);
                                break;
                            }

                            if (summary.total_files_to_sync == 0) {
                                item.status = L"Al dia (0 cambios)";
                                item.files_synced = 0;
                                totalSuccess++;
                                PostMessageW(hwnd, WM_BATCH_ITEM_UPDATE, (WPARAM)i, 0);
                                continue;
                            }

                            item.status = L"Sincronizando " + std::to_wstring(summary.total_files_to_sync) + L" archivos...";
                            PostMessageW(hwnd, WM_BATCH_ITEM_UPDATE, (WPARAM)i, 0);

                            bool ok = SyncEngine::ExecuteSync(item.pair, summary, &data->cancelRequested, nullptr);

                            if (data->cancelRequested.load()) {
                                item.status = L"Cancelado";
                                PostMessageW(hwnd, WM_BATCH_ITEM_UPDATE, (WPARAM)i, 0);
                                break;
                            }

                            if (ok) {
                                item.status = L"Completado (" + std::to_wstring(summary.total_files_to_sync) + L" archivos)";
                                item.files_synced = summary.total_files_to_sync;
                                totalSuccess++;
                            } else {
                                item.status = L"Finalizado con errores";
                                totalErrors++;
                            }
                            PostMessageW(hwnd, WM_BATCH_ITEM_UPDATE, (WPARAM)i, 0);
                        }
                    } catch (const std::exception& ex) {
                        LOG_ERROR(std::string("Excepción en cola de lotes: ") + ex.what());
                    } catch (...) {
                        LOG_ERROR("Excepción desconocida en cola de lotes");
                    }

                    PostMessageW(hwnd, WM_BATCH_FINISHED, (WPARAM)totalSuccess, (LPARAM)totalErrors);
                }).detach();

            } else if (id == IDC_BATCH_STOP) {
                if (data->isRunning.load()) {
                    data->cancelRequested.store(true);
                    Speech::Speak(L"Cancelacion solicitada, deteniendo despues del archivo en curso...");
                }
            } else if (id == IDCANCEL) {
                if (data->isRunning.load()) {
                    int res = MessageBoxW(hwnd, L"Hay una sincronizacion en curso. ¿Desea cancelarla y salir?", L"Confirmar", MB_YESNO | MB_ICONQUESTION);
                    if (res == IDYES) {
                        data->cancelRequested.store(true);
                        DestroyWindow(hwnd);
                    }
                } else {
                    DestroyWindow(hwnd);
                }
            }
            return 0;
        }

        case WM_BATCH_ITEM_UPDATE: {
            int idx = (int)wParam;
            if (idx >= 0 && idx < (int)data->queue.size()) {
                ListView_SetItemText(data->hList, idx, 4, (LPWSTR)data->queue[idx].status.c_str());
                ListView_EnsureVisible(data->hList, idx, FALSE);
            }
            return 0;
        }

        case WM_BATCH_FINISHED: {
            Speech::StopWaitingBeep();
            data->isRunning.store(false);
            EnableWindow(data->hBtnStart, TRUE);
            EnableWindow(data->hBtnStop, FALSE);

            int successCount = (int)wParam;
            int errorCount = (int)lParam;

            std::wstring finalMsg = L"Ejecución en serie finalizada: " + std::to_wstring(successCount) + L" completados con éxito";
            if (errorCount > 0) {
                finalMsg += L", " + std::to_wstring(errorCount) + L" con errores.";
            } else {
                finalMsg += L".";
            }

            SetWindowTextW(data->hStatusStatic, finalMsg.c_str());
            Speech::Speak(finalMsg);
            Speech::PlayAlertSound();
            return 0;
        }

        case WM_CLOSE:
            if (data->isRunning.load()) {
                int res = MessageBoxW(hwnd, L"Hay una sincronizacion en curso. ¿Desea cancelarla y salir?", L"Confirmar", MB_YESNO | MB_ICONQUESTION);
                if (res != IDYES) return 0;
                data->cancelRequested.store(true);
            }
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (data->hFont) DeleteObject(data->hFont);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool ShowBatchRunnerDialog(HWND hwndParent, const AppSettings& settings, const SyncProject& currentProject) {
    static bool registered = false;
    HINSTANCE hInst = GetModuleHandleW(nullptr);

    if (!registered) {
        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = BatchDlgProc;
        wc.hInstance = hInst;
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"SyncBatchRunnerDialogClass";
        RegisterClassExW(&wc);
        registered = true;
    }

    BatchDlgData data;
    data.pSettings = &settings;
    data.pCurrentProject = &currentProject;

    RECT rcParent;
    GetWindowRect(hwndParent, &rcParent);
    int w = 855, h = 480;
    int x = rcParent.left + ((rcParent.right - rcParent.left) - w) / 2;
    int y = rcParent.top + ((rcParent.bottom - rcParent.top) - h) / 2;

    HWND hwndDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"SyncBatchRunnerDialogClass",
        L"Ejecución de Múltiples Proyectos y Pares de Carpetas en Serie (Cola de Ejecución)",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, w, h,
        hwndParent,
        nullptr,
        hInst,
        &data
    );

    EnableWindow(hwndParent, FALSE);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hwndDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(hwndParent, TRUE);
    SetForegroundWindow(hwndParent);

    return true;
}

// -------------------------------------------------------------
// ABOUT AND MANUAL DIALOGS
// Autor: José Manuel Fernández Cortés
// -------------------------------------------------------------

static LRESULT CALLBACK AboutDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            HFONT hFont = GetAppFont();
            HFONT hBoldFont = CreateFontW(
                -16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
            );

            HWND hTitle = CreateWindowExW(0, L"STATIC", L"SyncAccesible (Versión 2.0 Nativa C++)", WS_CHILD | WS_VISIBLE, 20, 18, 460, 26, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(hTitle, WM_SETFONT, (WPARAM)hBoldFont, TRUE);

            std::wstring aboutText =
                L"Autor y Desarrollador:\r\n"
                L"José Manuel Fernández Cortés\r\n\r\n"
                L"Software accesible de sincronización de carpetas y proyectos en serie para Windows.\r\n\r\n"
                L"Diseñado especialmente para garantizar la máxima accesibilidad para personas usuarias de lectores de pantalla (NVDA, JAWS) y navegación completa por teclado.\r\n\r\n"
                L"© 2026 José Manuel Fernández Cortés. Todos los derechos reservados.";

            HWND hText = CreateWindowExW(0, L"STATIC", aboutText.c_str(), WS_CHILD | WS_VISIBLE, 20, 52, 460, 150, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(hText, WM_SETFONT, (WPARAM)hFont, TRUE);

            HWND hBtn = CreateWindowExW(0, L"BUTTON", L"&Aceptar", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 370, 215, 110, 32, hwnd, (HMENU)IDOK, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(hBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
            SetFocus(hBtn);

            Speech::Speak(L"Acerca de SyncAccesible. Autor y desarrollador: José Manuel Fernández Cortés.");
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == IDOK || id == IDCANCEL) {
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_CLOSE: {
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowAboutDialog(HWND hwndParent) {
    static bool registered = false;
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = AboutDlgProc;
        wc.hInstance = hInst;
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"SyncAboutDialogClass";
        RegisterClassExW(&wc);
        registered = true;
    }

    RECT rcParent;
    GetWindowRect(hwndParent, &rcParent);
    int w = 520, h = 300;
    int x = rcParent.left + ((rcParent.right - rcParent.left) - w) / 2;
    int y = rcParent.top + ((rcParent.bottom - rcParent.top) - h) / 2;

    HWND hwndDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"SyncAboutDialogClass",
        L"Acerca de SyncAccesible",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, w, h,
        hwndParent,
        nullptr,
        hInst,
        nullptr
    );

    EnableWindow(hwndParent, FALSE);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hwndDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(hwndParent, TRUE);
    SetForegroundWindow(hwndParent);
}

#define IDC_MANUAL_OPEN_FILE 5001

static LRESULT CALLBACK ManualDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            HFONT hFont = GetAppFont();

            wchar_t exePath[MAX_PATH];
            GetModuleFileNameW(nullptr, exePath, MAX_PATH);
            std::wstring dir = exePath;
            size_t lastSlash = dir.find_last_of(L"\\/");
            if (lastSlash != std::wstring::npos) dir = dir.substr(0, lastSlash);
            std::wstring manualPath = dir + L"\\MANUAL.md";

            std::wstring manualContent;
            FILE* f = _wfopen(manualPath.c_str(), L"rt, ccs=UTF-8");
            if (f) {
                wchar_t buf[1024];
                while (fgetws(buf, 1024, f)) {
                    manualContent += buf;
                }
                fclose(f);
            }
            if (manualContent.empty()) {
                manualContent =
                    L"MANUAL DE USUARIO - SYNCACCESIBLE\r\n"
                    L"Autor y Desarrollador: José Manuel Fernández Cortés\r\n"
                    L"Versión 2.0 (C++ Nativo)\r\n\r\n"
                    L"Consulte el archivo MANUAL.md incluido con la aplicación para más detalles.";
            }

            HWND hEdit = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                manualContent.c_str(),
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | WS_TABSTOP | ES_AUTOVSCROLL,
                15, 15, 770, 480,
                hwnd,
                (HMENU)101,
                GetModuleHandleW(nullptr),
                nullptr
            );
            SendMessageW(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

            HWND hBtnOpen = CreateWindowExW(
                0, L"BUTTON", L"&Abrir Archivo MANUAL.md",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                15, 510, 210, 32,
                hwnd,
                (HMENU)IDC_MANUAL_OPEN_FILE,
                GetModuleHandleW(nullptr),
                nullptr
            );
            SendMessageW(hBtnOpen, WM_SETFONT, (WPARAM)hFont, TRUE);

            HWND hBtnClose = CreateWindowExW(
                0, L"BUTTON", L"&Cerrar",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                675, 510, 110, 32,
                hwnd,
                (HMENU)IDCANCEL,
                GetModuleHandleW(nullptr),
                nullptr
            );
            SendMessageW(hBtnClose, WM_SETFONT, (WPARAM)hFont, TRUE);

            SetFocus(hEdit);

            Speech::Speak(L"Manual de usuario de SyncAccesible. Autor: José Manuel Fernández Cortés.");
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == IDC_MANUAL_OPEN_FILE) {
                OpenManualHtml(hwnd);
            } else if (id == IDOK || id == IDCANCEL) {
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_CLOSE: {
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowManualDialog(HWND hwndParent) {
    static bool registered = false;
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = ManualDlgProc;
        wc.hInstance = hInst;
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"SyncManualDialogClass";
        RegisterClassExW(&wc);
        registered = true;
    }

    RECT rcParent;
    GetWindowRect(hwndParent, &rcParent);
    int w = 820, h = 600;
    int x = rcParent.left + ((rcParent.right - rcParent.left) - w) / 2;
    int y = rcParent.top + ((rcParent.bottom - rcParent.top) - h) / 2;

    HWND hwndDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"SyncManualDialogClass",
        L"Manual de Usuario - SyncAccesible (Autor: José Manuel Fernández Cortés)",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, w, h,
        hwndParent,
        nullptr,
        hInst,
        nullptr
    );

    EnableWindow(hwndParent, FALSE);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hwndDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(hwndParent, TRUE);
    SetForegroundWindow(hwndParent);
}

void OpenManualHtml(HWND hwndParent) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring dir = exePath;
    size_t lastSlash = dir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) dir = dir.substr(0, lastSlash);
    std::wstring manualHtmlPath = dir + L"\\MANUAL.html";
    std::wstring manualMdPath = dir + L"\\MANUAL.md";

    if (GetFileAttributesW(manualHtmlPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        Speech::Speak(L"Abriendo el Manual de Usuario en el navegador web.");
        ShellExecuteW(hwndParent, L"open", manualHtmlPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else if (GetFileAttributesW(manualMdPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        Speech::Speak(L"Abriendo el Manual de Usuario en formato Markdown.");
        ShellExecuteW(hwndParent, L"open", manualMdPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else {
        ShowManualDialog(hwndParent);
    }
}

// -------------------------------------------------------------
// SHORTCUTS DIALOG (Shift + F1)
// Autor: José Manuel Fernández Cortés
// -------------------------------------------------------------

struct ShortcutDef {
    const wchar_t* key;
    const wchar_t* action;
};

static const ShortcutDef g_allShortcuts[] = {
    { L"Shift + F1",                     L"Abrir la ventana rápida con todos los Atajos de Teclado" },
    { L"F1",                             L"Abrir el Manual de Usuario en HTML en el navegador web" },
    { L"F5",                             L"Comparar el par de carpetas seleccionado" },
    { L"F6",                             L"Sincronizar el par de carpetas seleccionado" },
    { L"F7",                             L"Sincronizar en serie los pares seleccionados (uno tras otro)" },
    { L"Ctrl + F6",                      L"Sincronizar todos los pares activos del proyecto en serie" },
    { L"Alt + L",                        L"Abrir la Cola de Lotes para múltiples proyectos" },
    { L"Ctrl + P",                       L"Exportar informe de resultados a fichero .txt" },
    { L"Ctrl + H",                       L"Alternar filtro en Vista Previa (solo cambios / todos los archivos)" },
    { L"Alt + O",                        L"Ordenar la Vista Previa por acción requerida" },
    { L"Ctrl + N",                       L"Añadir un nuevo par de carpetas al proyecto" },
    { L"Ctrl + E / Enter",               L"Editar el par de carpetas seleccionado" },
    { L"Ctrl + I",                       L"Invertir el par seleccionado (intercambiar Origen y Destino)" },
    { L"Supr (Delete)",                  L"Eliminar el par de carpetas seleccionado" },
    { L"Ctrl + O",                       L"Abrir archivo de Proyecto (.syncproj o .json)" },
    { L"Ctrl + S",                       L"Guardar Proyecto actual" },
    { L"Ctrl + Shift + S",               L"Guardar Proyecto Como..." },
    { L"Ctrl + Shift + N",               L"Nuevo Proyecto limpio" },
    { L"Ctrl + Tab",                     L"Alternar entre Pestaña 1 y Pestaña 2" },
    { L"Alt + 1 / Ctrl + 1",             L"Ir a la Pestaña 1 (Pares de Carpetas)" },
    { L"Alt + 2 / Ctrl + 2",             L"Ir a la Pestaña 2 (Vista Previa de Sincronización)" },
    { L"Flecha Izquierda / Derecha",     L"Puntero Virtual de Columnas (anuncia celda por celda sin mover la fila)" },
    { L"Shift + F10 / Tecla Apps",       L"Abrir menú contextual del elemento seleccionado" },
    { L"Tab / Shift + Tab",              L"Desplazarse entre controles, listas y botones" },
    { L"Esc",                            L"Minimizar la aplicación a la bandeja del sistema" },
    { L"Alt + F4",                       L"Cerrar y salir por completo de la aplicación" }
};

#define IDC_SHORTCUTS_LIST 6101
#define IDC_SHORTCUTS_COPY 6102
#define IDC_SHORTCUTS_HTML 6103

static WNDPROC g_pOrigShortcutsListProc = nullptr;

static void CopyTextToClipboard(HWND hwnd, const std::wstring& text) {
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        void* pMem = GlobalLock(hMem);
        if (pMem) {
            memcpy(pMem, text.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
    }
    CloseClipboard();
}

static LRESULT CALLBACK ShortcutsListSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_LEFT || wParam == VK_RIGHT) {
            int sel = ListView_GetNextItem(hwnd, -1, LVNI_SELECTED);
            if (sel >= 0 && sel < (int)(sizeof(g_allShortcuts) / sizeof(g_allShortcuts[0]))) {
                if (wParam == VK_LEFT) {
                    Speech::Speak(std::wstring(L"Atajo: ") + g_allShortcuts[sel].key);
                } else {
                    Speech::Speak(std::wstring(L"Función: ") + g_allShortcuts[sel].action);
                }
            }
            return 0;
        } else if (wParam == VK_ESCAPE) {
            PostMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(IDCANCEL, 0), 0);
            return 0;
        }
    }
    return CallWindowProcW(g_pOrigShortcutsListProc, hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK ShortcutsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            HFONT hFont = GetAppFont();
            HFONT hBoldFont = CreateFontW(
                -16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
            );

            HWND hTitle = CreateWindowExW(0, L"STATIC", L"Atajos de Teclado de SyncAccesible", WS_CHILD | WS_VISIBLE, 15, 12, 500, 24, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(hTitle, WM_SETFONT, (WPARAM)hBoldFont, TRUE);

            HWND hDesc = CreateWindowExW(0, L"STATIC", L"Navegue con Flechas Arriba/Abajo. Con Flechas Izquierda/Derecha escuche cada celda. Pulse Alt+C para copiar la lista.", WS_CHILD | WS_VISIBLE, 15, 38, 750, 20, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(hDesc, WM_SETFONT, (WPARAM)hFont, TRUE);

            HWND hList = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                WC_LISTVIEWW,
                L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                15, 65, 755, 420,
                hwnd,
                (HMENU)IDC_SHORTCUTS_LIST,
                GetModuleHandleW(nullptr),
                nullptr
            );
            SendMessageW(hList, WM_SETFONT, (WPARAM)hFont, TRUE);
            ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

            LVCOLUMNW lvc = {0};
            lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            lvc.cx = 230;
            lvc.pszText = (LPWSTR)L"Atajo de Teclado";
            ListView_InsertColumn(hList, 0, &lvc);

            lvc.cx = 500;
            lvc.pszText = (LPWSTR)L"Función / Descripción";
            ListView_InsertColumn(hList, 1, &lvc);

            for (size_t i = 0; i < sizeof(g_allShortcuts) / sizeof(g_allShortcuts[0]); ++i) {
                LVITEMW lvi = {0};
                lvi.mask = LVIF_TEXT;
                lvi.iItem = (int)i;
                lvi.iSubItem = 0;
                lvi.pszText = (LPWSTR)g_allShortcuts[i].key;
                ListView_InsertItem(hList, &lvi);
                ListView_SetItemText(hList, (int)i, 1, (LPWSTR)g_allShortcuts[i].action);
            }

            g_pOrigShortcutsListProc = (WNDPROC)SetWindowLongPtrW(hList, GWLP_WNDPROC, (LONG_PTR)ShortcutsListSubclassProc);

            // Buttons
            HWND hBtnCopy = CreateWindowExW(0, L"BUTTON", L"&Copiar al Portapapeles", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 15, 498, 190, 32, hwnd, (HMENU)IDC_SHORTCUTS_COPY, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(hBtnCopy, WM_SETFONT, (WPARAM)hFont, TRUE);

            HWND hBtnHtml = CreateWindowExW(0, L"BUTTON", L"Abrir &Manual en HTML", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 215, 498, 180, 32, hwnd, (HMENU)IDC_SHORTCUTS_HTML, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(hBtnHtml, WM_SETFONT, (WPARAM)hFont, TRUE);

            HWND hBtnClose = CreateWindowExW(0, L"BUTTON", L"&Cerrar", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 660, 498, 110, 32, hwnd, (HMENU)IDCANCEL, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(hBtnClose, WM_SETFONT, (WPARAM)hFont, TRUE);

            if (sizeof(g_allShortcuts) > 0) {
                ListView_SetItemState(hList, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            }
            SetFocus(hList);

            Speech::Speak(L"Atajos de Teclado de SyncAccesible. 26 atajos disponibles. Use flechas arriba y abajo para navegar.");
            return 0;
        }
        case WM_NOTIFY: {
            NMHDR* pnm = (NMHDR*)lParam;
            if (pnm->idFrom == IDC_SHORTCUTS_LIST && pnm->code == LVN_ITEMCHANGED) {
                NMLISTVIEW* pnmv = (NMLISTVIEW*)lParam;
                if ((pnmv->uNewState & LVIS_SELECTED) && !(pnmv->uOldState & LVIS_SELECTED)) {
                    int sel = pnmv->iItem;
                    if (sel >= 0 && sel < (int)(sizeof(g_allShortcuts) / sizeof(g_allShortcuts[0]))) {
                        std::wstring text = std::wstring(g_allShortcuts[sel].key) + L": " + g_allShortcuts[sel].action;
                        Speech::Speak(text);
                    }
                }
            }
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == IDC_SHORTCUTS_COPY) {
                std::wstring fullList = L"=== ATAJOS DE TECLADO - SYNCACCESIBLE ===\r\n";
                fullList += L"Autor: José Manuel Fernández Cortés\r\n\r\n";
                for (size_t i = 0; i < sizeof(g_allShortcuts) / sizeof(g_allShortcuts[0]); ++i) {
                    fullList += std::wstring(g_allShortcuts[i].key) + L" -> " + g_allShortcuts[i].action + L"\r\n";
                }
                CopyTextToClipboard(hwnd, fullList);
                Speech::Speak(L"Todos los atajos de teclado han sido copiados al portapapeles.");
                MessageBoxW(hwnd, L"Lista de atajos copiada al portapapeles con éxito.", L"SyncAccesible", MB_OK | MB_ICONINFORMATION);
            } else if (id == IDC_SHORTCUTS_HTML) {
                OpenManualHtml(hwnd);
            } else if (id == IDOK || id == IDCANCEL) {
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_CLOSE: {
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowShortcutsDialog(HWND hwndParent) {
    static bool registered = false;
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = ShortcutsDlgProc;
        wc.hInstance = hInst;
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"SyncShortcutsDialogClass";
        RegisterClassExW(&wc);
        registered = true;
    }

    RECT rcParent;
    GetWindowRect(hwndParent, &rcParent);
    int w = 800, h = 580;
    int x = rcParent.left + ((rcParent.right - rcParent.left) - w) / 2;
    int y = rcParent.top + ((rcParent.bottom - rcParent.top) - h) / 2;

    HWND hwndDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"SyncShortcutsDialogClass",
        L"Atajos de Teclado - SyncAccesible (Autor: José Manuel Fernández Cortés)",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, w, h,
        hwndParent,
        nullptr,
        hInst,
        nullptr
    );

    EnableWindow(hwndParent, FALSE);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hwndDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(hwndParent, TRUE);
    SetForegroundWindow(hwndParent);
}

} // namespace Dialogs
