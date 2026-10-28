/**
 * @file
 * @brief Whisper speech recognition for short English commands.
 */

#pragma once

#include "transcriber.hpp"
#include <memory>
#include <whisper.h>

/**
 * @brief Transcribes utterances with a local Whisper GGML model.
 */
class WhisperTranscriber final : public Speech::Transcriber {
    
    /// Loaded model and inference state.
    std::unique_ptr<whisper_context, decltype(&whisper_free)> context{nullptr, whisper_free};

    /// Greedy, context-free decoding parameters shared by every utterance.
    whisper_full_params parameters;

public:
    /**
     * @brief Load a model and configure English, context-free greedy decoding.
     * @param[in] model Path to a Whisper GGML model.
     * @param[in] threads CPU inference threads, 1..256.
     * @param[in] gpu Whether to request the GPU backend when the Whisper build supports one.
     * @throws std::invalid_argument For an empty path or invalid thread count.
     * @throws std::runtime_error If the model cannot be loaded.
     */
    WhisperTranscriber(const std::string& model, int threads, bool gpu);

    /**
     * @brief Transcribe one utterance, padding it to Whisper's one-second minimum.
     * @throws std::runtime_error If inference fails.
     */
    std::string transcribe(Audio::Samples utterance) override;
};