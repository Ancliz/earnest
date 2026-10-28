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
} // namespace Grammar