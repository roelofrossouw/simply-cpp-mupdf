#pragma once

// Calling MuPDF from C++. MuPDF reports errors by longjmp out of fz_try (setjmp): nothing with a
// destructor may live inside a fz_try block, or be changed there and read in fz_catch. So the
// blocks only call MuPDF and keep raw pointers; a failure is thrown as sc::mupdf::error once the
// block is left. Only calls that are in MuPDF 1.19 (Ubuntu jammy) to 1.28 (Homebrew) are used.

#include "mupdf.h"

#include <mupdf/fitz.h>

#include <string>
#include <string_view>

namespace sc::mupdf::detail {
    // A caught error's message. MuPDF 1.24 and later also want it marked as dealt with, or they
    // print "UNHANDLED EXCEPTION!"; 1.19 and 1.23 don't have that.
    inline std::string caught(fz_context *context) {
        std::string message = fz_caught_message(context);
#if FZ_VERSION_MAJOR > 1 || FZ_VERSION_MINOR >= 24
        fz_ignore_error(context);
#endif
        return message;
    }

    // Runs MuPDF calls, throwing what they reported as "<what>: <MuPDF's message>".
    template<typename F>
    void call(fz_context *context, const std::string_view what, F &&calls) {
        std::string failure;
        bool failed = false;
        fz_try(context) {
            calls();
        }
        fz_catch(context) {
            failed = true;
            failure = caught(context);
        }
        if (failed) throw error{std::string{what} + ": " + failure};
    }
}
