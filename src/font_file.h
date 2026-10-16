#pragma once

#include <font.h>

#include <string>

namespace sc::mupdf::detail {
    // The font file fontconfig picks for spec, "" when none: the same lookup as sc-pdf's, so the two
    // writers embed the same fonts.
    std::string font_file(const font &spec);
}
