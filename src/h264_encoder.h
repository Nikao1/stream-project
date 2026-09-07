#pragma once

#include <windows.h>

#include <cstdint>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>

class H264Encoder
{
public:
    H264Encoder();
    ~H264Encoder();

    bool Start(
        uint32_t width,
        uint32_t height,
        uint32_t fps,
        uint32_t bitrateMbps = 8
    );

    bool EncodeFrame(
        const void* bgraData,
        uint32_t dataSize
    );

    bool GetEncodedData(
        std::vector<unsigned char>& output
    );

    void Stop();

    bool IsRunning() const;

private:
    bool StartProcess();

    void ReaderThread();

    uint32_t m_width;
    uint32_t m_height;
    uint32_t m_fps;
    uint32_t m_bitrateMbps;

    HANDLE m_process;
    HANDLE m_stdinWrite;
    HANDLE m_stdoutRead;

    std::thread m_readerThread;

    std::mutex m_outputMutex;

    std::vector<unsigned char> m_encodedData;

    std::atomic<bool> m_running;
};