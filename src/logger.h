#pragma once
#ifndef LOGGER_H
#define LOGGER_H

#include <windows.h>
#include <string>

enum class LogLevel {
    Info,
    Warn,
    Error,
    Critical
};

namespace Logger {

void Initialize(const std::wstring& appDir);
void Shutdown();

void Log(LogLevel level, const std::string& message, const char* file = nullptr, int line = 0);
void Log(LogLevel level, const std::wstring& message, const char* file = nullptr, int line = 0);

std::wstring GetLogFilePath();
void OpenLogInDefaultEditor(HWND hwndParent = nullptr);

} // namespace Logger

#define LOG_INFO(msg)     Logger::Log(LogLevel::Info, msg, __FILE__, __LINE__)
#define LOG_WARN(msg)     Logger::Log(LogLevel::Warn, msg, __FILE__, __LINE__)
#define LOG_ERROR(msg)    Logger::Log(LogLevel::Error, msg, __FILE__, __LINE__)
#define LOG_CRITICAL(msg) Logger::Log(LogLevel::Critical, msg, __FILE__, __LINE__)

#endif // LOGGER_H

