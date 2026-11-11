#pragma once
#ifndef JSON_HELPER_H
#define JSON_HELPER_H

#include <windows.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>
#include "models.h"

namespace JsonHelper {

inline std::string Utf16ToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &result[0], size, nullptr, nullptr);
    return result;
}

inline std::wstring Utf8ToUtf16(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0);
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), &result[0], size);
    return result;
}

inline std::string EscapeJsonString(const std::wstring& wstr) {
    std::string s = Utf16ToUtf8(wstr);
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\b') out += "\\b";
        else if (c == '\f') out += "\\f";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return out;
}

inline void SkipWhitespace(const std::string& s, size_t& pos) {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\r' || s[pos] == '\n')) {
        pos++;
    }
}

inline std::string ParseString(const std::string& s, size_t& pos) {
    SkipWhitespace(s, pos);
    if (pos >= s.size() || s[pos] != '"') return "";
    pos++; // skip opening quote
    std::string result;
    while (pos < s.size() && s[pos] != '"') {
        if (s[pos] == '\\' && pos + 1 < s.size()) {
            pos++;
            char esc = s[pos];
            if (esc == '"' || esc == '\\' || esc == '/') result += esc;
            else if (esc == 'b') result += '\b';
            else if (esc == 'f') result += '\f';
            else if (esc == 'n') result += '\n';
            else if (esc == 'r') result += '\r';
            else if (esc == 't') result += '\t';
            else result += esc;
        } else {
            result += s[pos];
        }
        pos++;
    }
    if (pos < s.size() && s[pos] == '"') pos++; // skip closing quote
    return result;
}

inline std::string SerializePairs(const std::vector<FolderPair>& pairs, const std::string& indent = "    ") {
    std::ostringstream ss;
    ss << indent << "\"pairs\": [\n";
    for (size_t i = 0; i < pairs.size(); ++i) {
        const auto& p = pairs[i];
        ss << indent << "  {\n";
        ss << indent << "    \"id\": \"" << EscapeJsonString(p.id) << "\",\n";
        ss << indent << "    \"name\": \"" << EscapeJsonString(p.name) << "\",\n";
        ss << indent << "    \"left_folder\": \"" << EscapeJsonString(p.left_folder) << "\",\n";
        ss << indent << "    \"right_folder\": \"" << EscapeJsonString(p.right_folder) << "\",\n";
        
        std::string modeStr = (p.mode == SyncMode::Mirror ? "mirror" : (p.mode == SyncMode::TwoWay ? "two_way" : "update"));
        ss << indent << "    \"mode\": \"" << modeStr << "\",\n";

        std::string schedStr = "manual";
        if (p.schedule_type == ScheduleType::Interval) schedStr = "interval";
        else if (p.schedule_type == ScheduleType::Daily) schedStr = "daily";
        else if (p.schedule_type == ScheduleType::Realtime) schedStr = "realtime";
        ss << indent << "    \"schedule_type\": \"" << schedStr << "\",\n";

        ss << indent << "    \"interval_minutes\": " << p.interval_minutes << ",\n";
        ss << indent << "    \"daily_time\": \"" << EscapeJsonString(p.daily_time) << "\",\n";
        ss << indent << "    \"enabled\": " << (p.enabled ? "true" : "false") << ",\n";
        ss << indent << "    \"last_sync_time\": \"" << EscapeJsonString(p.last_sync_time) << "\",\n";
        ss << indent << "    \"last_sync_status\": \"" << EscapeJsonString(p.last_sync_status) << "\",\n";

        ss << indent << "    \"exclusions\": [\n";
        for (size_t j = 0; j < p.exclusions.size(); ++j) {
            ss << indent << "      \"" << EscapeJsonString(p.exclusions[j]) << "\"";
            if (j + 1 < p.exclusions.size()) ss << ",";
            ss << "\n";
        }
        ss << indent << "    ]\n";

        ss << indent << "  }";
        if (i + 1 < pairs.size()) ss << ",";
        ss << "\n";
    }
    ss << indent << "]\n";
    return ss.str();
}

inline void ParsePairsFromBuffer(const std::string& buffer, std::vector<FolderPair>& pairs) {
    pairs.clear();
    size_t pairsArrayPos = buffer.find("\"pairs\"");
    if (pairsArrayPos == std::string::npos) return;

    size_t bracketPos = buffer.find('[', pairsArrayPos);
    if (bracketPos == std::string::npos) return;

    size_t pos = bracketPos + 1;
    while (pos < buffer.size()) {
        SkipWhitespace(buffer, pos);
        if (pos >= buffer.size() || buffer[pos] == ']') break;
        if (buffer[pos] == '{') {
            size_t objStart = pos;
            int braceCount = 1;
            pos++;
            while (pos < buffer.size() && braceCount > 0) {
                if (buffer[pos] == '"') {
                    pos++;
                    while (pos < buffer.size() && buffer[pos] != '"') {
                        if (buffer[pos] == '\\') pos++;
                        pos++;
                    }
                } else if (buffer[pos] == '{') {
                    braceCount++;
                } else if (buffer[pos] == '}') {
                    braceCount--;
                }
                pos++;
            }
            std::string objStr = buffer.substr(objStart, pos - objStart);

            FolderPair pair;
            auto getObjVal = [&](const std::string& key) -> std::string {
                std::string pat = "\"" + key + "\"";
                size_t kp = objStr.find(pat);
                if (kp == std::string::npos) return "";
                size_t cp = objStr.find(':', kp + pat.size());
                if (cp == std::string::npos) return "";
                cp++;
                SkipWhitespace(objStr, cp);
                if (cp < objStr.size() && objStr[cp] == '"') {
                    return ParseString(objStr, cp);
                }
                size_t ep = objStr.find_first_of(",}\n\r", cp);
                if (ep == std::string::npos) ep = objStr.size();
                std::string v = objStr.substr(cp, ep - cp);
                while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(0, 1);
                while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
                return v;
            };

            pair.id = Utf8ToUtf16(getObjVal("id"));
            pair.name = Utf8ToUtf16(getObjVal("name"));
            pair.left_folder = Utf8ToUtf16(getObjVal("left_folder"));
            pair.right_folder = Utf8ToUtf16(getObjVal("right_folder"));

            std::string mStr = getObjVal("mode");
            if (mStr == "two_way") pair.mode = SyncMode::TwoWay;
            else if (mStr == "update") pair.mode = SyncMode::Update;
            else pair.mode = SyncMode::Mirror;

            std::string scStr = getObjVal("schedule_type");
            if (scStr == "interval") pair.schedule_type = ScheduleType::Interval;
            else if (scStr == "daily") pair.schedule_type = ScheduleType::Daily;
            else if (scStr == "realtime") pair.schedule_type = ScheduleType::Realtime;
            else pair.schedule_type = ScheduleType::Manual;

            std::string minVal = getObjVal("interval_minutes");
            if (!minVal.empty()) {
                try { pair.interval_minutes = std::stoi(minVal); } catch (...) {}
            }

            pair.daily_time = Utf8ToUtf16(getObjVal("daily_time"));
            if (pair.daily_time.empty()) pair.daily_time = L"22:00";

            std::string enVal = getObjVal("enabled");
            pair.enabled = (enVal != "false");

            pair.last_sync_time = Utf8ToUtf16(getObjVal("last_sync_time"));
            if (pair.last_sync_time.empty()) pair.last_sync_time = L"Nunca";

            pair.last_sync_status = Utf8ToUtf16(getObjVal("last_sync_status"));
            if (pair.last_sync_status.empty()) pair.last_sync_status = L"Sin sincronizar";

            // Exclusions
            size_t excPos = objStr.find("\"exclusions\"");
            if (excPos != std::string::npos) {
                size_t arrStart = objStr.find('[', excPos);
                size_t arrEnd = objStr.find(']', arrStart);
                if (arrStart != std::string::npos && arrEnd != std::string::npos) {
                    pair.exclusions.clear();
                    size_t ppos = arrStart + 1;
                    while (ppos < arrEnd) {
                        SkipWhitespace(objStr, ppos);
                        if (ppos >= arrEnd) break;
                        if (objStr[ppos] == '"') {
                            std::string item = ParseString(objStr, ppos);
                            pair.exclusions.push_back(Utf8ToUtf16(item));
                        } else {
                            ppos++;
                        }
                    }
                }
            }

            pairs.push_back(pair);
        } else {
            pos++;
        }
    }
}

// -------------------------------------------------------------
// STANDALONE PROJECT FILE (.syncproj or .json)
// -------------------------------------------------------------
inline std::string SerializeProject(const SyncProject& project) {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"project_name\": \"" << EscapeJsonString(project.name) << "\",\n";
    ss << "  \"version\": 1,\n";
    ss << SerializePairs(project.pairs, "  ");
    ss << "}\n";
    return ss.str();
}

inline bool SaveProjectToFile(const std::wstring& filePath, const SyncProject& project) {
    std::string json = SerializeProject(project);
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD bytesWritten = 0;
    WriteFile(hFile, json.data(), (DWORD)json.size(), &bytesWritten, nullptr);
    CloseHandle(hFile);
    return true;
}

inline bool LoadProjectFromFile(const std::wstring& filePath, SyncProject& project) {
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD size = GetFileSize(hFile, nullptr);
    if (size == 0 || size == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        return false;
    }

    std::string buffer(size, 0);
    DWORD bytesRead = 0;
    ReadFile(hFile, &buffer[0], size, &bytesRead, nullptr);
    CloseHandle(hFile);

    project.file_path = filePath;

    // Extract project_name
    std::string pat = "\"project_name\"";
    size_t kpos = buffer.find(pat);
    if (kpos != std::string::npos) {
        size_t cpos = buffer.find(':', kpos + pat.size());
        if (cpos != std::string::npos) {
            cpos++;
            SkipWhitespace(buffer, cpos);
            if (cpos < buffer.size() && buffer[cpos] == '"') {
                project.name = Utf8ToUtf16(ParseString(buffer, cpos));
            }
        }
    }
    if (project.name.empty()) {
        size_t lastSlash = filePath.find_last_of(L"\\/");
        std::wstring fname = (lastSlash == std::wstring::npos) ? filePath : filePath.substr(lastSlash + 1);
        size_t dot = fname.find_last_of(L'.');
        if (dot != std::wstring::npos) fname = fname.substr(0, dot);
        project.name = fname;
    }

    ParsePairsFromBuffer(buffer, project.pairs);
    return true;
}

// -------------------------------------------------------------
// APP CONFIG (config.json)
// -------------------------------------------------------------
inline std::string Serialize(const AppSettings& settings, const SyncProject& currentProject) {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"settings\": {\n";
    ss << "    \"start_with_windows\": " << (settings.start_with_windows ? "true" : "false") << ",\n";
    ss << "    \"start_minimized\": " << (settings.start_minimized ? "true" : "false") << ",\n";
    ss << "    \"minimize_to_tray_on_close\": " << (settings.minimize_to_tray_on_close ? "true" : "false") << ",\n";
    ss << "    \"speech_enabled\": " << (settings.speech_enabled ? "true" : "false") << ",\n";
    ss << "    \"sound_alerts\": " << (settings.sound_alerts ? "true" : "false") << ",\n";
    ss << "    \"auto_sync_on_startup\": " << (settings.auto_sync_on_startup ? "true" : "false") << ",\n";
    ss << "    \"current_project_path\": \"" << EscapeJsonString(settings.current_project_path) << "\",\n";
    
    ss << "    \"recent_projects\": [\n";
    for (size_t i = 0; i < settings.recent_projects.size(); ++i) {
        ss << "      \"" << EscapeJsonString(settings.recent_projects[i]) << "\"";
        if (i + 1 < settings.recent_projects.size()) ss << ",";
        ss << "\n";
    }
    ss << "    ]\n";
    ss << "  },\n";

    ss << "  \"current_project_name\": \"" << EscapeJsonString(currentProject.name) << "\",\n";
    ss << SerializePairs(currentProject.pairs, "  ");
    ss << "}\n";
    return ss.str();
}

inline bool SaveToFile(const std::wstring& filePath, const AppSettings& settings, const SyncProject& currentProject) {
    std::string json = Serialize(settings, currentProject);
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD bytesWritten = 0;
    WriteFile(hFile, json.data(), (DWORD)json.size(), &bytesWritten, nullptr);
    CloseHandle(hFile);
    return true;
}

inline bool LoadFromFile(const std::wstring& filePath, AppSettings& settings, SyncProject& currentProject) {
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD size = GetFileSize(hFile, nullptr);
    if (size == 0 || size == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        return false;
    }

    std::string buffer(size, 0);
    DWORD bytesRead = 0;
    ReadFile(hFile, &buffer[0], size, &bytesRead, nullptr);
    CloseHandle(hFile);

    auto extractValue = [&](const std::string& key) -> std::string {
        std::string pattern = "\"" + key + "\"";
        size_t kpos = buffer.find(pattern);
        if (kpos == std::string::npos) return "";
        size_t cpos = buffer.find(':', kpos + pattern.size());
        if (cpos == std::string::npos) return "";
        cpos++;
        SkipWhitespace(buffer, cpos);
        if (cpos < buffer.size() && buffer[cpos] == '"') {
            return ParseString(buffer, cpos);
        }
        size_t endpos = buffer.find_first_of(",}\n\r", cpos);
        if (endpos == std::string::npos) endpos = buffer.size();
        std::string val = buffer.substr(cpos, endpos - cpos);
        while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(0, 1);
        while (!val.empty() && (val.back() == ' ' || val.back() == '\t')) val.pop_back();
        return val;
    };

    std::string startWin = extractValue("start_with_windows");
    if (!startWin.empty()) settings.start_with_windows = (startWin == "true");

    std::string minClose = extractValue("minimize_to_tray_on_close");
    if (!minClose.empty()) settings.minimize_to_tray_on_close = (minClose == "true");

    std::string startMin = extractValue("start_minimized");
    if (!startMin.empty()) settings.start_minimized = (startMin == "true");

    std::string spch = extractValue("speech_enabled");
    if (!spch.empty()) settings.speech_enabled = (spch == "true");

    std::string snd = extractValue("sound_alerts");
    if (!snd.empty()) settings.sound_alerts = (snd == "true");

    std::string autoStart = extractValue("auto_sync_on_startup");
    if (!autoStart.empty()) settings.auto_sync_on_startup = (autoStart == "true");

    settings.current_project_path = Utf8ToUtf16(extractValue("current_project_path"));

    // Recent projects
    size_t recentPos = buffer.find("\"recent_projects\"");
    if (recentPos != std::string::npos) {
        size_t arrStart = buffer.find('[', recentPos);
        size_t arrEnd = buffer.find(']', arrStart);
        if (arrStart != std::string::npos && arrEnd != std::string::npos) {
            settings.recent_projects.clear();
            size_t ppos = arrStart + 1;
            while (ppos < arrEnd) {
                SkipWhitespace(buffer, ppos);
                if (ppos >= arrEnd) break;
                if (buffer[ppos] == '"') {
                    std::string item = ParseString(buffer, ppos);
                    settings.recent_projects.push_back(Utf8ToUtf16(item));
                } else {
                    ppos++;
                }
            }
        }
    }

    std::string prjName = extractValue("current_project_name");
    currentProject.name = prjName.empty() ? L"Proyecto Principal" : Utf8ToUtf16(prjName);
    currentProject.file_path = settings.current_project_path;

    ParsePairsFromBuffer(buffer, currentProject.pairs);
    return true;
}

} // namespace JsonHelper

#endif // JSON_HELPER_H
