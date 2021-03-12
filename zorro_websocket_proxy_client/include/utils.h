#pragma once

#include <Windows.h>
#include <tlhelp32.h>


namespace zorro {
namespace websocket {

    inline uint64_t get_timestamp() {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    }

    template<typename T>
    inline uint32_t get_message_size(size_t data_len = 0) {
        return sizeof(Message) + sizeof(T) + data_len * sizeof(char);
    }

    inline bool isProcessRunning(const wchar_t* name) {
        bool exists = false;
        PROCESSENTRY32 entry;
        entry.dwSize = sizeof(PROCESSENTRY32);
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
        if (Process32First(snapshot, &entry)) {
            while (Process32Next(snapshot, &entry)) {
                if (!_wcsicmp(entry.szExeFile, name)) {
                    exists = true;
                    break;
                }
            }
        }
        CloseHandle(snapshot);
        return exists;
    }

    inline bool isProcessRunning(DWORD processID) {
        if (HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, processID)) {
            DWORD exitCodeOut;
            // GetExitCodeProcess returns zero on failure
            if (GetExitCodeProcess(process, &exitCodeOut)) {
                return exitCodeOut == STILL_ACTIVE;
            }
        }
        return false;
    }
}
}