// Writes a PDF with MuPDF: the same page as sc-pdf's sc-pdf-demo (which writes with PDFHummus),
// call for call, so the two files can be compared. A heading, text measured to centre it, lines,
// shapes, SVG drawings (curves, transforms, an even-odd hole, a clip path) and a link. Each line
// shows a call, as written, and what it returned. --help lists the options.

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
            {"svg", {{"value", "FILE"}, {"help", "an SVG file to draw on a second page"}}}}},
        {"positional", {
            {"output", {{"default", (std::filesystem::temp_directory_path() / "sc-mupdf-write.pdf").string()},
                        {"help", "the PDF to write"}}}}}}};

    try {
        const std::filesystem::path output = args.as<std::string>("output");
        const std::filesystem::path svg_file = args.as<std::string>("svg", "");
        sc::console::title("simply-cpp mupdf: text, shapes, SVG drawings and a link on an A4 page, written with MuPDF");
        sc::timer sw;

        // [readme]
        sc::console::heading("Starting the file");
        sc::mupdf::writer pdf{output};
        sc::console::step("sc::mupdf::writer pdf{output};   // " + output.string());
        SC_SHOW(pdf.page_width());  // points, 1/72 inch: A4 by default
        SC_SHOW(pdf.page_height());

        sc::console::heading("Text: sc::text{content, where its baseline starts, sc::font}");
        SC_STEP(pdf.draw(sc::text{"simply-cpp pdf", {50, 90}, sc::font::bold("sans-serif", 32)}));
        SC_STEP(pdf.draw(sc::text{"Text, shapes and SVG on MuPDF", {50, 120}, {"serif", 14, 400, true}}));
        sc::text centred{"This line is centred on the page", {0, 150}}; // the default font, sans-serif 12
        SC_SHOW(pdf.text_width(centred));
        centred.position.x((pdf.page_width() - pdf.text_width(centred)) / 2);
        SC_STEP(pdf.draw(centred, sc::color{0.2, 0.2, 0.6}));

        sc::console::heading("Lines and shapes: draw(geometry, stroke, fill, width)");
        SC_STEP(pdf.draw(sc::line{{50, 165}, {545, 165}}, sc::color::Black, 0.5));
        const sc::rect box{50, 190, 230, 120};
        SC_STEP(pdf.draw(box, sc::color{0.16, 0.44, 0.59}, sc::color{0.85, 0.93, 0.98}, 2, 12)); // stroke, fill, width, radius
        SC_STEP(pdf.draw(sc::text{"A rounded box", {70, 255}, {"sans-serif", 16}}));
        SC_STEP(pdf.draw(sc::circle{{110, 400}, 40}, sc::color::Black, sc::color{0.96, 0.83, 0.37}, 2));
        SC_STEP(pdf.draw(sc::polygon{{180, 440}, {230, 360}, {280, 440}}, sc::color{0.93, 0.42, 0.3}, sc::color::Transparent, 3));
        SC_STEP(pdf.draw(sc::rotated_rect{{380, 400}, {120, 50}, 20}, sc::color::Black, sc::color{0.85, 0.93, 0.98}));

        sc::console::heading("An SVG drawing, scaled to fit and centred");
        const std::string svg = R"(<svg xmlns="http://www.w3.org/2000/svg" width="320" height="200">)"
                                R"(<rect width="320" height="200" rx="16" fill="#2a6f97"/>)"
                                R"(<circle cx="160" cy="100" r="70" fill="#f4d35e"/>)"
                                R"(<path d="M120 130 Q160 170 200 130" stroke="#2a6f97" stroke-width="8" fill="none"/></svg>)";
        SC_STEP(pdf.draw(sc::svg{svg}, sc::rect{315, 190, 230, 120})); // sc-graphics reads it into paths

        sc::console::heading("More SVG: transforms, arcs with an even-odd hole, a clip path");
        const sc::svg star{R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100">)svg"
                           R"svg(<g transform="rotate(15 50 50)"><polygon fill="#e76f51" stroke="#264653" stroke-width="3")svg"
                           R"svg( points="50,5 61,38 95,38 67,58 78,92 50,72 22,92 33,58 5,38 39,38"/></g></svg>)svg"};
        const sc::svg ring{R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100">)svg"
                           R"svg(<path fill="#2a9d8f" fill-rule="evenodd" d="M5 50 A45 45 0 1 1 95 50 A45 45 0 1 1 5 50 Z)svg"
                           R"svg( M25 50 A25 25 0 1 0 75 50 A25 25 0 1 0 25 50 Z"/></svg>)svg"};
        const sc::svg disc{R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100">)svg"
                           R"svg(<defs><clipPath id="round"><circle cx="50" cy="50" r="45"/></clipPath></defs>)svg"
                           R"svg(<rect clip-path="url(#round)" width="100" height="50" fill="#e9c46a"/>)svg"
                           R"svg(<rect clip-path="url(#round)" y="50" width="100" height="50" fill="#264653"/></svg>)svg"};
        double left_edge = 50;
        for (const auto &[drawing, caption]: {std::pair{&star, "rotate(15)"}, std::pair{&ring, "evenodd"}, std::pair{&disc, "clip-path"}}) {
            const sc::rect frame{left_edge, 530, 150, 150};
            pdf.draw(frame, sc::color{0.8, 0.8, 0.8}, sc::color::Transparent, 0.5);
            pdf.draw(*drawing, frame - 10); // 10 points inside the frame
            pdf.draw(sc::text{caption, {left_edge, 698}, {"monospace", 10}});
            left_edge += 172.5;
        }
        sc::console::step("pdf.draw(star / ring / disc, frame - 10);   // three 150-point frames");

        sc::console::heading("A link and finishing the file");
        SC_STEP(pdf.draw(sc::text{"github.com/roelofrossouw/simply-cpp", {50, 490}}, sc::color::Blue));
        SC_STEP(pdf.add_link("https://github.com/roelofrossouw/simply-cpp", {50, 476, 220, 18}));
        if (!svg_file.empty()) {
            SC_STEP(pdf.new_page());
            const auto drawing = sc::svg::load(svg_file);
            sc::console::step("const auto drawing = sc::svg::load(svg_file);   // " + svg_file.string());
            SC_SHOW(drawing.elements().size());
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
