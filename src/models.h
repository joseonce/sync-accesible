#pragma once
#ifndef MODELS_H
#define MODELS_H

#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

enum class SyncMode {
    Mirror = 0,    // Espejo: Destino se vuelve idéntico al origen (copia y borra)
    TwoWay = 1,    // Bidireccional: Cambios más recientes en ambos lados
    Update = 2     // Actualizar: Copia nuevos y modificados de Izq a Der, no borra
};

enum class ScheduleType {
    Manual = 0,    // Manual
    Interval = 1,  // Intervalo (cada X minutos)
    Daily = 2,     // Diariamente a una hora fija (HH:MM)
    Realtime = 3   // En tiempo real (al detectar cambios)
};

enum class ActionType {
    CopyToRight = 0, // -> Copiar a Destino
    CopyToLeft = 1,  // <- Copiar a Origen
    DeleteRight = 2, // X Eliminar en Destino
    DeleteLeft = 3,  // X Eliminar en Origen
    Identical = 4,   // = Archivos idénticos
    Conflict = 5     // ! Conflicto
};

inline const wchar_t* SyncModeToString(SyncMode mode) {
    switch (mode) {
        case SyncMode::Mirror: return L"Espejo (Mirror)";
        case SyncMode::TwoWay: return L"Bidireccional (Two-Way)";
        case SyncMode::Update: return L"Actualizar (Update)";
        default: return L"Desconocido";
    }
}

inline const wchar_t* ScheduleTypeToString(ScheduleType type) {
    switch (type) {
        case ScheduleType::Manual: return L"Manual";
        case ScheduleType::Interval: return L"Intervalo";
        case ScheduleType::Daily: return L"Diaria";
        case ScheduleType::Realtime: return L"Tiempo Real";
        default: return L"Manual";
    }
}

inline const wchar_t* ActionTypeToString(ActionType action) {
    switch (action) {
        case ActionType::CopyToRight: return L"-> Copiar a Destino";
        case ActionType::CopyToLeft:  return L"<- Copiar a Origen";
        case ActionType::DeleteRight: return L"X Eliminar en Destino";
        case ActionType::DeleteLeft:  return L"X Eliminar en Origen";
        case ActionType::Identical:   return L"= Identico";
        case ActionType::Conflict:    return L"! Conflicto";
        default: return L"-";
    }
}

struct FolderPair {
    std::wstring id;
    std::wstring name;
    std::wstring left_folder;
    std::wstring right_folder;
    SyncMode mode = SyncMode::Mirror;
    ScheduleType schedule_type = ScheduleType::Manual;
    int interval_minutes = 30;
    std::wstring daily_time = L"22:00";
    bool enabled = true;
    std::vector<std::wstring> exclusions = {
        L"*.tmp",
        L"~*",
        L"desktop.ini",
        L"thumbs.db",
        L"$RECYCLE.BIN",
        L"System Volume Information",
        L".git",
        L"__pycache__",
        L"*.ffs_db",
        L"*.ffs_lock",
        L"sync.ffs_db"
    };
    std::wstring last_sync_time = L"Nunca";
    std::wstring last_sync_status = L"Sin sincronizar";
};

struct SyncItem {
    std::wstring rel_path;
    ActionType action = ActionType::Identical;
    bool is_dir = false;
    bool left_exists = false;
    bool right_exists = false;
    uint64_t left_size = 0;
    uint64_t right_size = 0;
    FILETIME left_mtime = {0, 0};
    FILETIME right_mtime = {0, 0};
    std::wstring status = L"Pendiente";
    std::wstring error_msg;
};

struct ComparisonSummary {
    std::wstring pair_id;
    std::wstring pair_name;
    std::vector<SyncItem> items;
    int total_files_to_sync = 0;
    uint64_t total_bytes_to_sync = 0;
    int files_to_copy_right = 0;
    int files_to_copy_left = 0;
    int files_to_delete_right = 0;
    int files_to_delete_left = 0;
    int identical_count = 0;
    int conflict_count = 0;
};

struct SyncProject {
    std::wstring name = L"Proyecto Sin Titulo";
    std::wstring file_path;
    std::vector<FolderPair> pairs;
};

struct BatchTaskItem {
    std::wstring project_name;
    FolderPair pair;
    std::wstring status = L"En cola";
    int files_synced = 0;
    std::wstring error_msg;
};

struct AppSettings {
    bool start_with_windows = false;
    bool start_minimized = false;
    bool minimize_to_tray_on_close = true;
    bool speech_enabled = true;
    bool sound_alerts = true;
    bool auto_sync_on_startup = false;
    std::wstring current_project_path;
    std::vector<std::wstring> recent_projects;
};

#endif // MODELS_H

