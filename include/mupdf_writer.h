#pragma once

#include "mupdf.h"

#include <canvas.h>
#include <font.h>
#include <page.h>
#include <rect.h>
#include <text.h>

#include <filesystem>
#include <memory>
#include <string>

namespace sc::mupdf {
    // A PDF file written with MuPDF: an sc::canvas with the same calls as sc-pdf's sc::pdf (PDFHummus),
    // so the two can be swapped and their files compared. Positions are in points (1/72 inch) from
    // the page's top-left corner, y down. A colour's alpha is honoured; fonts are found through
    // fontconfig, as sc::pdf finds them, and embedded. Drawing goes to the current page: the newest,
    // unless set_page() moved back. close() finishes the file, or else the destructor, which can't
    // report a failure.
    class writer : public canvas {
    public:
        // Starts the file with one page; every page has this size and orientation (page.h). Throws
        // sc::mupdf::error when MuPDF can't.
        explicit writer(const std::filesystem::path &filename, page_size size = page_size::a4, orientation turned = orientation::portrait);

        ~writer() override;

        writer(const writer &) = delete;

        writer &operator=(const writer &) = delete;

        writer(writer &&) noexcept;

        writer &operator=(writer &&) noexcept;

        // Writes out the pages and saves the file; throws sc::mupdf::error when that fails. Nothing
        // can be drawn afterwards. Calling it again does nothing.
        void close();

        // Adds a page after the last one and makes it current.
        void new_page();

        // Makes page (counting from 1) current, adding pages up to it when there are fewer.
        void set_page(unsigned page);

        [[nodiscard]] unsigned page() const;

        [[nodiscard]] unsigned page_count() const;

        [[nodiscard]] double page_width() const;

        [[nodiscard]] double page_height() const;

        // An image file MuPDF reads (JPEG, PNG, TIFF, ...), scaled to fit pos with its aspect ratio
        // kept and centred in it. Throws sc::mupdf::error when it can't be read.
        void draw_image(const std::filesystem::path &filename, const rect &pos);

        // Makes pos on the current page a link to url.
        void add_link(const std::string &url, const rect &pos);

    protected:
        // Strokes are a colour and a width, fills a colour and a fill rule (even-odd or nonzero).
        // Other stroke and fill styles aren't drawn yet. Text throws std::runtime_error when
        // fontconfig finds no font.
        void paint_polygon(const polygon &shape, const sc::appearance &look) override;

        void paint_circle(const circle &shape, const sc::appearance &look) override;

        void paint_line(const sc::line &shape, const sc::stroke &stroke) override; // sc::line: in here, line is the reader's

        void paint_text(const text &sample, const sc::fill &fill) override;

        // Throws sc::mupdf::error when MuPDF can't read the image.
        void paint_bitmap(const bitmap &picture, const transform &placement) override;

        // In points, from the font's glyphs: the advances, and the height of x and H above the
        // baseline.
        double measure_width(const text &sample) override;

        double measure_x_height(const font &font) override;

        double measure_cap_height(const font &font) override;

        // Real curves, and clipping (so SVG clip paths work).
        void paint_rounded_rect(const rounded_rect &shape, const sc::appearance &look) override;

        void paint_path(const path &shape, const sc::appearance &look) override;

    public:
        void push_clip(const path &shape) override;

        void pop_clip() override;

    private:
        struct impl;
        std::unique_ptr<impl> impl_;
    };
}
