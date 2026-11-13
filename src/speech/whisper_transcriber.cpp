/**
 * @file
 * @brief Whisper model loading and inference.
 */

#include "whisper_transcriber.hpp"
#include "log/log.hpp"

#include <spdlog/spdlog.h>
#include <stdexcept>

using Audio::sampleRate;
using std::string;

static_assert(sampleRate == WHISPER_SAMPLE_RATE);

namespace {
/// Segments whose text tokens average a lower probability than this are dropped as guesses.
constexpr float minConfidence = 0.4f;
} // namespace

WhisperTranscriber::WhisperTranscriber(const string& model, int threads, bool gpu)
    : parameters(whisper_full_default_params(WHISPER_SAMPLING_GREEDY)) {
    if(model.empty() || threads < 1 || threads > 256) {
        throw std::invalid_argument("Expected a model path and 1..256 transcription threads");
    }

    auto config = whisper_context_default_params();
    config.use_gpu = gpu;
    context.reset(whisper_init_from_file_with_params(model.c_str(), config));

    if(!context) {
        throw std::runtime_error("Failed to load Whisper model: " + model);
    }

    parameters.n_threads = threads;
    parameters.language = "en";
    parameters.translate = false;
    parameters.no_context = true;
    parameters.no_timestamps = true;
    parameters.print_realtime = false;
    parameters.print_progress = false;
    parameters.print_timestamps = false;
    parameters.print_special = false;
    parameters.suppress_blank = true;
    parameters.suppress_nst = true;
    parameters.temperature_inc = 0; // Avoid costly fallback retries for short commands
}

string WhisperTranscriber::transcribe(Audio::Samples utterance) {
    // Short commands still need a minimum audio window for Whisper
    if(utterance.size() < sampleRate) {
        utterance.resize(sampleRate, 0);
    }

    if(whisper_full(context.get(), parameters, utterance.data(), static_cast<int>(utterance.size())) != 0) {
        throw std::runtime_error("Whisper transcription failed");
    }

    string text;

    // Noise is often transcribed as a vocabulary word ("AC"); Whisper's low confidence gives it away
    // even when the text is short. Its no-speech probability is not used.
    for(int segment = 0; segment < whisper_full_n_segments(context.get()); ++segment) {
        const string words = whisper_full_get_segment_text(context.get(), segment);
        float total = 0;
        int tokens = 0;

        // Special tokens (timestamps, end of text) sort after the text tokens and are not words.
        for(int token = 0; token < whisper_full_n_tokens(context.get(), segment); ++token) {
            if(whisper_full_get_token_id(context.get(), segment, token) < whisper_token_eot(context.get())) {
                total += whisper_full_get_token_p(context.get(), segment, token);
                ++tokens;
            }
        }

        const auto confidence = tokens ? total / tokens : 0.0f;
        const bool speech = confidence >= minConfidence;
        Log::hear()->debug("Whisper segment \"{}\": confidence {:.2f}{}", words, confidence, speech ? "" : " (dropped)");

        if(speech) {
            text += words;
        }
    }

    return text;
}

void WhisperTranscriber::setVocabulary(const std::vector<string>& words) {
    prompt.clear();

    for(const auto& word : words) {
        prompt += (prompt.empty() ? "" : ", ") + word;
    }

    // Whisper treats the prompt as speech just before the utterance; ending the sentence stops it
    // assuming the last word was already said and skipping it in the transcript.
    if(!prompt.empty()) {
        prompt += ".";
    }

    parameters.initial_prompt = prompt.empty() ? nullptr : prompt.c_str();
}