/**
 * @file
 * @brief Whisper model loading and inference.
 */

#include "whisper_transcriber.hpp"

#include <stdexcept>

using Audio::sampleRate;
using std::string;

static_assert(sampleRate == WHISPER_SAMPLE_RATE);

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

    for(int i = 0; i < whisper_full_n_segments(context.get()); ++i) {
        text += whisper_full_get_segment_text(context.get(), i);
    }

    return text;
}

void WhisperTranscriber::setVocabulary(const std::vector<string>& words) {
    prompt.clear();

    for(const auto& word : words) {
        prompt += (prompt.empty() ? "" : ", ") + word;
    }

    parameters.initial_prompt = prompt.empty() ? nullptr : prompt.c_str();
}