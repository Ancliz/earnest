/**
 * @file
 * @brief Whisper speech recognition for short English commands.
 */

#pragma once

#include "transcriber.hpp"
#include <memory>
#include <string>
#include <vector>
#include <whisper.h>

using std::string;

/**
 * @brief Transcribes utterances with a local Whisper GGML model.
 */
class WhisperTranscriber final : public Speech::Transcriber {
    
    /// Loaded model and inference state.
    std::unique_ptr<whisper_context, decltype(&whisper_free)> context{nullptr, whisper_free};

    /// Greedy, context-free decoding parameters shared by every utterance.
    whisper_full_params parameters;

    /// Initial prompt text; parameters.initial_prompt points into it.
    string prompt;

public:
    /**
     * @brief Load a model and configure English, context-free greedy decoding.
     * @param[in] model Path to a Whisper GGML model.
     * @param[in] threads CPU inference threads, 1..256.
     * @param[in] gpu Whether to request the GPU backend when the Whisper build supports one.
     * @throws std::invalid_argument For an empty path or invalid thread count.
     * @throws std::runtime_error If the model cannot be loaded.
     */
    WhisperTranscriber(const string& model, int threads, bool gpu);

    /**
     * @brief Transcribe one utterance, padding it to Whisper's one-second minimum.
     *
     * Segments whose words Whisper is unsure of are left out, so noise yields an empty transcript
     * rather than a guessed vocabulary word.
     * @throws std::runtime_error If inference fails.
     */
    string transcribe(Audio::Samples utterance) override;

    /**
     * @brief Bias recognition towards words that commands use, such as device names.
     *
     * Whisper reads the words as text preceding each utterance, so it favours their spellings
     * ("Q990" over "queue"). Words placed here can be dropped from the start of a transcript as
     * if already said, so never include a wake word. An empty list clears the bias.
     */
    void setVocabulary(const std::vector<string>& words) override;
};