# resources/polyfill.js — VERBATIM extraction of the fork's
# `features/plugins/runtime/js/JsBindings.kt` polyfill (fetch, Abort,
# base64, URL, CryptoJS/subtle, TextEncoder, cheerio, require, Array,
# Object, String sections + the SCRAPER_ID/SCRAPER_SETTINGS header).
#
# Regeneration: the sections are Kotlin `"""...""".trimIndent()` blocks
# interpolated as `${xxxPolyfill()}` into the header; re-run the
# extraction script and diff. The ONLY intentional deltas from the
# fork source are the two injection slots:
#   %1 = JSON-encoded scraper id, %2 = JSON-encoded settings object
# (QString::arg substitution; the JS contains no other %N sequences).
# Native functions referenced here (__native_fetch, __parse_url,
# __crypto_*, __cheerio_*) are bound by the C++ host bridges with
# identical names and string/hex transport semantics.
