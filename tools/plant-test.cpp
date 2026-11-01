// Harmless stand-in for a planted DLL: when loaded it only writes a marker file saying which name it was loaded under.
// Used by attack-test.ps1 to prove whether the exe can be made to load a DLL from its own folder.
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        wchar_t temp[MAX_PATH], path[MAX_PATH], name[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        wsprintfW(path, L"%ssnowdesk-planted-marker.txt", temp);
        GetModuleFileNameW(self, name, MAX_PATH);
        HANDLE f = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD written;
            WriteFile(f, name, DWORD(lstrlenW(name) * sizeof(wchar_t)), &written, nullptr);
            CloseHandle(f);
        }
    }
    return TRUE;
}
