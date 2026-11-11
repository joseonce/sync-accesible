#pragma once
#ifndef SYNC_ENGINE_H
#define SYNC_ENGINE_H

#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include "models.h"

namespace SyncEngine {

bool MatchesExclusions(const std::wstring& relPath, const std::vector<std::wstring>& exclusions);

ComparisonSummary CompareFolders(
    const FolderPair& pair,
    std::atomic<bool>* cancelToken = nullptr,
    std::function<void(int percent, const std::wstring& status)> progressCallback = nullptr
);

bool ExecuteSync(
    const FolderPair& pair,
    ComparisonSummary& summary,
    std::atomic<bool>* cancelToken = nullptr,
    std::function<void(int percent, int current, int total, const std::wstring& relPath)> progressCallback = nullptr
);

} // namespace SyncEngine

#endif // SYNC_ENGINE_H

