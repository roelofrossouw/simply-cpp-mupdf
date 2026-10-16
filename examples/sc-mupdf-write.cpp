// Writes a PDF with MuPDF: the same page as sc-pdf's sc-pdf-demo (which writes with PDFHummus),
// call for call, so the two files can be compared. A heading, text measured to centre it, lines,
// shapes, SVG drawings (curves, transforms, an even-odd hole, a clip path), a half-transparent
// circle over the boxes, a link, and a rubber-band path placed in the bottom-right corner by its
// bounds(). Each line shows a call, as written, and what it returned. --help lists the options.

#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

#include <params.h>
#include <sc.h>

#include <mupdf_writer.h>

int main(int argc, char **argv) {
    // The command line: -h/--help comes with sc::params, which also reports mistakes and exits.
    const sc::params args{argc, argv, {
        {"name", "sc-mupdf-write"},
        {"description", "Writes sc-pdf-demo's page with MuPDF instead of PDFHummus, so the two PDFs can be compared."},
        {"options", {
            {"size", {{"default", "a4"}, {"choices", {"a3", "a4", "letter", "legal"}}, {"help", "the paper"}}},
            {"landscape", {{"type", "boolean"}, {"help", "turn the pages sideways"}}},
            {"svg", {{"value", "FILE"}, {"help", "an SVG file to draw on a page of its own"}}}}},
        {"positional", {
            {"output", {{"default", (std::filesystem::temp_directory_path() / "sc-mupdf-write.pdf").string()},
                        {"help", "the PDF to write"}}}}}}};

    try {
        const std::filesystem::path output = args.as<std::string>("output");
        const std::filesystem::path svg_file = args.as<std::string>("svg", "");
        const auto paper = sc::page_size_from(args.as<std::string>("size"));
        const auto turned = args.as<bool>("landscape") ? sc::orientation::landscape : sc::orientation::portrait;
        sc::console::title("simply-cpp mupdf: text, shapes, SVG drawings, a link and a corner path, written with MuPDF");
        sc::timer sw;

        // [readme]
        sc::console::heading("Starting the file");
        sc::mupdf::writer pdf{output, paper, turned};
        sc::console::step("sc::mupdf::writer pdf{output, paper, turned};   // " + output.string());
        SC_SHOW(pdf.page_width());  // points, 1/72 inch: --size and --landscape
        SC_SHOW(pdf.page_height());

        sc::console::heading("Text: sc::text{content, where its baseline starts, sc::font}");
        SC_STEP(pdf.draw(sc::text{"simply-cpp pdf", {50, 90}, sc::font::bold("sans-serif", 32)}));
        SC_STEP(pdf.draw(sc::text{"Text, shapes and SVG on MuPDF", {50, 120}, {"serif", 14, 400, true}}));
        sc::text centred{"This line is centred on the page", {0, 150}}; // the default font, sans-serif 12
        SC_SHOW(pdf.text_width(centred));
        centred.position.x((pdf.page_width() - pdf.text_width(centred)) / 2);
        SC_STEP(pdf.draw(centred, sc::color{0.2, 0.2, 0.6}));

        sc::console::heading("Lines and shapes: draw(geometry, appearance)");
        SC_STEP(pdf.draw(sc::line{{50, 165}, {pdf.page_width() - 50, 165}}, sc::stroke{sc::color::Black, 0.5}));
        const sc::rect box{50, 190, 230, 120};
        SC_STEP(pdf.draw(sc::rounded_rect{box, 12}, {sc::stroke{sc::color{0.16, 0.44, 0.59}, 2}, sc::color{0.85, 0.93, 0.98}})); // stroke, fill
        SC_STEP(pdf.draw(sc::text{"A rounded box", {70, 255}, {"sans-serif", 16}}));
        SC_STEP(pdf.draw(sc::circle{{110, 400}, 40}, {sc::stroke{sc::color::Black, 2}, sc::color{0.96, 0.83, 0.37}}));
        SC_STEP(pdf.draw(sc::polygon{{180, 440}, {230, 360}, {280, 440}}, sc::stroke{sc::color{0.93, 0.42, 0.3}, 3}));
        SC_STEP(pdf.draw(sc::rotated_rect{{380, 400}, {120, 50}, 20}, {sc::color::Black, sc::color{0.85, 0.93, 0.98}}));

        sc::console::heading("An SVG drawing, scaled to fit and centred");
        const std::string svg = R"(<svg xmlns="http://www.w3.org/2000/svg" width="320" height="200">)"
                                R"(<rect width="320" height="200" rx="16" fill="#2a6f97"/>)"
                                R"(<circle cx="160" cy="100" r="70" fill="#f4d35e"/>)"
                                R"(<path d="M120 130 Q160 170 200 130" stroke="#2a6f97" stroke-width="8" fill="none"/></svg>)";
        SC_STEP(pdf.draw(sc::drawing::from_svg(svg), sc::rect{315, 190, 230, 120})); // sc-graphics reads it into a drawing

        sc::console::heading("Transparency: a colour's alpha");
        // Over the two boxes, so what's behind shows through: filled half opaque, outlined three-quarters.
        SC_STEP(pdf.draw(sc::circle{{297.5, 250}, 75}, {sc::stroke{sc::color{0.75, 0.1, 0.1, 0.75}, 3}, sc::color{0.9, 0.2, 0.2, 0.5}}));

        sc::console::heading("A link");
        SC_STEP(pdf.draw(sc::text{"github.com/roelofrossouw/simply-cpp", {50, 490}}, sc::color::Blue));
        SC_STEP(pdf.add_link("https://github.com/roelofrossouw/simply-cpp", {50, 476, 220, 18}));

        sc::console::heading("More SVG: transforms, arcs with an even-odd hole, a clip path");
        const auto star = sc::drawing::from_svg(R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100">)svg"
                                                R"svg(<g transform="rotate(15 50 50)"><polygon fill="#e76f51" stroke="#264653" stroke-width="3")svg"
                                                R"svg( points="50,5 61,38 95,38 67,58 78,92 50,72 22,92 33,58 5,38 39,38"/></g></svg>)svg");
        const auto ring = sc::drawing::from_svg(R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100">)svg"
                                                R"svg(<path fill="#2a9d8f" fill-rule="evenodd" d="M5 50 A45 45 0 1 1 95 50 A45 45 0 1 1 5 50 Z)svg"
                                                R"svg( M25 50 A25 25 0 1 0 75 50 A25 25 0 1 0 25 50 Z"/></svg>)svg");
        const auto disc = sc::drawing::from_svg(R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100">)svg"
                                                R"svg(<defs><clipPath id="round"><circle cx="50" cy="50" r="45"/></clipPath></defs>)svg"
                                                R"svg(<rect clip-path="url(#round)" width="100" height="50" fill="#e9c46a"/>)svg"
                                                R"svg(<rect clip-path="url(#round)" y="50" width="100" height="50" fill="#264653"/></svg>)svg");
        double top = 530, left_edge = 50;
        if (top + 300 > pdf.page_height()) { // a shorter page (landscape, Letter) has no room left: the frames go on the next
            SC_STEP(pdf.new_page());
            top = 50;
        }
        for (const auto &[drawing, caption]: {std::pair{&star, "rotate(15)"}, std::pair{&ring, "evenodd"}, std::pair{&disc, "clip-path"}}) {
            const sc::rect frame{left_edge, top, 150, 150};
            pdf.draw(frame, sc::stroke{sc::color{0.8, 0.8, 0.8}, 0.5});
            pdf.draw(*drawing, frame - 10); // 10 points inside the frame
            pdf.draw(sc::text{caption, {left_edge, top + 168}, {"monospace", 10}});
            left_edge += 172.5;
        }
        sc::console::step("pdf.draw(star / ring / disc, frame - 10);   // three 150-point frames");

        sc::console::heading("A path in the bottom-right corner, placed by its bounds()");
        // A rubber band pinched round two pegs, tilted: its curves' control points reach well past
        // the curves themselves, so the box round them isn't the band's. bounds() is the band's own.
        sc::path band;
        band.move_to({0, 40})
            .cubic_to({0, -10}, {50, -10}, {60, 20})    // over the left peg
            .cubic_to({70, -10}, {120, -10}, {120, 40}) // over the right peg
            .cubic_to({120, 90}, {70, 90}, {60, 60})    // under it
            .cubic_to({50, 90}, {0, 90}, {0, 40})       // and back
            .close();
        band = sc::transform::rotate(-15).apply(band);
        SC_SHOW(band.bounds()); // the band's top, bottom and sides, not its control points'
        const sc::point corner{pdf.page_width() - 30, pdf.page_height() - 30};    // 30 points in from the corner
        band = sc::transform::translate(corner - band.bounds().right_bottom()).apply(band);
        SC_STEP(pdf.draw(band.bounds(), sc::stroke{sc::color{0.6, 0.6, 0.6}, 0.5})); // its bounds, touching it all round
        SC_STEP(pdf.draw(band, {sc::stroke{sc::color{0.55, 0.27, 0.07}, 2}, sc::color{0.96, 0.64, 0.38}}));

        sc::console::heading("Finishing the file");
        if (!svg_file.empty()) {
            SC_STEP(pdf.new_page());
            const auto drawing = sc::drawing::load_svg(svg_file);
            sc::console::step("const auto drawing = sc::drawing::load_svg(svg_file);   // " + svg_file.string());
            SC_SHOW(drawing.size());
            SC_STEP(pdf.draw(drawing, sc::rect{50, 50, pdf.page_width() - 100, pdf.page_height() - 100}));
        }
        SC_SHOW(pdf.page_count());
        SC_STEP(pdf.close());
        SC_SHOW(std::filesystem::file_size(output)); // bytes
        // [/readme]

        sc::console::output() << "\nThe result is " << output.string() << "\nAll of that took " << sw << ".\n";
    } catch (const std::exception &error) {
        std::cerr << "sc-mupdf-write: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
