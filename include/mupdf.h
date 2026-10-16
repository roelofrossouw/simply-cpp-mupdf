#pragma once

#include <rect.h>

#include <cstddef>
#include <filesystem>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

// MuPDF's own types, so this header needs none of its headers.
struct fz_context;
struct fz_stext_block;
struct fz_stext_line;

// Reading PDFs (and the other formats MuPDF opens: XPS, EPUB, CBZ, images) with MuPDF: a document's
// pages, each page's text as blocks of lines with their positions and fonts, and page images.
//
// MuPDF is AGPL-3.0, so this module is too; sc-pdf (PDFHummus) is the Apache-2.0 writer. Writing
// with MuPDF: mupdf_writer.h.
//
//     const sc::mupdf::document doc{"letter.pdf"};
//     for (const auto page: doc.pages()) {
//         for (const auto block: page.blocks()) {          // keep page.blocks() alive while iterating
//             for (const auto line: block) {
//                 std::cout << line.bbox() << ' ' << line.font_size() << (line.bold() ? " bold " : " ") << line.text() << '\n';
//             }
//         }
//     }
//
// Positions are in points (1/72 inch) from the page's top-left corner, y down. A document isn't
// thread-safe: use one per thread.
namespace sc::mupdf {
    // What MuPDF reported when it couldn't do something (open a file, load a page, ...).
    class error : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    // An input iterator over cheap handles (page, block, line) that holds the current one by value,
    // so iterations over the same range never share state. T needs ++ and ==.
    template<typename T>
    class iterator {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T *;
        using reference = T; // a copy: T is a handle, a pointer or two wide

        iterator() = default;

        explicit iterator(T current) : current_(std::move(current)) {
        }

        reference operator*() const { return current_; }

        pointer operator->() const { return &current_; }

        iterator &operator++() {
            ++current_;
            return *this;
        }

        iterator operator++(int) {
            iterator previous = *this;
            ++current_;
            return previous;
        }

        bool operator==(const iterator &other) const { return current_ == other.current_; }

    private:
        T current_{};
    };

    enum class block_type { text, image, other };

    // One line of text: a handle into its page's text, valid while that page's blocks are.
    class line {
    public:
        line() = default;

        line(fz_context *context, fz_stext_line *line) : context_(context), line_(line) {
        }

        // The line's text, trimmed: empty when it has none or it doesn't look like text (a font
        // without a usable character map gives control characters and the like).
        [[nodiscard]] std::string text() const;

        [[nodiscard]] rect bbox() const;

        // The most common character size in the line, in points: superscripts and footnote marks
        // don't count for more than they are.
        [[nodiscard]] double font_size() const;

        // Most of the characters bold (or italic): by the font's flags or, as many PDFs embed bold
        // faces without the flag, its name (Bold, Black, Heavy, SemiBold; Italic, Oblique).
        [[nodiscard]] bool bold() const;

        [[nodiscard]] bool italic() const;

        // The first character's font, subset prefix included ("ABCDEF+Helvetica-Bold").
        [[nodiscard]] std::string font_name() const;

        line &operator++();

        bool operator==(const line &other) const { return line_ == other.line_; }

    private:
        fz_context *context_ = nullptr;
        fz_stext_line *line_ = nullptr;
    };

    // A block of a page: text (a range of lines), an image or something else (vector graphics,
    // structure). A handle, valid while its page's blocks are.
    class block {
    public:
        block() = default;

        block(fz_context *context, fz_stext_block *block) : context_(context), block_(block) {
        }

        [[nodiscard]] block_type type() const;

        [[nodiscard]] rect bbox() const;

        // The lines of a text block; none for other blocks.
        [[nodiscard]] iterator<line> begin() const;

        [[nodiscard]] iterator<line> end() const { return {}; }

        [[nodiscard]] std::size_t line_count() const;

        // Its lines' text, those with any, joined by the separator.
        [[nodiscard]] std::string text(std::string_view line_separator = "\n") const;

        // The first line's typography (see line); while iterating, ask each line instead.
        [[nodiscard]] double font_size() const;

        [[nodiscard]] bool bold() const;

        [[nodiscard]] std::string font_name() const;

        block &operator++();

        bool operator==(const block &other) const { return block_ == other.block_; }

    private:
        fz_context *context_ = nullptr;
        fz_stext_block *block_ = nullptr;
    };

    class document;

    // A page's text, read once: the range of its blocks. The blocks and lines point into it, so keep
    // it while using them. A range-for over page.blocks() keeps it for the loop.
    class blocks {
    public:
        ~blocks();

        blocks(blocks &&) noexcept;

        blocks &operator=(blocks &&) noexcept;

        blocks(const blocks &) = delete;

        blocks &operator=(const blocks &) = delete;

        [[nodiscard]] iterator<block> begin() const;

        [[nodiscard]] iterator<block> end() const { return {}; }

    private:
        friend class page;
        struct impl;

        explicit blocks(std::unique_ptr<impl> impl);

        std::unique_ptr<impl> impl_;
    };

    // A page of a document: a cheap handle (the document and the page's index) that keeps the
    // document open. MuPDF's page is loaded when first needed and kept by this handle.
    class page {
    public:
        page() = default;

        // 0-based, and number() 1-based, as people count.
        [[nodiscard]] int index() const { return index_; }

        [[nodiscard]] int number() const { return index_ + 1; }

        // The page's size and position in points; most pages start at 0,0.
        [[nodiscard]] rect bounds() const;

        // The page's text as blocks of lines, read now.
        [[nodiscard]] mupdf::blocks blocks() const;

        // All its text: blocks joined by block_separator, lines by line_separator.
        [[nodiscard]] std::string text(std::string_view block_separator = "\n\n", std::string_view line_separator = "\n") const;

        // The page drawn as a PNG, `scale` pixels per point (1 is 72 dpi, 2 is 144 dpi).
        [[nodiscard]] std::string png(double scale = 1.5) const;

        page &operator++();

        bool operator==(const page &other) const { return index_ == other.index_; }

    private:
        friend class document;
        friend class pages;
        friend class blocks;
        struct state;

        page(std::shared_ptr<state> document, int index);

        // The loaded fz_page, loading it the first time.
        [[nodiscard]] void *loaded() const;

        std::shared_ptr<state> document_;
        int index_ = 0;
        mutable std::shared_ptr<void> page_; // the fz_page, dropped by the last copy holding it
    };

    // The pages of a document, for a range-for.
    class pages {
    public:
        [[nodiscard]] iterator<page> begin() const;

        [[nodiscard]] iterator<page> end() const;

        [[nodiscard]] int size() const { return count_; }

    private:
        friend class document;

        pages(std::shared_ptr<page::state> document, int count) : document_(std::move(document)), count_(count) {
        }

        std::shared_ptr<page::state> document_;
        int count_ = 0;
    };

    class document {
    public:
        // Opens a file; a password if it's encrypted. Throws error when it can't be opened or the
        // password is wrong.
        explicit document(const std::filesystem::path &file, std::string_view password = {});

        // Opens a document held in memory. `type` is a MIME type or file extension ("application/pdf",
        // "pdf", "xps", "png", ...).
        static document from_memory(std::string data, std::string_view type = "application/pdf", std::string_view password = {});

        [[nodiscard]] int page_count() const;

        [[nodiscard]] mupdf::pages pages() const;

        // 0-based; throws std::out_of_range outside 0 to page_count() - 1.
        [[nodiscard]] mupdf::page page(int index) const;

        // The document's metadata ("Title", "Author", "Subject", "Keywords", "Creator", "Producer",
        // "CreationDate", "ModDate", or "format": "PDF 1.7"); empty when it has none.
        [[nodiscard]] std::string metadata(std::string_view key) const;

    private:
        document() = default;

        std::shared_ptr<mupdf::page::state> state_; // mupdf::: page here is the function above
    };
}
