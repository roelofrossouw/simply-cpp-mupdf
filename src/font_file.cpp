#include "font_file.h"

#include <fontconfig/fontconfig.h>

#include <mutex>

namespace sc::mupdf::detail {
    namespace {
        // CSS weights (100..900) to fontconfig's scale.
        int fc_weight(const int weight) {
            if (weight <= 100) return FC_WEIGHT_THIN;
            if (weight <= 200) return FC_WEIGHT_EXTRALIGHT;
            if (weight <= 300) return FC_WEIGHT_LIGHT;
            if (weight <= 350) return FC_WEIGHT_BOOK;
            if (weight <= 400) return FC_WEIGHT_REGULAR;
            if (weight <= 500) return FC_WEIGHT_MEDIUM;
            if (weight <= 600) return FC_WEIGHT_DEMIBOLD;
            if (weight <= 700) return FC_WEIGHT_BOLD;
            if (weight <= 800) return FC_WEIGHT_EXTRABOLD;
            return FC_WEIGHT_BLACK;
        }

        const FcChar8 *fc_text(const std::string &text) { return reinterpret_cast<const FcChar8 *>(text.c_str()); }
    }

    std::string font_file(const font &spec) {
        // fontconfig's default configuration is loaded once and kept for the whole process: FcFini()
        // while another writer still looks fonts up would pull it away underneath.
        static std::once_flag initialised;
        static bool available = false;
        std::call_once(initialised, [] { available = FcInit() == FcTrue; });
        if (!available) return {};

        FcPattern *pattern = FcPatternCreate();
        FcPatternAddString(pattern, FC_FAMILY, fc_text(spec.family));
        FcPatternAddInteger(pattern, FC_WEIGHT, fc_weight(spec.weight));
        FcPatternAddInteger(pattern, FC_SLANT, spec.italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
        if (!spec.style.empty()) FcPatternAddString(pattern, FC_STYLE, fc_text(spec.style));
        FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
        FcDefaultSubstitute(pattern);

        std::string file;
        FcResult result;
        if (FcPattern *font = FcFontMatch(nullptr, pattern, &result)) {
            FcChar8 *path = nullptr;
            if (FcPatternGetString(font, FC_FILE, 0, &path) == FcResultMatch) file = reinterpret_cast<const char *>(path);
            FcPatternDestroy(font);
        }
        FcPatternDestroy(pattern);
        return file;
    }
}
