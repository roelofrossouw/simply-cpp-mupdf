// Reads a PDF with MuPDF: its pages, each page's blocks of text and their lines with positions and
// fonts, and a page drawn as a PNG. Each line shows a call, as written, and what it returned. With
// no FILE it reads a small PDF made on the spot; --help lists the options.

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <params.h>
#include <sc.h>

#include <mupdf.h>

#include "sample_pdf.h"

int main(int argc, char **argv) {
    // sc::params brings -h/--help, and reports mistakes and exits.
    const sc::params args{argc, argv, {
        {"name", "sc-mupdf-demo"},
        {"description", "Reads a PDF (or XPS, EPUB, CBZ or an image) with MuPDF and walks through it: its pages, the blocks "
                        "and lines on them, a page's text, and the page drawn as a PNG. Without FILE it makes a small PDF on "
                        "the spot and reads that.\nMore on the wiki: https://github.com/roelofrossouw/simply-cpp/wiki/MuPDF"},
        {"options", {
            {"password", {{"short", "p"}, {"value", "PASSWORD"}, {"help", "the password of an encrypted FILE"}}},
            {"page", {{"type", "integer"}, {"default", 1}, {"value", "N"}, {"help", "the page whose text and image are shown"}}},
            {"scale", {{"type", "number"}, {"default", 2}, {"help", "pixels per point for the page image (2 is 144 dpi)"}}},
            {"png", {{"value", "FILE"}, {"default", (std::filesystem::temp_directory_path() / "sc-mupdf-demo.png").string()},
                     {"help", "where to write the page image"}}}}},
        {"positional", {
            {"file", {{"required", false}, {"help", "the document to read"}}}}}}};

    try {
        const auto password = args.as<std::string>("password", "");
        const int page_number = args.as<int>("page");
        const double scale = args.as<double>("scale");
        const std::filesystem::path png_file = args.as<std::string>("png");
        sc::console::title("simply-cpp mupdf: pages, text blocks, lines and page images");
        sc::timer sw;

        // [readme]
        sc::console::heading("Opening the document");
        const auto *file = args.lookup("file");
        const auto doc = file ? sc::mupdf::document{file->get<std::string>(), password}
                              : sc::mupdf::document::from_memory(sample_pdf::make());
        sc::console::step(file ? "sc::mupdf::document doc{\"" + file->get<std::string>() + "\"};"
                               : std::string{"auto doc = sc::mupdf::document::from_memory(pdf_bytes);   // the sample PDF"});
        SC_SHOW(doc.page_count());
        if (page_number < 1 || page_number > doc.page_count()) {
            throw std::out_of_range{"--page " + std::to_string(page_number) + ": the document has " + std::to_string(doc.page_count()) + " pages"};
        }
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
        const auto page = doc.page(page_number - 1); // 0-based; --page counts from 1
        SC_SHOW(page.text());

        sc::console::heading("A page drawn as a PNG");
        const auto png = page.png(scale); // pixels per point: 2 is 144 dpi
        SC_SHOW(png.size());
        std::ofstream{png_file, std::ios::binary} << png;
        // [/readme]

        sc::console::output() << "\nThe page image is " << png_file.string() << "\nAll of that took " << sw << ".\n";
    } catch (const std::exception &error) {
        std::cerr << "sc-mupdf-demo: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
