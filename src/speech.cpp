#include "speech.h"
#include <sapi.h>
#include <mutex>
#include <thread>
#include <atomic>

namespace Speech {

static const GUID MY_CLSID_SpVoice = { 0x96749377, 0x3391, 0x11D2, { 0x9E, 0xE3, 0x00, 0xC0, 0x4F, 0x79, 0x73, 0x96 } };
static const GUID MY_IID_ISpVoice   = { 0x6C44DF74, 0x72B9, 0x4992, { 0xA1, 0xEC, 0xEF, 0x99, 0x66, 0x9F, 0xA5, 0x66 } };

typedef error_status_t (__stdcall *NvdaSpeakFunc)(const wchar_t*);
typedef error_status_t (__stdcall *NvdaCancelFunc)();

static HMODULE hNvdaDll = nullptr;
static NvdaSpeakFunc nvdaSpeak = nullptr;
static NvdaCancelFunc nvdaCancel = nullptr;
static ISpVoice* pSpVoice = nullptr;
static std::mutex speechMutex;
static std::atomic<bool> g_enabled(true);
static std::atomic<bool> g_waitingBeepRunning(false);
static std::thread g_waitingBeepThread;

void Initialize() {
    // 1. Try NVDA Controller Client DLL
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring dir = exePath;
    size_t lastSlash = dir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) dir = dir.substr(0, lastSlash);

    std::wstring dll64 = dir + L"\\nvdaControllerClient64.dll";
    std::wstring dll32 = dir + L"\\nvdaControllerClient32.dll";

    hNvdaDll = LoadLibraryW(dll64.c_str());
    if (!hNvdaDll) hNvdaDll = LoadLibraryW(L"nvdaControllerClient64.dll");
    if (!hNvdaDll) hNvdaDll = LoadLibraryW(dll32.c_str());
    if (!hNvdaDll) hNvdaDll = LoadLibraryW(L"nvdaControllerClient.dll");

    if (hNvdaDll) {
        nvdaSpeak = (NvdaSpeakFunc)GetProcAddress(hNvdaDll, "nvdaController_speakText");
        nvdaCancel = (NvdaCancelFunc)GetProcAddress(hNvdaDll, "nvdaController_cancelSpeech");
    }

    // 2. Initialize SAPI Voice as standard/fallback
    CoInitialize(nullptr);
    HRESULT hr = CoCreateInstance(MY_CLSID_SpVoice, NULL, CLSCTX_ALL, MY_IID_ISpVoice, (void**)&pSpVoice);
    if (!SUCCEEDED(hr)) {
        pSpVoice = nullptr;
    }
}

void Shutdown() {
    StopWaitingBeep();
    std::lock_guard<std::mutex> lock(speechMutex);
    if (pSpVoice) {
        pSpVoice->Release();
        pSpVoice = nullptr;
    }
    if (hNvdaDll) {
        FreeLibrary(hNvdaDll);
        hNvdaDll = nullptr;
        nvdaSpeak = nullptr;
        nvdaCancel = nullptr;
    }
}

void SetEnabled(bool enabled) {
    g_enabled = enabled;
}

bool IsEnabled() {
    return g_enabled.load();
}

void Speak(const std::wstring& text, bool interrupt) {
    if (!g_enabled.load() || text.empty()) return;

    std::thread([text, interrupt]() {
        CoInitialize(nullptr);
        {
            std::lock_guard<std::mutex> lock(speechMutex);
            bool spokenByNvda = false;
            if (nvdaSpeak) {
                if (interrupt && nvdaCancel) {
                    nvdaCancel();
                }
                if (nvdaSpeak(text.c_str()) == 0) {
                    spokenByNvda = true;
                }
            }

            if (!spokenByNvda && pSpVoice) {
                DWORD flags = SPF_ASYNC;
                if (interrupt) {
                    flags |= SPF_PURGEBEFORESPEAK;
                }
                pSpVoice->Speak(text.c_str(), flags, nullptr);
            }
        }
        CoUninitialize();
    }).detach();
}

void PlayAlertSound() {
    if (!g_enabled.load()) return;
    MessageBeep(MB_OK);
}

void StartWaitingBeep() {
    if (g_waitingBeepRunning.exchange(true)) {
        return;
    }
    if (g_waitingBeepThread.joinable()) {
        g_waitingBeepThread.join();
    }
    g_waitingBeepThread = std::thread([]() {
        while (g_waitingBeepRunning.load()) {
            for (int i = 0; i < 12 && g_waitingBeepRunning.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (!g_waitingBeepRunning.load()) break;

            if (g_enabled.load()) {
                Beep(850, 35);
            }
        }
    });
}

void StopWaitingBeep() {
    if (g_waitingBeepRunning.exchange(false)) {
        if (g_waitingBeepThread.joinable()) {
            g_waitingBeepThread.join();
        }
    }
}

} // namespace Speech

