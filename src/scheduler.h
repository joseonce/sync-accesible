#pragma once
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <windows.h>
#include <vector>
#include <memory>
#include <functional>
#include <atomic>
#include <thread>
#include "models.h"

namespace Scheduler {

typedef std::function<void(const std::wstring& pairId, bool success, const std::wstring& statusMsg)> SyncCallback;

void Start(std::vector<FolderPair>* pairs, SyncCallback onSyncCompleted);
void Stop();
void Pause(bool paused);
bool IsPaused();
void NotifyPairsUpdated();

} // namespace Scheduler

#endif // SCHEDULER_H

