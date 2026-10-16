// Reads a PDF with MuPDF: its pages, each page's blocks of text and their lines with positions and
// fonts, and a page drawn as a PNG. Each line shows a call, as written, and what it returned. With
// no argument it reads a small PDF made on the spot; pass a file name to read that instead.

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <sc.h>

#include <mupdf.h>

#include "sample_pdf.h"

int main(int argc, char **argv) {
    try {
        sc::console::title("simply-cpp mupdf: pages, text blocks, lines and page images");
        sc::timer sw;

        // [readme]
        sc::console::heading("Opening the document");
        const auto doc = argc > 1 ? sc::mupdf::document{argv[1]} : sc::mupdf::document::from_memory(sample_pdf::make());
        sc::console::step(argc > 1 ? std::string{"sc::mupdf::document doc{\""} + argv[1] + "\"};"
                                   : "auto doc = sc::mupdf::document::from_memory(pdf_bytes);   // the sample PDF");
        SC_SHOW(doc.page_count());
        SC_SHOW(doc.metadata("Title"));
        SC_SHOW(doc.metadata("format"));

        sc::console::heading("Pages, blocks and lines: for (const auto page: doc.pages())");
        for (const auto page: doc.pages()) {
            sc::console::subheading("page " + std::to_string(page.number()) + ", " + sc::console::format(page.bounds()));
            for (const auto block: page.blocks()) { // keeps the page's text while the loop runs
                for (const auto line: block) {
                    sc::console::note(sc::console::format(line.bbox()) + "  " + sc::console::format(line.font_size()) + " pt" +
                                      (line.bold() ? " bold" : "") + (line.italic() ? " italic" : "") + "  " + line.text());
                }
            }
        }

        sc::console::heading("All of a page's text");
        const auto first = doc.page(0);
        SC_SHOW(first.text());

        sc::console::heading("A page drawn as a PNG");
        const auto png = first.png(2); // 2 pixels per point: 144 dpi
        SC_SHOW(png.size());
        const auto file = std::filesystem::temp_directory_path() / "sc-mupdf-demo.png";
        std::ofstream{file, std::ios::binary} << png;
        // [/readme]

        sc::console::output() << "\nThe page image is " << file.string() << "\nAll of that took " << sw << ".\n";
    } catch (const std::exception &error) {
        std::cerr << "sc-mupdf-demo: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
