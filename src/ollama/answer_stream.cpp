/**
 * @file
 * @brief Pulling finished sentences out of an answer while the model is still writing it.
 */

#include "answer_stream.hpp"
#include "parser/speech_text.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <regex>

using nlohmann::json;
using std::regex;
using std::string;

namespace {
/**
 * @brief Whether text ends in a word whose full stop doesn't end the sentence.
 */
bool endsInAbbreviation(const string& text) {
    static const regex abbreviation(R"(\b(?:mr|mrs|ms|dr|st|prof|vs|e\.g|i\.e)$)", regex::icase);
    return std::regex_search(text, abbreviation);
}
} // namespace

void AnswerStream::finishSentence(const string& raw) {
    const auto decoded = json::parse("\"" + raw + "\"", nullptr, false);
    auto sentence = decoded.is_string() ? decoded.get<string>() : raw;
    std::replace_if(sentence.begin(), sentence.end(), [](char c) { return c == '\n' || c == '\t' || c == '\r'; }, ' ');
    sentence = Grammar::trim(sentence);

    if(sentence.empty()) {
        return;
    }

    whole += (whole.empty() ? "" : " ") + sentence;

    if(receiver) {
        receiver(sentence);
    }
}

void AnswerStream::feed(const string& piece) {
    // End marks, then any closing quote or bracket, then a space or an escaped line break. Still JSON-escaped here.
    static const regex boundary(R"([.!?]+(?:\\"|'|\))*(?:\s|\\n))");

    for(const char c : piece) {
        // The key's two quotes come first, and the third opens the text.
        if(closed || quotes < 3) {
            quotes += c == '"';
        } else if(!escaped && c == '"') {
            closed = true;
        } else {
            escaped = !escaped && c == '\\';
            pending += c;
        }
    }

    std::smatch found;
    auto from = pending.cbegin();

    while(std::regex_search(from, pending.cend(), found, boundary)) {
        const auto end = found[0].second;

        if(endsInAbbreviation(string(pending.cbegin(), found[0].first))) {
            from = end;
            continue;
        }

        finishSentence(string(pending.cbegin(), end));
        pending.erase(pending.cbegin(), end);
        from = pending.cbegin();
    }
}

void AnswerStream::finish() {
    if(closed) {
        finishSentence(pending);
    }

    pending.clear();
}