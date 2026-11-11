#include "speech.h"
#include <oleauto.h>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>

namespace Speech {

// NVDA Controller Client (64-bit)
typedef error_status_t (__stdcall *NvdaSpeakFunc)(const wchar_t*);
typedef error_status_t (__stdcall *NvdaCancelFunc)();
static HMODULE hNvdaDll = nullptr;
static NvdaSpeakFunc nvdaSpeak = nullptr;
static NvdaCancelFunc nvdaCancel = nullptr;

// Dolphin SuperNova
typedef void (__stdcall *DolCommandFunc)(const wchar_t*, int, int);
typedef void (__stdcall *DolActionFunc)(int);
static HMODULE hDolDll = nullptr;
static DolCommandFunc dolCommand = nullptr;
static DolActionFunc dolAction = nullptr;

static std::mutex speechMutex;
static std::atomic<bool> g_enabled(true);
static std::atomic<bool> g_waitingBeepRunning(false);
static std::thread g_waitingBeepThread;

// -------------------------------------------------------------
// JAWS COM Dispatch (FreedomSci.JawsApi)
// -------------------------------------------------------------
static bool JawsComSpeak(const std::wstring& text, bool interrupt) {
    CLSID clsid;
    if (FAILED(CLSIDFromProgID(L"FreedomSci.JawsApi", &clsid))) {
        return false;
    }

    IDispatch* pJaws = nullptr;
    HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, IID_IDispatch, (void**)&pJaws);
    if (FAILED(hr) || !pJaws) {
        return false;
    }

    DISPID dispid = 0;
    OLECHAR* szName = (OLECHAR*)L"SayString";
    hr = pJaws->GetIDsOfNames(IID_NULL, &szName, 1, LOCALE_USER_DEFAULT, &dispid);
    if (FAILED(hr)) {
        pJaws->Release();
        return false;
    }

    // Prepend leading spaces like accessible_output2 to prevent JAWS swallowing start of word
    std::wstring speechText = L"      " + text;

    VARIANTARG args[2];
    VariantInit(&args[0]);
    VariantInit(&args[1]);

    // IDispatch::Invoke parameters in reverse order: SayString(BSTR text, VARIANT_BOOL bFlush)
    args[0].vt = VT_BOOL;
    args[0].boolVal = interrupt ? VARIANT_TRUE : VARIANT_FALSE;

    args[1].vt = VT_BSTR;
    args[1].bstrVal = SysAllocString(speechText.c_str());

    DISPPARAMS params = { args, nullptr, 2, 0 };
    VARIANT vResult;
    VariantInit(&vResult);

    hr = pJaws->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &params, &vResult, nullptr, nullptr);
    SysFreeString(args[1].bstrVal);

    bool spoken = false;
    // When JAWS is running and speaks, SayString returns VARIANT_TRUE (-1)
    if (SUCCEEDED(hr) && vResult.vt == VT_BOOL && vResult.boolVal == VARIANT_TRUE) {
        spoken = true;
    }
    VariantClear(&vResult);
    pJaws->Release();

    return spoken;
}

// -------------------------------------------------------------
// SAPI 5 COM Dispatch (SAPI.SpVoice)
// -------------------------------------------------------------
static bool SapiSpeak(const std::wstring& text, bool interrupt) {
    CLSID clsid;
    if (FAILED(CLSIDFromProgID(L"SAPI.SpVoice", &clsid))) {
        return false;
    }

    IDispatch* pVoice = nullptr;
    HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_ALL, IID_IDispatch, (void**)&pVoice);
    if (FAILED(hr) || !pVoice) {
        return false;
    }

    DISPID dispid = 0;
    OLECHAR* szName = (OLECHAR*)L"Speak";
    hr = pVoice->GetIDsOfNames(IID_NULL, &szName, 1, LOCALE_USER_DEFAULT, &dispid);
    if (FAILED(hr)) {
        pVoice->Release();
        return false;
    }

    VARIANTARG args[2];
    VariantInit(&args[0]);
    VariantInit(&args[1]);

    // Speak(BSTR pwcs, SpeechVoiceSpeakFlags dwFlags)
    // SVSFlagsAsync = 1, SVSFPurgeBeforeSpeak = 2
    args[0].vt = VT_I4;
    args[0].lVal = interrupt ? (1 | 2) : 1;

    args[1].vt = VT_BSTR;
    args[1].bstrVal = SysAllocString(text.c_str());

    DISPPARAMS dp = { args, nullptr, 2, 0 };
    VARIANT res;
    VariantInit(&res);

    hr = pVoice->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &dp, &res, nullptr, nullptr);
    SysFreeString(args[1].bstrVal);
    VariantClear(&res);
    pVoice->Release();

    return SUCCEEDED(hr);
}

// -------------------------------------------------------------
// Screen Reader Detection
// -------------------------------------------------------------
bool IsJawsActive() {
    CoInitialize(nullptr);
    bool active = JawsComSpeak(L"", false);
    CoUninitialize();
    return active;
}

bool IsNvdaActive() {
    if (nvdaSpeak && nvdaSpeak(L"") == 0) return true;
    if (FindWindowW(L"wxWindowClassNRDialog", nullptr) != nullptr) return true;
    return false;
}

bool IsScreenReaderActive() {
    if (IsNvdaActive()) return true;
    if (IsJawsActive()) return true;
    if (FindWindowW(L"NarratorHelperWindow", nullptr) != nullptr) return true;
    if (FindWindowW(L"StandardWindow_Narrator", nullptr) != nullptr) return true;
    return false;
}

// -------------------------------------------------------------
// Initialization & Shutdown
// -------------------------------------------------------------
void Initialize() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring dir = exePath;
    size_t lastSlash = dir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) dir = dir.substr(0, lastSlash);

    // Try 64-bit NVDA Controller Client DLL if present
    std::wstring dll64 = dir + L"\\nvdaControllerClient64.dll";
    hNvdaDll = LoadLibraryW(dll64.c_str());
    if (!hNvdaDll) hNvdaDll = LoadLibraryW(L"nvdaControllerClient64.dll");

    if (hNvdaDll) {
        nvdaSpeak = (NvdaSpeakFunc)GetProcAddress(hNvdaDll, "nvdaController_speakText");
        nvdaCancel = (NvdaCancelFunc)GetProcAddress(hNvdaDll, "nvdaController_cancelSpeech");
    }

    // Try Dolphin dolapi.dll if present
    hDolDll = LoadLibraryW(L"dolapi.dll");
    if (hDolDll) {
        dolCommand = (DolCommandFunc)GetProcAddress(hDolDll, "DolAccess_Command");
        dolAction = (DolActionFunc)GetProcAddress(hDolDll, "DolAccess_Action");
    }
}

void Shutdown() {
    StopWaitingBeep();
    std::lock_guard<std::mutex> lock(speechMutex);

    if (hNvdaDll) {
        FreeLibrary(hNvdaDll);
        hNvdaDll = nullptr;
        nvdaSpeak = nullptr;
        nvdaCancel = nullptr;
    }
    if (hDolDll) {
        FreeLibrary(hDolDll);
        hDolDll = nullptr;
        dolCommand = nullptr;
        dolAction = nullptr;
    }
}

void SetEnabled(bool enabled) {
    g_enabled = enabled;
}

bool IsEnabled() {
    return g_enabled.load();
}

// -------------------------------------------------------------
// Universal Speak Engine: NVDA -> JAWS -> Dolphin -> SAPI5
// -------------------------------------------------------------
void Speak(const std::wstring& text, bool interrupt) {
    if (!g_enabled.load() || text.empty()) return;

    std::thread([text, interrupt]() {
        CoInitialize(nullptr);
        {
            std::lock_guard<std::mutex> lock(speechMutex);
            bool spoken = false;

            // 1. Try NVDA if DLL is loaded
            if (nvdaSpeak) {
                if (interrupt && nvdaCancel) {
                    nvdaCancel();
                }
                if (nvdaSpeak(text.c_str()) == 0) {
                    spoken = true;
                }
            }

            // 2. Try JAWS via FreedomSci.JawsApi COM automation
            if (!spoken) {
                if (JawsComSpeak(text, interrupt)) {
                    spoken = true;
                }
            }

            // 3. Try Dolphin SuperNova if DLL is loaded
            if (!spoken && dolCommand) {
                if (interrupt && dolAction) dolAction(141);
                dolCommand(text.c_str(), (int)((text.size() * 2) + 2), 1);
                spoken = true;
            }

            // 4. SAPI 5 fallback (Works with Windows Narrator, System Speech, or standalone)
            if (!spoken) {
                SapiSpeak(text, interrupt);
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
