#include <windows.h>
#include <commctrl.h>
#include <string>
#include "gui.h"
#include "logger.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    // Get application directory
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring dir = exePath;
    size_t lastSlash = dir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) dir = dir.substr(0, lastSlash);

    // Initialize Logger
    Logger::Initialize(dir);
    LOG_INFO(L"Iniciando aplicación SyncAccesible 2.0");

    // Initialize COM for Shell & SAPI
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Initialize Common Controls
    INITCOMMONCONTROLSEX icex = {0};
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES | ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icex);

    int result = 0;
    try {
        result = Gui::Run(hInstance, nCmdShow);
    } catch (const std::exception& ex) {
        LOG_CRITICAL(std::string("Excepción no controlada en bucle principal: ") + ex.what());
    } catch (...) {
        LOG_CRITICAL("Excepción no controlada desconocida en bucle principal");
    }

    CoUninitialize();
    LOG_INFO(L"Cerrando aplicación SyncAccesible");
    Logger::Shutdown();
    return result;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    return wWinMain(hInstance, hPrevInstance, nullptr, nCmdShow);
}

