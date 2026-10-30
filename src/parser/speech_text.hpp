/**
 * @file
 * @brief Transcript normalization and strict spoken-number parsing.
 */

#pragma once

#include <optional>
#include <string>

namespace Grammar {
using std::string;

/**
 * @brief Remove leading and trailing spaces; other whitespace is kept.
 */
string trim(const string& text);

/**
 * @brief Normalize recognized speech for matching.
 *
 * Lowercases, expands "&" to "and", turns hyphens, whitespace, commas, "!" and "?" into single
 * spaces, and drops trailing dots while keeping decimal points and entity IDs intact.
 */
string normalizeText(const string& text);

/**
 * @brief Canonical spelling of a device name or alias.
 * @return normalizeText() without a leading "the " or a trailing possessive "'s".
 */
string normalizeName(const string& name);

/**
 * @brief Whether text ends in " percent", " per cent" or "%".
 */
bool hasPercentSuffix(const string& text);

/**
 * @brief Parse a spoken amount as a 0..1 level.
 *
 * Whole numbers are percentages, bare decimals up to one are levels, and a percent suffix
 * always means a percentage. Accepts digits, number words up to ninety-nine, "point" decimals,
 * and half/full/max/min. Anything else, including trailing words, is rejected so that another
 * command is never swallowed.
 *
 * @return Level within 0..1, or nullopt.
 */
std::optional<double> parseLevel(string text);

/**
 * @brief Whether a transcript holds no words, only sounds.
 *
 * True when everything left after removing Whisper's sound labels ("(laughs)", "[MUSIC]",
 * "*sighs*") is laughter, a hum or a hesitation: "hahaha", "mhm", "mm hmm", "hmm", "uh", "um",
 * "er", "ah", "oh" and drawn-out spellings of them. A transcript with any real word is speech.
 */
bool isNonSpeech(const string& text);

/**
 * @brief Remove a fuzzily matched wake phrase from the start of a transcript.
 *
 * One leading greeting ("hey", "ok", ...) may precede the phrase, and the phrase may be misheard
 * by up to half its letters (at most three) or split or merged across words, so "hey jarvis" and "travis,"
 * both match "jarvis".
 *
 * @param[in] text Transcript.
 * @param[in] wakePhrase Phrase that must open a command; empty disables the check.
 * @return Normalized text after the phrase, possibly empty, or nullopt when it does not open with
 *         the phrase. With no phrase, the text unchanged.
 */
std::optional<string> stripWakeWord(const string& text, const string& wakePhrase);
} // namespace Grammar