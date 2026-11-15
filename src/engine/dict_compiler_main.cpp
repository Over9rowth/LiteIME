#include "engine/binary_dict.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 3) {
        std::wcerr << L"Usage: dict_compiler <output.dict> [--cn <file.yaml|tsv>]... [--en <file.yaml|tsv>]...\n";
        return 1;
    }

    fs::path outPath = argv[1];
    std::vector<liteime::engine::RawPhraseRecord> phrases;
    std::vector<liteime::engine::RawEnglishRecord> english;

    for (int i = 2; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--cn" && i + 1 < argc) {
            fs::path p = argv[++i];
            std::wcout << L"[CN] Parsing " << p.wstring() << L"...\n";
            if (!liteime::engine::BinaryDict::parseDictYaml(p, phrases, english, false)) {
                std::wcerr << L"Failed to parse CN dict: " << p.wstring() << L"\n";
                return 2;
            }
        } else if (arg == L"--en" && i + 1 < argc) {
            fs::path p = argv[++i];
            std::wcout << L"[EN] Parsing " << p.wstring() << L"...\n";
            if (!liteime::engine::BinaryDict::parseDictYaml(p, phrases, english, true)) {
                std::wcerr << L"Failed to parse EN dict: " << p.wstring() << L"\n";
                return 2;
            }
        }
    }

    std::cout << "Compiling " << phrases.size() << " CN entries and " << english.size() << " EN entries...\n";
    if (!liteime::engine::BinaryDict::compileToFile(outPath, std::move(phrases), std::move(english))) {
        std::cerr << "Failed to write output dictionary.\n";
        return 3;
    }

    liteime::engine::BinaryDict verify;
    if (!verify.loadFile(outPath)) {
        std::cerr << "Verification load failed.\n";
        return 4;
    }
    std::cout << "Compiled successfully! Phrases=" << verify.phraseCount()
              << ", Jianpin=" << verify.jianpinCount()
              << ", English=" << verify.englishCount() << "\n";
    return 0;
}
