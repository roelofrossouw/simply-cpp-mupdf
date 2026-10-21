#include "mupdf.h"

#include <core.h>
#include <utf8.h>

#include "fitz_call.h"

#include <algorithm>
#include <map>

// MuPDF's error handling (fz_try, longjmp) and the versions used: see fitz_call.h.

namespace sc::mupdf {
    using detail::caught;

    // The document's MuPDF state, shared by every page handle and block range made from it.
    struct page::state {
        fz_context *context = nullptr;
        fz_document *document = nullptr;
        int count = 0;
        std::string data; // from_memory(): MuPDF reads it in place

        state() {
            context = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
            if (!context) throw error{"MuPDF: could not create a context"};
            try {
                detail::call(context, "MuPDF", [&] { fz_register_document_handlers(context); });
            } catch (...) {
                fz_drop_context(context);
                throw;
            }
        }

        ~state() {
            if (document) fz_drop_document(context, document);
            fz_drop_context(context);
        }

        state(const state &) = delete;

        state &operator=(const state &) = delete;

        template<typename F>
        void run(const std::string_view what, F &&calls) const { detail::call(context, what, std::forward<F>(calls)); }

        // After opening: the password, if one is needed, and the page count.
        void opened(const std::string_view name, const std::string_view password) {
            int needs = 0;
            run(name, [&] { needs = fz_needs_password(context, document); });
            if (needs) {
                const std::string key{password};
                int accepted = 0;
                run(name, [&] { accepted = fz_authenticate_password(context, document, key.c_str()); });
                if (!accepted) throw error{std::string{name} + (password.empty() ? ": needs a password" : ": wrong password")};
            }
            run(name, [&] { count = fz_count_pages(context, document); });
        }
    };

    namespace {
        rect to_rect(const fz_rect &r) { return {r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0}; }

        bool name_has(const char *name, const std::string_view part) {
            if (!name) return false;
            return lowercase(name).find(part) != std::string::npos;
        }

        bool looks_bold(fz_context *context, fz_font *font) {
            if (!font) return false;
            if (fz_font_is_bold(context, font)) return true;
            const char *name = fz_font_name(context, font);
            return name_has(name, "bold") || name_has(name, "black") || name_has(name, "heavy") || name_has(name, "semib");
        }

        bool looks_italic(fz_context *context, fz_font *font) {
            if (!font) return false;
            if (fz_font_is_italic(context, font)) return true;
            const char *name = fz_font_name(context, font);
            return name_has(name, "italic") || name_has(name, "oblique");
        }

        std::string trimmed(const std::string &text) {
            constexpr std::string_view space = " \t\r\n\f\v";
            const auto first = text.find_first_not_of(space);
            if (first == std::string::npos) return {};
            return text.substr(first, text.find_last_not_of(space) - first + 1);
        }

        template<typename Test>
        bool most_chars(fz_context *context, const fz_stext_line *line, Test test) {
            if (!line) return false;
            int total = 0, passing = 0;
            for (const fz_stext_char *c = line->first_char; c; c = c->next) {
                ++total;
                if (test(context, c->font)) ++passing;
            }
            return total > 0 && passing * 2 > total;
        }

        fz_stext_line *first_line(fz_stext_block *block) {
            return block && block->type == FZ_STEXT_BLOCK_TEXT ? block->u.t.first_line : nullptr;
        }
    }

    std::string line::text() const {
        if (!line_) return {};
        std::string text;
        for (const fz_stext_char *c = line_->first_char; c; c = c->next) utf8::append_codepoint(text, static_cast<std::uint32_t>(c->c));
        text = trimmed(text);
        if (const auto analysis = utf8::analyze(text); analysis.codepoints == 0 || !analysis.likely_text()) return {};
        return text;
    }

    rect line::bbox() const { return line_ ? to_rect(line_->bbox) : rect{}; }

    double line::font_size() const {
        if (!line_ || !line_->first_char) return 0;
        // In tenths of a point, so float noise doesn't split a size in two.
        std::map<int, int> counts;
        for (const fz_stext_char *c = line_->first_char; c; c = c->next) ++counts[static_cast<int>(c->size * 10.f + 0.5f)];
        const auto most = std::ranges::max_element(counts, {}, [](const auto &entry) { return entry.second; });
        return most->first / 10.0;
    }

    bool line::bold() const { return most_chars(context_, line_, looks_bold); }

    bool line::italic() const { return most_chars(context_, line_, looks_italic); }

    std::string line::font_name() const {
        if (!line_ || !line_->first_char || !line_->first_char->font) return {};
        const char *name = fz_font_name(context_, line_->first_char->font);
        return name ? name : "";
    }

    line &line::operator++() {
        if (line_) line_ = line_->next;
        return *this;
    }

    block_type block::type() const {
        if (!block_) return block_type::other;
        if (block_->type == FZ_STEXT_BLOCK_TEXT) return block_type::text;
        if (block_->type == FZ_STEXT_BLOCK_IMAGE) return block_type::image;
        return block_type::other; // MuPDF 1.23+ also has struct, vector and grid blocks
    }

    rect block::bbox() const { return block_ ? to_rect(block_->bbox) : rect{}; }

    iterator<line> block::begin() const { return iterator<line>{line{context_, first_line(block_)}}; }

    std::size_t block::line_count() const { return static_cast<std::size_t>(std::distance(begin(), end())); }

    std::string block::text(const std::string_view line_separator) const {
        std::string text;
        for (const auto each: *this) {
            if (const auto line_text = each.text(); !line_text.empty()) {
                if (!text.empty()) text += line_separator;
                text += line_text;
            }
        }
        return text;
    }

    double block::font_size() const { return line{context_, first_line(block_)}.font_size(); }

    bool block::bold() const { return line{context_, first_line(block_)}.bold(); }

    std::string block::font_name() const { return line{context_, first_line(block_)}.font_name(); }

    block &block::operator++() {
        if (block_) block_ = block_->next;
        return *this;
    }

    struct blocks::impl {
        std::shared_ptr<page::state> document;
        fz_stext_page *text = nullptr;

        ~impl() {
            if (text) fz_drop_stext_page(document->context, text);
        }
    };

    blocks::blocks(std::unique_ptr<impl> impl) : impl_(std::move(impl)) {
    }

    blocks::~blocks() = default;

    blocks::blocks(blocks &&) noexcept = default;

    blocks &blocks::operator=(blocks &&) noexcept = default;

    iterator<block> blocks::begin() const {
        if (!impl_ || !impl_->text) return end();
        return iterator<block>{block{impl_->document->context, impl_->text->first_block}};
    }

    page::page(std::shared_ptr<state> document, const int index) : document_(std::move(document)), index_(index) {
    }

    void *page::loaded() const {
        if (!document_) throw error{"MuPDF: an empty page handle"};
        if (!page_) {
            fz_page *raw = nullptr;
            document_->run("page " + std::to_string(number()), [&] { raw = fz_load_page(document_->context, document_->document, index_); });
            auto document = document_;
            page_ = std::shared_ptr<void>{raw, [document](void *p) { fz_drop_page(document->context, static_cast<fz_page *>(p)); }};
        }
        return page_.get();
    }

    rect page::bounds() const {
        auto *p = static_cast<fz_page *>(loaded());
        fz_rect bounds{};
        document_->run("page " + std::to_string(number()), [&] { bounds = fz_bound_page(document_->context, p); });
        return to_rect(bounds);
    }

    mupdf::blocks page::blocks() const {
        auto *p = static_cast<fz_page *>(loaded());
        auto text = std::make_unique<mupdf::blocks::impl>();
        text->document = document_;
        fz_stext_page *raw = nullptr;
        document_->run("page " + std::to_string(number()), [&] {
            fz_stext_options options{}; // MuPDF's defaults (ligatures expanded, whitespace normalised),
            options.flags = FZ_STEXT_PRESERVE_IMAGES; // plus the image blocks, left out otherwise
            raw = fz_new_stext_page_from_page(document_->context, p, &options);
        });
        text->text = raw;
        return mupdf::blocks{std::move(text)};
    }

    std::string page::text(const std::string_view block_separator, const std::string_view line_separator) const {
        std::string text;
        for (const auto each: blocks()) {
            if (const auto block_text = each.text(line_separator); !block_text.empty()) {
                if (!text.empty()) text += block_separator;
                text += block_text;
            }
        }
        return text;
    }

    std::string page::png(const double scale) const {
        if (scale <= 0) throw std::invalid_argument{"sc::mupdf::page::png: the scale must be above 0"};
        auto *p = static_cast<fz_page *>(loaded());
        fz_context *context = document_->context;
        fz_pixmap *pixmap = nullptr;
        fz_buffer *buffer = nullptr;
        unsigned char *bytes = nullptr;
        std::size_t length = 0;
        std::string failure;
        bool failed = false;
        fz_var(pixmap);
        fz_var(buffer);
        fz_try(context) {
            const auto s = static_cast<float>(scale);
            pixmap = fz_new_pixmap_from_page(context, p, fz_scale(s, s), fz_device_rgb(context), 0);
            buffer = fz_new_buffer_from_pixmap_as_png(context, pixmap, fz_default_color_params);
            length = fz_buffer_storage(context, buffer, &bytes);
        }
        fz_catch(context) {
            failed = true;
            failure = caught(context);
        }
        std::string png;
        if (!failed) png.assign(reinterpret_cast<const char *>(bytes), length);
        fz_drop_buffer(context, buffer);
        fz_drop_pixmap(context, pixmap);
        if (failed) throw error{"page " + std::to_string(number()) + ": " + failure};
        return png;
    }

    page &page::operator++() {
        ++index_;
        page_.reset(); // the next page loads when used; the end of the range never does
        return *this;
    }

    iterator<page> pages::begin() const { return iterator<page>{page{document_, 0}}; }

    iterator<page> pages::end() const { return iterator<page>{page{document_, count_}}; }

    document::document(const std::filesystem::path &file, const std::string_view password)
        : state_(std::make_shared<page::state>()) {
        const std::string name = file.string();
        fz_document *opened = nullptr;
        state_->run(name, [&] { opened = fz_open_document(state_->context, name.c_str()); });
        state_->document = opened;
        state_->opened(name, password);
    }

    document document::from_memory(std::string data, const std::string_view type, const std::string_view password) {
        document result;
        result.state_ = std::make_shared<page::state>();
        auto &state = *result.state_;
        state.data = std::move(data);
        const std::string magic{type};
        fz_document *opened = nullptr;
        fz_stream *stream = nullptr;
        std::string failure;
        bool failed = false;
        fz_var(stream);
        fz_try(state.context) {
            stream = fz_open_memory(state.context, reinterpret_cast<const unsigned char *>(state.data.data()), state.data.size());
            opened = fz_open_document_with_stream(state.context, magic.c_str(), stream);
        }
        fz_always(state.context) {
            fz_drop_stream(state.context, stream); // the document keeps its own reference
        }
        fz_catch(state.context) {
            failed = true;
            failure = caught(state.context);
        }
        if (failed) throw error{"document in memory (" + magic + "): " + failure};
        state.document = opened;
        state.opened("document in memory", password);
        return result;
    }

    int document::page_count() const { return state_->count; }

    mupdf::pages document::pages() const { return {state_, state_->count}; }

    mupdf::page document::page(const int index) const {
        if (index < 0 || index >= state_->count) {
            throw std::out_of_range{"sc::mupdf::document::page: " + std::to_string(index) + " isn't 0 to " + std::to_string(state_->count - 1)};
        }
        return {state_, index};
    }

    std::string document::metadata(const std::string_view key) const {
        // MuPDF's names for the info dictionary are "info:Title" and so on; "format" and
        // "encryption" are its own.
        const std::string name = key == "format" || key == "encryption" ? std::string{key} : "info:" + std::string{key};
        char value[1024]{};
        int length = -1;
        state_->run("metadata", [&] { length = fz_lookup_metadata(state_->context, state_->document, name.c_str(), value, sizeof value); });
        return length > 0 ? std::string{value} : std::string{};
    }
}
