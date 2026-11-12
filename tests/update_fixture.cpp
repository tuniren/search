#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <string>

int wmain(int count, wchar_t** arguments)
{
    const auto directory = std::filesystem::path(arguments[0]).parent_path();
    if (count > 1 && std::wstring(arguments[1]) == L"--wait") {
        Sleep(300);
        return 0;
    }
    for (int i = 1; i + 1 < count; ++i) {
        if (std::wstring(arguments[i]) == L"--update-ready") {
            if (std::filesystem::exists(directory / L"startup-fail"))
                return 3;
            std::ofstream ready(std::filesystem::path(arguments[i + 1]), std::ios::binary);
            ready << "search-update-ready\n";
            ready.close();
            Sleep(400);
            return 0;
        }
    }
    return 0;
}
