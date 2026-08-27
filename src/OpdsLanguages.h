#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

/**
 * The languages the OPDS browser can filter on.
 *
 * A fixed table with a bitmask rather than free text: the mask is one uint32_t
 * in settings, the filter screen is a fixed list of rows, and neither has to
 * deal with a user typing a language code on an e-ink keyboard.
 *
 * Codes are primary subtags, matched against the normalised OpdsEntry::language
 * ("en-US" arrives as "en"). Order is by how much of the catalogue each covers,
 * so the common choices sit at the top of the screen.
 */
struct OpdsLanguage {
  const char* code;
  const char* label;  // Endonym: a reader looking for Spanish books scans for
                      // "Español", not "Spanish".
};

inline constexpr OpdsLanguage OPDS_LANGUAGES[] = {
    {"en", "English"}, {"es", "Spanish"},    {"fr", "French"},   {"de", "German"},
    {"it", "Italian"}, {"pt", "Portuguese"}, {"ru", "Russian"},  {"pl", "Polish"},
    {"sv", "Swedish"}, {"zh", "Chinese"},    {"ja", "Japanese"},
};

inline constexpr size_t OPDS_LANGUAGE_COUNT = sizeof(OPDS_LANGUAGES) / sizeof(OPDS_LANGUAGES[0]);

// Persisted as a comma-separated code list ("en,es") rather than a number:
// SettingInfo has no uint32_t variant, and a string round-trips through the
// existing String persistence while staying readable in settings.json.
inline constexpr const char* OPDS_LANGUAGES_DEFAULT = "en";

/** All languages selected -- the "no filtering" state. */
inline constexpr uint32_t opdsAllLanguagesMask() {
  return (OPDS_LANGUAGE_COUNT >= 32) ? 0xFFFFFFFFu : ((1u << OPDS_LANGUAGE_COUNT) - 1u);
}

/** "en,es" -> bits for English and Spanish. Unknown codes are ignored. */
inline uint32_t opdsLanguageMaskFromCodes(const char* codes) {
  uint32_t mask = 0;
  if (!codes) return mask;
  std::string current;
  for (const char* p = codes;; ++p) {
    if (*p != '\0' && *p != ',' && *p != ' ') {
      current += *p;
      continue;
    }
    if (!current.empty()) {
      for (size_t i = 0; i < OPDS_LANGUAGE_COUNT; ++i) {
        if (current == OPDS_LANGUAGES[i].code) {
          mask |= (1u << i);
          break;
        }
      }
      current.clear();
    }
    if (*p == '\0') break;
  }
  return mask;
}

/** Inverse of opdsLanguageMaskFromCodes, for writing the setting back. */
inline void opdsLanguageCodesFromMask(const uint32_t mask, char* out, const size_t outSize) {
  if (!out || outSize == 0) return;
  std::string joined;
  for (size_t i = 0; i < OPDS_LANGUAGE_COUNT; ++i) {
    if (!(mask & (1u << i))) continue;
    if (!joined.empty()) joined += ',';
    joined += OPDS_LANGUAGES[i].code;
  }
  const size_t copied = joined.size() < outSize - 1 ? joined.size() : outSize - 1;
  for (size_t i = 0; i < copied; ++i) out[i] = joined[i];
  out[copied] = '\0';
}

/**
 * Whether an entry survives the filter.
 *
 * Two deliberate "keep" cases, both of which would otherwise empty a catalogue:
 * an entry with no language at all (most feeds tag nothing), and an entry in a
 * language the table does not list (filtering those out would hide books the
 * user never chose to exclude).
 */
inline bool opdsLanguageAllowed(const std::string& language, const uint32_t mask) {
  if (language.empty()) return true;
  if (mask == 0) return true;  // nothing selected behaves as no filter, not as "hide all"

  bool known = false;
  for (size_t i = 0; i < OPDS_LANGUAGE_COUNT; ++i) {
    if (language == OPDS_LANGUAGES[i].code) {
      known = true;
      if (mask & (1u << i)) return true;
    }
  }
  return !known;
}
