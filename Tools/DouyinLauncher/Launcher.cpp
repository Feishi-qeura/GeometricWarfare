#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>
#include <algorithm>
#include <string>
#include <vector>

namespace {
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    void Close() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); value = nullptr; }
};
bool Equal(const std::wstring& left, const wchar_t* right) { return _wcsicmp(left.c_str(), right) == 0; }
std::wstring Quote(const std::wstring& arg)
{
    // CommandLineToArgvW / Microsoft CRT quoting: double backslashes before
    // quotes and the closing quote, including empty arguments.
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t ch : arg) {
        if (ch == L'\\') { ++slashes; continue; }
        if (ch == L'\"') out.append(slashes * 2 + 1, L'\\');
        else out.append(slashes, L'\\');
        slashes = 0;
        out.push_back(ch);
    }
    out.append(slashes * 2, L'\\');
    out.push_back(L'\"');
    return out;
}
bool InheritOutput(DWORD id, Handle& destination)
{
    HANDLE source = GetStdHandle(id);
    if (source && source != INVALID_HANDLE_VALUE && DuplicateHandle(GetCurrentProcess(), source,
        GetCurrentProcess(), &destination.value, 0, TRUE, DUPLICATE_SAME_ACCESS)) return true;
    SECURITY_ATTRIBUTES security{ sizeof(security), nullptr, TRUE };
    destination.value = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    return destination.value != INVALID_HANDLE_VALUE;
}
int Launch()
{
    int count = 0;
    LPWSTR* raw = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!raw) return 2;
    std::vector<std::wstring> forwarded;
    std::wstring token;
    bool foundToken = false;
    for (int index = 1; index < count; ++index) {
        std::wstring arg = raw[index];
        const auto equals = arg.find(L'=');
        const auto key = arg.substr(0, equals);
        if (Equal(key, L"-token") || Equal(key, L"--token")) {
            if (foundToken) { LocalFree(raw); return 2; }
            foundToken = true;
            if (equals != std::wstring::npos) token = arg.substr(equals + 1);
            else if (index + 1 < count) token = raw[++index];
            else { LocalFree(raw); return 2; }
            continue;
        }
        // This internal flag is generated only by this launcher.
        if (Equal(key, L"-GWCredentialStdin")) continue;
        forwarded.push_back(std::move(arg));
    }
    LocalFree(raw);
    std::vector<char> credential;
    if (foundToken) {
        if (token.empty() || token.find_first_of(L"\r\n") != std::wstring::npos) return 2;
        const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, token.data(),
            static_cast<int>(token.size()), nullptr, 0, nullptr, nullptr);
        if (size <= 0 || size + 1 > 16 * 1024) return 2;
        credential.resize(static_cast<size_t>(size) + 1);
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, token.data(), static_cast<int>(token.size()),
            credential.data(), size, nullptr, nullptr)) return 2;
        credential.back() = '\n';
        SecureZeroMemory(token.data(), token.size() * sizeof(wchar_t));
        token.clear();
        forwarded.erase(std::remove_if(forwarded.begin(), forwarded.end(), [](const auto& arg) {
            return Equal(arg.substr(0, arg.find(L'=')), L"-GWLocalTest"); }), forwarded.end());
        forwarded.emplace_back(L"-GWCredentialStdin");
    }
    std::vector<wchar_t> module(32768);
    const DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (!length || length >= module.size()) return 3;
    const std::wstring full(module.data(), length);
    const auto separator = full.find_last_of(L"\\/");
    if (separator == std::wstring::npos) return 3;
    const auto root = full.substr(0, separator);
    auto executable = root + L"\\GeometricWarfare\\Binaries\\Win64\\GeometricWarfare-Win64-Shipping.exe";
    if (GetFileAttributesW(executable.c_str()) == INVALID_FILE_ATTRIBUTES)
        executable = root + L"\\GeometricWarfare\\Binaries\\Win64\\GeometricWarfare.exe";
    if (GetFileAttributesW(executable.c_str()) == INVALID_FILE_ATTRIBUTES) return 3;
    std::wstring command = Quote(executable);
    for (const auto& arg : forwarded) { command.push_back(L' '); command += Quote(arg); }
    if (command.size() + 1 >= 32767) return 2;

    SECURITY_ATTRIBUTES security{ sizeof(security), nullptr, TRUE };
    Handle childInput, parentWrite, childOutput, childError;
    if (!CreatePipe(&childInput.value, &parentWrite.value, &security, 64 * 1024) ||
        !SetHandleInformation(parentWrite.value, HANDLE_FLAG_INHERIT, 0) ||
        !InheritOutput(STD_OUTPUT_HANDLE, childOutput) || !InheritOutput(STD_ERROR_HANDLE, childError)) return 4;
    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    auto* attributes = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(), 0, attributeBytes));
    if (!attributes) return 4;
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attributeBytes)) { HeapFree(GetProcessHeap(), 0, attributes); return 4; }
    HANDLE inherited[] = { childInput.value, childOutput.value, childError.value };
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        inherited, sizeof(inherited), nullptr, nullptr)) {
        DeleteProcThreadAttributeList(attributes); HeapFree(GetProcessHeap(), 0, attributes); return 4;
    }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = childInput.value;
    startup.StartupInfo.hStdOutput = childOutput.value;
    startup.StartupInfo.hStdError = childError.value;
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION process{};
    const bool created = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
        nullptr, root.c_str(), &startup.StartupInfo, &process) != FALSE;
    DeleteProcThreadAttributeList(attributes);
    HeapFree(GetProcessHeap(), 0, attributes);
    if (!created) return 5;
    Handle childProcess, childThread;
    childProcess.value = process.hProcess;
    childThread.value = process.hThread;
    childInput.Close(); childOutput.Close(); childError.Close();
    bool written = true;
    if (!credential.empty()) {
        DWORD bytes = 0;
        written = WriteFile(parentWrite.value, credential.data(), static_cast<DWORD>(credential.size()), &bytes, nullptr)
            && bytes == credential.size();
        SecureZeroMemory(credential.data(), credential.size());
    }
    parentWrite.Close();
    if (!written || ResumeThread(childThread.value) == static_cast<DWORD>(-1)) {
        TerminateProcess(childProcess.value, 6); WaitForSingleObject(childProcess.value, 1000); return 6;
    }
    childThread.Close();
    if (WaitForSingleObject(childProcess.value, INFINITE) != WAIT_OBJECT_0) {
        TerminateProcess(childProcess.value, 7); return 7;
    }
    DWORD exitCode = 7;
    if (!GetExitCodeProcess(childProcess.value, &exitCode)) return 7;
    return static_cast<int>(exitCode);
}
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    // Deliberately no stdout/stderr/MessageBox/file logging in the credential entry.
    try { return Launch(); } catch (...) { return 2; }
}
