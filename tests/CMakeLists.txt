## TESTS ##
set(SC_TEST_LINK_LIBRARIES sc-mupdf)
# examples/sample_pdf.h: the PDF the tests read, made on the spot.
set(SC_TEST_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/examples")

add_sc_test(mupdf LABELS unit)
add_sc_test(writer LABELS unit)
# Which JPEG writer MuPDF has (CMakeLists.txt): the JPEG test expects images or an error.
foreach (jpeg SC_MUPDF_JPEG_5 SC_MUPDF_JPEG_4)
    if (${jpeg})
        target_compile_definitions(test-mupdf PRIVATE ${jpeg})
    endif ()
endforeach ()
