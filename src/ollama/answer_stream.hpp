/**
 * @file
 * @brief Pulling finished sentences out of an answer while the model is still writing it.
 */

#pragma once

#include <functional>
#include <string>

/**
 * @brief Reads the reply {"answer": "..."} piece by piece and hands over each sentence as it ends.
 */
class AnswerStream {

    /// Told each finished sentence, decoded and trimmed; may be empty.
    std::function<void(const std::string&)> receiver;

    /// Quotes seen so far, up to the one opening the answer's text.
    int quotes = 0;

    /// Whether the previous character was an unescaped backslash.
    bool escaped = false;

    /// Whether the closing quote has been seen.
    bool closed = false;

    /// Text of the unfinished sentence, still JSON-escaped.
    std::string pending;

    /// Every sentence handed over so far.
    std::string whole;

    /**
     * @brief Decode one sentence, record it and hand it over.
     */
    void finishSentence(const std::string& raw);

public:
    /**
     * @brief Start reading.
     * @param[in] receiver Told each finished sentence; may be empty.
     */
    explicit AnswerStream(std::function<void(const std::string&)> receiver) : receiver(std::move(receiver)) {}

    /**
     * @brief Take the next piece of the reply, which may split anything anywhere.
     */
    void feed(const std::string& piece);

    /**
     * @brief The reply is over: say the last sentence if the answer closed, or drop it if it was cut off.
     */
    void finish();

    /**
     * @brief The sentences handed over so far, joined.
     */
    const std::string& text() const { return whole; }
};