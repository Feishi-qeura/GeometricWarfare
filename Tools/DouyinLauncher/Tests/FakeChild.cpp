#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>
#include <string>
#include <vector>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int count = 0;
    LPWSTR* raw = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!raw) return 91;
    std::vector<std::wstring> args;
    for (int index = 1; index < count; ++index) args.emplace_back(raw[index]);
    LocalFree(raw);
    bool credential = false, local = false;
    std::vector<std::wstring> ordinary;
    for (const auto& arg : args) {
        if (_wcsicmp(arg.c_str(), L"-GWCredentialStdin") == 0) credential = true;
        else if (_wcsicmp(arg.c_str(), L"-GWLocalTest") == 0) local = true;
        else if (_wcsnicmp(arg.c_str(), L"-token", 6) == 0 || _wcsnicmp(arg.c_str(), L"--token", 7) == 0) return 92;
        else ordinary.push_back(arg);
    }
    std::string input;
    char buffer[1024]; DWORD bytes = 0;
    while (ReadFile(GetStdHandle(STD_INPUT_HANDLE), buffer, sizeof(buffer), &bytes, nullptr) && bytes) input.append(buffer, bytes);
    const std::vector<std::wstring> expected = { L"-testcase=credential", L"a b", LR"(quote"and\)", LR"(trail \\)", L"", L"中文 参数" };
    if (!ordinary.empty() && ordinary[0] == L"-testcase=credential") {
        const std::string expectedToken = "假 token \"\\尾\n";
        if (!credential || local || input != expectedToken || ordinary != expected) return 93;
    } else if (ordinary == std::vector<std::wstring>{ L"-testcase=boundary" }) {
        if (!credential || local || input.size() != 16 * 1024 || input.back() != '\n' ||
            input.substr(0, input.size() - 1) != std::string(16 * 1024 - 1, 'x')) return 93;
    } else if (ordinary == std::vector<std::wstring>{ L"-testcase=local" }) {
        if (credential || !local || !input.empty()) return 94;
    } else return 95;
    static const char success[] = "PASS fake child exact args/stdin\n";
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), success, sizeof(success) - 1, &bytes, nullptr);
    return 37;
}
