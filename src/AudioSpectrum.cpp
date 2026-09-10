#include "AudioSpectrum.h"

// INITGUID 让下面的 DEFINE_GUID 在本编译单元内生成实体，不依赖 uuid 库
#define INITGUID
#include <windows.h>

#include <audioclient.h>
#include <mmdeviceapi.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#ifndef WAVE_FORMAT_IEEE_FLOAT
#define WAVE_FORMAT_IEEE_FLOAT 0x0003
#endif
#ifndef WAVE_FORMAT_EXTENSIBLE
#define WAVE_FORMAT_EXTENSIBLE 0xFFFE
#endif

namespace {

// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT（避免引入 ksmedia.h 带来的额外依赖）
const GUID kSubtypeIeeeFloat = {0x00000003,
                                0x0000,
                                0x0010,
                                {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

constexpr int kFftSize = 1024;
/** 一帧缓冲时长（100ns 单位）：20 ms，够快也不至于空转。 */
constexpr REFERENCE_TIME kBufferDuration = 200000;

/** 原地迭代式基 2 FFT。 */
void fft(std::vector<std::complex<double>>& data)
{
    const int n = static_cast<int>(data.size());
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(data[i], data[j]);
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double angle = -2.0 * M_PI / len;
        const std::complex<double> wn(std::cos(angle), std::sin(angle));
        for (int i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (int k = 0; k < len / 2; ++k) {
                const std::complex<double> u = data[i + k];
                const std::complex<double> v = data[i + k + len / 2] * w;
                data[i + k] = u + v;
                data[i + k + len / 2] = u - v;
                w *= wn;
            }
        }
    }
}

/** 汉宁窗，抑制频谱泄漏。 */
double window(int i, int n)
{
    return 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (n - 1)));
}

} // namespace

AudioSpectrum::AudioSpectrum(QObject* parent)
    : QObject(parent)
{
    m_bands.fill(0.0f, m_bandCount);
}

AudioSpectrum::~AudioSpectrum()
{
    stop();
}

bool AudioSpectrum::start(int bandCount)
{
    if (m_running.load()) {
        return true;
    }
    m_bandCount = qMax(16, bandCount);
    m_stop.store(false);
    m_running.store(true);
    m_thread = std::thread(&AudioSpectrum::runLoop, this, m_bandCount);
    return true;
}

void AudioSpectrum::stop()
{
    if (!m_running.load()) {
        return;
    }
    m_stop.store(true);
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_running.store(false);
}

QVector<float> AudioSpectrum::bands() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_bands;
}

void AudioSpectrum::publish(const QVector<float>& values)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_bands = values;
    }
    // Qt::QueuedConnection 是默认的（跨线程），界面线程里安全消费
    emit frameReady(values);
}

void AudioSpectrum::runLoop(int bandCount)
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioCaptureClient* capture = nullptr;
    WAVEFORMATEX* format = nullptr;

    auto cleanup = [&]() {
        if (capture) {
            capture->Release();
        }
        if (client) {
            client->Release();
        }
        if (device) {
            device->Release();
        }
        if (enumerator) {
            enumerator->Release();
        }
        if (format) {
            CoTaskMemFree(format);
        }
        CoUninitialize();
    };

    HRESULT hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                                  IID_IMMDeviceEnumerator, reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr)) {
        cleanup();
        m_running.store(false);
        return;
    }
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr)) {
        cleanup();
        m_running.store(false);
        return;
    }
    hr = device->Activate(IID_IAudioClient, CLSCTX_ALL, nullptr,
                          reinterpret_cast<void**>(&client));
    if (FAILED(hr)) {
        cleanup();
        m_running.store(false);
        return;
    }
    hr = client->GetMixFormat(&format);
    if (FAILED(hr)) {
        cleanup();
        m_running.store(false);
        return;
    }
    // 回环采集 = 共享模式 + LOOPBACK 标志（不是独立的 share mode）
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                            kBufferDuration, 0, format, nullptr);
    if (FAILED(hr)) {
        cleanup();
        m_running.store(false);
        return;
    }
    hr = client->GetService(IID_IAudioCaptureClient, reinterpret_cast<void**>(&capture));
    if (FAILED(hr)) {
        cleanup();
        m_running.store(false);
        return;
    }
    hr = client->Start();
    if (FAILED(hr)) {
        cleanup();
        m_running.store(false);
        return;
    }

    const bool isFloat = (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
                         || (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE
                             && reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format)->SubFormat
                                    == kSubtypeIeeeFloat);
    const int channels = qMax(1, static_cast<int>(format->nChannels));

    std::vector<double> samples;
    samples.reserve(kFftSize * 2);
    std::vector<std::complex<double>> spectrum(kFftSize);
    QVector<float> previous;
    previous.fill(0.0f, bandCount);

    while (!m_stop.load()) {
        UINT32 packetSize = 0;
        if (FAILED(capture->GetNextPacketSize(&packetSize))) {
            break;
        }
        while (packetSize != 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            UINT64 position = 0;
            UINT64 qpc = 0;
            if (FAILED(capture->GetBuffer(&data, &frames, &flags, &position, &qpc))) {
                break;
            }
            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data && frames > 0) {
                for (UINT32 i = 0; i < frames; ++i) {
                    double mono = 0.0;
                    if (isFloat) {
                        const float* p = reinterpret_cast<const float*>(data) + i * channels;
                        for (int c = 0; c < channels; ++c) {
                            mono += p[c];
                        }
                        mono /= channels;
                    } else {
                        const short* p = reinterpret_cast<const short*>(data) + i * channels;
                        for (int c = 0; c < channels; ++c) {
                            mono += p[c] / 32768.0;
                        }
                        mono /= channels;
                    }
                    samples.push_back(mono);
                }
            }
            capture->ReleaseBuffer(frames);
            if (FAILED(capture->GetNextPacketSize(&packetSize))) {
                packetSize = 0;
                break;
            }
        }

        while (static_cast<int>(samples.size()) >= kFftSize) {
            for (int i = 0; i < kFftSize; ++i) {
                spectrum[i] = std::complex<double>(samples[static_cast<size_t>(i)] * window(i, kFftSize), 0.0);
            }
            fft(spectrum);

            // 频段按对数分组：低频窄、高频宽，视觉上更接近听感
            QVector<float> values;
            values.fill(0.0f, bandCount);
            const int bins = kFftSize / 2;
            int startBin = 1;
            for (int b = 0; b < bandCount; ++b) {
                const double ratio = std::pow(static_cast<double>(bins) / startBin,
                                              1.0 / (bandCount - b));
                int endBin = static_cast<int>(startBin * ratio);
                if (endBin <= startBin) {
                    endBin = startBin + 1;
                }
                endBin = std::min(endBin, bins);
                double magnitude = 0.0;
                for (int i = startBin; i < endBin; ++i) {
                    magnitude = std::max(magnitude, std::abs(spectrum[static_cast<size_t>(i)]));
                }
                // 归一化：时域能量 / (N/2)，再放大并限制到 0..1
                const double normalized = magnitude / (kFftSize / 4.0);
                const double db = std::log10(1.0 + normalized * 9.0); // 0..1
                float value = static_cast<float>(std::clamp(db, 0.0, 1.0));
                // 峰值回落：让条子有惯性地掉下来，而不是抖成噪点
                value = std::max(value, previous.value(b, 0.0f) * 0.86f);
                values[b] = value;
                startBin = endBin;
                if (startBin >= bins) {
                    for (int k = b + 1; k < bandCount; ++k) {
                        values[k] = 0.0f;
                    }
                    break;
                }
            }
            previous = values;
            publish(values);
            samples.erase(samples.begin(), samples.begin() + kFftSize);
        }

        Sleep(12);
    }

    client->Stop();
    cleanup();
    m_running.store(false);
}
