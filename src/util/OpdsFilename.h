#pragma once
#include <cstdint>
#include <string>

// On-disk filename format for books downloaded from an OPDS server. Stored as a
// uint8_t in CrossPointSettings; cast to this enum at the call sites. `Count` is
// the number of selectable formats (used to cycle the setting in the UI).
enum class OpdsFilenameFormat : uint8_t {
  AuthorTitle = 0,  // "Author - Title.epub" (default; matches legacy behaviour)
  TitleAuthor = 1,  // "Title - Author.epub"
  TitleOnly = 2,    // "Title.epub"
  Count = 3,
};

// Composes and sanitizes the on-disk filename (including the ".epub" extension)
// for a downloaded OPDS book, according to `format`. When the author is empty,
// every format collapses to just the sanitized title. Pure: no I/O, no globals.
std::string opdsBookFilename(const std::string& author, const std::string& title, OpdsFilenameFormat format);

// The full destination path for a downloaded book: `folder` (nullptr or "" for
// the SD root), a '/', and opdsBookFilename() of the rest.
//
// One function rather than a concatenation at the call site, so the screen
// that reports what landed and the code that writes it cannot drift apart.
// `folder` is taken as a char* because the setting already is one -- no copy.
std::string opdsBookPath(const char* folder, const std::string& author, const std::string& title,
                         OpdsFilenameFormat format);

// The two halves of a destination path, for the screen that has to tell the
// reader what landed on the card. Both take the SAME string that was handed to
// HttpDownloader::downloadToFile(), which writes to exactly that path and
// renames nothing -- so what these return is the name the card really holds,
// not a second guess at it composed from the catalog's title again.
//
// The split is safe because the components went through
// StringUtils::sanitizeFilename first, which turns '/' into '_': a title like
// "Faust I/II" cannot introduce a separator that moves the split.
std::string opdsPathBasename(const std::string& path);

// The directory half, "" for the SD root. Note this is the folder actually
// USED, including the root fallback taken when the configured folder could not
// be created -- which is exactly the case where a reader looking in the folder
// they configured would never find the book.
std::string opdsPathFolder(const std::string& path);
