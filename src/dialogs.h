#pragma once
#ifndef DIALOGS_H
#define DIALOGS_H

#include <windows.h>
#include <string>
#include <vector>
#include "models.h"

namespace Dialogs {

bool ShowPairDialog(HWND hwndParent, FolderPair& pair, bool isNew);
bool ShowSettingsDialog(HWND hwndParent, AppSettings& settings);
bool ShowBatchRunnerDialog(HWND hwndParent, const AppSettings& settings, const SyncProject& currentProject);

bool PromptOpenProject(HWND hwndParent, std::wstring& outPath);
bool PromptSaveProject(HWND hwndParent, std::wstring& inOutPath);

void ShowAboutDialog(HWND hwndParent);
void ShowManualDialog(HWND hwndParent);
void OpenManualHtml(HWND hwndParent);
void ShowShortcutsDialog(HWND hwndParent);

} // namespace Dialogs

#endif // DIALOGS_H
