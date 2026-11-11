#include "updater.h"
#include "speech.h"
#include "logger.h"
#include "dialogs.h"
#include <wininet.h>
#include <sstream>
#include <chrono>
#include <thread>
#include <atomic>
#include <filesystem>
#include <shlwapi.h>

namespace fs = std::filesystem;

namespace Updater {

const wchar_t* APP_VERSION = L"2.0.0";
const wchar_t* APP_NAME = L"SyncAccesible";
static const wchar_t* CHECK_UPDATE_URL = L"https://raw.githubusercontent.com/joseonce/sync-accesible/main/version.json";
static const wchar_t* FALLBACK_UPDATE_URL = L"https://api.github.com/repos/joseonce/sync-accesible/releases/latest";

// Parse version strings like "2.0.0" or "v2.0.1" into 3 numbers
static std::vector<int> ParseVersion(const std::wstring& verStr) {
    std::vector<int> parts;
    std::wstring clean = verStr;
    while (!clean.empty() && (clean.front() == L'v' || clean.front() == L'V' || clean.front() == L' ')) {
        clean.erase(0, 1);
    }
    std::wstringstream ss(clean);
    std::wstring token;
    while (std::getline(ss, token, L'.')) {
        try {
            parts.push_back(std::stoi(token));
        } catch (...) {
            parts.push_back(0);
        }
    }
    while (parts.size() < 3) parts.push_back(0);
    return parts;
}

static bool IsNewerVersion(const std::wstring& remoteVer, const std::wstring& localVer) {
    auto r = ParseVersion(remoteVer);
    auto l = ParseVersion(localVer);
    for (size_t i = 0; i < 3; ++i) {
        if (r[i] > l[i]) return true;
        if (r[i] < l[i]) return false;
    }
    return false;
}

static std::string HttpGet(const std::wstring& url, std::wstring& errOut) {
    HINTERNET hInternet = InternetOpenW(L"SyncAccesible-Updater/2.0", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hInternet) {
        errOut = L"Error al inicializar WinINet.";
        return "";
    }

    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE;
    HINTERNET hUrl = InternetOpenUrlW(hInternet, url.c_str(), nullptr, 0, flags, 0);
    if (!hUrl) {
        DWORD err = GetLastError();
        InternetCloseHandle(hInternet);
        errOut = L"Error de conexión con el servidor (código: " + std::to_wstring(err) + L").";
        return "";
    }

    std::string response;
    char buffer[4096];
    DWORD bytesRead = 0;
    while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        response.append(buffer, bytesRead);
    }

    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);
    return response;
}

bool FetchLatestVersionInfo(UpdateInfo& outInfo, std::wstring& outError) {
    std::string jsonStr = HttpGet(CHECK_UPDATE_URL, outError);
    if (jsonStr.empty()) {
        return false;
    }

    // Parse simple JSON fields: version, date, url, changelog
    auto extractString = [](const std::string& json, const std::string& key) -> std::wstring {
        size_t pos = json.find("\"" + key + "\"");
        if (pos == std::string::npos) return L"";
        size_t colon = json.find(':', pos);
        if (colon == std::string::npos) return L"";
        size_t startQuote = json.find('\"', colon);
        if (startQuote == std::string::npos) return L"";
        size_t endQuote = json.find('\"', startQuote + 1);
        if (endQuote == std::string::npos) return L"";
        std::string val = json.substr(startQuote + 1, endQuote - startQuote - 1);

        int len = MultiByteToWideChar(CP_UTF8, 0, val.c_str(), (int)val.size(), nullptr, 0);
        std::wstring wval(len, 0);
        MultiByteToWideChar(CP_UTF8, 0, val.c_str(), (int)val.size(), &wval[0], len);
        return wval;
    };

    outInfo.version = extractString(jsonStr, "version");
    outInfo.name = extractString(jsonStr, "name");
    outInfo.date = extractString(jsonStr, "date");
    outInfo.download_url = extractString(jsonStr, "url");

    if (outInfo.version.empty() || outInfo.download_url.empty()) {
        outError = L"El archivo de versión no contiene datos válidos.";
        return false;
    }

    // Extract changelog array
    outInfo.changelog.clear();
    size_t clPos = jsonStr.find("\"changelog\"");
    if (clPos != std::string::npos) {
        size_t startBracket = jsonStr.find('[', clPos);
        size_t endBracket = jsonStr.find(']', startBracket);
        if (startBracket != std::string::npos && endBracket != std::string::npos) {
            std::string clArr = jsonStr.substr(startBracket, endBracket - startBracket + 1);
            size_t idx = 0;
            while (true) {
                size_t q1 = clArr.find('\"', idx);
                if (q1 == std::string::npos) break;
                size_t q2 = clArr.find('\"', q1 + 1);
                if (q2 == std::string::npos) break;
                std::string item = clArr.substr(q1 + 1, q2 - q1 - 1);
                int len = MultiByteToWideChar(CP_UTF8, 0, item.c_str(), (int)item.size(), nullptr, 0);
                std::wstring witem(len, 0);
                MultiByteToWideChar(CP_UTF8, 0, item.c_str(), (int)item.size(), &witem[0], len);
                outInfo.changelog.push_back(witem);
                idx = q2 + 1;
            }
        }
    }

    return true;
}

bool DownloadUpdatePackage(
    const std::wstring& url,
    const std::wstring& destZipPath,
    std::function<void(int percent, uint64_t downloaded, uint64_t total)> progressCb,
    std::atomic<bool>* cancelToken,
    std::wstring& outError
) {
    HINTERNET hInternet = InternetOpenW(L"SyncAccesible-Downloader/2.0", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hInternet) {
        outError = L"Error al inicializar WinINet.";
        return false;
    }

    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE;
    HINTERNET hUrl = InternetOpenUrlW(hInternet, url.c_str(), nullptr, 0, flags, 0);
    if (!hUrl) {
        DWORD err = GetLastError();
        InternetCloseHandle(hInternet);
        outError = L"No se pudo conectar a la URL de descarga (código: " + std::to_wstring(err) + L").";
        return false;
    }

    // Get Content-Length
    DWORD contentLength = 0;
    DWORD clSize = sizeof(contentLength);
    DWORD index = 0;
    HttpQueryInfoW(hUrl, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &contentLength, &clSize, &index);

    FILE* fOut = nullptr;
    _wfopen_s(&fOut, destZipPath.c_str(), L"wb");
    if (!fOut) {
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);
        outError = L"No se pudo crear el archivo temporal de descarga en disco.";
        return false;
    }

    char buffer[65536];
    DWORD bytesRead = 0;
    uint64_t downloaded = 0;
    int lastPercent = -1;

    while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        if (cancelToken && cancelToken->load()) {
            fclose(fOut);
            _wremove(destZipPath.c_str());
            InternetCloseHandle(hUrl);
            InternetCloseHandle(hInternet);
            outError = L"Descarga cancelada por el usuario.";
            return false;
        }

        fwrite(buffer, 1, bytesRead, fOut);
        downloaded += bytesRead;

        if (contentLength > 0 && progressCb) {
            int pct = (int)((downloaded * 100) / contentLength);
            if (pct != lastPercent) {
                lastPercent = pct;
                progressCb(pct, downloaded, contentLength);
            }
        }
    }

    fclose(fOut);
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);
    return true;
}

// Transactional PowerShell Installer Script (Same reliable architecture as youtube1)
bool LaunchPortableUpdater(const std::wstring& zipPath, HWND hwndParent) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring dir = exePath;
    size_t lastSlash = dir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) dir = dir.substr(0, lastSlash);

    // Create unique temporary work directory
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    auto token = std::to_wstring(std::chrono::system_clock::now().time_since_epoch().count());
    std::wstring workDir = std::wstring(tempPath) + L"sync_update_" + token;

    std::error_code ec;
    fs::create_directories(workDir, ec);
    if (ec) {
        MessageBoxW(hwndParent, L"No se pudo crear el directorio temporal de actualización.", L"Error de Actualización", MB_OK | MB_ICONERROR);
        return false;
    }

    std::wstring pkgZip = workDir + L"\\package.zip";
    fs::copy_file(zipPath, pkgZip, fs::copy_options::overwrite_existing, ec);

    DWORD pid = GetCurrentProcessId();

    // PowerShell installation script (transactional with backup and retry)
    std::wstring psScript = LR"PS(
param([string]$TargetDir, [string]$WorkDir, [int]$OldPid, [string]$Token)
$ErrorActionPreference = 'Stop'

# 1. Wait for parent process to exit
for ($i = 0; $i -lt 30; $i++) {
    $proc = Get-Process -Id $OldPid -ErrorAction SilentlyContinue
    if (-not $proc) { break }
    Start-Sleep -Milliseconds 300
}

# 2. Extract new package into staging
$extractDir = Join-Path $WorkDir 'extract'
$pkgZip = Join-Path $WorkDir 'package.zip'
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::ExtractToDirectory($pkgZip, $extractDir)

# Detect root subfolder if present inside zip
$srcFiles = $extractDir
$subDirs = Get-ChildItem -Path $extractDir -Directory
if ($subDirs.Count -eq 1 -and (Get-ChildItem -Path $extractDir -File).Count -eq 0) {
    $srcFiles = $subDirs[0].FullName
}

# 3. Create backup of current files
$backupDir = Join-Path (Split-Path -Parent $TargetDir) ('.sync-backup-' + $Token)
New-Item -ItemType Directory -Path $backupDir -Force | Out-Null
Get-ChildItem -Path $TargetDir -Exclude '.sync-backup*','config.json','*.log' | Copy-Item -Destination $backupDir -Recurse -Force

# 4. Copy new files to target (Preserving config.json and logs)
try {
    Get-ChildItem -Path $srcFiles -Exclude 'config.json','*.log' | Copy-Item -Destination $TargetDir -Recurse -Force
} catch {
    # Restore from backup on error
    Get-ChildItem -Path $backupDir | Copy-Item -Destination $TargetDir -Recurse -Force
    Remove-Item -Path $backupDir -Recurse -Force -ErrorAction SilentlyContinue
    throw
}

# 5. Clean up temporary directories
Remove-Item -Path $backupDir -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Path $WorkDir -Recurse -Force -ErrorAction SilentlyContinue

# 6. Relaunch updated SyncAccesible.exe
$exePath = Join-Path $TargetDir 'SyncAccesible.exe'
Start-Process -FilePath $exePath -WorkingDirectory $TargetDir
)PS";

    std::wstring scriptPath = workDir + L"\\install.ps1";
    FILE* fScript = nullptr;
    _wfopen_s(&fScript, scriptPath.c_str(), L"w, ccs=UTF-8");
    if (fScript) {
        fputws(psScript.c_str(), fScript);
        fclose(fScript);
    }

    // Launch PowerShell in background without visible window
    std::wstring cmdLine = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" +
                           scriptPath + L"\" -TargetDir \"" + dir + L"\" -WorkDir \"" + workDir +
                           L"\" -OldPid " + std::to_wstring(pid) + L" -Token \"" + token + L"\"";

    STARTUPINFOW si = { sizeof(STARTUPINFOW) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {0};

    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');

    if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, tempPath, &si, &pi)) {
        MessageBoxW(hwndParent, L"No se pudo iniciar el proceso de instalación de la actualización.", L"Error", MB_OK | MB_ICONERROR);
        return false;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // Clean exit of current process
    ExitProcess(0);
    return true;
}

void CheckForUpdates(HWND hwndParent, bool silentIfUpToDate) {
    Speech::Speak(L"Comprobando actualizaciones...");
    std::thread([hwndParent, silentIfUpToDate]() {
        UpdateInfo info;
        std::wstring err;
        bool ok = FetchLatestVersionInfo(info, err);

        if (!ok) {
            if (!silentIfUpToDate) {
                Speech::Speak(L"No se pudo comprobar si hay actualizaciones disponibles.");
                MessageBoxW(hwndParent, (L"No se pudo comprobar si hay actualizaciones:\n\n" + err).c_str(), L"Comprobar Actualizaciones", MB_OK | MB_ICONWARNING);
            }
            return;
        }

        if (IsNewerVersion(info.version, APP_VERSION)) {
            // New version available! Open accessible update dialog
            PostMessageW(hwndParent, WM_USER + 50, 0, (LPARAM)new UpdateInfo(info));
        } else {
            if (!silentIfUpToDate) {
                std::wstring msg = L"SyncAccesible está actualizado a la última versión disponible (v" + std::wstring(APP_VERSION) + L").";
                Speech::Speak(msg);
                MessageBoxW(hwndParent, (msg + L"\nNo hay nuevas actualizaciones en este momento.").c_str(), L"SyncAccesible al día", MB_OK | MB_ICONINFORMATION);
            }
        }
    }).detach();
}

} // namespace Updater

