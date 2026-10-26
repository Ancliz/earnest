#pragma once

#include "audio.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <portaudio.h>

class Microphone {
    PaStream* stream = nullptr;
    std::thread worker;
    std::atomic<bool> stopping{false};
    std::mutex audioMutex;
    std::condition_variable ready;
    std::deque<Audio::Samples> pending;
    PaError failure = paNoError;
    bool lostAudio = false;

    void capture();

public:
    Microphone();
    ~Microphone();
    Microphone(const Microphone&) = delete;
    Microphone& operator=(const Microphone&) = delete;
    bool read(Audio::Samples& block, bool& discontinuity);
};