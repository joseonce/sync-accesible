#pragma once
#ifndef UPDATER_H
#define UPDATER_H

#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <cstdint>

namespace Updater {

extern const wchar_t* APP_VERSION;
extern const wchar_t* APP_NAME;

struct UpdateInfo {
    std::wstring version;
    std::wstring name;
    std::wstring date;
    std::wstring download_url;
    std::vector<std::wstring> changelog;
};

void CheckForUpdates(HWND hwndParent, bool silentIfUpToDate = false);

bool FetchLatestVersionInfo(UpdateInfo& outInfo, std::wstring& outError);

bool DownloadUpdatePackage(
    const std::wstring& url,
    const std::wstring& destZipPath,
    std::function<void(int percent, uint64_t downloaded, uint64_t total)> progressCb,
    std::atomic<bool>* cancelToken,
    std::wstring& outError
);

bool LaunchPortableUpdater(const std::wstring& zipPath, HWND hwndParent);

} // namespace Updater

#endif // UPDATER_H

