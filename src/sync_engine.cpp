#include "sync_engine.h"
#include "logger.h"
#include <shlwapi.h>
#include <shellapi.h>
#include <filesystem>
#include <map>
#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;

namespace SyncEngine {

struct FileMeta {
    std::wstring rel_path;
    bool is_dir = false;
    uint64_t size = 0;
    FILETIME mtime = {0, 0};
    bool exists = false;
};

static uint64_t FileTimeToUint64(const FILETIME& ft) {
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}

static bool AreTimestampsClose(const FILETIME& ft1, const FILETIME& ft2, int toleranceSeconds = 2) {
    uint64_t t1 = FileTimeToUint64(ft1);
    uint64_t t2 = FileTimeToUint64(ft2);
    uint64_t diff = (t1 > t2) ? (t1 - t2) : (t2 - t1);
    // 1 second = 10,000,000 intervals of 100ns
    uint64_t maxDiff = (uint64_t)toleranceSeconds * 10000000ULL;
    return diff <= maxDiff;
}

static bool IsTimeNewer(const FILETIME& ftTarget, const FILETIME& ftBase, int toleranceSeconds = 2) {
    uint64_t tTarget = FileTimeToUint64(ftTarget);
    uint64_t tBase = FileTimeToUint64(ftBase);
    uint64_t maxDiff = (uint64_t)toleranceSeconds * 10000000ULL;
    return (tTarget > tBase + maxDiff);
}

bool MatchesExclusions(const std::wstring& relPath, const std::vector<std::wstring>& exclusions) {
    if (exclusions.empty()) return false;

    // Check fileName component
    size_t lastSlash = relPath.find_last_of(L"\\/");
    std::wstring fileName = (lastSlash == std::wstring::npos) ? relPath : relPath.substr(lastSlash + 1);

    for (const auto& pat : exclusions) {
        if (pat.empty()) continue;
        if (PathMatchSpecW(fileName.c_str(), pat.c_str())) return true;
        if (PathMatchSpecW(relPath.c_str(), pat.c_str())) return true;

        // Check if pattern matches any parent directory component
        size_t start = 0;
        while (start < relPath.size()) {
            size_t slash = relPath.find_first_of(L"\\/", start);
            if (slash == std::wstring::npos) break;
            std::wstring segment = relPath.substr(start, slash - start);
            if (!segment.empty() && PathMatchSpecW(segment.c_str(), pat.c_str())) {
                return true;
            }
            start = slash + 1;
        }
    }
    return false;
}

static void ScanDirectory(
    const std::wstring& rootDir,
    std::map<std::wstring, FileMeta>& outMap,
    const std::vector<std::wstring>& exclusions,
    std::atomic<bool>* cancelToken,
    std::function<void(int count, const std::wstring& relPath)> progressCb
) {
    if (rootDir.empty()) return;
    std::error_code ec;
    if (!fs::exists(rootDir, ec) || ec) {
        LOG_WARN(L"La ruta de directorio no existe o no es accesible: " + rootDir);
        return;
    }

    fs::path rootPath(rootDir);
    int scannedCount = 0;

    auto it = fs::recursive_directory_iterator(rootPath, fs::directory_options::skip_permission_denied, ec);
    auto end = fs::recursive_directory_iterator();

    if (ec) {
        LOG_WARN(L"Error al iniciar iterador en " + rootDir + L": " + std::to_wstring(ec.value()));
        return;
    }

    while (it != end) {
        if (cancelToken && cancelToken->load()) break;

        try {
            const auto& entry = *it;
            // Evitar bucles en enlaces simbólicos y puntos de unión (junctions)
            if (entry.is_symlink(ec)) {
                it.increment(ec);
                if (ec) ec.clear();
                continue;
            }

            std::wstring relPath = fs::relative(entry.path(), rootPath, ec).wstring();
            if (ec) {
                ec.clear();
                it.increment(ec);
                continue;
            }

            if (MatchesExclusions(relPath, exclusions)) {
                // Si es un directorio excluido, evitar entrar en todo su subárbol
                if (entry.is_directory(ec)) {
                    it.disable_recursion_pending();
                }
                it.increment(ec);
                if (ec) ec.clear();
                continue;
            }

            FileMeta meta;
            meta.rel_path = relPath;
            meta.is_dir = entry.is_directory(ec);
            meta.exists = true;

            if (!meta.is_dir) {
                meta.size = entry.file_size(ec);
                if (ec) {
                    meta.size = 0;
                    ec.clear();
                }

                std::wstring fullPath = entry.path().wstring();
                WIN32_FILE_ATTRIBUTE_DATA attr;
                if (GetFileAttributesExW(fullPath.c_str(), GetFileExInfoStandard, &attr)) {
                    meta.mtime = attr.ftLastWriteTime;
                }
            }

            outMap[relPath] = meta;
            scannedCount++;

            if (progressCb && (scannedCount % 25 == 0)) {
                progressCb(scannedCount, relPath);
            }
        } catch (const std::exception& ex) {
            LOG_WARN(std::string("Excepción al examinar archivo: ") + ex.what());
        } catch (...) {
            LOG_WARN("Excepción desconocida al examinar archivo en directorio");
        }

        it.increment(ec);
        if (ec) {
            LOG_WARN(L"Error de E/S al avanzar en directorio: " + std::to_wstring(ec.value()));
            ec.clear();
        }
    }
}

ComparisonSummary CompareFolders(
    const FolderPair& pair,
    std::atomic<bool>* cancelToken,
    std::function<void(int percent, const std::wstring& status)> progressCallback
) {
    LOG_INFO(L"Iniciando comparación para: " + pair.name + L" [Izq: " + pair.left_folder + L" | Der: " + pair.right_folder + L"]");
    ComparisonSummary summary;
    summary.pair_id = pair.id;
    summary.pair_name = pair.name;

    std::map<std::wstring, FileMeta> leftFiles;
    std::map<std::wstring, FileMeta> rightFiles;

    if (progressCallback) progressCallback(5, L"Examinando carpeta de origen...");

    // Scan Left
    ScanDirectory(pair.left_folder, leftFiles, pair.exclusions, cancelToken, [&](int cnt, const std::wstring& rp) {
        if (progressCallback && cnt % 20 == 0) {
            progressCallback(10 + (cnt % 15), L"Examinando origen: " + rp);
        }
    });
    if (cancelToken && cancelToken->load()) return summary;

    if (progressCallback) progressCallback(25, L"Examinando carpeta de destino...");

    // Scan Right
    ScanDirectory(pair.right_folder, rightFiles, pair.exclusions, cancelToken, [&](int cnt, const std::wstring& rp) {
        if (progressCallback && cnt % 20 == 0) {
            progressCallback(30 + (cnt % 15), L"Examinando destino: " + rp);
        }
    });
    if (cancelToken && cancelToken->load()) return summary;

    if (progressCallback) progressCallback(50, L"Comparando archivos y atributos...");

    // Combine all relative paths
    std::map<std::wstring, bool> allPaths;
    for (const auto& kv : leftFiles) allPaths[kv.first] = true;
    for (const auto& kv : rightFiles) allPaths[kv.first] = true;

    size_t totalPaths = allPaths.size();
    size_t processed = 0;

    for (const auto& kv : allPaths) {
        if (cancelToken && cancelToken->load()) break;
        processed++;

        const std::wstring& relPath = kv.first;
        if (progressCallback && (processed % 15 == 0 || processed == totalPaths)) {
            int pct = 50 + (int)((processed * 50) / (totalPaths > 0 ? totalPaths : 1));
            progressCallback(pct, L"Comparando: " + relPath);
        }
        auto itL = leftFiles.find(relPath);
        auto itR = rightFiles.find(relPath);

        bool inLeft = (itL != leftFiles.end());
        bool inRight = (itR != rightFiles.end());

        FileMeta metaL = inLeft ? itL->second : FileMeta{};
        FileMeta metaR = inRight ? itR->second : FileMeta{};

        bool isDir = (inLeft && metaL.is_dir) || (inRight && metaR.is_dir);

        SyncItem item;
        item.rel_path = relPath;
        item.is_dir = isDir;
        item.left_exists = inLeft;
        item.right_exists = inRight;
        item.left_size = inLeft ? metaL.size : 0;
        item.right_size = inRight ? metaR.size : 0;
        item.left_mtime = metaL.mtime;
        item.right_mtime = metaR.mtime;

        if (isDir) {
            // Directories themselves are handled during item copying, but we can track creation
            if (inLeft && !inRight) {
                item.action = ActionType::CopyToRight;
            } else if (!inLeft && inRight) {
                item.action = (pair.mode == SyncMode::Mirror) ? ActionType::DeleteRight : ActionType::Identical;
            } else {
                item.action = ActionType::Identical;
            }
        } else {
            // File comparison
            if (inLeft && !inRight) {
                item.action = ActionType::CopyToRight;
            } else if (!inLeft && inRight) {
                if (pair.mode == SyncMode::Mirror) {
                    item.action = ActionType::DeleteRight;
                } else if (pair.mode == SyncMode::TwoWay) {
                    item.action = ActionType::CopyToLeft;
                } else { // Update
                    item.action = ActionType::Identical;
                }
            } else {
                // Both exist
                bool sameSize = (metaL.size == metaR.size);
                bool sameTime = AreTimestampsClose(metaL.mtime, metaR.mtime, 2);

                if (sameSize && sameTime) {
                    item.action = ActionType::Identical;
                } else {
                    if (pair.mode == SyncMode::Mirror) {
                        item.action = ActionType::CopyToRight;
                    } else if (pair.mode == SyncMode::Update) {
                        if (IsTimeNewer(metaL.mtime, metaR.mtime, 2)) {
                            item.action = ActionType::CopyToRight;
                        } else {
                            item.action = ActionType::Identical;
                        }
                    } else { // Two-Way
                        if (IsTimeNewer(metaL.mtime, metaR.mtime, 2)) {
                            item.action = ActionType::CopyToRight;
                        } else if (IsTimeNewer(metaR.mtime, metaL.mtime, 2)) {
                            item.action = ActionType::CopyToLeft;
                        } else {
                            item.action = ActionType::Conflict;
                        }
                    }
                }
            }
        }

        // Set descriptive initial status explaining the reason
        if (item.action == ActionType::Identical) {
            item.status = L"Idéntico";
        } else if (item.action == ActionType::CopyToRight) {
            item.status = inRight ? L"Actualizar en destino" : L"Nuevo en origen";
        } else if (item.action == ActionType::CopyToLeft) {
            item.status = inLeft ? L"Actualizar en origen" : L"Nuevo en destino";
        } else if (item.action == ActionType::DeleteRight) {
            item.status = L"No existe en origen";
        } else if (item.action == ActionType::DeleteLeft) {
            item.status = L"No existe en destino";
        } else if (item.action == ActionType::Conflict) {
            item.status = L"Conflicto de fecha/tamaño";
        }

        // Aggregate statistics
        switch (item.action) {
            case ActionType::CopyToRight:
                if (!item.is_dir) {
                    summary.files_to_copy_right++;
                    summary.total_bytes_to_sync += item.left_size;
                }
                break;
            case ActionType::CopyToLeft:
                if (!item.is_dir) {
                    summary.files_to_copy_left++;
                    summary.total_bytes_to_sync += item.right_size;
                }
                break;
            case ActionType::DeleteRight:
                if (!item.is_dir) summary.files_to_delete_right++;
                break;
            case ActionType::DeleteLeft:
                if (!item.is_dir) summary.files_to_delete_left++;
                break;
            case ActionType::Identical:
                summary.identical_count++;
                break;
            case ActionType::Conflict:
                summary.conflict_count++;
                break;
        }

        summary.items.push_back(item);
    }

    summary.total_files_to_sync = summary.files_to_copy_right + summary.files_to_copy_left +
                                  summary.files_to_delete_right + summary.files_to_delete_left;

    if (progressCallback) progressCallback(100, L"Comparación finalizada.");
    LOG_INFO(L"Comparación finalizada para: " + pair.name + L". Archivos a sincronizar: " + std::to_wstring(summary.total_files_to_sync) + L" (Total analizados: " + std::to_wstring(summary.items.size()) + L")");
    return summary;
}

static bool CopyFilePreservingAttributes(const std::wstring& src, const std::wstring& dst, std::wstring& errOut) {
    std::error_code ec;
    fs::path parent = fs::path(dst).parent_path();
    if (!parent.empty() && !fs::exists(parent)) {
        fs::create_directories(parent, ec);
    }

    // Si el archivo de destino ya existe y tiene atributos de Solo Lectura, Oculto o Sistema,
    // Windows bloqueará CopyFileExW con ERROR_ACCESS_DENIED. Restablecemos atributos a Normal.
    DWORD dstAttr = GetFileAttributesW(dst.c_str());
    if (dstAttr != INVALID_FILE_ATTRIBUTES) {
        if (dstAttr & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) {
            SetFileAttributesW(dst.c_str(), FILE_ATTRIBUTE_NORMAL);
        }
    }

    if (!CopyFileExW(src.c_str(), dst.c_str(), nullptr, nullptr, nullptr, COPY_FILE_ALLOW_DECRYPTED_DESTINATION)) {
        DWORD err = GetLastError();
        // Si falló por acceso denegado, intentar forzar de nuevo el atributo a Normal y reintentar
        if (err == ERROR_ACCESS_DENIED) {
            SetFileAttributesW(dst.c_str(), FILE_ATTRIBUTE_NORMAL);
            if (CopyFileExW(src.c_str(), dst.c_str(), nullptr, nullptr, nullptr, COPY_FILE_ALLOW_DECRYPTED_DESTINATION)) {
                err = ERROR_SUCCESS;
            } else {
                err = GetLastError();
            }
        }

        if (err != ERROR_SUCCESS) {
            wchar_t buf[256];
            FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, err, 0, buf, 256, nullptr);
            errOut = buf;
            while (!errOut.empty() && (errOut.back() == L'\r' || errOut.back() == L'\n' || errOut.back() == L'.')) {
                errOut.pop_back();
            }
            if (err == ERROR_ACCESS_DENIED) {
                errOut += L" (acceso denegado: el archivo puede estar en uso por otra aplicación, bloqueado por OneDrive o con permisos restringidos)";
            } else if (err == ERROR_SHARING_VIOLATION) {
                errOut += L" (violación de uso compartido: el archivo está abierto exclusivamente por otro proceso)";
            }
            return false;
        }
    }

    // Preserve LastWriteTime
    HANDLE hSrc = CreateFileW(src.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hSrc != INVALID_HANDLE_VALUE) {
        FILETIME ftCreate, ftAccess, ftWrite;
        if (GetFileTime(hSrc, &ftCreate, &ftAccess, &ftWrite)) {
            HANDLE hDst = CreateFileW(dst.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (hDst != INVALID_HANDLE_VALUE) {
                SetFileTime(hDst, &ftCreate, &ftAccess, &ftWrite);
                CloseHandle(hDst);
            }
        }
        CloseHandle(hSrc);
    }
    return true;
}

static bool DeleteToRecycleBin(const std::wstring& path, std::wstring& errOut) {
    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY)) {
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    }

    std::wstring doubleNullPath = path;
    doubleNullPath.push_back(L'\0'); // Double null-terminated for SHFileOperation

    SHFILEOPSTRUCTW fileOp = {0};
    fileOp.wFunc = FO_DELETE;
    fileOp.pFrom = doubleNullPath.c_str();
    fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;

    int ret = SHFileOperationW(&fileOp);
    if (ret != 0) {
        // Fallback to permanent deletion if recycle bin fails
        std::error_code ec;
        if (fs::is_directory(path, ec)) {
            fs::remove_all(path, ec);
        } else {
            fs::remove(path, ec);
        }
        if (ec) {
            errOut = fs::path(path).wstring() + L": " + std::wstring(ec.message().begin(), ec.message().end());
            return false;
        }
    }
    return true;
}

bool ExecuteSync(
    const FolderPair& pair,
    ComparisonSummary& summary,
    std::atomic<bool>* cancelToken,
    std::function<void(int percent, int current, int total, const std::wstring& relPath)> progressCallback
) {
    fs::path leftRoot(pair.left_folder);
    fs::path rightRoot(pair.right_folder);

    // Filter items requiring action
    std::vector<size_t> actionIndices;
    for (size_t i = 0; i < summary.items.size(); ++i) {
        if (summary.items[i].action != ActionType::Identical && summary.items[i].action != ActionType::Conflict) {
            actionIndices.push_back(i);
        }
    }

    int totalActions = (int)actionIndices.size();
    LOG_INFO(L"Iniciando ExecuteSync para: " + pair.name + L". Total de acciones requeridas: " + std::to_wstring(totalActions));

    if (totalActions == 0) {
        if (progressCallback) progressCallback(100, 0, 0, L"Sin acciones pendientes");
        LOG_INFO(L"ExecuteSync: Sin cambios pendientes para: " + pair.name);
        return true;
    }

    // Sort deletions so deeper files are deleted before parent folders
    std::sort(actionIndices.begin(), actionIndices.end(), [&](size_t a, size_t b) {
        const auto& itemA = summary.items[a];
        const auto& itemB = summary.items[b];
        bool isDelA = (itemA.action == ActionType::DeleteRight || itemA.action == ActionType::DeleteLeft);
        bool isDelB = (itemB.action == ActionType::DeleteRight || itemB.action == ActionType::DeleteLeft);
        if (isDelA && !isDelB) return false; // deletions later or earlier? Deletions deeper first
        if (!isDelA && isDelB) return true;
        if (isDelA && isDelB) {
            return itemA.rel_path.size() > itemB.rel_path.size(); // deeper path first
        }
        // Non-deletions: directory first
        if (itemA.is_dir && !itemB.is_dir) return true;
        if (!itemA.is_dir && itemB.is_dir) return false;
        return itemA.rel_path < itemB.rel_path;
    });

    int currentIdx = 0;
    bool allSuccess = true;

    for (size_t idx : actionIndices) {
        if (cancelToken && cancelToken->load()) {
            allSuccess = false;
            break;
        }

        SyncItem& item = summary.items[idx];
        currentIdx++;

        if (progressCallback) {
            int pct = (int)((currentIdx * 100) / (totalActions > 0 ? totalActions : 1));
            progressCallback(pct, currentIdx, totalActions, item.rel_path);
        }

        std::wstring srcPath, dstPath;
        std::wstring errStr;
        bool ok = true;

        if (item.action == ActionType::CopyToRight) {
            srcPath = (leftRoot / item.rel_path).wstring();
            dstPath = (rightRoot / item.rel_path).wstring();
            if (item.is_dir) {
                std::error_code ec;
                fs::create_directories(dstPath, ec);
                ok = !ec;
                if (!ok) errStr = L"Error al crear directorio destino";
            } else {
                ok = CopyFilePreservingAttributes(srcPath, dstPath, errStr);
            }
        } else if (item.action == ActionType::CopyToLeft) {
            srcPath = (rightRoot / item.rel_path).wstring();
            dstPath = (leftRoot / item.rel_path).wstring();
            if (item.is_dir) {
                std::error_code ec;
                fs::create_directories(dstPath, ec);
                ok = !ec;
                if (!ok) errStr = L"Error al crear directorio origen";
            } else {
                ok = CopyFilePreservingAttributes(srcPath, dstPath, errStr);
            }
        } else if (item.action == ActionType::DeleteRight) {
            dstPath = (rightRoot / item.rel_path).wstring();
            ok = DeleteToRecycleBin(dstPath, errStr);
        } else if (item.action == ActionType::DeleteLeft) {
            dstPath = (leftRoot / item.rel_path).wstring();
            ok = DeleteToRecycleBin(dstPath, errStr);
        }

        if (ok) {
            item.status = L"Completado";
            LOG_INFO(L"Sincronizado con éxito: " + item.rel_path);
        } else {
            item.status = L"Error: " + errStr;
            item.error_msg = errStr;
            allSuccess = false;
            LOG_ERROR(L"Error al procesar: " + item.rel_path + L" - Causa: " + errStr);
        }
    }

    LOG_INFO(L"ExecuteSync concluido para: " + pair.name + (allSuccess ? L" [ÉXITO]" : L" [CON ERRORES]"));
    return allSuccess;
}

} // namespace SyncEngine

