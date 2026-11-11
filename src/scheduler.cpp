#include "scheduler.h"
#include "sync_engine.h"
#include "logger.h"
#include <chrono>
#include <ctime>
#include <mutex>
#include <map>
#include <sstream>
#include <iomanip>

namespace Scheduler {

static std::atomic<bool> g_running(false);
static std::atomic<bool> g_paused(false);
static std::thread g_workerThread;
static std::vector<FolderPair>* g_pairsPtr = nullptr;
static SyncCallback g_callback = nullptr;
static std::mutex g_schedulerMutex;

struct PairState {
    std::chrono::steady_clock::time_point last_interval_sync = std::chrono::steady_clock::now();
    int last_daily_day = -1;
    HANDLE hChangeNotification = INVALID_HANDLE_VALUE;
    std::chrono::steady_clock::time_point last_change_detected = std::chrono::steady_clock::time_point::min();
    bool change_pending = false;
};

static std::map<std::wstring, PairState> g_states;

static std::wstring GetCurrentTimeString() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &t);
    std::wostringstream ss;
    ss << std::setfill(L'0') << std::setw(2) << tm.tm_hour << L":"
       << std::setfill(L'0') << std::setw(2) << tm.tm_min << L":"
       << std::setfill(L'0') << std::setw(2) << tm.tm_sec;
    return ss.str();
}

static void CleanWatchers() {
    for (auto& kv : g_states) {
        if (kv.second.hChangeNotification != INVALID_HANDLE_VALUE) {
            FindCloseChangeNotification(kv.second.hChangeNotification);
            kv.second.hChangeNotification = INVALID_HANDLE_VALUE;
        }
    }
}

static void WorkerLoop() {
    while (g_running.load()) {
        if (g_paused.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        std::vector<FolderPair> activePairs;
        {
            std::lock_guard<std::mutex> lock(g_schedulerMutex);
            if (g_pairsPtr) {
                activePairs = *g_pairsPtr;
            }
        }

        auto nowSteady = std::chrono::steady_clock::now();
        auto nowSystem = std::chrono::system_clock::now();
        std::time_t nowTimeT = std::chrono::system_clock::to_time_t(nowSystem);
        std::tm nowTm;
        localtime_s(&nowTm, &nowTimeT);

        wchar_t currentHourMin[16];
        swprintf_s(currentHourMin, L"%02d:%02d", nowTm.tm_hour, nowTm.tm_min);

        for (auto& pair : activePairs) {
            if (!pair.enabled) continue;

            auto& state = g_states[pair.id];
            bool shouldSync = false;

            // 1. Interval
            if (pair.schedule_type == ScheduleType::Interval && pair.interval_minutes > 0) {
                auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(nowSteady - state.last_interval_sync).count();
                if (elapsed >= pair.interval_minutes) {
                    shouldSync = true;
                    state.last_interval_sync = nowSteady;
                }
            }

            // 2. Daily
            if (pair.schedule_type == ScheduleType::Daily) {
                if (pair.daily_time == currentHourMin && state.last_daily_day != nowTm.tm_yday) {
                    shouldSync = true;
                    state.last_daily_day = nowTm.tm_yday;
                }
            }

            // 3. Realtime Watchdog
            if (pair.schedule_type == ScheduleType::Realtime) {
                if (state.hChangeNotification == INVALID_HANDLE_VALUE && !pair.left_folder.empty()) {
                    state.hChangeNotification = FindFirstChangeNotificationW(
                        pair.left_folder.c_str(),
                        TRUE,
                        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE
                    );
                }

                if (state.hChangeNotification != INVALID_HANDLE_VALUE) {
                    DWORD waitRes = WaitForSingleObject(state.hChangeNotification, 0);
                    if (waitRes == WAIT_OBJECT_0) {
                        state.change_pending = true;
                        state.last_change_detected = nowSteady;
                        FindNextChangeNotification(state.hChangeNotification);
                    }
                }

                // Debounce real-time changes (wait 5 seconds of stabilization)
                if (state.change_pending) {
                    auto debounceElapsed = std::chrono::duration_cast<std::chrono::seconds>(nowSteady - state.last_change_detected).count();
                    if (debounceElapsed >= 5) {
                        shouldSync = true;
                        state.change_pending = false;
                    }
                }
            }

            // Perform Background Sync if triggered
            if (shouldSync) {
                try {
                    LOG_INFO(L"Programador en segundo plano: Iniciando sincronización para: " + pair.name);
                    ComparisonSummary summary = SyncEngine::CompareFolders(pair, nullptr, nullptr);
                    bool ok = true;
                    if (summary.total_files_to_sync > 0) {
                        ok = SyncEngine::ExecuteSync(pair, summary, nullptr, nullptr);
                    }

                    std::wstring statusMsg = ok ? L"Sincronizado (" + std::to_wstring(summary.total_files_to_sync) + L" archivos)"
                                                : L"Error en sincronizacion";

                    // Update pair metadata in master list
                    {
                        std::lock_guard<std::mutex> lock(g_schedulerMutex);
                        if (g_pairsPtr) {
                            for (auto& p : *g_pairsPtr) {
                                if (p.id == pair.id) {
                                    p.last_sync_time = GetCurrentTimeString();
                                    p.last_sync_status = statusMsg;
                                    break;
                                }
                            }
                        }
                    }

                    if (g_callback) {
                        g_callback(pair.id, ok, statusMsg);
                    }
                } catch (const std::exception& ex) {
                    LOG_ERROR(std::string("Excepción en tarea de programador: ") + ex.what());
                } catch (...) {
                    LOG_ERROR("Excepción desconocida en tarea de programador");
                }
            }
        }

        // Sleep 1 second before next cycle
        for (int i = 0; i < 10 && g_running.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    CleanWatchers();
}

void Start(std::vector<FolderPair>* pairs, SyncCallback onSyncCompleted) {
    if (g_running.load()) return;

    g_pairsPtr = pairs;
    g_callback = onSyncCompleted;
    g_running.store(true);
    g_paused.store(false);

    g_workerThread = std::thread(WorkerLoop);
}

void Stop() {
    if (!g_running.load()) return;

    g_running.store(false);
    if (g_workerThread.joinable()) {
        g_workerThread.join();
    }
}

void Pause(bool paused) {
    g_paused.store(paused);
}

bool IsPaused() {
    return g_paused.load();
}

void NotifyPairsUpdated() {
    std::lock_guard<std::mutex> lock(g_schedulerMutex);
    CleanWatchers();
}

} // namespace Scheduler

