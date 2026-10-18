// libstdc++ on NtCRT: streams, wide conversions, file names, formatting, exceptions and threads in the "C" and
// "C.UTF-8" locales. libstdc++ of MinGW-w64 uses the generic locale model, its codecvt converts by the C library.
#include <chrono>
#include <clocale>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <dirent.h>
#include <errno.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <io.h>
#include <iostream>
#include <locale>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

namespace fs = std::filesystem;

static int g_Failures;

static void check(bool Ok, const char* What, const std::string& Detail = {})
{
    if (!Ok)
    {
        g_Failures++;
        std::printf("FAIL %s %s\n", What, Detail.c_str());
    } else
    {
        std::printf("ok   %s\n", What);
    }
    std::fflush(stdout);
}

static std::string hex(const std::string& s)
{
    std::string r;
    char b[4];

    for (unsigned char c : s)
    {
        std::snprintf(b, sizeof(b), "%02x", c);
        r += b;
    }
    return r;
}

static std::string read_file(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s;

    s << f.rdbuf();
    return s.str();
}

// The UTF-8 of 中😀é
static const char U8[] = "\xe4\xb8\xad\xf0\x9f\x98\x80\xc3\xa9";
static const wchar_t W[] = L"\x4e2d\xd83d\xde00\xe9";

static void test_codecvt()
{
    // std::codecvt<wchar_t, char, mbstate_t> of the classic locale converts by mbsrtowcs and wcsrtombs
    struct Codecvt : std::codecvt<wchar_t, char, std::mbstate_t>
    {
    };
    std::wstring_convert<Codecvt> Convert;
    bool Ok;

    try
    {
        Ok = Convert.to_bytes(W) == U8 && Convert.from_bytes(U8) == W;
    } catch (const std::range_error&)
    {
        Ok = false;
    }
    check(Ok, "codecvt<wchar_t, char> converts UTF-8");

    bool Thrown = false;
    try
    {
        Convert.from_bytes("a\x80z");
    } catch (const std::range_error&)
    {
        Thrown = true;
    }
    check(Thrown, "codecvt<wchar_t, char> rejects invalid UTF-8");
}

static void test_wfstream(const fs::path& Dir)
{
    fs::path p = Dir / "w.txt";
    {
        std::wofstream f(p);
        f << W << L'\n';
    }
    check(read_file(p) == std::string(U8) + "\r\n", "wofstream writes UTF-8", hex(read_file(p)));

    std::wifstream f(p);
    std::wstring Line;
    std::getline(f, Line);
    check(Line == W, "wifstream reads UTF-8");
}

static void test_wcout(const fs::path& Dir)
{
    // stdout redirected to a file, std::wcout writes by putwc, which keeps a surrogate pair between calls
    fs::path p = Dir / "wcout.txt";
    std::fflush(stdout);
    int Saved = _dup(1);
    FILE* f = _wfopen(p.c_str(), L"w");
    _dup2(_fileno(f), 1);
    std::wcout << W << L'|' << 42 << std::endl;
    std::cout << U8 << '|' << 3.5 << std::endl;
    std::printf("%ls|%s\n", W, U8);
    std::fflush(stdout);
    _dup2(Saved, 1);
    _close(Saved);
    std::fclose(f);
    std::string Expect = std::string(U8) + "|42\r\n" + U8 + "|3.5\r\n" + U8 + "|" + U8 + "\r\n";
    check(read_file(p) == Expect, "wcout, cout and printf to a file", hex(read_file(p)));
}

static void test_wcin(const fs::path& Dir)
{
    fs::path p = Dir / "wcin.txt";
    {
        std::ofstream f(p, std::ios::binary);
        f << U8 << " 42\n";
    }
    FILE* f = _wfreopen(p.c_str(), L"r", stdin);
    std::wstring Word;
    int n = 0;
    std::wcin >> Word >> n;
    check(f != nullptr && Word == W && n == 42, "wcin reads UTF-8");
    std::wcin.clear();
    _wfreopen(L"NUL", L"r", stdin);
}

static void test_paths(const fs::path& Dir)
{
    // A narrow file name is UTF-8 in "C.UTF-8", for fopen and for std::filesystem::path
    std::string Name = (Dir / "n_").string() + U8 + ".txt";
    {
        std::ofstream f(Name);
        f << "x";
    }
    std::wstring Wide = Dir.wstring() + L"\\n_" + W + L".txt";
    check(GetFileAttributesW(Wide.c_str()) != INVALID_FILE_ATTRIBUTES, "ofstream opens a UTF-8 name");
    check(fs::exists(fs::path(Name)), "filesystem::path from a UTF-8 string");
    check(fs::path(reinterpret_cast<const char8_t*>(U8)).wstring() == W, "filesystem::path from char8_t");

    bool Found = false;
    for (const auto& e : fs::directory_iterator(Dir))
        if (e.path().filename().string() == std::string("n_") + U8 + ".txt")
            Found = true;
    check(Found, "directory_iterator names convert to UTF-8");

    std::FILE* f = std::fopen(Name.c_str(), "r");
    check(f != nullptr, "fopen of the same name");
    if (f)
        std::fclose(f);
    fs::remove(fs::path(Wide));
    check(!fs::exists(fs::path(Wide)), "filesystem::remove");
}

static void test_format()
{
    char Buffer[64];

    check(std::to_string(3.25) == "3.250000", "to_string(double)", std::to_string(3.25));
    check(std::format("{}|{:>5}|{:.3f}", 42, "ab", 2.5) == "42|   ab|2.500", "std::format");
    check(std::snprintf(Buffer, sizeof(Buffer), "%.4ls", W) == 3 && std::string(Buffer) == "\xe4\xb8\xad",
          "snprintf %.4ls counts bytes");
    std::ostringstream s;
    s << 1.5 << ' ' << std::hex << 255 << ' ' << true;
    check(s.str() == "1.5 ff 1", "ostringstream", s.str());
}

static void test_locale_names()
{
    // The generic model of libstdc++ knows "C" and "POSIX" only, the C library locale is set by setlocale
    bool Named = true;
    try
    {
        std::locale l("C.UTF-8");
    } catch (const std::runtime_error&)
    {
        Named = false;
    }
    std::printf("info std::locale(\"C.UTF-8\") %s\n", Named ? "constructed" : "throws");
    check(std::locale::classic().name() == "C", "std::locale::classic");
    std::locale::global(std::locale::classic());
    check(std::strcmp(std::setlocale(LC_ALL, nullptr), "C") == 0, "locale::global(classic) sets the C locale");
    std::setlocale(LC_ALL, "C.UTF-8");
}

// readdir of libmingwex ends the directory with errno 0 when _findnext leaves ERROR_NO_MORE_FILES
static void test_readdir(const fs::path& Dir)
{
    _WDIR* d = _wopendir(Dir.c_str());
    int Count = 0;

    errno = 0;
    while (d != nullptr && _wreaddir(d) != nullptr)
        Count++;
    check(d != nullptr && Count >= 2 && errno == 0, "readdir ends with errno 0", std::to_string(errno));
    if (d)
        _wclosedir(d);
}

static void test_runtime()
{
    bool Caught = false;
    try
    {
        throw std::runtime_error("x");
    } catch (const std::exception& e)
    {
        Caught = std::strcmp(e.what(), "x") == 0;
    }
    check(Caught, "exceptions");

    std::mutex Lock;
    int Count = 0;
    std::vector<std::thread> Threads;
    for (int i = 0; i < 4; i++)
        Threads.emplace_back([&] {
            for (int k = 0; k < 1000; k++)
            {
                std::lock_guard<std::mutex> g(Lock);
                Count++;
            }
        });
    for (auto& t : Threads)
        t.join();
    check(Count == 4000, "std::thread and std::mutex");

    auto Now = std::chrono::system_clock::now().time_since_epoch();
    check(std::chrono::duration_cast<std::chrono::hours>(Now).count() > 480000, "system_clock");
}

int main()
{
    fs::path Dir = fs::temp_directory_path() / ("ntcrt_cxx_" + std::to_string(GetCurrentProcessId()));
    fs::create_directories(Dir);

    if (std::setlocale(LC_ALL, "C.UTF-8") == nullptr)
    {
        std::printf("FAIL setlocale\n");
        return 1;
    }
    test_codecvt();
    test_wfstream(Dir);
    test_wcout(Dir);
    test_wcin(Dir);
    test_paths(Dir);
    test_format();
    test_locale_names();
    test_readdir(Dir);
    test_runtime();
    std::error_code Error;
    fs::remove_all(Dir, Error);
    check(!Error, "filesystem::remove_all", Error.message());
    std::printf("cxx: %d failures\n", g_Failures);
    return g_Failures != 0;
}
