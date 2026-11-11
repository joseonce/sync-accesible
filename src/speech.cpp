#include "speech.h"
#include <sapi.h>
#include <oleauto.h>
#include <mutex>
#include <thread>
#include <atomic>
#include <vector>
#include <mmsystem.h>

namespace Speech {

static const GUID MY_CLSID_SpVoice = { 0x96749377, 0x3391, 0x11D2, { 0x9E, 0xE3, 0x00, 0xC0, 0x4F, 0x79, 0x73, 0x96 } };
static const GUID MY_IID_ISpVoice   = { 0x6C44DF74, 0x72B9, 0x4992, { 0xA1, 0xEC, 0xEF, 0x99, 0x66, 0x9F, 0xA5, 0x66 } };

// NVDA
typedef error_status_t (__stdcall *NvdaSpeakFunc)(const wchar_t*);
typedef error_status_t (__stdcall *NvdaCancelFunc)();
static HMODULE hNvdaDll = nullptr;
static NvdaSpeakFunc nvdaSpeak = nullptr;
static NvdaCancelFunc nvdaCancel = nullptr;

// JAWS (jfwapi.dll fallback)
typedef BOOL (WINAPI *JfwSayStringFunc)(LPCTSTR, BOOL);
typedef BOOL (WINAPI *JfwRunFunctionFunc)(LPCTSTR);
static HMODULE hJfwDll = nullptr;
static JfwSayStringFunc jfwSayString = nullptr;
static JfwRunFunctionFunc jfwRunFunction = nullptr;

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
// JAWS COM Helper
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

    VARIANTARG args[2];
    VariantInit(&args[0]);
    VariantInit(&args[1]);

    // IDispatch::Invoke takes parameters in reverse order:
    // SayString(BSTR text, VARIANT_BOOL bFlush)
    args[0].vt = VT_BOOL;
    args[0].boolVal = interrupt ? VARIANT_TRUE : VARIANT_FALSE;

    args[1].vt = VT_BSTR;
    args[1].bstrVal = SysAllocString(text.c_str());

    DISPPARAMS params = { args, nullptr, 2, 0 };
    VARIANT vResult;
    VariantInit(&vResult);

    hr = pJaws->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &params, &vResult, nullptr, nullptr);
    SysFreeString(args[1].bstrVal);
    pJaws->Release();

    return SUCCEEDED(hr);
}

// -------------------------------------------------------------
// Screen Reader Detection
// -------------------------------------------------------------
bool IsJawsActive() {
    if (FindWindowW(L"JFWUI2", L"JAWS") != nullptr) return true;
    if (FindWindowW(L"JFWUI2", nullptr) != nullptr) return true;
    return false;
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

    // 2. Try JAWS jfwapi.dll
    hJfwDll = LoadLibraryW(L"jfwapi.dll");
    if (hJfwDll) {
        jfwSayString = (JfwSayStringFunc)GetProcAddress(hJfwDll, "JFW_SayString");
        jfwRunFunction = (JfwRunFunctionFunc)GetProcAddress(hJfwDll, "JFW_RunFunction");
    }

    // 3. Try Dolphin dolapi.dll
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
    if (hJfwDll) {
        FreeLibrary(hJfwDll);
        hJfwDll = nullptr;
        jfwSayString = nullptr;
        jfwRunFunction = nullptr;
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

            // 1. Try NVDA
            if (nvdaSpeak) {
                if (interrupt && nvdaCancel) {
                    nvdaCancel();
                }
                if (nvdaSpeak(text.c_str()) == 0) {
                    spoken = true;
                }
            }

            // 2. Try JAWS (COM Automation or jfwapi.dll)
            if (!spoken) {
                if (IsJawsActive() || jfwSayString != nullptr) {
                    if (jfwSayString && jfwSayString(text.c_str(), interrupt ? TRUE : FALSE)) {
                        spoken = true;
                    } else if (JawsComSpeak(text, interrupt)) {
                        spoken = true;
                    }
                }
            }

            // 3. Try Dolphin SuperNova
            if (!spoken && dolCommand) {
                if (interrupt && dolAction) dolAction(141);
                dolCommand(text.c_str(), (int)((text.size() * 2) + 2), 1);
                spoken = true;
            }

            // 4. Try SAPI 5 fallback (Voice created on this thread to avoid apartment violation)
            if (!spoken) {
                ISpVoice* pLocalVoice = nullptr;
                HRESULT hr = CoCreateInstance(MY_CLSID_SpVoice, NULL, CLSCTX_ALL, MY_IID_ISpVoice, (void**)&pLocalVoice);
                if (SUCCEEDED(hr) && pLocalVoice) {
                    DWORD flags = SPF_DEFAULT;
                    if (interrupt) {
                        flags |= SPF_PURGEBEFORESPEAK;
                    }
                    pLocalVoice->Speak(text.c_str(), flags, nullptr);
                    pLocalVoice->Release();
                }
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
