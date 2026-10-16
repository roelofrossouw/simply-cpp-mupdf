#include "mupdf_writer.h"

#include "fitz_call.h"

#include <font.h>

#include <mupdf/pdf.h>

#include <algorithm>
#include <climits>
#include <iostream>
#include <map>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

// Each page is drawn through MuPDF's PDF device (pdf_page_write), which takes MuPDF's own y-down
// page coordinates, so canvas coordinates go in as they are and the device writes PDF's y-up ones.
// The pages, with their link annotations, are added to the document and saved by close(). MuPDF's
// errors become sc::mupdf::error (fitz_call.h).

namespace sc::mupdf {
    namespace {
        std::pair<double, double> dimensions(const page_size size) {
            switch (size) {
                case page_size::a5: return {420, 595};
                case page_size::letter: return {612, 792};
                case page_size::legal: return {612, 1008};
                case page_size::a4: break;
            }
            return {595, 842};
        }

        // Control point distance for a quarter circle drawn as one cubic Bezier curve, as a fraction
        // of its radius.
        constexpr double quarter_circle = 0.5522847498;

        struct rgb {
            float values[3];

            explicit rgb(const color &colour)
                : values{static_cast<float>(colour.red()), static_cast<float>(colour.green()), static_cast<float>(colour.blue())} {
            }
        };

        bool stroked(const color &stroke, const double width) { return stroke.alpha() > 0 && width > 0; }

        bool filled(const color &fill) { return fill.alpha() > 0; }

        auto f(const double value) { return static_cast<float>(value); }
    }

    struct writer::impl {
        struct page_state {
            fz_device *device = nullptr;
            pdf_obj *resources = nullptr;
            fz_buffer *contents = nullptr;
            std::vector<std::pair<std::string, rect>> links; // added as annotations by close()
        };

        fz_context *context = nullptr;
        pdf_document *document = nullptr;
        std::string filename;
        double width = 0;
        double height = 0;
        std::vector<page_state> pages;
        unsigned current = 0; // index into pages
        bool closed = false;
        // One fz_font serves every size of a face: keyed by family, weight, italic and style.
        std::map<std::tuple<std::string, int, bool, std::string>, fz_font *> fonts;

        impl(const std::filesystem::path &file, const page_size size) : filename(file.string()) {
            std::tie(width, height) = dimensions(size);
            context = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
            if (!context) throw error{"MuPDF: could not create a context"};
            try {
                pdf_document *created = nullptr;
                detail::call(context, "sc::mupdf::writer", [&] { created = pdf_create_document(context); });
                document = created;
                add_page();
            } catch (...) {
                release();
                throw;
            }
        }

        ~impl() { release(); }

        impl(const impl &) = delete;

        impl &operator=(const impl &) = delete;

        // Drops everything MuPDF holds for the file (none of these calls throw).
        void release() {
            if (!context) return;
            for (auto &page: pages) {
                if (page.device) fz_drop_device(context, page.device);
                pdf_drop_obj(context, page.resources);
                fz_drop_buffer(context, page.contents);
            }
            pages.clear();
            for (const auto &[key, font]: fonts) fz_drop_font(context, font);
            fonts.clear();
            if (document) pdf_drop_document(context, document);
            document = nullptr;
            fz_drop_context(context);
            context = nullptr;
        }

        void check_open() const {
            if (closed) throw std::logic_error("sc::mupdf::writer: drawing after close()");
        }

        fz_device *device() {
            check_open();
            return pages[current].device;
        }

        void add_page() {
            page_state page;
            fz_device *device = nullptr;
            pdf_obj *resources = nullptr;
            fz_buffer *contents = nullptr;
            const fz_rect media = fz_make_rect(0, 0, f(width), f(height));
            detail::call(context, "sc::mupdf::writer: a new page", [&] { device = pdf_page_write(context, document, media, &resources, &contents); });
            page.device = device;
            page.resources = resources;
            page.contents = contents;
            pages.push_back(std::move(page));
            current = static_cast<unsigned>(pages.size() - 1);
        }

        // A path built by build(fz_path *), filled and stroked: whichever of the two isn't left out.
        template<typename Build>
        void paint(const color &stroke, const color &fill, const double line_width, const bool even_odd, Build build) {
            const bool stroke_it = stroked(stroke, line_width), fill_it = filled(fill);
            if (!stroke_it && !fill_it) return;
            fz_device *target = device();
            fz_path *shape = nullptr;
            detail::call(context, "sc::mupdf::writer", [&] { shape = fz_new_path(context); });
            fz_stroke_state *stroke_state = nullptr;
            try {
                const rgb fill_colour{fill}, stroke_colour{stroke};
                detail::call(context, "sc::mupdf::writer", [&] {
                    build(shape);
                    // Opaque, as sc::pdf paints them, so the files compare.
                    if (fill_it) fz_fill_path(context, target, shape, even_odd, fz_identity, fz_device_rgb(context), fill_colour.values, 1, fz_default_color_params);
                });
                if (stroke_it) {
                    detail::call(context, "sc::mupdf::writer", [&] { stroke_state = fz_new_stroke_state(context); });
                    stroke_state->linewidth = f(line_width);
                    detail::call(context, "sc::mupdf::writer", [&] {
                        fz_stroke_path(context, target, shape, stroke_state, fz_identity, fz_device_rgb(context), stroke_colour.values, 1, fz_default_color_params);
                    });
                }
            } catch (...) {
                fz_drop_stroke_state(context, stroke_state);
                fz_drop_path(context, shape);
                throw;
            }
            fz_drop_stroke_state(context, stroke_state);
            fz_drop_path(context, shape);
        }

        // MuPDF's version of a canvas path (curves kept).
        void trace(fz_path *shape, const path &steps) const {
            for (const auto &step: steps.steps()) {
                switch (step.type) {
                    case path::kind::move: fz_moveto(context, shape, f(step.to.x()), f(step.to.y())); break;
                    case path::kind::line: fz_lineto(context, shape, f(step.to.x()), f(step.to.y())); break;
                    case path::kind::cubic:
                        fz_curveto(context, shape, f(step.first.x()), f(step.first.y()), f(step.second.x()), f(step.second.y()),
                                   f(step.to.x()), f(step.to.y()));
                        break;
                    case path::kind::close: fz_closepath(context, shape); break;
                }
            }
        }

        fz_font *font_for(const font &spec) {
            const auto key = std::tuple{spec.family, spec.weight, spec.italic, spec.style};
            if (const auto found = fonts.find(key); found != fonts.end()) return found->second;
            // sc-graphics' lookup (fontconfig), the same as every canvas's.
            const auto face = spec.face();
            if (!face) throw std::runtime_error("sc::mupdf::writer: fontconfig found no font for \"" + spec.family + "\"");
            const std::string file = face->file();
            const int index = face->index();
            fz_font *loaded = nullptr;
            detail::call(context, "sc::mupdf::writer: the font " + file, [&] { loaded = fz_new_font_from_file(context, nullptr, file.c_str(), index, 0); });
            fonts.emplace(key, loaded);
            return loaded;
        }

        // How far a glyph of letter rises above the baseline, in points: the top of its outline
        // (fz_bound_glyph can give the whole font's box instead). Glyph space is y up.
        double glyph_height(const font &spec, const int letter) {
            fz_font *face = font_for(spec);
            fz_path *outline = nullptr;
            detail::call(context, "sc::mupdf::writer", [&] { outline = fz_outline_glyph(context, face, fz_encode_character(context, face, letter), fz_identity); });
            if (!outline) return 0; // no outline: a bitmap font, or no such letter
            fz_rect box{};
            try {
                detail::call(context, "sc::mupdf::writer", [&] { box = fz_bound_path(context, outline, nullptr, fz_identity); });
            } catch (...) {
                fz_drop_path(context, outline);
                throw;
            }
            fz_drop_path(context, outline);
            return box.y1 * spec.size;
        }

        void close() {
            if (closed) return;
            closed = true;
            try {
                const fz_rect media = fz_make_rect(0, 0, f(width), f(height));
                for (auto &page: pages) {
                    detail::call(context, "sc::mupdf::writer: finishing a page", [&] { fz_close_device(context, page.device); });
                    fz_drop_device(context, page.device);
                    page.device = nullptr;
                    pdf_obj *added = nullptr;
                    detail::call(context, "sc::mupdf::writer: adding a page", [&] { added = pdf_add_page(context, document, media, 0, page.resources, page.contents); });
                    try {
                        for (const auto &[url, area]: page.links) add_link(added, url, area);
                        detail::call(context, "sc::mupdf::writer: adding a page", [&] { pdf_insert_page(context, document, INT_MAX, added); });
                    } catch (...) {
                        pdf_drop_obj(context, added);
                        throw;
                    }
                    pdf_drop_obj(context, added);
                }
#ifdef SC_MUPDF_HAS_SUBSET_FONTS
                // Only the glyphs the pages use, as sc::pdf embeds them; older MuPDFs embed whole fonts.
                detail::call(context, "sc::mupdf::writer: subsetting fonts", [&] { pdf_subset_fonts(context, document, 0, nullptr); });
#endif
                pdf_write_options options = pdf_default_write_options;
                options.do_compress = 1;
                options.do_compress_fonts = 1;
                options.do_garbage = 1;
                detail::call(context, "sc::mupdf::writer: saving " + filename, [&] { pdf_save_document(context, document, filename.c_str(), &options); });
            } catch (...) {
                release();
                throw;
            }
            release();
        }

        // A link annotation on the page: a URI action, no border, printed. PDF's rectangle counts y up.
        void add_link(pdf_obj *page, const std::string &url, const rect &area) const {
            detail::call(context, "sc::mupdf::writer: a link to " + url, [&] {
                pdf_obj *annotation = pdf_new_dict(context, document, 5);
                pdf_dict_put(context, annotation, PDF_NAME(Type), PDF_NAME(Annot));
                pdf_dict_put(context, annotation, PDF_NAME(Subtype), PDF_NAME(Link));
                pdf_dict_put_int(context, annotation, PDF_NAME(F), 4); // printed, as sc::pdf's links are
                pdf_dict_put_rect(context, annotation, PDF_NAME(Rect),
                                  fz_make_rect(f(area.left()), f(height - area.bottom()), f(area.right()), f(height - area.top())));
                pdf_obj *border = pdf_dict_put_array(context, annotation, PDF_NAME(Border), 3);
                for (int i = 0; i < 3; ++i) pdf_array_push_int(context, border, 0);
                pdf_obj *action = pdf_dict_put_dict(context, annotation, PDF_NAME(A), 2);
                pdf_dict_put(context, action, PDF_NAME(S), PDF_NAME(URI));
                pdf_dict_put_string(context, action, PDF_NAME(URI), url.c_str(), url.size());
                pdf_obj *reference = pdf_add_object(context, document, annotation);
                pdf_obj *annotations = pdf_dict_get(context, page, PDF_NAME(Annots));
                if (!annotations) annotations = pdf_dict_put_array(context, page, PDF_NAME(Annots), 1);
                pdf_array_push(context, annotations, reference);
                pdf_drop_obj(context, reference);
                pdf_drop_obj(context, annotation);
            });
        }
    };

    writer::writer(const std::filesystem::path &filename, const page_size size) : impl_(std::make_unique<impl>(filename, size)) {
    }

    writer::~writer() {
        if (!impl_) return;
        try {
            impl_->close();
        } catch (const std::exception &failure) {
            std::cerr << failure.what() << '\n';
        }
    }

    writer::writer(writer &&) noexcept = default;

    writer &writer::operator=(writer &&other) noexcept {
        if (this != &other) {
            if (impl_) {
                try {
                    impl_->close();
                } catch (const std::exception &failure) {
                    std::cerr << failure.what() << '\n';
                }
            }
            impl_ = std::move(other.impl_);
        }
        return *this;
    }

    void writer::close() { impl_->close(); }

    void writer::new_page() {
        impl_->check_open();
        impl_->add_page();
    }

    void writer::set_page(const unsigned page) {
        impl_->check_open();
        if (page == 0) throw std::out_of_range("sc::mupdf::writer::set_page: pages count from 1");
        while (impl_->pages.size() < page) impl_->add_page();
        impl_->current = page - 1;
    }

    unsigned writer::page() const { return impl_->current + 1; }

    unsigned writer::page_count() const { return static_cast<unsigned>(impl_->pages.size()); }

    double writer::page_width() const { return impl_->width; }

    double writer::page_height() const { return impl_->height; }

    void writer::draw_image(const std::filesystem::path &filename, const rect &pos) {
        fz_device *target = impl_->device();
        fz_context *context = impl_->context;
        const std::string name = filename.string();
        fz_image *image = nullptr;
        detail::call(context, "sc::mupdf::writer: the image " + name, [&] { image = fz_new_image_from_file(context, name.c_str()); });
        try {
            if (image->w <= 0 || image->h <= 0) throw error{"sc::mupdf::writer: the image " + name + " is empty"};
            const double scale = std::min(pos.width() / image->w, pos.height() / image->h);
            const double w = image->w * scale, h = image->h * scale;
            const double x = pos.left() + (pos.width() - w) / 2, y = pos.top() + (pos.height() - h) / 2;
            // MuPDF draws an image into the unit square, its top row at y = 0.
            detail::call(context, "sc::mupdf::writer: the image " + name, [&] {
                fz_fill_image(context, target, image, fz_make_matrix(f(w), 0, 0, f(h), f(x), f(y)), 1, fz_default_color_params);
            });
        } catch (...) {
            fz_drop_image(context, image);
            throw;
        }
        fz_drop_image(context, image);
    }

    void writer::add_link(const std::string &url, const rect &pos) {
        impl_->check_open();
        impl_->pages[impl_->current].links.emplace_back(url, pos);
    }

    void writer::paint_polygon(const polygon &shape, const color &stroke, const color &fill, const double width) {
        impl_->paint(stroke, fill, width, false, [&](fz_path *p) {
            bool first = true;
            for (const auto &corner: shape) {
                if (first) fz_moveto(impl_->context, p, f(corner.x()), f(corner.y()));
                else fz_lineto(impl_->context, p, f(corner.x()), f(corner.y()));
                first = false;
            }
            fz_closepath(impl_->context, p);
        });
    }

    void writer::paint_circle(const circle &shape, const color &stroke, const color &fill, const double width) {
        const double cx = shape.center().x(), cy = shape.center().y(), r = shape.radius(), k = r * quarter_circle;
        impl_->paint(stroke, fill, width, false, [&](fz_path *p) {
            fz_context *c = impl_->context;
            fz_moveto(c, p, f(cx + r), f(cy));
            fz_curveto(c, p, f(cx + r), f(cy + k), f(cx + k), f(cy + r), f(cx), f(cy + r));
            fz_curveto(c, p, f(cx - k), f(cy + r), f(cx - r), f(cy + k), f(cx - r), f(cy));
            fz_curveto(c, p, f(cx - r), f(cy - k), f(cx - k), f(cy - r), f(cx), f(cy - r));
            fz_curveto(c, p, f(cx + k), f(cy - r), f(cx + r), f(cy - k), f(cx + r), f(cy));
            fz_closepath(c, p);
        });
    }

    void writer::paint_line(const point &from, const point &to, const color &colour, const double width) {
        impl_->paint(colour, color::Transparent, width, false, [&](fz_path *p) {
            fz_moveto(impl_->context, p, f(from.x()), f(from.y()));
            fz_lineto(impl_->context, p, f(to.x()), f(to.y()));
        });
    }

    void writer::paint_rounded_rect(const rect &shape, const double radius, const color &stroke, const color &fill, const double width) {
        const double l = shape.left(), t = shape.top(), r = shape.right(), b = shape.bottom(), k = radius * quarter_circle;
        impl_->paint(stroke, fill, width, false, [&](fz_path *p) {
            fz_context *c = impl_->context;
            fz_moveto(c, p, f(l + radius), f(t));
            fz_lineto(c, p, f(r - radius), f(t));
            fz_curveto(c, p, f(r - radius + k), f(t), f(r), f(t + radius - k), f(r), f(t + radius));
            fz_lineto(c, p, f(r), f(b - radius));
            fz_curveto(c, p, f(r), f(b - radius + k), f(r - radius + k), f(b), f(r - radius), f(b));
            fz_lineto(c, p, f(l + radius), f(b));
            fz_curveto(c, p, f(l + radius - k), f(b), f(l), f(b - radius + k), f(l), f(b - radius));
            fz_lineto(c, p, f(l), f(t + radius));
            fz_curveto(c, p, f(l), f(t + radius - k), f(l + radius - k), f(t), f(l + radius), f(t));
            fz_closepath(c, p);
        });
    }

    void writer::paint_path(const path &shape, const color &stroke, const color &fill, const double width) {
        impl_->paint(stroke, fill, width, shape.even_odd, [&](fz_path *p) { impl_->trace(p, shape); });
    }

    void writer::push_clip(const path &shape) {
        fz_device *target = impl_->device();
        fz_context *context = impl_->context;
        fz_path *clip = nullptr;
        detail::call(context, "sc::mupdf::writer", [&] { clip = fz_new_path(context); });
        try {
            detail::call(context, "sc::mupdf::writer: a clip path", [&] {
                impl_->trace(clip, shape);
                fz_clip_path(context, target, clip, shape.even_odd, fz_identity, fz_infinite_rect);
            });
        } catch (...) {
            fz_drop_path(context, clip);
            throw;
        }
        fz_drop_path(context, clip);
    }

    void writer::pop_clip() {
        fz_device *target = impl_->device();
        detail::call(impl_->context, "sc::mupdf::writer", [&] { fz_pop_clip(impl_->context, target); });
    }

    void writer::paint_text(const text &sample, const color &colour) {
        fz_device *target = impl_->device();
        fz_context *context = impl_->context;
        fz_font *face = impl_->font_for(sample.font);
        fz_text *line = nullptr;
        detail::call(context, "sc::mupdf::writer", [&] { line = fz_new_text(context); });
        try {
            const rgb paint{colour};
            const double size = sample.font.size;
            // Glyphs are y up, the page y down: the text matrix flips them upright at the baseline.
            const fz_matrix placement = fz_make_matrix(f(size), 0, 0, f(-size), f(sample.position.x()), f(sample.position.y()));
            detail::call(context, "sc::mupdf::writer: text", [&] {
                fz_show_string(context, line, face, placement, sample.content.c_str(), 0, 0, FZ_BIDI_LTR, FZ_LANG_UNSET);
                fz_fill_text(context, target, line, fz_identity, fz_device_rgb(context), paint.values, 1, fz_default_color_params);
            });
        } catch (...) {
            fz_drop_text(context, line);
            throw;
        }
        fz_drop_text(context, line);
    }

    double writer::measure_width(const text &sample) {
        fz_context *context = impl_->context;
        fz_font *face = impl_->font_for(sample.font);
        double advance = 0;
        detail::call(context, "sc::mupdf::writer", [&] {
            for (const char *s = sample.content.c_str(); *s;) {
                int letter = 0;
                s += fz_chartorune(&letter, s);
                advance += fz_advance_glyph(context, face, fz_encode_character(context, face, letter), 0);
            }
        });
        return advance * sample.font.size;
    }

    double writer::measure_x_height(const font &font) { return impl_->glyph_height(font, 'x'); }

    double writer::measure_cap_height(const font &font) { return impl_->glyph_height(font, 'H'); }
}
