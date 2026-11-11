#pragma once
#ifndef SPEECH_H
#define SPEECH_H

#include <windows.h>
#include <string>

namespace Speech {

void Initialize();
void Shutdown();
void Speak(const std::wstring& text, bool interrupt = true);
void PlayAlertSound();
void StartWaitingBeep();
void StopWaitingBeep();
void SetEnabled(bool enabled);
bool IsEnabled();

} // namespace Speech

#endif // SPEECH_H

