#include "miniz.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <bcrypt.h>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <shellapi.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace
{
constexpr auto inventoryName = L"installed-files.txt";
constexpr auto transactionName = L".search-update";

struct HandleCloser {
    void operator()(void* value) const
    {
        if (value && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
};
using Handle = std::unique_ptr<void, HandleCloser>;

std::wstring fromUtf8(const std::string& value)
{
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0);
    if (!count)
        throw std::runtime_error("Invalid UTF-8 path");
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                        result.data(), count);
    return result;
}

std::string toUtf8(const std::wstring& value)
{
    const auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(count, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                        result.data(), count, nullptr, nullptr);
    return result;
}

std::wstring folded(std::wstring text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return text;
}

fs::path relativePath(const std::string& text)
{
    if (text.empty() || text.size() > 1024 || text.find('\\') != std::string::npos)
        throw std::runtime_error("Unsafe package path");
    const fs::path result(fromUtf8(text));
    if (result.is_absolute() || result.has_root_name() || result.has_root_directory())
        throw std::runtime_error("Absolute package path");
    for (const auto& component : result) {
        const auto name = component.wstring();
        if (name.empty() || name == L"." || name == L".." || name.back() == L'.' || name.back() == L' ' ||
            name.find_first_of(L"<>:\"|?*\\") != std::wstring::npos ||
            std::any_of(name.begin(), name.end(), [](wchar_t c) { return c < 32 || c >= 127; }) ||
            folded(name).starts_with(L".search-"))
            throw std::runtime_error("Unsafe package path component");
        const auto base = folded(name.substr(0, name.find(L'.')));
        if (base == L"con" || base == L"prn" || base == L"aux" || base == L"nul" ||
            (base.size() == 4 && (base.starts_with(L"com") || base.starts_with(L"lpt")) &&
             (base[3] >= L'1' && base[3] <= L'9')))
            throw std::runtime_error("Reserved Windows filename");
    }
    return result;
}

void noReparsePoints(const fs::path& path)
{
    for (auto current = path; !current.empty(); current = current.parent_path()) {
        const auto attributes = GetFileAttributesW(current.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("Update paths must not contain symbolic links or junctions");
        if (current == current.parent_path())
            break;
    }
}

std::string read(const fs::path& path, size_t limit = 1024 * 1024)
{
    if (!fs::is_regular_file(path) || fs::file_size(path) > limit)
        throw std::runtime_error("Missing or oversized package inventory");
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot read package file");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write(const fs::path& path, const std::string& data)
{
    Handle file(
        CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    DWORD written = 0;
    if (file.get() == INVALID_HANDLE_VALUE ||
        !WriteFile(file.get(), data.data(), static_cast<DWORD>(data.size()), &written, nullptr) ||
        written != data.size() || !FlushFileBuffers(file.get()))
        throw std::runtime_error("Cannot write update journal");
}

using Files = std::set<std::wstring>;

Files inventory(const fs::path& path)
{
    const auto data = read(path);
    Files result;
    size_t position = 0;
    while (position < data.size()) {
        const auto end = data.find('\n', position);
        const auto line =
            data.substr(position, end == std::string::npos ? data.size() - position : end - position);
        const auto relative = relativePath(line).generic_wstring();
        if (!result.insert(folded(relative)).second || result.size() > 4096)
            throw std::runtime_error("Duplicate or excessive package files");
        position = end == std::string::npos ? data.size() : end + 1;
    }
    for (const auto* required : {L"search.exe", L"search-updater.exe", L"package-info.json", inventoryName}) {
        if (!result.contains(required))
            throw std::runtime_error("Incomplete package inventory");
    }
    return result;
}

void verifyHash(const fs::path& archive, const std::wstring& expected)
{
    if (expected.size() != 64 || !fs::is_regular_file(archive) || fs::file_size(archive) > 128 * 1024 * 1024)
        throw std::runtime_error("Invalid archive size or checksum");
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("Cannot initialize SHA-256");
    const auto closeAlgorithm =
        std::unique_ptr<void, decltype([](void* handle) { BCryptCloseAlgorithmProvider(handle, 0); })>(
            algorithm);
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0)
        throw std::runtime_error("Cannot initialize SHA-256 hash");
    const auto closeHash =
        std::unique_ptr<void, decltype([](void* handle) { BCryptDestroyHash(handle); })>(hash);
    std::ifstream input(archive, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot open update archive");
    std::array<char, 65536> buffer;
    while (input.read(buffer.data(), buffer.size()) || input.gcount()) {
        if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(input.gcount()),
                           0) < 0)
            throw std::runtime_error("Cannot hash update archive");
    }
    if (!input.eof())
        throw std::runtime_error("Cannot read update archive");
    std::array<unsigned char, 32> digest{};
    if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
        throw std::runtime_error("Cannot finish update hash");
    constexpr wchar_t digits[] = L"0123456789abcdef";
    std::wstring actual;
    for (const auto byte : digest) {
        actual += digits[byte >> 4];
        actual += digits[byte & 15];
    }
    if (actual != expected)
        throw std::runtime_error("Update archive SHA-256 mismatch");
}

Files extract(const fs::path& archive, const fs::path& destination, const std::wstring& root)
{
    relativePath(toUtf8(root));
    if (root.find_first_of(L"/\\") != std::wstring::npos)
        throw std::runtime_error("Invalid archive root");
    FILE* input = nullptr;
    if (_wfopen_s(&input, archive.c_str(), L"rb") || !input)
        throw std::runtime_error("Cannot open archive");
    const auto close = std::unique_ptr<FILE, decltype(&fclose)>(input, fclose);
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_cfile(&zip, input, 0, 0))
        throw std::runtime_error("Invalid ZIP archive");
    struct ZipCloser {
        mz_zip_archive* value;
        ~ZipCloser()
        {
            mz_zip_reader_end(value);
        }
    } closeZip{&zip};
    const auto count = mz_zip_reader_get_num_files(&zip);
    if (!count || count > 4096)
        throw std::runtime_error("Invalid ZIP file count");
    Files files;
    uint64_t total = 0;
    const auto prefix = toUtf8(root) + "/";
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, i, &stat) || stat.m_is_encrypted || stat.m_is_directory ||
            stat.m_uncomp_size > 128 * 1024 * 1024 || ((stat.m_external_attr >> 16) & 0170000) == 0120000)
            throw std::runtime_error("Unsupported ZIP entry");
        const auto length = mz_zip_reader_get_filename(&zip, i, nullptr, 0);
        if (!length || length > 2048)
            throw std::runtime_error("Invalid ZIP filename");
        std::string name(length, '\0');
        if (mz_zip_reader_get_filename(&zip, i, name.data(), length) != length || name.back() != '\0')
            throw std::runtime_error("Invalid ZIP filename");
        name.pop_back();
        if (name.find('\0') != std::string::npos || !name.starts_with(prefix))
            throw std::runtime_error("Unexpected ZIP root");
        const auto relative = relativePath(name.substr(prefix.size()));
        if (!files.insert(folded(relative.generic_wstring())).second ||
            (total += stat.m_uncomp_size) > 512 * 1024 * 1024)
            throw std::runtime_error("Duplicate ZIP path or excessive uncompressed size");
        std::vector<unsigned char> content(static_cast<size_t>(stat.m_uncomp_size));
        if (!mz_zip_reader_extract_to_mem(&zip, i, content.data(), content.size(), 0))
            throw std::runtime_error("ZIP decompression or CRC verification failed");
        const auto output = destination / relative;
        fs::create_directories(output.parent_path());
        std::ofstream stream(output, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(content.data()),
                     static_cast<std::streamsize>(content.size()));
        stream.close();
        if (!stream)
            throw std::runtime_error("Cannot extract package; check disk space");
    }
    if (inventory(destination / inventoryName) != files)
        throw std::runtime_error("Package inventory does not match ZIP contents");
    return files;
}

void removeTransaction(const fs::path& transaction)
{
    noReparsePoints(transaction);
    // Do not follow a junction introduced into a temporary tree.
    for (const auto& entry : fs::recursive_directory_iterator(transaction))
        noReparsePoints(entry.path());
    fs::remove_all(transaction);
}

void rollback(const fs::path& target)
{
    const auto transaction = target / transactionName;
    if (!fs::exists(transaction))
        return;
    noReparsePoints(transaction);
    if (fs::exists(transaction / L"committed") || !fs::exists(transaction / L"journal-ready")) {
        removeTransaction(transaction);
        return;
    }
    const auto oldFiles = inventory(transaction / L"old.txt");
    const auto newFiles = inventory(transaction / L"new.txt");
    // Files may have been installed only after *all* old files reached backup.
    if (fs::exists(transaction / L"backup-complete")) {
        for (const auto& name : newFiles) {
            noReparsePoints(target / name);
            if (fs::exists(target / name) && !fs::remove(target / name))
                throw std::runtime_error("Cannot remove new file during rollback; backup retained");
        }
    }
    for (const auto& name : oldFiles) {
        const auto backup = transaction / L"backup" / name;
        noReparsePoints(backup);
        noReparsePoints(target / name);
        if (fs::exists(backup)) {
            fs::create_directories((target / name).parent_path());
            fs::copy_file(backup, target / name, fs::copy_options::overwrite_existing);
        }
    }
    write(transaction / L"committed", "rolled back\n");
    try {
        removeTransaction(transaction);
    } catch (...) {
    } // Recovery may run from recover.exe itself.
}

void waitForParent(DWORD pid, const fs::path& executable)
{
    Handle process(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!process) {
        if (GetLastError() == ERROR_INVALID_PARAMETER)
            return; // The parent already exited after launching this helper.
        throw std::runtime_error("Cannot wait for search to exit");
    }
    std::array<wchar_t, 32768> name{};
    DWORD size = static_cast<DWORD>(name.size());
    if (!QueryFullProcessImageNameW(process.get(), 0, name.data(), &size) ||
        folded(fs::path(name.data()).lexically_normal().wstring()) !=
            folded(executable.lexically_normal().wstring()))
        throw std::runtime_error("Update parent does not match the installed application");
    if (WaitForSingleObject(process.get(), 30000) != WAIT_OBJECT_0)
        throw std::runtime_error("search did not exit; no files were replaced");
}

struct Process {
    Handle process;
    Handle thread;
};

Process launch(const fs::path& target, const fs::path& ready = {})
{
    const auto exe = target / L"search.exe";
    auto command = L"\"" + exe.wstring() + L"\" --background";
    if (!ready.empty())
        command += L" --update-ready \"" + ready.wstring() + L"\"";
    STARTUPINFOW startup{sizeof(STARTUPINFOW)};
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, target.c_str(),
                        &startup, &info))
        throw std::runtime_error("Cannot launch search after update");
    return {Handle(info.hProcess), Handle(info.hThread)};
}

Process confirmStartup(const fs::path& target, const fs::path& ready)
{
    auto child = launch(target, ready);
    try {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (std::chrono::steady_clock::now() < deadline) {
            if (WaitForSingleObject(child.process.get(), 0) == WAIT_OBJECT_0)
                throw std::runtime_error("The new version exited before confirming startup");
            if (fs::exists(ready) && read(ready, 128) == "search-update-ready\n")
                return child;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        throw std::runtime_error("The new version did not confirm startup; restoring previous version");
    } catch (...) {
        TerminateProcess(child.process.get(), 1); // Only the child created by this update attempt.
        WaitForSingleObject(child.process.get(), 5000);
        throw;
    }
}

void apply(DWORD pid, const fs::path& target, const fs::path& archive, const std::wstring& checksum,
           const std::wstring& root)
{
    noReparsePoints(target);
    noReparsePoints(archive);
    if (!fs::is_directory(target) || !fs::is_regular_file(target / L"search.exe"))
        throw std::runtime_error("Invalid installation directory");
    Handle lock(CreateFileW((target / L".search-update.lock").c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr));
    if (lock.get() == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Another update is running or the program directory is not writable");
    verifyHash(archive, checksum);
    waitForParent(pid, target / L"search.exe");
    rollback(target); // Recover an interrupted transaction before beginning another.
    const auto oldFiles = inventory(target / inventoryName);
    const auto transaction = target / transactionName;
    fs::create_directory(transaction);
    bool modifying = false;
    std::optional<Process> started;
    try {
        const auto newFiles = extract(archive, transaction / L"new", root);
        for (const auto& name : oldFiles) {
            noReparsePoints(target / name);
            if (fs::exists(target / name) && !fs::is_regular_file(target / name))
                throw std::runtime_error("An installed file was replaced by a directory");
        }
        for (const auto& name : newFiles) {
            noReparsePoints(target / name);
            if (fs::exists(target / name) && !oldFiles.contains(name))
                throw std::runtime_error(
                    "An unrelated file conflicts with the new package; it was preserved");
        }
        std::array<wchar_t, 32768> self{};
        GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size()));
        fs::copy_file(self.data(), transaction / L"recover.exe");
        write(transaction / L"old.txt", read(target / inventoryName));
        write(transaction / L"new.txt", read(transaction / L"new" / inventoryName));
        write(transaction / L"journal-ready", "ready\n");
        modifying = true;
        for (const auto& name : oldFiles) {
            if (fs::exists(target / name)) {
                const auto backup = transaction / L"backup" / name;
                fs::create_directories(backup.parent_path());
                fs::rename(target / name, backup);
            }
        }
        write(transaction / L"backup-complete", "ready\n");
        for (const auto& name : newFiles) {
            fs::create_directories((target / name).parent_path());
            fs::rename(transaction / L"new" / name, target / name);
        }
        started = confirmStartup(target, transaction / L"started");
        write(transaction / L"committed", "updated\n");
    } catch (...) {
        if (started) {
            TerminateProcess(started->process.get(), 1);
            WaitForSingleObject(started->process.get(), 5000);
        }
        if (modifying) {
            rollback(target);
        } else {
            removeTransaction(transaction);
        }
        throw;
    }
    // Successful startup is the commit point. Cleanup failure must never undo it.
    try {
        removeTransaction(transaction);
    } catch (...) {
    }
    // The externally running helper cannot remove itself. The app cleans its cache later.
    try {
        write(archive.parent_path() / L"complete", "updated\n");
    } catch (...) {
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int count = 0;
    auto** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments)
        return 1;
    std::vector<std::wstring> args(arguments, arguments + count);
    LocalFree(arguments);
    bool quiet = args.size() > 1 && args.back() == L"--quiet";
    if (quiet)
        args.pop_back();
    fs::path logDirectory;
    fs::path restartTarget;
    try {
        if (args.size() == 3 && args[1] == L"--recover") {
            const auto target = fs::absolute(args[2]).lexically_normal();
            noReparsePoints(target);
            rollback(target);
            launch(target);
        } else if (args.size() == 7 && args[1] == L"--apply") {
            const auto target = fs::absolute(args[3]).lexically_normal();
            const auto archive = fs::absolute(args[4]).lexically_normal();
            logDirectory = archive.parent_path();
            restartTarget = target;
            size_t consumed = 0;
            const auto number = std::stoul(args[2], &consumed);
            if (consumed != args[2].size() || !number || number > MAXDWORD)
                throw std::runtime_error("Invalid parent process ID");
            apply(static_cast<DWORD>(number), target, archive, args[5], args[6]);
        } else {
            throw std::runtime_error(
                "Usage: search-updater --apply PID DIRECTORY ZIP SHA256 ROOT | --recover DIRECTORY");
        }
        return 0;
    } catch (const std::exception& error) {
        if (!restartTarget.empty()) {
            const auto transaction = restartTarget / transactionName;
            if (!fs::exists(transaction) || fs::exists(transaction / L"committed") ||
                !fs::exists(transaction / L"journal-ready")) {
                try {
                    launch(restartTarget);
                } catch (...) {
                }
            }
        }
        if (!logDirectory.empty()) {
            try {
                write(logDirectory / L"error.log", std::string(error.what()) + "\n");
            } catch (...) {
            }
        }
        if (!quiet) {
            const auto message = L"更新未能完成。已保留可恢复的文件；若存在 .search-update "
                                 L"目录，可运行其中的 recover.exe --recover \"程序目录\" 恢复。\n\n" +
                                 fromUtf8(error.what());
            MessageBoxW(nullptr, message.c_str(), L"search 更新", MB_OK | MB_ICONERROR);
        }
        return 1;
    }
}
