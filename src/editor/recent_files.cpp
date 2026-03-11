#include "recent_files.h"
#include <cstdio>
#include <fstream>

void RecentFiles::loadFrom(const std::string& filepath) {
    files_.clear();
    
    if (!std::filesystem::exists(filepath)) return;

    FILE* f = fopen(filepath.c_str(), "r");
    if (!f) return;

    char line[2048];
    while (fgets(line, sizeof(line), f) && files_.size() < MAX) {
        std::string p = line;
        while (!p.empty() && (p.back() == '\n' || p.back() == '\r')) {
            p.pop_back();
        }
        if (!p.empty() && std::filesystem::exists(p)) {
            files_.push_back(p);
        }
    }
    fclose(f);
}

void RecentFiles::saveTo(const std::string& filepath) const {
    FILE* f = fopen(filepath.c_str(), "w");
    if (!f) return;

    for (const auto& p : files_) {
        fprintf(f, "%s\n", p.c_str());
    }
    fclose(f);
}
