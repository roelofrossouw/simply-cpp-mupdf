// Reads the sample PDF (examples/sample_pdf.h) from memory and from a file: pages, blocks, lines,
// their positions and fonts, metadata and page images, plus the mistakes that throw.
#include "mupdf.h"

#include "sample_pdf.h"
#include "sc_test.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
    bool contains(const std::string &text, const std::string &part) { return text.find(part) != std::string::npos; }

    // The line with exactly this text on the page, with its block's line count.
    struct found_line {
        sc::rect bbox;
        double font_size;
        bool bold;
        bool italic;
        std::string font_name;
        std::size_t block_lines;
    };

    std::optional<found_line> find_line(const sc::mupdf::page &page, const std::string &text) {
        for (const auto block: page.blocks()) {
            for (const auto line: block) {
                if (line.text() == text) return found_line{line.bbox(), line.font_size(), line.bold(), line.italic(), line.font_name(), block.line_count()};
            }
        }
        return std::nullopt;
    }

    template<typename F>
    bool throws_error(F &&call) {
        try {
            call();
        } catch (const sc::mupdf::error &) {
            return true;
        }
        return false;
    }
}

int main() {
    const auto document = sc::mupdf::document::from_memory(sample_pdf::make());

    SECTION("The document: pages and metadata");
    CHECK_EQ(document.page_count(), 2);
    CHECK_EQ(document.pages().size(), 2);
    CHECK_EQ(document.metadata("Title"), std::string{"simply-cpp mupdf sample"});
    CHECK_EQ(document.metadata("Author"), std::string{"simply-cpp"});
    CHECK(document.metadata("format").starts_with("PDF"));
    CHECK(document.metadata("Keywords").empty()); // not set

    SECTION("Iterating the pages, more than once and at the same time");
    {
        std::vector<int> numbers;
        for (const auto page: document.pages()) numbers.push_back(page.number());
        CHECK_EQ(numbers, (std::vector<int>{1, 2}));
        std::vector<int> pairs; // nested: each loop keeps its own place
        for (const auto outer: document.pages()) {
            for (const auto inner: document.pages()) pairs.push_back(outer.number() * 10 + inner.number());
        }
        CHECK_EQ(pairs, (std::vector<int>{11, 12, 21, 22}));
    }

    SECTION("A page's size, in points");
    {
        const auto bounds = document.page(0).bounds();
        CHECK_EQ(bounds.left(), 0.0);
        CHECK_EQ(bounds.top(), 0.0);
        CHECK_EQ(bounds.width(), sample_pdf::page_width);
        CHECK_EQ(bounds.height(), sample_pdf::page_height);
    }

    SECTION("Lines with their positions and fonts, y from the top");
    {
        const auto page = document.page(0);
        const auto heading = find_line(page, "simply-cpp mupdf");
        CHECK(heading.has_value());
        if (heading) {
            CHECK_EQ(heading->font_size, 24.0);
            CHECK(heading->bold);
            CHECK(!heading->italic);
            CHECK(contains(heading->font_name, "Helvetica"));
            CHECK_BETWEEN(heading->bbox.left(), 71.0, 73.0);
            // The baseline is 770 from the bottom, so 72 from the top: the line's box ends near it.
            CHECK_BETWEEN(heading->bbox.bottom(), 72.0, 80.0);
            CHECK_BETWEEN(heading->bbox.top(), 45.0, 60.0);
        }
        const auto body = find_line(page, "each with its position and font.");
        CHECK(body.has_value());
        if (body) {
            CHECK_EQ(body->font_size, 12.0);
            CHECK(!body->bold);
            CHECK(!body->italic);
            CHECK_EQ(body->block_lines, std::size_t{2}); // with the line above it
        }
        const auto italic = find_line(page, "An italic line, in a block of its own.");
        CHECK(italic.has_value());
        if (italic) {
            CHECK(italic->italic);
            CHECK(!italic->bold);
            CHECK_EQ(italic->block_lines, std::size_t{1});
        }
    }

    SECTION("Blocks: type, text and typography");
    {
        const auto blocks = document.page(0).blocks();
        std::vector<std::string> texts;
        for (const auto block: blocks) {
            CHECK(block.type() == sc::mupdf::block_type::text);
            texts.push_back(block.text(" / "));
        }
        CHECK(!texts.empty());
        if (!texts.empty()) CHECK_EQ(texts.front(), std::string{"simply-cpp mupdf"});
        bool joined = false;
        for (const auto &text: texts) joined = joined || text == "Reading a PDF with MuPDF: pages, blocks and lines, / each with its position and font.";
        CHECK(joined);
        const auto first = *blocks.begin();
        CHECK_EQ(first.font_size(), 24.0);
        CHECK(first.bold());
        CHECK(contains(first.font_name(), "Bold"));
    }

    SECTION("A page's text, in reading order");
    {
        const auto text = document.page(1).text();
        CHECK_EQ(text, std::string{"The second page\n\nPage images come from the same document."});
        CHECK(contains(document.page(0).text(" | ", " "), "lines, each with"));
    }

    SECTION("The block range keeps the page's text: moved, it still works");
    {
        auto blocks = document.page(1).blocks();
        auto moved = std::move(blocks);
        CHECK_EQ((*moved.begin()).text(), std::string{"The second page"});
        const sc::mupdf::block none;
        CHECK(none.type() == sc::mupdf::block_type::other);
        CHECK_EQ(none.line_count(), std::size_t{0});
        CHECK(sc::mupdf::line{}.text().empty());
    }

    SECTION("Page images as PNG");
    {
        const auto png = document.page(0).png(1);
        CHECK(png.starts_with("\x89PNG\r\n\x1a\n"));
        CHECK_LT(std::size_t{1000}, png.size());
        // Twice the scale: twice the width and height, in the IHDR chunk (big-endian, bytes 16-23).
        const auto width = [](const std::string &image) {
            return (static_cast<unsigned char>(image[16]) << 24) | (static_cast<unsigned char>(image[17]) << 16) |
                   (static_cast<unsigned char>(image[18]) << 8) | static_cast<unsigned char>(image[19]);
        };
        CHECK_EQ(width(png), 595);
        CHECK_EQ(width(document.page(0).png(2)), 1190);
    }

    SECTION("From a file");
    {
        const auto file = std::filesystem::temp_directory_path() / "sc-mupdf-test.pdf";
        std::ofstream{file, std::ios::binary} << sample_pdf::make();
        const sc::mupdf::document from_file{file};
        CHECK_EQ(from_file.page_count(), 2);
        CHECK_EQ(from_file.page(1).text(), document.page(1).text());
        std::filesystem::remove(file);
    }

    SECTION("Mistakes throw");
    {
        CHECK(throws_error([] { sc::mupdf::document{"/no/such/file.pdf"}; }));
        CHECK(throws_error([] { sc::mupdf::document::from_memory("not a PDF at all"); }));
        bool out_of_range = false;
        try {
            (void) document.page(2);
        } catch (const std::out_of_range &) {
            out_of_range = true;
        }
        CHECK(out_of_range);
        bool negative = false;
        try {
            (void) document.page(-1);
        } catch (const std::out_of_range &) {
            negative = true;
        }
        CHECK(negative);
        bool bad_scale = false;
        try {
            (void) document.page(0).png(0);
        } catch (const std::invalid_argument &) {
            bad_scale = true;
        }
        CHECK(bad_scale);
        CHECK(throws_error([] { (void) sc::mupdf::page{}.text(); }));
    }

    SECTION("Pages keep their document open");
    {
        std::optional<sc::mupdf::page> kept;
        {
            const auto temporary = sc::mupdf::document::from_memory(sample_pdf::make());
            kept = temporary.page(1);
        }
        CHECK_EQ(kept->text(), std::string{"The second page\n\nPage images come from the same document."});
    }

    TEST_SUMMARY();
}
