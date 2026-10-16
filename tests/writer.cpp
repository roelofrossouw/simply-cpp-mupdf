// Writes PDFs with sc::mupdf::writer into the temporary directory and reads them back with the
// reader: pages and sizes (A3 landscape too), transparency, where text lands and how it measures,
// links, images, going back to an earlier page, plus the mistakes that throw.
#include "mupdf.h"
#include "mupdf_writer.h"

#include "sc_test.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>

namespace {
    const auto directory = std::filesystem::temp_directory_path() / "sc-mupdf-writer-test";

    std::string read(const std::filesystem::path &path) {
        std::ifstream file{path, std::ios::binary};
        return {std::istreambuf_iterator<char>{file}, {}};
    }

    bool contains(const std::string &text, const std::string &part) { return text.find(part) != std::string::npos; }

    // The box of the line with exactly this text on the page.
    std::optional<sc::rect> line_box(const sc::mupdf::page &page, const std::string &text) {
        for (const auto block: page.blocks()) {
            for (const auto line: block) {
                if (line.text() == text) return line.bbox();
            }
        }
        return std::nullopt;
    }

    template<typename Exception, typename F>
    bool throws(F &&call) {
        try {
            call();
        } catch (const Exception &) {
            return true;
        }
        return false;
    }
}

int main() {
    std::filesystem::create_directories(directory);

    SECTION("Pages, their size, and going back to an earlier page");
    const auto pages_file = directory / "pages.pdf";
    {
        sc::mupdf::writer pdf{pages_file, sc::page_size::letter};
        CHECK_EQ(pdf.page_width(), 612.0);
        CHECK_EQ(pdf.page_height(), 792.0);
        CHECK_EQ(pdf.page_count(), 1u);
        pdf.draw(sc::text{"First page", {72, 100}});
        pdf.new_page();
        pdf.draw(sc::text{"Second page", {72, 100}});
        CHECK_EQ(pdf.page(), 2u);
        pdf.set_page(1);
        pdf.draw(sc::text{"Added to the first afterwards", {72, 200}});
        pdf.set_page(3); // adds the third
        CHECK_EQ(pdf.page_count(), 3u);
        pdf.close();
        pdf.close(); // again: nothing
    }
    {
        const sc::mupdf::document doc{pages_file};
        CHECK_EQ(doc.page_count(), 3);
        CHECK_EQ(doc.page(0).bounds().width(), 612.0);
        CHECK_EQ(doc.page(0).bounds().height(), 792.0);
        CHECK_EQ(doc.page(0).text(), std::string{"First page\n\nAdded to the first afterwards"});
        CHECK_EQ(doc.page(1).text(), std::string{"Second page"});
        CHECK(doc.page(2).text().empty());
    }
    {
        sc::mupdf::writer wide{directory / "a3-landscape.pdf", sc::page_size::a3, sc::orientation::landscape};
        CHECK_EQ(wide.page_width(), 1191.0);
        CHECK_EQ(wide.page_height(), 842.0);
    }

    SECTION("Transparency: a colour's alpha");
    const auto alpha_file = directory / "alpha.pdf";
    {
        sc::mupdf::writer pdf{alpha_file};
        pdf.draw(sc::circle{{100, 100}, 20}, sc::stroke::none(), sc::color{0, 0, 0, 0.5});
        pdf.draw(sc::rect{50, 150, 100, 50}, sc::stroke{sc::color{1, 0, 0, 0.75}, 2});
        pdf.draw(sc::text{"faint", {50, 250}}, sc::color{0, 0, 1, 0.25});
    }
    {
        const auto bytes = read(alpha_file);
        CHECK(contains(bytes, "/ExtGState"));
        CHECK(contains(bytes, "/ca"));
        CHECK(contains(bytes, "/CA"));
        CHECK_EQ(sc::mupdf::document{alpha_file}.page(0).text(), std::string{"faint"});
    }

    SECTION("Text lands where it was drawn, y down, as wide as text_width() says");
    const auto text_file = directory / "text.pdf";
    double width = 0, cap = 0, x_height = 0;
    {
        sc::mupdf::writer pdf{text_file};
        const sc::text heading{"A heading", {50, 90}, sc::font::bold("sans-serif", 24)};
        width = pdf.text_width(heading);
        cap = pdf.text_cap_height(heading.font);
        x_height = pdf.text_x_height(heading.font);
        pdf.draw(heading);
        pdf.draw(sc::text{"Body text", {50, 130}, {"serif", 12}}, sc::color::Blue);
        CHECK_EQ(pdf.text_width(sc::text{"", {0, 0}}), 0.0);
    }
    CHECK_BETWEEN(width, 90.0, 150.0);
    CHECK_BETWEEN(cap, 24 * 0.6, 24 * 0.8);   // a capital is about 70% of the size
    CHECK_BETWEEN(x_height, 24 * 0.4, 24 * 0.6);
    CHECK_LT(x_height, cap);
    {
        const sc::mupdf::document doc{text_file};
        const auto page = doc.page(0);
        const auto heading = line_box(page, "A heading");
        CHECK(heading.has_value());
        if (heading) {
            CHECK_BETWEEN(heading->left(), 49.0, 51.0);
            CHECK_BETWEEN(heading->width(), width - 1, width + 1);
            CHECK_BETWEEN(heading->bottom(), 90.0, 100.0); // the baseline at 90, descenders below it
            CHECK_BETWEEN(heading->top(), 60.0, 72.0);
        }
        for (const auto block: page.blocks()) {
            if (block.text() == "A heading") CHECK(block.bold());
        }
        CHECK(line_box(page, "Body text").has_value());
    }

    SECTION("Shapes, an SVG drawing with a clip path, an image and a link");
    const auto drawing_file = directory / "drawing.pdf";
    const auto image_file = directory / "image.png";
    {
        // The image: a page of the text file drawn as a PNG by the reader.
        std::ofstream{image_file, std::ios::binary} << sc::mupdf::document{text_file}.page(0).png(0.5);
        sc::mupdf::writer pdf{drawing_file};
        pdf.draw(sc::rounded_rect{sc::rect{50, 50, 200, 100}, 12}, sc::stroke{sc::color::Black, 2}, sc::color{0.85, 0.93, 0.98});
        pdf.draw(sc::circle{{400, 100}, 40}, sc::stroke{sc::color::Black, 2}, sc::color{0.96, 0.83, 0.37});
        pdf.draw(sc::line{{50, 200}, {545, 200}}, sc::stroke{sc::color::Black, 0.5});
        pdf.draw(sc::polygon{{50, 300}, {100, 220}, {150, 300}}, sc::stroke{sc::color::Red, 3});
        pdf.draw(sc::svg{R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100">)svg"
                         R"svg(<defs><clipPath id="c"><circle cx="50" cy="50" r="45"/></clipPath></defs>)svg"
                         R"svg(<rect clip-path="url(#c)" width="100" height="100" fill="#264653"/></svg>)svg"},
                 sc::rect{250, 220, 100, 100});
        pdf.draw_image(image_file, {50, 350, 300, 200});
        pdf.add_link("https://example.com/sc-mupdf", {50, 600, 120, 20});
        pdf.close();
        CHECK(throws<std::logic_error>([&] { pdf.draw(sc::text{"after close", {50, 50}}); }));
        CHECK(throws<std::logic_error>([&] { pdf.new_page(); }));
    }
    {
        const auto bytes = read(drawing_file);
        CHECK(bytes.starts_with("%PDF-"));
        CHECK(contains(bytes, "/URI(https://example.com/sc-mupdf)") || contains(bytes, "/URI (https://example.com/sc-mupdf)"));
        const sc::mupdf::document doc{drawing_file};
        CHECK_EQ(doc.page_count(), 1);
        bool image = false;
        for (const auto block: doc.page(0).blocks()) image = image || block.type() == sc::mupdf::block_type::image;
        CHECK(image);
        CHECK_LT(std::size_t{1000}, doc.page(0).png(0.5).size()); // it renders
    }

    SECTION("Mistakes throw");
    {
        sc::mupdf::writer pdf{directory / "mistakes.pdf"};
        CHECK(throws<std::out_of_range>([&] { pdf.set_page(0); }));
        CHECK(throws<sc::mupdf::error>([&] { pdf.draw_image(directory / "no-such-image.png", {0, 0, 10, 10}); }));
        CHECK(throws<sc::mupdf::error>([&] { pdf.draw_image(pages_file, {0, 0, 10, 10}); })); // a PDF isn't an image
    }
    CHECK(throws<sc::mupdf::error>([] {
        sc::mupdf::writer pdf{"/no/such/directory/file.pdf"};
        pdf.close(); // the file is only written now
    }));

    std::filesystem::remove_all(directory);
    TEST_SUMMARY();
}
