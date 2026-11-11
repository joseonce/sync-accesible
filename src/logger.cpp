#include "logger.h"
#include <mutex>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <cstdio>
#include <share.h>
#include <shellapi.h>
#include <filesystem>

namespace Logger {

static std::wstring g_logFilePath;
static std::mutex g_logMutex;
static LPTOP_LEVEL_EXCEPTION_FILTER g_prevExceptionFilter = nullptr;

static std::wstring GetTimestampString() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmNow;
    localtime_s(&tmNow, &t);

    std::wostringstream ss;
    ss << std::setfill(L'0')
       << std::setw(4) << (tmNow.tm_year + 1900) << L"-"
       << std::setw(2) << (tmNow.tm_mon + 1) << L"-"
       << std::setw(2) << tmNow.tm_mday << L" "
       << std::setw(2) << tmNow.tm_hour << L":"
       << std::setw(2) << tmNow.tm_min << L":"
       << std::setw(2) << tmNow.tm_sec << L"."
       << std::setw(3) << ms.count();
    return ss.str();
}

static const wchar_t* LevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Info:     return L"INFO";
        case LogLevel::Warn:     return L"ADVERTENCIA";
        case LogLevel::Error:    return L"ERROR";
        case LogLevel::Critical: return L"CRITICO";
        default:                 return L"INFO";
    }
}

// Writes line safely without keeping an exclusive open file handle on the disk.
// Uses _SH_DENYNO so other processes (Notepad, NVDA, editors) can read/write concurrently.
static void WriteToFileInternal(const std::wstring& line) {
    if (g_logFilePath.empty()) return;
    FILE* f = _wfsopen(g_logFilePath.c_str(), L"a, ccs=UTF-8", _SH_DENYNO);
    if (!f) return;
    fputws(line.c_str(), f);
    fflush(f);
    fclose(f);
}

static LONG WINAPI GlobalExceptionHandler(EXCEPTION_POINTERS* pExceptionInfo) {
    DWORD code = pExceptionInfo ? pExceptionInfo->ExceptionRecord->ExceptionCode : 0;
    PVOID addr = pExceptionInfo ? pExceptionInfo->ExceptionRecord->ExceptionAddress : nullptr;

    std::wostringstream ss;
    ss << L"¡EXCEPCIÓN NO CONTROLADA CAPTURADA! Código: 0x"
       << std::hex << std::uppercase << code
       << L" en la dirección de memoria: 0x" << addr;

    Logger::Log(LogLevel::Critical, ss.str());

    std::wstring userMsg =
        L"Se ha detectado un error inesperado en la aplicación.\n\n"
        L"Código de error: 0x" + ss.str().substr(ss.str().find(L"0x")) + L"\n"
        L"Se ha registrado el detalle del fallo en el archivo:\n" +
        g_logFilePath + L"\n\n"
        L"Puede consultar este archivo para diagnosticar el problema.";

    MessageBoxW(nullptr, userMsg.c_str(), L"SyncAccesible - Error Inesperado", MB_OK | MB_ICONERROR);

    if (g_prevExceptionFilter) {
        return g_prevExceptionFilter(pExceptionInfo);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

void Initialize(const std::wstring& appDir) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_logFilePath = appDir;
    if (!g_logFilePath.empty() && g_logFilePath.back() != L'\\' && g_logFilePath.back() != L'/') {
        g_logFilePath += L"\\";
    }
    g_logFilePath += L"sync_accesible.log";

    std::wstring sep = L"================================================================================\n";
    std::wstring header = L"[" + GetTimestampString() + L"] [INFO] --- INICIO DE SESIÓN DE SYNCACCESIBLE ---\n";
    WriteToFileInternal(sep + header);

    // Install unhandled exception filter for crash diagnostics
    g_prevExceptionFilter = SetUnhandledExceptionFilter(GlobalExceptionHandler);
}

void Shutdown() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::wstring footer = L"[" + GetTimestampString() + L"] [INFO] --- CIERRE NORMAL DE SESIÓN ---\n\n";
    WriteToFileInternal(footer);

    if (g_prevExceptionFilter) {
        SetUnhandledExceptionFilter(g_prevExceptionFilter);
        g_prevExceptionFilter = nullptr;
    }
}

void Log(LogLevel level, const std::wstring& message, const char* file, int line) {
    std::lock_guard<std::mutex> lock(g_logMutex);

    std::wstring ts = GetTimestampString();
    const wchar_t* lvl = LevelToString(level);

    std::wostringstream ss;
    ss << L"[" << ts << L"] [" << lvl << L"] ";
    if (file && line > 0) {
        std::string fn(file);
        size_t pos = fn.find_last_of("\\/");
        if (pos != std::string::npos) fn = fn.substr(pos + 1);
        std::wstring wf(fn.begin(), fn.end());
        ss << L"(" << wf << L":" << line << L") ";
    }
    ss << message << L"\n";

    WriteToFileInternal(ss.str());
}

void Log(LogLevel level, const std::string& message, const char* file, int line) {
    std::wstring wmsg(message.begin(), message.end());
    Log(level, wmsg, file, line);
}

std::wstring GetLogFilePath() {
    return g_logFilePath;
}

void OpenLogInDefaultEditor(HWND hwndParent) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logFilePath.empty()) return;

    // Launch notepad with quotes around path so it opens immediately without locking conflicts
    std::wstring params = L"\"" + g_logFilePath + L"\"";
    HINSTANCE res = ShellExecuteW(hwndParent, L"open", L"notepad.exe", params.c_str(), nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)res <= 32) {
        // Fallback to ShellExecute open verb if notepad direct launch returns error
        ShellExecuteW(hwndParent, L"open", g_logFilePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

} // namespace Logger
