## TESTS ##
set(SC_TEST_LINK_LIBRARIES sc-mupdf)
# examples/sample_pdf.h: the PDF the tests read, made on the spot.
set(SC_TEST_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/examples")

add_sc_test(mupdf LABELS unit)
add_sc_test(writer LABELS unit)
