#pragma once

// A small PDF, made on the spot so the demo and the tests need no file: two A4 pages of text in the
// standard PDF fonts (Helvetica, -Bold, -Oblique), which every PDF reader has built in, plus a title
// and author.

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace sample_pdf {
    // A line of text: its font (F1 regular, F2 bold, F3 italic), size, and where its baseline starts,
    // in PDF coordinates (points from the bottom-left corner).
    struct text_line {
        const char *font;
        double size;
        double x;
        double y;
        const char *text; // no parentheses or backslashes: they'd need escaping
    };

    constexpr double page_width = 595;  // A4
    constexpr double page_height = 842;

    inline const std::vector<std::vector<text_line>> &pages() {
        static const std::vector<std::vector<text_line>> pages{
            {
                {"F2", 24, 72, 770, "simply-cpp mupdf"},
                {"F1", 12, 72, 730, "Reading a PDF with MuPDF: pages, blocks and lines,"},
                {"F1", 12, 72, 715, "each with its position and font."},
                {"F3", 12, 72, 680, "An italic line, in a block of its own."},
            },
            {
                {"F2", 18, 72, 770, "The second page"},
                {"F1", 12, 72, 740, "Page images come from the same document."},
            },
        };
        return pages;
    }

    inline std::string make() {
        std::vector<std::string> objects{
            "<< /Type /Catalog /Pages 2 0 R >>",
            "", // the page tree, once the pages are numbered
            "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
            "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>",
            "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Oblique >>",
            "<< /Title (simply-cpp mupdf sample) /Author (simply-cpp) >>",
        };
        std::string kids;
        for (const auto &lines: pages()) {
            std::string content;
            char text[256];
            for (const auto &line: lines) {
                std::snprintf(text, sizeof text, "BT /%s %g Tf %g %g Td (%s) Tj ET\n", line.font, line.size, line.x, line.y, line.text);
                content += text;
            }
            const auto page_object = objects.size() + 1;
            std::snprintf(text, sizeof text,
                          "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %g %g] /Contents %zu 0 R"
                          " /Resources << /Font << /F1 3 0 R /F2 4 0 R /F3 5 0 R >> >> >>",
                          page_width, page_height, page_object + 1);
            objects.emplace_back(text);
            objects.push_back("<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "endstream");
            kids += std::to_string(page_object) + " 0 R ";
        }
        objects[1] = "<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(pages().size()) + " >>";

        std::string pdf = "%PDF-1.4\n";
        std::vector<std::size_t> offsets;
        for (std::size_t i = 0; i < objects.size(); ++i) {
            offsets.push_back(pdf.size());
            pdf += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
        }
        const auto xref = pdf.size();
        pdf += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
        for (const auto offset: offsets) {
            char entry[24];
            std::snprintf(entry, sizeof entry, "%010zu 00000 n \n", offset);
            pdf += entry;
        }
        pdf += "trailer\n<< /Size " + std::to_string(objects.size() + 1) + " /Root 1 0 R /Info 6 0 R >>\nstartxref\n" +
                std::to_string(xref) + "\n%%EOF\n";
        return pdf;
    }
}
