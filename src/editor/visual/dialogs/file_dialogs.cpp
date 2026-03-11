#include "file_dialogs.h"
#include <nfd.h>

namespace an24 {
namespace dialogs {

std::optional<std::string> openBlueprint() {
    nfdu8filteritem_t filterItem = {"Blueprint", "blueprint"};
    nfdchar_t* outPath = nullptr;
    nfdresult_t result = NFD_OpenDialog(&outPath, &filterItem, 1, nullptr);
    
    if (result == NFD_OKAY) {
        std::string path(outPath);
        NFD_FreePath(outPath);
        return path;
    }
    return std::nullopt;
}

std::optional<std::string> saveBlueprint(const std::string& defaultName) {
    nfdu8filteritem_t filterItem = {"Blueprint", "blueprint"};
    nfdchar_t* outPath = nullptr;
    nfdresult_t result = NFD_SaveDialog(&outPath, &filterItem, 1, nullptr, defaultName.c_str());
    
    if (result == NFD_OKAY) {
        std::string path(outPath);
        NFD_FreePath(outPath);
        return path;
    }
    return std::nullopt;
}

} // namespace dialogs
} // namespace an24
