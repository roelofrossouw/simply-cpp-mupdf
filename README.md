# simply-cpp-mupdf

C++20 PDF reader on [MuPDF](https://mupdf.com/): a document's pages, each
page's text as blocks of lines with their positions and fonts, and pages
drawn as PNG images. MuPDF also opens XPS, EPUB, CBZ and images.

**License: AGPL-3.0**, because MuPDF is (or a commercial license from
[Artifex](https://artifex.com/licensing/)). A program that links this module
falls under the AGPL. The rest of simply-cpp is Apache-2.0, and
[sc-pdf](https://github.com/roelofrossouw/simply-cpp-pdf), the PDF *writer*,
doesn't use MuPDF, so it stays Apache-2.0.

**Documentation:** the [simply-cpp wiki](https://github.com/roelofrossouw/simply-cpp/wiki) has [getting started](https://github.com/roelofrossouw/simply-cpp/wiki/Getting-Started) for the whole suite.

## Usage

```cmake
find_package(sc-mupdf CONFIG REQUIRED)

target_link_libraries(myapp PRIVATE sc::sc-mupdf-shared)
```

```cpp
#include <mupdf.h>

const sc::mupdf::document doc{"letter.pdf"};       // or document::from_memory(bytes); a password if needed
for (const auto page: doc.pages()) {
    for (const auto block: page.blocks()) {         // a page's text, read once
        for (const auto line: block) {
            std::cout << line.bbox() << ' ' << line.font_size() << (line.bold() ? " bold " : " ") << line.text() << '\n';
        }
    }
}
const std::string text = doc.page(0).text();        // blocks joined by "\n\n", lines by "\n"
const std::string png = doc.page(0).png(2);         // 2 pixels per point: 144 dpi
```

Positions are `sc::rect`s in points (1/72 inch) from the page's top-left
corner, y down. Pages, blocks and lines are cheap handles held by value, so
the same range can be iterated more than once, and nested. A page keeps its
document open. Blocks and lines point into their page's text, the object
`page.blocks()` returns, so keep that while using them; a range-for over it
does. A block is `block_type::text`, `image` or `other` (newer MuPDFs also
report vector graphics and structure). Only text blocks have lines.

`line.bold()` and `italic()` go by most of the line's characters, using the
font's flags or, since many PDFs embed bold faces without the flag, its name.
`font_size()` is the line's most common character size, so a superscript
doesn't change it. `text()` is empty for a line that doesn't decode to text
(a font without a usable character map), so a scanned or oddly encoded page
reads as empty rather than as garbage.

Failures throw `sc::mupdf::error` with MuPDF's message: a file that can't be
opened, a wrong password, a damaged page. `document.page(n)` throws
`std::out_of_range`. A document isn't thread-safe: use one per thread.

MuPDF is found through Homebrew (`mupdf`) on macOS and Ubuntu's
`libmupdf-dev` on Linux: 1.19 on jammy, 1.23 on noble, 1.27 on resolute.
Only MuPDF calls that all of them have are used.

## Demo

`sc-mupdf-demo` reads a small two-page PDF it makes itself, or the file
given, and shows each call with what it returned: the pages, every line with
its box, size and style, a page's text, and a page as a PNG (written to the
temporary directory). It is installed with the runtime package
(`simply-cpp-mupdf`):

<!-- sc-example: examples/sc-mupdf-demo.cpp -->
```cpp
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
```
<!-- /sc-example -->
