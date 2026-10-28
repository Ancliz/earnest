/**
 * @file
 * @brief Whisper model ownership and isolated speech-to-text inference.
 */

#pragma once

#include "audio/audio.hpp"
#include <memory>
#include <stdexcept>
#include <string>
#include <whisper.h>

class WhisperTranscriber {
    std::unique_ptr<whisper_context, decltype(&whisper_free)> context{nullptr, whisper_free};
    whisper_full_params parameters;

public:
    /**
     * @brief Load a Whisper model and configure independent English utterance recognition.
     *
     * Uses greedy decoding without translation, prior-utterance context, timestamps,
     * or progress output. GPU execution is requested only when enabled and supported
     * by the linked Whisper build; temperature fallback retries are disabled.
     *
     * @param[in] model Nonempty filesystem path to a compatible Whisper GGML model.
     * @param[in] threads CPU inference worker count within 1..256.
     * @param[in] gpu Whether to request the available Whisper GPU backend.
     * @throws std::invalid_argument For an empty model path or invalid thread count.
     * @throws std::runtime_error If Whisper cannot initialize the model context.
     * @note Loads model resources immediately. Whisper may emit its own diagnostics;
     *       no command parsing, discovery, or device-control request occurs here.
     */
    WhisperTranscriber(const std::string& model, int threads, bool gpu)
        : parameters(whisper_full_default_params(WHISPER_SAMPLING_GREEDY)) {
        static_assert(Audio::sampleRate == WHISPER_SAMPLE_RATE);
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

    /**
     * @brief Convert one completed utterance into concatenated recognized text.
     *
     * Pads audio shorter than one second with silence, runs the configured Whisper
     * context, and concatenates all returned segment text in order. Contextual carryover
     * between utterances is disabled; the caller decides how to interpret the text.
     *
     * @param[in] audio Owned mono float32 samples at Audio::sampleRate; padding affects
     *                  only this copy or the storage explicitly moved by the caller.
     * @return Raw concatenated segment text, possibly empty or containing extra spaces.
     * @throws std::runtime_error If whisper_full reports inference failure.
     * @note Mutates the owned Whisper inference context. Calls on one instance must be
     *       serialized. Does not parse commands, query Home Assistant, or execute actions.
     */
    std::string transcribe(Audio::Samples audio) {
        // Short commands still need a minimum audio window for Whisper
        if(audio.size() < Audio::sampleRate) {
            audio.resize(Audio::sampleRate, 0);
        }

        if(whisper_full(context.get(), parameters, audio.data(), static_cast<int>(audio.size())) != 0) {
            throw std::runtime_error("Whisper transcription failed");
        }

        std::string text;

        for(int i = 0; i < whisper_full_n_segments(context.get()); ++i) {
            text += whisper_full_get_segment_text(context.get(), i);
        }

        return text;
    }
};
