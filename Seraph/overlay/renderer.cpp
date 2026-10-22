// Windows headers define the `min` / `max` preprocessor macros
// which clash with `std::max` / `std::min` used by the
// MenuWeather particle engine. Defining NOMINMAX FIRST ensures
// every windows-family header (transitively pulled in by the
// project headers on lines below) respects the opt-out.
#define NOMINMAX
#include "renderer.h"
#include "ragesubtabs.h"
#include "../features/pf_diag.h"
#include "../features/stealth/byovd_client.h"
#include "ui.h"
#include "fonts_ex.h"
#include "../features/chams.h"
#include "../rbx/configs/configs.h"
#include "../rbx/globals/RobloxVersion.h"
#include "../features/desync.h"
#include "../features/ragebot.h"
#include "../features/aimbot.h"
#include "../features/aimline.h"
#include "../features/playerfilter.h"
#include "../features/playerfling.h"
#include "../features/aimline.h"
#include "../features/aimline.h"
#include "../features/orbit.h"
#include "../features/PlayerAvatars.h"
#include "../features/server_browser.h"
#include "../features/modelchanger.h"
#include "../features/diagnostics.h"
#include "../features/boxfill_window.h"
#include "../features/fovfill_window.h"
#include "animation.h"
#include "explorer/explorer_window.h"
#include "explorer/lua_editor.h"
#include "executor/executor.h"
#include "injector.h"
#include <shobjidl.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ole32.lib")
#include "../seraph_log.h"

// Windows Media Control (WinRT) for Spotify integration
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/base.h>
#pragma comment(lib, "windowsapp.lib")

// Image decoding for Spotify album art (declarations only; implementation lives in stb_image_impl)
#include "../features/stb_image.h"

// WASAPI Loopback Capture for real audio visualization
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "avrt.lib")

#include <random>
#include <algorithm>
#include <cctype>
#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>
#include <future>
#include <cstdint>
#include <vector>
#include <cmath>
#include <complex>

// KissFFT - simple embedded radix-2 FFT (no external deps, power-of-two sizes)
#define kiss_fft_scalar float

// Shaders
#include "shaders/ShaderBackgrounds.h"

struct kiss_fft_state {
    int nfft;
    int inverse;
    std::vector<kiss_fft_scalar> cosTbl;  // cos(2*pi*i/n), i in [0, n)
    std::vector<kiss_fft_scalar> sinTbl;  // sin(2*pi*i/n)
    std::vector<kiss_fft_scalar> work;    // reusable FFT workspace (2*nfft)
};

typedef struct kiss_fft_state* kiss_fft_cfg;

static kiss_fft_cfg kiss_fft_alloc(int nfft, int inverse_fft, void* mem, size_t* lenmem)
{
    (void)mem; // we always self-allocate
    if (nfft <= 0 || (nfft & (nfft - 1)) != 0) // power of two only
    {
        if (lenmem) *lenmem = 0;
        return nullptr;
    }

    kiss_fft_state* st = new (std::nothrow) kiss_fft_state();
    if (!st) return nullptr;
    st->nfft = nfft;
    st->inverse = inverse_fft;
    try
    {
        st->cosTbl.resize((size_t)nfft);
        st->sinTbl.resize((size_t)nfft);
        st->work.resize((size_t)nfft * 2);
    }
    catch (...)
    {
        delete st;
        return nullptr;
    }
    if (lenmem) *lenmem = sizeof(kiss_fft_state);

    for (int i = 0; i < nfft; ++i)
    {
        float ang = 2.0f * 3.14159265358979323846f * i / nfft;
        st->cosTbl[i] = cosf(ang);
        st->sinTbl[i] = sinf(ang);
    }
    return st;
}

static void kiss_fft(kiss_fft_cfg cfg, const kiss_fft_scalar* fin, kiss_fft_scalar* fout)
{
    const int n = cfg->nfft;
    // Bit-reverse copy into workspace (interleaved real/imag)
    int levels = 0;
    for (int t = n; t > 1; t >>= 1) levels++;

    kiss_fft_scalar* w = cfg->work.data();
    for (int i = 0; i < n; ++i)
    {
        int j = 0, x = i;
        for (int b = 0; b < levels; ++b) { j = (j << 1) | (x & 1); x >>= 1; }
        w[2 * j]     = fin[2 * i];
        w[2 * j + 1] = fin[2 * i + 1];
    }

    const kiss_fft_scalar* ct = cfg->cosTbl.data();
    const kiss_fft_scalar* st = cfg->sinTbl.data();
    const float sign = cfg->inverse ? 1.0f : -1.0f;

    // Iterative radix-2 decimation-in-time butterflies
    for (int len = 2; len <= n; len <<= 1)
    {
        const int half = len >> 1;
        const int step = n / len;
        for (int i = 0; i < n; i += len)
        {
            for (int k = 0; k < half; ++k)
            {
                const int idx = k * step;
                const float wr = ct[idx];
                const float wi = sign * st[idx];

                const int ai = 2 * (i + k);
                const int bi = 2 * (i + half + k);

                const float tr = w[bi] * wr - w[bi + 1] * wi;
                const float ti = w[bi] * wi + w[bi + 1] * wr;

                const float ar = w[ai];
                const float ai_ = w[ai + 1];

                w[ai]     = ar + tr;
                w[ai + 1] = ai_ + ti;
                w[bi]     = ar - tr;
                w[bi + 1] = ai_ - ti;
            }
        }
    }

    if (cfg->inverse)
    {
        const float inv = 1.0f / n;
        for (int i = 0; i < 2 * n; ++i) fout[i] = w[i] * inv;
    }
    else
    {
        for (int i = 0; i < 2 * n; ++i) fout[i] = w[i];
    }
}

static void kiss_fft_free(kiss_fft_cfg cfg)
{
    delete cfg;
}

// ============================================================
// WASAPI Loopback Capture for Real Audio Visualization
// ============================================================
namespace AudioCapture
{
    static constexpr int SAMPLE_RATE = 48000;
    static constexpr int FRAME_SIZE = 1024;        // FFT size (power of 2)
    static constexpr int HOP_SIZE = 512;           // Overlap for smoother FFT
    static constexpr int NUM_CHANNELS = 2;         // Stereo loopback
    
    static IMMDeviceEnumerator* pEnumerator = nullptr;
    static IMMDevice* pDevice = nullptr;
    static IAudioClient* pAudioClient = nullptr;
    static IAudioCaptureClient* pCaptureClient = nullptr;
    static WAVEFORMATEX* pWaveFormat = nullptr;
    static UINT32 bufferFrameCount = 0;
    
    static std::thread captureThread;
    static std::atomic<bool> captureRunning{false};
    static std::mutex fftMutex;
    static std::vector<float> audioBuffer;
    static std::vector<float> windowedBuffer(FRAME_SIZE);   // Hann window, kept pristine
    static std::vector<float> fftSamples(FRAME_SIZE);       // windowed frame sent to the FFT
    static std::vector<float> fftInput(FRAME_SIZE * 2);  // Real + Imag
    static std::vector<float> fftOutput(FRAME_SIZE * 2);
    static kiss_fft_cfg fftCfg = nullptr;
    static std::vector<float> latestFFT(64, 0.0f);  // 64 bins for visualizer
    static std::atomic<bool> captureInitialized{ false };
    static float lastFFTTime = 0.0f;
    static float peakHold = 1e-6f;                 // smoothed peak for dynamics
    static bool isFloatFormat = true;              // default mix format is IEEE float
    static int captureChannels = 2;
    static std::string lastError = "idle";

    static const std::string& GetLastError() { return lastError; }

    static std::string HexStr(HRESULT hr)
    {
        char buf[16];
        sprintf_s(buf, "0x%08X", (unsigned int)hr);
        return std::string(buf);
    }
    
    // Hann window for FFT
    static void GenerateHannWindow(std::vector<float>& window, int size) {
        window.resize(size);
        for (int i = 0; i < size; ++i) {
            window[i] = 0.5f * (1.0f - cosf(2.0f * 3.14159265358979323846f * i / (size - 1)));
        }
    }
    
    static void ReleaseWASAPI();  // fwd

    static bool InitializeWASAPI()
    {
        HRESULT hr;
        
        // Clean any leftover state from a previous failed attempt so retries
        // start fresh (COM objects may be half-created).
        ReleaseWASAPI();
        
        // Create device enumerator
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                              __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
        if (FAILED(hr)) { lastError = "CoCreateInstance MMDeviceEnumerator failed " + HexStr(hr); return false; }
        
        // Get default audio endpoint (render device for loopback). Fall back to the
        // multimedia role if the console role isn't present.
        static const ERole kRoles[] = { eConsole, eMultimedia, eCommunications };
        for (ERole role : kRoles)
        {
            hr = pEnumerator->GetDefaultAudioEndpoint(eRender, role, &pDevice);
            if (SUCCEEDED(hr))
            {
                LPWSTR devId = nullptr;
                std::wstring devName;
                if (SUCCEEDED(pDevice->GetId(&devId)))
                {
                    devName = devId ? devId : L""; 
                    CoTaskMemFree(devId);
                }
                lastError = "endpoint acquired (role " + std::to_string((int)role) + ")";
                break;
            }
        }
        if (FAILED(hr)) { lastError = "GetDefaultAudioEndpoint failed " + HexStr(hr); ReleaseWASAPI(); return false; }
        
        // Activate audio client
        hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, nullptr, (void**)&pAudioClient);
        if (FAILED(hr)) { lastError = "Activate IAudioClient failed " + HexStr(hr); ReleaseWASAPI(); return false; }
        
        // Get mix format
        hr = pAudioClient->GetMixFormat(&pWaveFormat);
        if (FAILED(hr)) { lastError = "GetMixFormat failed " + HexStr(hr); return false; }
        
        // Mix format can be float32 (typical) or 16-bit PCM; detect it so the
        // capture loop decodes the loopback buffer with the right sample type.
        captureChannels = (pWaveFormat->nChannels > 0) ? pWaveFormat->nChannels : 2;
        if (pWaveFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        {
            WAVEFORMATEXTENSIBLE* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pWaveFormat);
            isFloatFormat = (ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
        }
        else
        {
            isFloatFormat = (pWaveFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
        }
        
        // Initialize audio client for loopback capture
        REFERENCE_TIME hnsRequestedDuration = 10000000; // 1 second buffer
        hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                      AUDCLNT_STREAMFLAGS_LOOPBACK,
                                      hnsRequestedDuration,
                                      0,
                                      pWaveFormat,
                                      nullptr);
        if (FAILED(hr)) return false;
        
        // Get buffer size
        hr = pAudioClient->GetBufferSize(&bufferFrameCount);
        if (FAILED(hr)) { lastError = "GetBufferSize failed " + HexStr(hr); ReleaseWASAPI(); return false; }
        
        // Get capture client
        hr = pAudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&pCaptureClient);
        if (FAILED(hr)) { lastError = "GetService IAudioCaptureClient failed " + HexStr(hr); ReleaseWASAPI(); return false; }
        
        // Prepare buffers
        audioBuffer.resize(bufferFrameCount * NUM_CHANNELS);
        GenerateHannWindow(windowedBuffer, FRAME_SIZE);
        
        // Initialize FFT
        size_t fftWorkSize = 0;
        fftCfg = kiss_fft_alloc(FRAME_SIZE, 0, nullptr, &fftWorkSize);
        if (!fftCfg) { lastError = "kiss_fft_alloc failed"; ReleaseWASAPI(); return false; }
        
        captureInitialized.store(true);
        lastError = "ready";
        return true;
    }
    
    static void ReleaseWASAPI()
    {
        // COM/model teardown ONLY - never joins the capture thread, so it is
        // safe to call from inside CaptureLoop (a self-join would deadlock).
        if (pCaptureClient) { pCaptureClient->Release(); pCaptureClient = nullptr; }
        if (pAudioClient) { pAudioClient->Release(); pAudioClient = nullptr; }
        if (pDevice) { pDevice->Release(); pDevice = nullptr; }
        if (pEnumerator) { pEnumerator->Release(); pEnumerator = nullptr; }
        if (pWaveFormat) { CoTaskMemFree(pWaveFormat); pWaveFormat = nullptr; }
        if (fftCfg) { kiss_fft_free(fftCfg); fftCfg = nullptr; }
        captureInitialized.store(false);
    }

    static void CleanupWASAPI()
    {
        captureRunning = false;
        if (captureThread.joinable())
            captureThread.join();
        ReleaseWASAPI();
    }
    
    static void CaptureLoop()
    {
        // Needs a COM apartment on THIS thread before touching the WASAPI
        // objects created during InitializeWASAPI (CO_E_NOTINITIALIZED
        // otherwise).
        HRESULT coInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(coInit))
        {
            lastError = "CoInitializeEx failed " + HexStr(coInit);
            captureRunning = false;
            return;
        }

        HRESULT hr;
        UINT32 packetLength = 0;
        BYTE* pData = nullptr;
        DWORD flags = 0;
        UINT32 numFramesAvailable = 0;
        UINT64 devicePosition = 0, qpcPosition = 0;
        
        hr = pAudioClient->Start();
        if (FAILED(hr))
        {
            lastError = "IAudioClient Start failed " + HexStr(hr);
            captureRunning = false;
            ReleaseWASAPI();
            CoUninitialize();
            return;
        }
        lastError = "ready";
        
        std::vector<float> monoBuffer(FRAME_SIZE);
        int monoWritePos = 0;
        
        while (captureRunning)
        {
            hr = pCaptureClient->GetNextPacketSize(&packetLength);
            if (FAILED(hr)) { Sleep(1); continue; }
            
            while (packetLength > 0 && captureRunning)
            {
                hr = pCaptureClient->GetBuffer(&pData, &numFramesAvailable, &flags, &devicePosition, &qpcPosition);
                if (FAILED(hr)) break;
                
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                {
                    // Silence - fill with zeros and advance so the FFT pipeline
                    // keeps running and the visualizer decays instead of freezing.
                    UINT32 nZeros = std::min(numFramesAvailable, (UINT32)(FRAME_SIZE - monoWritePos));
                    for (UINT32 i = 0; i < nZeros; ++i)
                        monoBuffer[monoWritePos++] = 0.0f;
                }
                else
                {
                    // Convert to mono. Mix format is usually IEEE float32, but
                    // protect against 16-bit PCM endpoints too.
                    UINT32 nFrames = std::min(numFramesAvailable, (UINT32)(FRAME_SIZE - monoWritePos));
                    if (isFloatFormat)
                    {
                        const float* samples = reinterpret_cast<const float*>(pData);
                        for (UINT32 i = 0; i < nFrames; ++i)
                        {
                            float sum = 0.0f;
                            for (int c = 0; c < captureChannels; ++c)
                                sum += samples[i * captureChannels + c];
                            monoBuffer[monoWritePos++] = sum / captureChannels;
                        }
                    }
                    else
                    {
                        const short* samples = reinterpret_cast<const short*>(pData);
                        for (UINT32 i = 0; i < nFrames; ++i)
                        {
                            float sum = 0.0f;
                            for (int c = 0; c < captureChannels; ++c)
                                sum += samples[i * captureChannels + c];
                            monoBuffer[monoWritePos++] = sum / captureChannels / 32768.0f;
                        }
                    }
                }
                
                hr = pCaptureClient->ReleaseBuffer(numFramesAvailable);
                if (FAILED(hr)) break;
                
                hr = pCaptureClient->GetNextPacketSize(&packetLength);
                if (FAILED(hr)) break;
                
                // Process FFT when we have enough samples
                if (monoWritePos >= FRAME_SIZE)
                {
                    // Apply Hann window into the dedicated FFT frame buffer so
                    // the stored window itself is never overwritten.
                    for (int i = 0; i < FRAME_SIZE; ++i)
                        fftSamples[i] = monoBuffer[i] * windowedBuffer[i];
                    
                    // Prepare FFT input (real part only, imag = 0)
                    for (int i = 0; i < FRAME_SIZE; ++i)
                    {
                        fftInput[2*i] = fftSamples[i];
                        fftInput[2*i + 1] = 0.0f;
                    }
                    
                    // Run FFT
                    kiss_fft(fftCfg, fftInput.data(), fftOutput.data());
                    
                    // Compute magnitude spectrum (first FRAME_SIZE/2 bins)
                    int numBins = FRAME_SIZE / 2;
                    std::vector<float> magnitudes(numBins);
                    float maxMag = 0.0f;
                    
                    for (int i = 0; i < numBins; ++i)
                    {
                        float real = fftOutput[2*i];
                        float imag = fftOutput[2*i + 1];
                        float mag = sqrtf(real * real + imag * imag);
                        magnitudes[i] = mag;
                        if (mag > maxMag) maxMag = mag;
                    }
                    
                    // Slowly-decaying peak makes the bars react to loudness
                    // instead of being pinned to full scale by every frame's max.
                    peakHold = std::max(maxMag, peakHold * 0.985f);
                    float masterScale = (peakHold > 1e-6f) ? 0.68f / peakHold : 0.0f;
                    
                    // Normalize and downsample to 64 bins with log-frequency spacing
                    {
                        std::lock_guard<std::mutex> lock(fftMutex);
                        for (int i = 0; i < 64; ++i)
                        {
                            // Log-frequency mapping for better visual distribution
                            float logIdx = powf(2.0f, (float)i / 64.0f * log2f(numBins));
                            int binIdx = (int)logIdx;
                            binIdx = std::min(binIdx, numBins - 1);
                            
                            float val = ImClamp(magnitudes[binIdx] * masterScale, 0.0f, 1.0f);
                            
                            // Smooth with previous frame
                            latestFFT[i] = latestFFT[i] * 0.3f + val * 0.7f;
                        }
                        lastFFTTime = 0.0f; // Will be updated by render loop
                    }
                    
                    // Shift buffer for overlap (hop size)
                    int remaining = monoWritePos - HOP_SIZE;
                    if (remaining > 0)
                        std::copy(monoBuffer.begin() + HOP_SIZE, monoBuffer.begin() + monoWritePos, monoBuffer.begin());
                    monoWritePos = remaining;
                }
            }
            
            Sleep(1); // Small sleep to prevent busy waiting
        }
        
        pAudioClient->Stop();
        
        CoUninitialize();
    }
    
    static bool StartCapture()
    {
        if (captureRunning) return true;
        if (!captureInitialized)
        {
            if (!InitializeWASAPI()) return false;
        }
        
        captureRunning = true;
        try
        {
            captureThread = std::thread(CaptureLoop);
        }
        catch (...)
        {
            captureRunning = false;
            lastError = "capture thread spawn failed";
            CleanupWASAPI();
            return false;
        }
        return true;
    }
    
    static void StopCapture()
    {
        captureRunning = false;
        if (captureThread.joinable())
            captureThread.join();
    }
    
    static bool IsCaptureActive() { return captureRunning && captureInitialized; }

    // ── System volume control via IAudioEndpointVolume (render thread) ──────
    static IMMDeviceEnumerator* g_volEnum = nullptr;
    static IAudioEndpointVolume*  g_vol     = nullptr;
    static bool g_volComInit = false;

    static bool EnsureVolumeInterface()
    {
        if (g_vol) return true;
        if (!g_volComInit)
        {
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            g_volComInit = true;
        }
        if (!g_volEnum)
            CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                             CLSCTX_INPROC_SERVER,
                             __uuidof(IMMDeviceEnumerator),
                             (void**)&g_volEnum);
        if (!g_volEnum) return false;

        IMMDevice* dev = nullptr;
        if (FAILED(g_volEnum->GetDefaultAudioEndpoint(eRender, eConsole, &dev)))
            return false;
        HRESULT hr = dev->Activate(__uuidof(IAudioEndpointVolume),
                                   CLSCTX_INPROC_SERVER, nullptr,
                                   (void**)&g_vol);
        dev->Release();
        return SUCCEEDED(hr) && g_vol != nullptr;
    }

    static float GetSystemVolume()
    {
        if (!EnsureVolumeInterface()) return -1.0f;
        float v = 0.0f;
        if (SUCCEEDED(g_vol->GetMasterVolumeLevelScalar(&v)))
            return ImClamp(v, 0.0f, 1.0f);
        return -1.0f;
    }

    static void SetSystemVolume(float frac)
    {
        if (!EnsureVolumeInterface()) return;
        g_vol->SetMasterVolumeLevelScalar(ImClamp(frac, 0.0f, 1.0f), nullptr);
    }
    
    static void GetFFTData(std::vector<float>& out, int size = 64)
    {
        std::lock_guard<std::mutex> lock(fftMutex);
        out.resize(size);
        int copySize = std::min(size, (int)latestFFT.size());
        for (int i = 0; i < copySize; ++i)
            out[i] = latestFFT[i];
    }
    
    static void Update(float dt)
    {
        lastFFTTime += dt;
    }
}

// Spotify Visualizer - Windows Media Control Integration
namespace SpotifyVisualizer
{
    using namespace winrt::Windows::Media::Control;
    using namespace winrt::Windows::Foundation;
    using namespace winrt::Windows::Storage::Streams;
    using namespace winrt::Windows::Graphics::Imaging;
    using namespace winrt::Windows::Storage;

    struct TrackInfo
    {
        std::string title = "Unknown Track";
        std::string artist = "Unknown Artist";
        std::string album = "Unknown Album";
        bool isPlaying = false;
        float progress = 0.0f;
        float duration = 0.0f;
    };

    // Render-thread only: mirrored from the worker snapshot every frame.
    static TrackInfo currentTrack;
    static std::vector<float> fftData(64, 0.0f);
    static float visualizationTime = 0.0f;

    enum class VisualizerMode
    {
        RadialBars,
        Spectrum,
        Waveform,
        Particles
    };

    static VisualizerMode currentMode = VisualizerMode::RadialBars;

    // Album art (D3D SRV) is device-owned; created + released only on the
    // render thread that owns g_pd3dDevice.
    static ImTextureID albumArtTexture = nullptr;
    static bool albumArtLoaded = false;

    // Pixel dimensions of albumArtTexture, captured at upload so the
    // miniplayer can cover-crop non-square art into a wide card via UVs.
    static int albumArtPxW = 0;
    static int albumArtPxH = 0;

    // Screen-space rect of the miniplayer card from the most recent frame,
    // plus the hotzone state owned by UpdateMiniplayerClickThrough. Both are
    // read/written only inside renderer.cpp (same translation unit).
    static ImVec2 g_mpRectMin(0.0f, 0.0f);
    static ImVec2 g_mpRectMax(0.0f, 0.0f);
    static bool g_mpRectValid = false;
    static bool g_mpHotActive = false;

    // ---------------------------------------------------------------------
    // Worker side: every SMTC/WASAPI call lives on a dedicated MTA worker
    // thread so nothing can ever block the present loop. The render thread
    // reads a locked snapshot + published album-art pixels only.
    // ---------------------------------------------------------------------
    struct SharedSnapshot
    {
        std::string title = "Unknown Track";
        std::string artist = "Unknown Artist";
        std::string album = "Unknown Album";
        bool isPlaying = false;
        float progress = 0.0f;
        float duration = 0.0f;
    };

    enum class SmtcStatus
    {
        Starting = 0,
        Connecting = 1,
        Connected = 2,
        NoSession = 3,
        Failed = 4
    };

    static GlobalSystemMediaTransportControlsSessionManager sessionManager = nullptr; // worker only
    static GlobalSystemMediaTransportControlsSession currentSession = nullptr;       // worker only
    static std::string lastArtKey;                                                    // worker only

    static std::mutex snapshotMutex;
    static SharedSnapshot snapshot;
    static std::vector<unsigned char> artPixels;
    static int artW = 0, artH = 0;
    static std::atomic<uint64_t> artVersion{ 0 };
    static std::atomic<int> statusInt{ static_cast<int>(SmtcStatus::Starting) };

    enum class ControlCommand { None, PlayPause, Next, Prev, Seek };
    static std::atomic<ControlCommand> pendingControl{ ControlCommand::None };
    static std::atomic<float> pendingSeek{ 0.0f };

    static std::thread smtcThread;
    static std::mutex threadStartMutex;
    static std::atomic<bool> smtcStop{ false };
    static std::atomic<bool> smtcRunning{ false };

    // Wait on a WinRT async op with a hard timeout, so a wedged SMTC/COM
    // service can stall a worker tick but never the UI.
    template <typename AsyncOp>
    static bool WaitOp(AsyncOp op, std::chrono::milliseconds timeout)
    {
        auto status = op.Status();
        if (status == AsyncStatus::Completed || status == AsyncStatus::Error || status == AsyncStatus::Canceled)
            return true;

        auto sp = std::make_shared<std::promise<void>>();
        auto fut = sp->get_future();
        op.Completed([sp](auto&&, AsyncStatus) {
            try { sp->set_value(); } catch (...) {}
        });
        return fut.wait_for(timeout) == std::future_status::ready;
    }

    static void TryInitManager()
    {
        try
        {
            auto op = GlobalSystemMediaTransportControlsSessionManager::RequestAsync();
            if (!WaitOp(op, std::chrono::seconds(3)))
            {
                statusInt.store(static_cast<int>(SmtcStatus::Failed));
                return;
            }
            sessionManager = op.GetResults();
            statusInt.store(static_cast<int>(SmtcStatus::Connecting));
        }
        catch (...)
        {
            statusInt.store(static_cast<int>(SmtcStatus::Failed));
        }
    }

    static bool FindSpotifySession()
    {
        if (!sessionManager) return false;
        try
        {
            auto sessions = sessionManager.GetSessions();
            uint32_t size = sessions.Size();

            for (uint32_t i = 0; i < size; ++i)
            {
                auto session = sessions.GetAt(i);
                if (!session) continue;

                auto sourceAppUserModelId = session.SourceAppUserModelId();
                std::wstring appId(sourceAppUserModelId.c_str());

                if (appId.find(L"Spotify") != std::wstring::npos ||
                    appId.find(L"spotify") != std::wstring::npos)
                {
                    currentSession = session;
                    return true;
                }

                try
                {
                    auto propsOp = session.TryGetMediaPropertiesAsync();
                    if (!WaitOp(propsOp, std::chrono::seconds(1))) continue;
                    auto props = propsOp.GetResults();
                    if (props)
                    {
                        auto title = winrt::to_string(props.Title());
                        auto artist = winrt::to_string(props.Artist());
                        if (title.find("Spotify") != std::string::npos ||
                            artist.find("Spotify") != std::string::npos)
                        {
                            currentSession = session;
                            return true;
                        }
                    }
                }
                catch (...)
                {
                }
            }
        }
        catch (...)
        {
            return false;
        }
        return false;
    }
    
    // Decode the SMTC thumbnail and publish RGBA pixels to the render thread
    // (which does the D3D upload). Runs on the worker only.
    bool LoadTrackThumbnail(const GlobalSystemMediaTransportControlsSessionMediaProperties& props)
    {
        try
        {
            auto thumbnail = props.Thumbnail();
            if (!thumbnail) return false;

            auto openOp = thumbnail.OpenReadAsync();
            if (!WaitOp(openOp, std::chrono::seconds(2))) return false;
            auto stream = openOp.GetResults();
            if (!stream) return false;

            const uint64_t sz = stream.Size();
            if (sz == 0 || sz > 32ULL * 1024ULL * 1024ULL)
                return false;

            auto input = stream.GetInputStreamAt(0);
            Buffer buffer(static_cast<uint32_t>(sz));
            auto readOp = input.ReadAsync(buffer, static_cast<uint32_t>(sz),
                                          InputStreamOptions::ReadAhead);
            if (!WaitOp(readOp, std::chrono::seconds(2))) return false;

            const uint32_t bytesRead = buffer.Length();
            if (bytesRead == 0)
                return false;

            int w = 0, h = 0, ch = 0;
            unsigned char* px = stbi_load_from_memory(buffer.data(), static_cast<int>(bytesRead), &w, &h, &ch, 4);
            if (!px)
                return false;

            // Downscale oversized covers so the SRV stays cheap (<=512px).
            std::vector<unsigned char> scaled;
            if (w > 512 || h > 512)
            {
                const float scale = std::min(512.0f / w, 512.0f / h);
                const int nw = std::max(1, static_cast<int>(w * scale));
                const int nh = std::max(1, static_cast<int>(h * scale));
                scaled.resize(static_cast<size_t>(nw) * nh * 4);
                for (int y = 0; y < nh; ++y)
                {
                    const int sy = std::min(h - 1, static_cast<int>(y / scale));
                    for (int x = 0; x < nw; ++x)
                    {
                        const int sx = std::min(w - 1, static_cast<int>(x / scale));
                        const unsigned char* s = px + (static_cast<size_t>(sy) * w + sx) * 4;
                        unsigned char* d = scaled.data() + (static_cast<size_t>(y) * nw + x) * 4;
                        d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
                    }
                }
                w = nw; h = nh;
            }

            {
                std::lock_guard<std::mutex> lock(snapshotMutex);
                if (scaled.empty())
                    artPixels.assign(px, px + static_cast<size_t>(w) * h * 4);
                else
                    artPixels.swap(scaled);
                artW = w; artH = h;
            }
            artVersion.fetch_add(1, std::memory_order_release);

            stbi_image_free(px);
            return true;
        }
        catch (...)
        {
        }
        return false;
    }

    // Refresh the shared snapshot from the live media session. Runs on the
    // worker only; the render thread reads `snapshot`/`artPixels` under the
    // mutex.
    void UpdateTrackInfoWorker()
    {
        if (!currentSession) return;

        try
        {
            SharedSnapshot next;

            auto playbackInfo = currentSession.GetPlaybackInfo();
            auto playbackStatus = playbackInfo.PlaybackStatus();
            next.isPlaying = (playbackStatus == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing);

            auto timelineProps = currentSession.GetTimelineProperties();
            if (timelineProps)
            {
                auto duration = timelineProps.EndTime() - timelineProps.StartTime();
                long long durTicks = duration.count();
                if (durTicks > 0)
                {
                    float dur = static_cast<float>(durTicks) / 10000000.0f;
                    float pos = static_cast<float>(timelineProps.Position().count()) / 10000000.0f;
                    pos = std::max(0.0f, std::min(pos, dur));
                    next.duration = dur;
                    next.progress = dur > 0.0f ? pos / dur : 0.0f;
                }
            }

            auto mediaOp = currentSession.TryGetMediaPropertiesAsync();
            if (!WaitOp(mediaOp, std::chrono::milliseconds(800)))
            {
                {
                    std::lock_guard<std::mutex> lock(snapshotMutex);
                    snapshot = next;
                }
                return;
            }
            auto mediaProps = mediaOp.GetResults();
            if (!mediaProps)
            {
                {
                    std::lock_guard<std::mutex> lock(snapshotMutex);
                    snapshot = next;
                }
                return;
            }

            std::string title = winrt::to_string(mediaProps.Title());
            std::string artist = winrt::to_string(mediaProps.Artist());
            std::string album = winrt::to_string(mediaProps.AlbumTitle());
            if (album.empty())
                album = winrt::to_string(mediaProps.AlbumArtist());
            if (album.empty())
                album = "Unknown Album";
            if (title.empty()) title = "Unknown Track";
            if (artist.empty()) artist = "Unknown Artist";
            next.title = title;
            next.artist = artist;
            next.album = album;

            {
                std::lock_guard<std::mutex> lock(snapshotMutex);
                snapshot = next;
            }

            const std::string artKey = title + "|" + artist + "|" + album;
            if (artKey != lastArtKey)
            {
                lastArtKey = artKey;
                LoadTrackThumbnail(mediaProps);
            }
        }
        catch (...)
        {
            // Session vanished mid-poll; drop it and let the loop re-search.
            currentSession = nullptr;
            statusInt.store(static_cast<int>(SmtcStatus::NoSession));
        }
    }

    void ProcessControlCommand()
    {
        ControlCommand cmd = pendingControl.exchange(ControlCommand::None);
        if (cmd == ControlCommand::None || !currentSession) return;

        try
        {
            switch (cmd)
            {
                case ControlCommand::PlayPause:
                {
                    auto playbackInfo = currentSession.GetPlaybackInfo();
                    bool playing = (playbackInfo.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing);
                    if (playing) currentSession.TryPauseAsync();
                    else currentSession.TryPlayAsync();
                    break;
                }
                case ControlCommand::Next:
                    currentSession.TrySkipNextAsync();
                    break;
                case ControlCommand::Prev:
                    currentSession.TrySkipPreviousAsync();
                    break;
                case ControlCommand::Seek:
                {
                    SharedSnapshot s;
                    {
                        std::lock_guard<std::mutex> lock(snapshotMutex);
                        s = snapshot;
                    }
                    if (s.duration > 0.0f)
                    {
                        float frac = std::clamp(pendingSeek.load(std::memory_order_relaxed), 0.0f, 1.0f);
                        long long target = static_cast<long long>(s.duration * frac * 10000000.0f);
                        currentSession.TryChangePlaybackPositionAsync(target);
                    }
                    break;
                }
                default:
                    break;
            }
        }
        catch (...)
        {
        }
    }

    // Poll global media keybinds (works even when menu is closed)
    void PollGlobalMediaKeys()
    {
        auto checkKey = [](int vk, int toggleType, bool& toggled) -> bool
        {
            if (vk == 0) return false;
            if (toggleType == 2) return true; // Always on
            if (toggleType == 1) return (GetAsyncKeyState(vk) & 1) != 0; // Toggle - detect press
            return (GetAsyncKeyState(vk) & 0x8000) != 0; // Hold
        };

        static bool prevPlayPause = false, prevNext = false, prevPrev = false, prevStop = false;
        
        bool pp = checkKey(Options::Misc::MediaPlayPauseKey, Options::Misc::MediaToggleType, Options::Misc::MediaToggled);
        bool nx = checkKey(Options::Misc::MediaNextKey, Options::Misc::MediaToggleType, Options::Misc::MediaToggled);
        bool pr = checkKey(Options::Misc::MediaPrevKey, Options::Misc::MediaToggleType, Options::Misc::MediaToggled);
        bool st = checkKey(Options::Misc::MediaStopKey, Options::Misc::MediaToggleType, Options::Misc::MediaToggled);

        // Detect edge for toggle mode
        if (Options::Misc::MediaToggleType == 1)
        {
            if (pp && !prevPlayPause) pendingControl.store(ControlCommand::PlayPause);
            if (nx && !prevNext) pendingControl.store(ControlCommand::Next);
            if (pr && !prevPrev) pendingControl.store(ControlCommand::Prev);
            if (st && !prevStop) 
            {
                if (currentSession) 
                {
                    try { currentSession.TryPauseAsync(); } catch (...) {}
                }
            }
        }
        else // Hold or Always
        {
            if (pp) pendingControl.store(ControlCommand::PlayPause);
            if (nx) pendingControl.store(ControlCommand::Next);
            if (pr) pendingControl.store(ControlCommand::Prev);
            if (st) 
            {
                if (currentSession) 
                {
                    try { currentSession.TryPauseAsync(); } catch (...) {}
                }
            }
        }

        prevPlayPause = pp;
        prevNext = nx;
        prevPrev = pr;
        prevStop = st;
    }

    // Starts WASAPI loopback capture, retrying every few seconds until it
    // succeeds so a transient failure (device busy, COM not warmed up, no render
    // endpoint yet) recovers automatically instead of sticking the visualizer on
    // the simulated path forever.
    void AttemptAudioCapture()
    {
        if (AudioCapture::IsCaptureActive())
            return;

        auto now = std::chrono::steady_clock::now();
        static auto lastAttempt = now - std::chrono::seconds(10);
        if (now - lastAttempt < std::chrono::seconds(3))
            return;
        lastAttempt = now;

        bool ok = AudioCapture::StartCapture();
        static std::string lastReportedError;
        const std::string curErr = AudioCapture::GetLastError();
        if (ok)
        {
            if (lastReportedError != "active")
            {
                Executor::ConsolePush("[visualizer] WASAPI loopback capture started");
                lastReportedError = "active";
            }
        }
        else if (curErr != lastReportedError)
        {
            Executor::ConsolePush("[visualizer] capture failed: " + curErr);
            lastReportedError = curErr;
        }
    }

    void SmtcWorker()
    {
        try { winrt::init_apartment(winrt::apartment_type::multi_threaded); }
        catch (...) {}

        auto lastManagerTry = std::chrono::steady_clock::now() - std::chrono::seconds(5);
        auto lastFindTry = lastManagerTry;

        while (!smtcStop.load())
        {
            // Poll global media keybinds every frame so they work even without an active session
            PollGlobalMediaKeys();

            const auto now = std::chrono::steady_clock::now();

            if (!sessionManager)
            {
                if (now - lastManagerTry >= std::chrono::seconds(3))
                {
                    lastManagerTry = now;
                    TryInitManager();
                }
            }
            else if (!currentSession)
            {
                if (now - lastFindTry >= std::chrono::milliseconds(500))
                {
                    lastFindTry = now;
                    if (FindSpotifySession())
                        statusInt.store(static_cast<int>(SmtcStatus::Connected));
                    else
                        statusInt.store(static_cast<int>(SmtcStatus::NoSession));
                }
            }
            else
            {
                statusInt.store(static_cast<int>(SmtcStatus::Connected));
                ProcessControlCommand();
                UpdateTrackInfoWorker();

                SharedSnapshot s;
                {
                    std::lock_guard<std::mutex> lock(snapshotMutex);
                    s = snapshot;
                }
                if (s.isPlaying)
                    AttemptAudioCapture();
            }

            Sleep(250);
        }
    }

    void EnsureWorker()
    {
        std::lock_guard<std::mutex> lock(threadStartMutex);
        if (smtcRunning.load() || smtcStop.load())
            return;
        smtcStop.store(false);
        smtcThread = std::thread(SmtcWorker);
        smtcRunning.store(true);
    }

    // Stop + join the worker and release every device-owned resource. Called
    // once from the overlay teardown path before D3D is destroyed.
    void Shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(threadStartMutex);
            if (!smtcRunning.load())
                return;
            smtcStop.store(true);
            if (smtcThread.joinable())
                smtcThread.join();
            smtcThread = std::thread();
            smtcRunning.store(false);
        }

        AudioCapture::StopCapture();

        if (albumArtTexture)
        {
            ID3D11ShaderResourceView* srv = reinterpret_cast<ID3D11ShaderResourceView*>(reinterpret_cast<intptr_t>(albumArtTexture));
            srv->Release();
            albumArtTexture = nullptr;
        }
        albumArtLoaded = false;

        artVersion.store(0);
        currentSession = nullptr;
        sessionManager = nullptr;
    }
    
    // Upload raw RGBA pixels to a GPU texture, returning an ImTextureID (D3D11 SRV).
    ImTextureID UploadTexture(const unsigned char* px, int w, int h)
    {
        ID3D11Device* device = g_pd3dDevice;
        if (!device || !px || w <= 0 || h <= 0)
            return 0;

        D3D11_TEXTURE2D_DESC td{};
        td.Width = static_cast<UINT>(w);
        td.Height = static_cast<UINT>(h);
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA sd{};
        sd.pSysMem = px;
        sd.SysMemPitch = static_cast<UINT>(w * 4);

        ID3D11Texture2D* tex = nullptr;
        HRESULT hr = device->CreateTexture2D(&td, &sd, &tex);
        if (FAILED(hr) || !tex)
            return 0;

        D3D11_SHADER_RESOURCE_VIEW_DESC svd{};
        svd.Format = td.Format;
        svd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        svd.Texture2D.MipLevels = 1;

        ID3D11ShaderResourceView* srv = nullptr;
        hr = device->CreateShaderResourceView(tex, &svd, &srv);
        tex->Release();
        if (FAILED(hr))
            return 0;

        return reinterpret_cast<ImTextureID>(reinterpret_cast<intptr_t>(srv));
    }

    // Free the currently cached album art SRV.
    void ReleaseAlbumArt()
    {
        if (albumArtTexture)
        {
            ID3D11ShaderResourceView* srv = reinterpret_cast<ID3D11ShaderResourceView*>(reinterpret_cast<intptr_t>(albumArtTexture));
            srv->Release();
            albumArtTexture = nullptr;
        }
        albumArtLoaded = false;
    }

    // Update FFT data - uses real audio capture when available, falls back to simulation
    void SimulateFFT(float dt)
    {
        visualizationTime += dt;
        
        // Try to get real audio data from WASAPI loopback capture
        if (AudioCapture::IsCaptureActive())
        {
            AudioCapture::GetFFTData(fftData, (int)fftData.size());
            AudioCapture::Update(dt);
            return;
        }
        
        // Fallback: More realistic simulation with beat detection
        static float beatPhase = 0.0f;
        static float lastBeatTime = 0.0f;
        static float beatInterval = 0.5f; // ~120 BPM
        
        beatPhase += dt;
        if (beatPhase - lastBeatTime > beatInterval)
        {
            lastBeatTime = beatPhase;
            beatInterval = 0.4f + (sinf(visualizationTime * 0.7f) * 0.15f + 0.5f) * 0.2f;
        }
        
        float beatIntensity = 1.0f - ImClamp((beatPhase - lastBeatTime) / beatInterval * 2.0f, 0.0f, 1.0f);
        beatIntensity = beatIntensity * beatIntensity;
        
        for (size_t i = 0; i < fftData.size(); ++i)
        {
            float baseFreq = static_cast<float>(i) / static_cast<float>(fftData.size());
            
            float oscillator1 = sinf(visualizationTime * 1.3f + baseFreq * 8.0f) * 0.25f;
            float oscillator2 = sinf(visualizationTime * 0.7f + baseFreq * 15.0f) * 0.15f;
            float oscillator3 = sinf(visualizationTime * 2.1f + baseFreq * 5.0f) * 0.1f;
            
            float noise = (oscillator1 + oscillator2 + oscillator3) * 0.5f + 0.5f;
            
            float freqWeight = 0.3f + baseFreq * 0.7f;
            float bassBoost = (1.0f - baseFreq) * 0.5f + 0.5f;
            
            float beatResponse = 1.0f + beatIntensity * bassBoost * 0.8f;
            
            float constantFloor = 0.15f + baseFreq * 0.25f;
            
            fftData[i] = ImClamp((noise * beatResponse * freqWeight + constantFloor) * (0.5f + beatIntensity * 0.5f), 0.0f, 1.0f);
        }
    }
    
    // Queue a playback command for the worker thread. The worker owns the
    // SMTC session, so all controls pass through it and apply on its next poll.
    void QueueControl(ControlCommand cmd)
    {
        pendingControl.store(cmd, std::memory_order_release);
    }

    // Pull the worker snapshot and album-art revision into render-side state.
    // Shared by Render (music tab) and RenderMiniplayer (floating card) so
    // track info and artwork stay live even while the music tab is hidden.
    void SyncState()
    {
        EnsureWorker();

        // Mirror the worker's latest snapshot onto the render-side track
        // state (currentTrack is render-thread-only).
        {
            SharedSnapshot s;
            {
                std::lock_guard<std::mutex> lock(snapshotMutex);
                s = snapshot;
            }
            currentTrack.title = s.title;
            currentTrack.artist = s.artist;
            currentTrack.album = s.album;
            currentTrack.isPlaying = s.isPlaying;
            currentTrack.progress = s.progress;
            currentTrack.duration = s.duration;
        }

        // Publish worker-decoded album art to the GPU exactly once per
        // revision. D3D resource creation must stay on the render thread.
        static uint64_t lastUploadedArtVersion = 0;
        const uint64_t version = artVersion.load(std::memory_order_acquire);
        if (version != lastUploadedArtVersion)
        {
            std::vector<unsigned char> px;
            int w = 0, h = 0;
            {
                std::lock_guard<std::mutex> lock(snapshotMutex);
                px = artPixels;
                w = artW; h = artH;
            }
            if (!px.empty())
            {
                ImTextureID tex = UploadTexture(px.data(), w, h);
                if (tex)
                {
                    ReleaseAlbumArt();
                    albumArtTexture = tex;
                    albumArtLoaded = true;
                    albumArtPxW = w;
                    albumArtPxH = h;
                }
            }
            else
            {
                ReleaseAlbumArt();
                albumArtPxW = 0;
                albumArtPxH = 0;
            }
            lastUploadedArtVersion = version;
        }
    }

    // Render visualizer
    void Render(ImDrawList* draw, ImVec2 pos, ImVec2 size, float dt)
    {
        SyncState();

        SimulateFFT(dt);
        
        ImVec2 center = ImVec2(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
        float radius = ImMin(size.x, size.y) * 0.4f;
        
        ImU32 accentColor = IM_COL32(30, 215, 96, 255); // Spotify Green
        ImU32 accentDim = IM_COL32(30, 215, 96, 100);
        ImU32 bgColor = IM_COL32(20, 20, 30, 200);
        ImU32 textColor = IM_COL32(255, 255, 255, 255);
        ImU32 mutedColor = IM_COL32(180, 180, 180, 255);
        ImU32 warningColor = IM_COL32(255, 180, 60, 255);

        ImU32 textSecondary = IM_COL32(176, 182, 190, 255);
        ImU32 textTertiary  = IM_COL32(118, 124, 134, 255);
        ImU32 trackFill     = IM_COL32(44, 45, 56, 255);
        ImU32 surfaceHov    = IM_COL32(52, 54, 66, 255);

        ImFont* fTitle = UI::medium_font ? UI::medium_font : ImGui::GetFont();
        ImFont* fBody  = UI::small_font  ? UI::small_font  : ImGui::GetFont();
        
        // Background panel
        draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bgColor, 8.0f);
        draw->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(60, 60, 80, 255), 8.0f);
        
        // Album art (left side) - draw Spotify-style placeholder with album art area
        float artSize = ImMin(size.x * 0.28f, size.y * 0.5f);
        ImVec2 artPos = ImVec2(pos.x + 16.0f, pos.y + 14.0f);
        draw->AddRectFilled(artPos, ImVec2(artPos.x + artSize, artPos.y + artSize), IM_COL32(18, 18, 28, 255), 6.0f);
        draw->AddRect(artPos, ImVec2(artPos.x + artSize, artPos.y + artSize), IM_COL32(60, 60, 80, 255), 6.0f);
        
        // Draw actual album art if loaded, otherwise Spotify logo placeholder
        if (albumArtTexture)
        {
            draw->AddImageRounded(albumArtTexture, artPos, ImVec2(artPos.x + artSize, artPos.y + artSize),
                                  ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 6.0f);
        }
        else
        {
        // Draw Spotify logo / album art placeholder
        float cx = artPos.x + artSize * 0.5f;
        float cy = artPos.y + artSize * 0.5f;
        float r = artSize * 0.35f;
        
        // Draw Spotify-like circular waves (iconic Spotify logo)
        for (int wave = 0; wave < 3; ++wave)
        {
            float waveR = r * (0.4f + wave * 0.2f);
            float waveThickness = r * 0.08f;
            float startAngle = -IM_PI * 0.6f;
            float endAngle = IM_PI * 0.6f;
            int segments = 24;
            
            for (int i = 0; i < segments; ++i)
            {
                float a1 = startAngle + (endAngle - startAngle) * i / segments;
                float a2 = startAngle + (endAngle - startAngle) * (i + 1) / segments;
                ImVec2 p1(cx + cosf(a1) * waveR, cy + sinf(a1) * waveR);
                ImVec2 p2(cx + cosf(a2) * waveR, cy + sinf(a2) * waveR);
                ImU32 waveColor = IM_COL32(30, 215, 96, 180 - wave * 40);
                draw->AddLine(p1, p2, waveColor, waveThickness);
            }
        }
        }
        
        // Track info (right of album art)
        float infoX = artPos.x + artSize + 20.0f;
        float infoY = artPos.y + 10.0f;
        float infoW = size.x - (infoX - pos.x) - 20.0f;

        // Ellipsize helper so long titles/artists never overflow.
        auto ellipsize = [&](const std::string& text, float maxW, ImFont* font, float size) -> std::string
        {
            if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x <= maxW)
                return text;
            std::string t = text;
            while (!t.empty() && font->CalcTextSizeA(size, FLT_MAX, 0.0f, (t + "...").c_str()).x > maxW)
                t.pop_back();
            return t + "...";
        };

        // Playback status: green animated dot + small caps label
        bool playing = currentTrack.isPlaying;
        ImU32 statusDotCol = playing ? accentColor : textTertiary;
        float dotR = 3.5f;
        ImVec2 dotC(infoX + 4.0f, infoY + 6.0f);
        if (playing)
        {
            // Soft glow so the status reads as "live"
            draw->AddCircleFilled(dotC, dotR + 3.0f, accentDim, 24);
            draw->AddCircleFilled(dotC, dotR, statusDotCol, 24);
        }
        else
        {
            draw->AddCircleFilled(dotC, dotR, statusDotCol, 24);
        }
        draw->AddText(fTitle, 11.0f, ImVec2(infoX + 12.0f, infoY), playing ? accentColor : textSecondary, playing ? "NOW PLAYING" : "PAUSED");
        infoY += 15.0f;
        
        // Title (primary)
        std::string titleTxt = ellipsize(currentTrack.title, infoW, fTitle, 21.0f);
        draw->AddText(fTitle, 21.0f, ImVec2(infoX, infoY), textColor, titleTxt.c_str());
        infoY += 25.0f;
        
        // Artist + album (secondary lines)
        std::string artistTxt = ellipsize(currentTrack.artist, infoW, fBody, 14.0f);
        draw->AddText(fBody, 14.0f, ImVec2(infoX, infoY), textSecondary, artistTxt.c_str());
        infoY += 17.0f;
        if (!currentTrack.album.empty())
        {
            std::string albumTxt = ellipsize(currentTrack.album, infoW, fBody, 12.0f);
            draw->AddText(fBody, 12.0f, ImVec2(infoX, infoY), textTertiary, albumTxt.c_str());
            infoY += 15.0f;
        }
        infoY += 4.0f;
        
        // Progress bar (seekable)
        float progressBarW = infoW;
        float progressBarH = 5.0f;
        float pbCX = infoX, pbCY = infoY;
        ImVec2 pbMin(pbCX, pbCY);
        ImVec2 pbMax(pbCX + progressBarW, pbCY + progressBarH);
        float prog = ImClamp(currentTrack.progress, 0.0f, 1.0f);

        // Wider invisible hit area for easier clicking
        ImVec2 pbHitMin(infoX, infoY - 9.0f);
        ImVec2 pbHitMax(infoX + progressBarW, infoY + progressBarH + 9.0f);
        bool pbHovered = ImGui::IsMouseHoveringRect(pbHitMin, pbHitMax);

        static bool pbDragging = false;
        static float pbDragFrac = 0.0f;

        if (pbHovered && ImGui::IsMouseClicked(0))
        {
            pbDragging = true;
            pbDragFrac = ImClamp((ImGui::GetIO().MousePos.x - infoX) / progressBarW, 0.0f, 1.0f);
        }
        if (pbDragging)
        {
            if (ImGui::IsMouseDown(0))
            {
                pbDragFrac = ImClamp((ImGui::GetIO().MousePos.x - infoX) / progressBarW, 0.0f, 1.0f);
                pendingSeek.store(pbDragFrac, std::memory_order_relaxed);
                QueueControl(ControlCommand::Seek);
            }
            else
            {
                pendingSeek.store(pbDragFrac, std::memory_order_relaxed);
                QueueControl(ControlCommand::Seek);
                pbDragging = false;
            }
        }

        float dispProg = pbDragging ? pbDragFrac : prog;
        draw->AddRectFilled(pbMin, pbMax, trackFill, 3.0f);
        if (dispProg > 0.001f)
        {
            // Fill: subtle horizontal gradient accent -> lighter green
            ImVec2 fillMin(pbMin.x, pbMin.y);
            ImVec2 fillMax(pbMin.x + progressBarW * dispProg, pbMax.y);
            ImU32 fillA = IM_COL32(30, 215, 96, 255);
            ImU32 fillB = IM_COL32(82, 240, 138, 255);
            draw->AddRectFilledMultiColor(fillMin, fillMax, fillA, fillB, fillA, fillB);
            // Rounded right edge for fill when not full
            if (dispProg > 0.02f && dispProg < 0.99f)
                draw->AddCircleFilled(ImVec2(fillMax.x, pbCY + progressBarH * 0.5f), progressBarH * 0.5f, fillB, 16);
        }

        // Knob
        float knobR = (pbHovered || pbDragging) ? 6.5f : 5.0f;
        float knobX = pbMin.x + progressBarW * dispProg;
        ImVec2 knobC(knobX, pbCY + progressBarH * 0.5f);
        if (pbHovered || pbDragging)
        {
            draw->AddCircleFilled(knobC, knobR + 4.0f, IM_COL32(30, 215, 96, 60), 24);
            draw->AddCircleFilled(knobC, knobR, IM_COL32(255, 255, 255, 255), 24);
        }
        else
        {
            draw->AddCircleFilled(knobC, knobR, IM_COL32(235, 240, 245, 255), 24);
        }

        if (pbHovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        infoY += 15.0f;

        // Time display (current left, total right - Spotify style)
        auto formatTime = [](float seconds) -> std::string {
            if (seconds < 0) seconds = 0;
            if (!isfinite(seconds)) seconds = 0;
            int mins = static_cast<int>(seconds) / 60;
            int secs = static_cast<int>(seconds) % 60;
            char buf[16];
            sprintf_s(buf, "%d:%02d", mins, secs);
            return std::string(buf);
        };

        float displaySecs = dispProg * currentTrack.duration;
        float totalSecs = currentTrack.duration;
        std::string curTime = formatTime(displaySecs);
        std::string totTime = formatTime(totalSecs);
        draw->AddText(fBody, 11.0f, ImVec2(infoX, infoY), textTertiary, curTime.c_str());
        float totW = fBody->CalcTextSizeA(11.0f, FLT_MAX, 0.0f, totTime.c_str()).x;
        draw->AddText(fBody, 11.0f, ImVec2(pbMax.x - totW, infoY), textTertiary, totTime.c_str());
        infoY += 17.0f;

        // Volume slider (speaker icon + slim bar + percent)
        float volLabelW = 18.0f;
        float volBarX = infoX + volLabelW;
        float volBarW = infoW - volLabelW - 42.0f;
        float volH = 4.0f;
        float volCY = infoY + 2.0f;

        // Speaker icon (wedge + sound arcs)
        ImU32 volIconCol = textSecondary;
        float sx = infoX + 2.0f;
        float sy = infoY + 2.0f;
        // speaker body: small rounded rect
        draw->AddRectFilled(ImVec2(sx, sy + 2.0f), ImVec2(sx + 4.0f, sy + 6.0f), volIconCol, 1.0f);
        // cone
        draw->AddTriangleFilled(ImVec2(sx + 4.0f, sy + 2.0f), ImVec2(sx + 4.0f, sy + 6.0f), ImVec2(sx + 8.0f, sy + 4.0f), volIconCol);
        // arcs (two sound waves)
        float arcR1 = 3.0f, arcR2 = 5.0f;
        for (int i = 0; i < 2; ++i)
        {
            float rr = i ? arcR2 : arcR1;
            float aa = ImAcos(6.0f / rr); // clamp into [0, pi/2]
            int segs = 8;
            for (int s = 0; s < segs; ++s)
            {
                float a1 = -aa + aa * 2 * s / segs;
                float a2 = -aa + aa * 2 * (s + 1) / segs;
                draw->AddLine(
                    ImVec2(sx + 8.0f + cosf(a1) * rr, sy + 4.0f + sinf(a1) * rr),
                    ImVec2(sx + 8.0f + cosf(a2) * rr, sy + 4.0f + sinf(a2) * rr),
                    volIconCol, 1.2f);
            }
        }

        ImVec2 volMin(volBarX, volCY);
        ImVec2 volMax(volBarX + volBarW, volCY + volH);
        ImVec2 volHitMin(volBarX, infoY - 8.0f);
        ImVec2 volHitMax(volBarX + volBarW, infoY + volH + 8.0f);
        bool volHovered = ImGui::IsMouseHoveringRect(volHitMin, volHitMax);

        static bool volDragging = false;
        static float volDragVal = 0.5f;

        float curVol = AudioCapture::GetSystemVolume();
        if (curVol < 0.0f) curVol = 0.5f;
        float dispVol = volDragging ? volDragVal : curVol;

        if (volHovered && ImGui::IsMouseClicked(0))
        {
            volDragging = true;
            volDragVal = ImClamp((ImGui::GetIO().MousePos.x - volBarX) / volBarW, 0.0f, 1.0f);
            AudioCapture::SetSystemVolume(volDragVal);
        }
        if (volDragging)
        {
            if (ImGui::IsMouseDown(0))
            {
                volDragVal = ImClamp((ImGui::GetIO().MousePos.x - volBarX) / volBarW, 0.0f, 1.0f);
                AudioCapture::SetSystemVolume(volDragVal);
            }
            else
                volDragging = false;
        }

        draw->AddRectFilled(volMin, volMax, trackFill, 2.0f);
        if (dispVol > 0.001f)
        {
            ImVec2 vfillMax(volMin.x + volBarW * dispVol, volMax.y);
            draw->AddRectFilledMultiColor(volMin, vfillMax, accentColor, accentColor, accentColor, accentColor);
        }

        float volKnobX = volMin.x + volBarW * dispVol;
        ImU32 volKnobCol = (volHovered || volDragging) ? IM_COL32(255, 255, 255, 255) : IM_COL32(235, 240, 245, 255);
        draw->AddCircleFilled(ImVec2(volKnobX, volCY + volH * 0.5f), (volHovered || volDragging) ? 5.5f : 4.5f, volKnobCol, 16);

        // Volume percentage
        char volPct[8];
        sprintf_s(volPct, "%d%%", (int)(dispVol * 100.0f + 0.5f));
        draw->AddText(fBody, 11.0f, ImVec2(volBarX + volBarW + 8.0f, infoY + 1.0f), textTertiary, volPct);

        if (volHovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        infoY += 15.0f;
        
        // Playback controls (Spotify style: ghost skip buttons + large play)
        float ghostBtn = 32.0f;
        float playBtn = 38.0f;
        float spacing = 12.0f;
        float totalBtnW = ghostBtn * 2 + playBtn + spacing * 2;
        float btnStartX = infoX + (infoW - totalBtnW) * 0.5f;
        float btnY = infoY;

        auto drawSkipBtn = [&](float x, int dir) -> bool {
            ImVec2 bmin(x, btnY);
            ImVec2 bmax(x + ghostBtn, btnY + ghostBtn);
            bool hovered = ImGui::IsMouseHoveringRect(bmin, bmax);
            bool clicked = hovered && ImGui::IsMouseClicked(0);
            if (hovered)
                draw->AddCircleFilled(ImVec2(x + ghostBtn * 0.5f, btnY + ghostBtn * 0.5f), ghostBtn * 0.5f, IM_COL32(50, 51, 62, 230), 40);
            ImU32 col = hovered ? textColor : textSecondary;
            float cx = x + ghostBtn * 0.5f;
            float cy = btnY + ghostBtn * 0.5f;
            float s = ghostBtn * 0.30f;
            if (dir == 0) // previous
            {
                draw->AddRectFilled(ImVec2(cx - s * 0.7f, cy - s), ImVec2(cx - s * 0.3f, cy + s), col);
                draw->AddTriangleFilled(ImVec2(cx - s * 0.1f, cy), ImVec2(cx + s * 0.4f, cy - s), ImVec2(cx + s * 0.4f, cy + s), col);
            }
            else // next
            {
                draw->AddRectFilled(ImVec2(cx + s * 0.3f, cy - s), ImVec2(cx + s * 0.7f, cy + s), col);
                draw->AddTriangleFilled(ImVec2(cx + s * 0.1f, cy), ImVec2(cx - s * 0.4f, cy - s), ImVec2(cx - s * 0.4f, cy + s), col);
            }
            return clicked;
        };

        // prev
        if (drawSkipBtn(btnStartX, 0)) QueueControl(ControlCommand::Prev);
        // play/pause - large green circle with dark icon
        float playX = btnStartX + ghostBtn + spacing;
        ImVec2 playC(playX + playBtn * 0.5f, btnY + playBtn * 0.5f);
        bool playHovered = ImGui::IsMouseHoveringRect(ImVec2(playX, btnY), ImVec2(playX + playBtn, btnY + playBtn));
        if (playHovered)
        {
            draw->AddCircleFilled(playC, playBtn * 0.5f + 4.0f, IM_COL32(30, 215, 96, 55), 48);
        }
        ImU32 playBg = playHovered ? IM_COL32(52, 235, 118, 255) : accentColor;
        draw->AddCircleFilled(playC, playBtn * 0.5f, playBg, 48);
        ImU32 playIconCol = IM_COL32(16, 20, 24, 255);
        {
            float cxp = playC.x, cyp = playC.y;
            if (playing) // pause bars
            {
                draw->AddRectFilled(ImVec2(cxp - 6.0f, cyp - 7.0f), ImVec2(cxp - 1.5f, cyp + 7.0f), playIconCol, 1.5f);
                draw->AddRectFilled(ImVec2(cxp + 5.0f, cyp - 7.0f), ImVec2(cxp + 1.5f, cyp + 7.0f), playIconCol, 1.5f);
            }
            else // play triangle
            {
                draw->AddTriangleFilled(ImVec2(cxp - 4.0f, cyp - 8.0f), ImVec2(cxp - 4.0f, cyp + 8.0f), ImVec2(cxp + 10.0f, cyp), playIconCol);
            }
        }
        if (playHovered && ImGui::IsMouseClicked(0)) QueueControl(ControlCommand::PlayPause);
        // next
        float nextX = btnStartX + ghostBtn + spacing + playBtn + spacing;
        if (drawSkipBtn(nextX, 1)) QueueControl(ControlCommand::Next);

        infoY += playBtn + 8.0f;
        
        // Visualizer mode selector label (small caps)
        draw->AddText(fTitle, 10.0f, ImVec2(infoX, infoY), textTertiary, "VISUALIZER MODE");
        infoY += 17.0f;
        
        // Mode pills (rounded seg control)
        const char* modes[] = { "Radial", "Spectrum", "Waveform", "Particles" };
        float pillGap = 6.0f;
        float modeBtnW = (infoW - pillGap * 3) / 4.0f;
        float pillH = 26.0f;
        for (int i = 0; i < 4; ++i)
        {
            float mx = infoX + i * (modeBtnW + pillGap);
            ImVec2 btnMin(mx, infoY);
            ImVec2 btnMax(mx + modeBtnW, infoY + pillH);
            bool isActive = (currentMode == static_cast<VisualizerMode>(i));
            bool hovered = ImGui::IsMouseHoveringRect(btnMin, btnMax);
            bool clicked = hovered && ImGui::IsMouseClicked(0);
            
            if (clicked) currentMode = static_cast<VisualizerMode>(i);
            
            ImU32 bg = isActive ? accentColor : (hovered ? surfaceHov : IM_COL32(24, 25, 32, 255));
            ImU32 bdr = isActive ? accentColor : IM_COL32(44, 45, 56, 255);
            ImU32 txt = isActive ? IM_COL32(12, 16, 14, 255) : (hovered ? textColor : textSecondary);
            
            draw->AddRectFilled(btnMin, btnMax, bg, pillH * 0.5f);
            if (!isActive) draw->AddRect(btnMin, btnMax, bdr, pillH * 0.5f);
            
            std::string label = modes[i];
            ImVec2 ts = fBody->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, label.c_str());
            float tx = mx + (modeBtnW - ts.x) * 0.5f;
            float ty = infoY + (pillH - ts.y) * 0.5f;
            draw->AddText(fBody, 12.0f, ImVec2(tx, ty), txt, label.c_str());
        }
        
        infoY += pillH + 6.0f;
        
        // Status dot + text rows
        auto drawStatusRow = [&](ImU32 col, const char* label, const char* detail)
        {
            draw->AddCircleFilled(ImVec2(infoX + 3.5f, infoY + 6.0f), 3.5f, col, 20);
            std::string full = std::string(label) + "  " + detail;
            draw->AddText(fBody, 11.0f, ImVec2(infoX + 12.0f, infoY), textSecondary, full.c_str());
            infoY += 16.0f;
        };

        const char* smtcText = "";
        ImU32 smtcCol = textSecondary;
        switch (statusInt.load())
        {
            case 0: smtcText = "starting..."; smtcCol = textTertiary; break;
            case 1: smtcText = "connecting"; smtcCol = warningColor; break;
            case 2: smtcText = "live session"; smtcCol = accentColor; break;
            case 3: smtcText = "no active session"; smtcCol = warningColor; break;
            default: smtcText = "unavailable"; smtcCol = warningColor; break;
        }
        drawStatusRow(smtcCol, "SPOTIFY", smtcText);

        std::string audioDetail;
        ImU32 audioCol;
        if (AudioCapture::IsCaptureActive())
        {
            audioDetail = "capturing audio loopback";
            audioCol = accentColor;
        }
        else
        {
            audioDetail = "simulated  (" + AudioCapture::GetLastError() + ")";
            audioCol = warningColor;
        }
        drawStatusRow(audioCol, "AUDIO", audioDetail.c_str());
        infoY += 2.0f;
        
        // Visualizer rendering area (below controls, spanning full width)
        // Reserve a guaranteed minimum height so the selected mode is always visible
        // even when the info column is tall.
        float visY = pos.y + size.y - ImMax(120.0f, size.y * 0.22f) - 12.0f;
        float visH = size.y - (visY - pos.y) - 12.0f;
        float visW = size.x - 40.0f;
        ImVec2 visPos(pos.x + 20.0f, visY);
        
        if (visH > 50.0f && visW > 50.0f)
        {
            switch (currentMode)
            {
                case VisualizerMode::RadialBars:
                {
                    int barCount = 32;
                    float maxRadius = ImMin(visW, visH) * 0.45f;
                    float minRadius = maxRadius * 0.3f;
                    ImVec2 visCenter(visPos.x + visW * 0.5f, visY + visH * 0.5f);
                    
                    for (int i = 0; i < barCount; ++i)
                    {
                        float angle = (static_cast<float>(i) / barCount) * 2.0f * IM_PI - IM_PI * 0.5f;
                        float height = fftData[i % fftData.size()] * (maxRadius - minRadius);
                        float r = minRadius + height;
                        
                        ImVec2 p1(visCenter.x + cosf(angle) * minRadius, visCenter.y + sinf(angle) * minRadius);
                        ImVec2 p2(visCenter.x + cosf(angle) * r, visCenter.y + sinf(angle) * r);
                        
                        float hue = static_cast<float>(i) / barCount;
                        ImU32 col = IM_COL32(
                            static_cast<int>(128 + 127 * sinf(hue * 6.28f)),
                            static_cast<int>(215 * (0.5f + 0.5f * sinf(hue * 6.28f + 2.0f))),
                            static_cast<int>(96 + 80 * sinf(hue * 6.28f + 4.0f)),
                            255
                        );
                        
                        draw->AddLine(p1, p2, col, 3.0f);
                    }
                    break;
                }
                case VisualizerMode::Spectrum:
                {
                    int barCount = 48;
                    float barW = visW / barCount * 0.8f;
                    float spacing = visW / barCount * 0.2f;
                    float maxH = visH * 0.9f;
                    
                    for (int i = 0; i < barCount; ++i)
                    {
                        float x = visPos.x + i * (barW + spacing) + spacing * 0.5f;
                        float h = fftData[i % fftData.size()] * maxH;
                        ImVec2 p1(x, visY + visH);
                        ImVec2 p2(x + barW, visY + visH - h);
                        
                        float hue = static_cast<float>(i) / barCount;
                        ImU32 col = IM_COL32(
                            static_cast<int>(30 + 100 * hue),
                            static_cast<int>(215 * (0.3f + 0.7f * (1.0f - hue))),
                            static_cast<int>(96 + 100 * (1.0f - hue)),
                            255
                        );
                        
                        draw->AddRectFilled(p1, p2, col, 2.0f);
                    }
                    break;
                }
                case VisualizerMode::Waveform:
                {
                    int points = 128;
                    std::vector<ImVec2> wavePoints;
                    wavePoints.reserve(points);
                    
                    for (int i = 0; i < points; ++i)
                    {
                        float x = visPos.x + (static_cast<float>(i) / (points - 1)) * visW;
                        float sampleIdx = static_cast<float>(i) / points * fftData.size();
                        int idx0 = static_cast<int>(sampleIdx);
                        int idx1 = ImMin(idx0 + 1, static_cast<int>(fftData.size()) - 1);
                        float t = sampleIdx - idx0;
                        float val = fftData[idx0] * (1.0f - t) + fftData[idx1] * t;
                        float y = visY + visH * 0.5f + (val - 0.5f) * visH * 0.8f;
                        wavePoints.push_back(ImVec2(x, y));
                    }
                    
                    for (int i = 0; i < points - 1; ++i)
                    {
                        float hue = static_cast<float>(i) / points;
                        ImU32 col = IM_COL32(
                            30,
                            static_cast<int>(215 * (0.5f + 0.5f * sinf(hue * 6.28f + visualizationTime))),
                            96,
                            255
                        );
                        draw->AddLine(wavePoints[i], wavePoints[i + 1], col, 2.5f);
                    }
                    break;
                }
                case VisualizerMode::Particles:
                {
                    int particleCount = 60;
                    ImVec2 visCenter(visPos.x + visW * 0.5f, visY + visH * 0.5f);
                    
                    static std::vector<float> particleAngles;
                    static std::vector<float> particleRadii;
                    static std::vector<float> particleSpeeds;
                    static std::vector<ImU32> particleColors;
                    
                    if (particleAngles.empty())
                    {
                        particleAngles.resize(particleCount);
                        particleRadii.resize(particleCount);
                        particleSpeeds.resize(particleCount);
                        particleColors.resize(particleCount);
                        for (int i = 0; i < particleCount; ++i)
                        {
                            particleAngles[i] = static_cast<float>(rand()) / RAND_MAX * 2.0f * IM_PI;
                            particleRadii[i] = static_cast<float>(rand()) / RAND_MAX * ImMin(visW, visH) * 0.4f;
                            particleSpeeds[i] = 0.5f + static_cast<float>(rand()) / RAND_MAX * 1.5f;
                            float hue = static_cast<float>(i) / particleCount;
                            particleColors[i] = IM_COL32(
                                static_cast<int>(30 + 100 * hue),
                                static_cast<int>(215 * (0.5f + 0.5f * sinf(hue * 6.28f))),
                                static_cast<int>(96 + 100 * (1.0f - hue)),
                                255
                            );
                        }
                    }
                    
                    float avgFFT = 0.0f;
                    for (float v : fftData) avgFFT += v;
                    avgFFT /= fftData.size();
                    
                    for (int i = 0; i < particleCount; ++i)
                    {
                        particleAngles[i] += particleSpeeds[i] * dt * (0.5f + avgFFT);
                        float r = particleRadii[i] + sinf(visualizationTime * 2.0f + i * 0.1f) * 10.0f * avgFFT;
                        ImVec2 p(visCenter.x + cosf(particleAngles[i]) * r, visCenter.y + sinf(particleAngles[i]) * r);
                        draw->AddCircleFilled(p, 3.0f + avgFFT * 4.0f, particleColors[i], 8);
                    }
                    break;
                }
            }
        }
    }

    // ---------------------------------------------------------------------------
    // Miniplayer: compact floating now-playing card. The album art fills the
    // whole card (cover-cropped through UVs) under a uniform scrim; title,
    // artist, times, seek bar and transport controls sit on top, every glyph
    // double-drawn with a 1px shadow so text stays readable over any artwork.
    // Input only flows while the menu is open or the in-game hover hotzone is
    // active - see UpdateMiniplayerClickThrough.
    // ---------------------------------------------------------------------------
    void RenderMiniplayer(ImDrawList* draw, bool menuOpen)
    {
        SyncState();

        const float sc = UI::sc * ImClamp(Options::Music::MiniplayerSize, 0.7f, 1.6f);
        const float W = 340.0f * sc;
        const float H = 136.0f * sc;
        const float R = 10.0f * sc;
        const float pad = 14.0f * sc;

        const ImVec2 display = ImGui::GetIO().DisplaySize;
        if (display.x < 64.0f || display.y < 64.0f)
        {
            g_mpRectValid = false;
            return;
        }

        // First placement anchors to the bottom-right corner; afterwards the
        // saved position wins (clamped so the card always stays on screen).
        if (Options::Music::MiniplayerX < 0.0f || Options::Music::MiniplayerY < 0.0f)
        {
            Options::Music::MiniplayerX = display.x - W - 24.0f;
            Options::Music::MiniplayerY = display.y - H - 48.0f;
        }

        ImVec2 pos(Options::Music::MiniplayerX, Options::Music::MiniplayerY);
        auto clampPos = [&]() {
            pos.x = ImClamp(pos.x, 4.0f, ImMax(4.0f, display.x - W - 4.0f));
            pos.y = ImClamp(pos.y, 4.0f, ImMax(4.0f, display.y - H - 4.0f));
        };
        clampPos();
        ImVec2 p1(pos.x + W, pos.y + H);

        const bool interactive = menuOpen || g_mpHotActive;
        ImFont* fTitle = UI::medium_font ? UI::medium_font : ImGui::GetFont();
        ImFont* fSub   = UI::small_icon_font ? UI::small_icon_font : (UI::small_font ? UI::small_font : ImGui::GetFont());

        const ImU32 accent     = IM_COL32(30, 215, 96, 255);
        const ImU32 textMain   = IM_COL32(255, 255, 255, 255);
        const ImU32 textSecond = IM_COL32(196, 200, 206, 255);
        const ImU32 textTert   = IM_COL32(150, 155, 164, 255);
        const ImU32 warning    = IM_COL32(255, 180, 60, 255);
        const ImU32 trackFill  = IM_COL32(44, 45, 56, 235);

        auto shadowText = [&](ImFont* font, float size, ImVec2 at, ImU32 col, const std::string& s)
        {
            draw->AddText(font, size, ImVec2(at.x + 1.0f, at.y + 1.0f), IM_COL32(0, 0, 0, 175), s.c_str());
            draw->AddText(font, size, at, col, s.c_str());
        };

        auto ellipsize = [&](const std::string& text, float maxW, ImFont* font, float size) -> std::string
        {
            if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x <= maxW)
                return text;
            std::string t = text;
            while (!t.empty() && font->CalcTextSizeA(size, FLT_MAX, 0.0f, (t + "...").c_str()).x > maxW)
                t.pop_back();
            return t + "...";
        };

        auto formatTime = [](float seconds) -> std::string {
            if (seconds < 0 || !isfinite(seconds)) seconds = 0;
            int mins = static_cast<int>(seconds) / 60;
            int secs = static_cast<int>(seconds) % 60;
            char buf[16];
            sprintf_s(buf, "%d:%02d", mins, secs);
            return std::string(buf);
        };

        // ---- Drag (menu-open only): grabs from last frame's published rect so
        // every geometry value below is computed exactly once from final pos.
        static bool mpDragging = false;
        static ImVec2 mpDragOff(0.0f, 0.0f);
        static bool mpLastOverCtl = false;
        if (menuOpen && !mpDragging && !mpLastOverCtl && g_mpRectValid &&
            ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(g_mpRectMin, g_mpRectMax))
        {
            mpDragging = true;
            mpDragOff = ImGui::GetIO().MousePos - pos;
        }
        if (mpDragging)
        {
            if (menuOpen && ImGui::IsMouseDown(0))
            {
                pos.x = ImGui::GetIO().MousePos.x - mpDragOff.x;
                pos.y = ImGui::GetIO().MousePos.y - mpDragOff.y;
                clampPos();
            }
            else
            {
                mpDragging = false;
            }
        }
        Options::Music::MiniplayerX = pos.x;
        Options::Music::MiniplayerY = pos.y;
        p1 = ImVec2(pos.x + W, pos.y + H);

        // ---- Hit tests ----
        auto hit = [&](const ImVec2& mn, const ImVec2& mx) {
            return interactive && ImGui::IsMouseHoveringRect(mn, mx);
        };

        const float barY = pos.y + 85.0f * sc;
        const float barH = 4.0f * sc;
        const ImVec2 barMin(pos.x + pad, barY);
        const ImVec2 barMax(pos.x + W - pad, barY + barH);
        const float barW = barMax.x - barMin.x;

        const float deckY = pos.y + 94.0f * sc;
        const float prevS = 26.0f * sc;
        const float playS = 32.0f * sc;
        const float gapS  = 14.0f * sc;
        const float rowW  = prevS * 2 + playS + gapS * 2;
        const float rowX  = pos.x + (W - rowW) * 0.5f;
        const float rowY  = deckY + ((p1.y - 6.0f * sc) - deckY - playS) * 0.5f;
        const float prevX = rowX;
        const float playX = rowX + prevS + gapS;
        const float nextX = playX + playS + gapS;
        const float prevY = rowY + (playS - prevS) * 0.5f;

        const bool hovPrev = hit(ImVec2(prevX, prevY), ImVec2(prevX + prevS, prevY + prevS));
        const bool hovPlay = hit(ImVec2(playX, rowY), ImVec2(playX + playS, rowY + playS));
        const bool hovNext = hit(ImVec2(nextX, prevY), ImVec2(nextX + prevS, prevY + prevS));
        const bool hovBar  = hit(ImVec2(barMin.x, barY - 9.0f * sc), ImVec2(barMax.x, barY + barH + 9.0f * sc));
        mpLastOverCtl = hovPrev || hovPlay || hovNext || hovBar;

        // ---- Background: album art (cover-cropped) + scrim ----
        if (albumArtTexture)
        {
            ImVec2 uv0(0.0f, 0.0f), uv1(1.0f, 1.0f);
            if (albumArtPxW > 0 && albumArtPxH > 0)
            {
                const float ratio = (W / H) / ((float)albumArtPxW / (float)albumArtPxH);
                if (ratio > 1.0f)
                {
                    const float vSpan = 1.0f / ratio;
                    uv0.y = (1.0f - vSpan) * 0.5f;
                    uv1.y = 1.0f - uv0.y;
                }
                else
                {
                    uv0.x = (1.0f - ratio) * 0.5f;
                    uv1.x = 1.0f - uv0.x;
                }
            }
            draw->AddImageRounded(albumArtTexture, pos, p1, uv0, uv1, IM_COL32_WHITE, R);
        }
        else
        {
            draw->AddRectFilled(pos, p1, IM_COL32(16, 16, 22, 255), R);
            // Spotify-style wave placeholder while no artwork is loaded.
            const float cx = pos.x + W * 0.5f;
            const float cy = pos.y + H * 0.42f;
            const float r = 17.0f * sc;
            for (int wave = 0; wave < 3; ++wave)
            {
                const float waveR = r * (0.45f + wave * 0.25f);
                for (int i = 0; i < 20; ++i)
                {
                    const float a1 = -IM_PI * 0.55f + (IM_PI * 1.1f) * i / 20;
                    const float a2 = -IM_PI * 0.55f + (IM_PI * 1.1f) * (i + 1) / 20;
                    draw->AddLine(
                        ImVec2(cx + cosf(a1) * waveR, cy + sinf(a1) * waveR),
                        ImVec2(cx + cosf(a2) * waveR, cy + sinf(a2) * waveR),
                        IM_COL32(30, 215, 96, 130 - wave * 35), 2.2f * sc);
                }
            }
        }
        draw->AddRectFilled(pos, p1, IM_COL32(8, 8, 11, 150), R);
        draw->AddRect(pos, p1, IM_COL32(255, 255, 255, 18), R);

        // ---- Title + artist (non-connected states show status as the title) ----
        const int st = statusInt.load();
        const bool playing = currentTrack.isPlaying;
        const float infoW = W - pad * 2;
        std::string title;
        ImU32 titleCol = textMain;
        if (st == 2)
        {
            title = currentTrack.title.empty() ? "Nothing playing" : currentTrack.title;
        }
        else if (st == 0) { title = "STARTING...";       titleCol = textTert; }
        else if (st == 1) { title = "CONNECTING";        titleCol = warning; }
        else if (st == 3) { title = "NO ACTIVE SESSION"; titleCol = warning; }
        else              { title = "MEDIA UNAVAILABLE"; titleCol = warning; }

        shadowText(fTitle, 18.0f * sc, ImVec2(pos.x + pad, pos.y + 16.0f * sc), titleCol,
                   ellipsize(title, infoW, fTitle, 18.0f * sc));
        if (st == 2 && !currentTrack.artist.empty())
            shadowText(fSub, 12.5f * sc, ImVec2(pos.x + pad, pos.y + 43.0f * sc), textSecond,
                       ellipsize(currentTrack.artist, infoW, fSub, 12.5f * sc));

        // ---- Times + seek bar ----
        static bool mpPbDragging = false;
        static float mpPbDragFrac = 0.0f;
        if (hovBar && ImGui::IsMouseClicked(0))
        {
            mpPbDragging = true;
            mpPbDragFrac = ImClamp((ImGui::GetIO().MousePos.x - barMin.x) / barW, 0.0f, 1.0f);
        }
        if (mpPbDragging)
        {
            mpPbDragFrac = ImClamp((ImGui::GetIO().MousePos.x - barMin.x) / barW, 0.0f, 1.0f);
            pendingSeek.store(mpPbDragFrac, std::memory_order_relaxed);
            QueueControl(ControlCommand::Seek);
            if (!ImGui::IsMouseDown(0))
                mpPbDragging = false;
        }
        const float dispProg = mpPbDragging ? mpPbDragFrac : ImClamp(currentTrack.progress, 0.0f, 1.0f);

        {
            const float yTime = pos.y + 66.0f * sc;
            const std::string curTime = formatTime(dispProg * currentTrack.duration);
            const std::string totTime = formatTime(currentTrack.duration);
            shadowText(fSub, 10.0f * sc, ImVec2(pos.x + pad, yTime), textTert, curTime);
            const float totW = fSub->CalcTextSizeA(10.0f * sc, FLT_MAX, 0.0f, totTime.c_str()).x;
            shadowText(fSub, 10.0f * sc, ImVec2(p1.x - pad - totW, yTime), textTert, totTime);
        }

        draw->AddRectFilled(barMin, barMax, trackFill, 2.0f * sc);
        if (dispProg > 0.001f)
        {
            draw->AddRectFilledMultiColor(barMin, ImVec2(barMin.x + barW * dispProg, barMax.y),
                IM_COL32(30, 215, 96, 255), IM_COL32(82, 240, 138, 255),
                IM_COL32(82, 240, 138, 255), IM_COL32(30, 215, 96, 255));
        }
        if (hovBar || mpPbDragging)
        {
            draw->AddCircleFilled(ImVec2(barMin.x + barW * dispProg, barY + barH * 0.5f),
                                  5.5f * sc, IM_COL32(255, 255, 255, 255), 16);
        }

        // ---- Transport deck ----
        const ImVec2 deckMin(pos.x + 6.0f * sc, deckY);
        const ImVec2 deckMax(p1.x - 6.0f * sc, p1.y - 6.0f * sc);
        draw->AddRectFilled(deckMin, deckMax, IM_COL32(10, 10, 13, 150), 8.0f * sc);

        auto skipBtn = [&](float x, float y, float s, int dir, bool hov) -> bool
        {
            if (hov)
                draw->AddCircleFilled(ImVec2(x + s * 0.5f, y + s * 0.5f), s * 0.5f, IM_COL32(42, 43, 54, 220), 32);
            const ImU32 col = hov ? textMain : textSecond;
            const float cx = x + s * 0.5f;
            const float cy = y + s * 0.5f;
            const float k = s * 0.30f;
            if (dir == 0) // previous
            {
                draw->AddRectFilled(ImVec2(cx - k * 0.7f, cy - k), ImVec2(cx - k * 0.3f, cy + k), col, 0.5f);
                draw->AddTriangleFilled(ImVec2(cx - k * 0.1f, cy), ImVec2(cx + k * 0.45f, cy - k), ImVec2(cx + k * 0.45f, cy + k), col);
            }
            else // next
            {
                draw->AddRectFilled(ImVec2(cx + k * 0.3f, cy - k), ImVec2(cx + k * 0.7f, cy + k), col, 0.5f);
                draw->AddTriangleFilled(ImVec2(cx + k * 0.1f, cy), ImVec2(cx - k * 0.45f, cy - k), ImVec2(cx - k * 0.45f, cy + k), col);
            }
            if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            return hov && ImGui::IsMouseClicked(0);
        };

        if (skipBtn(prevX, prevY, prevS, 0, hovPrev)) QueueControl(ControlCommand::Prev);

        {
            const ImVec2 c(playX + playS * 0.5f, rowY + playS * 0.5f);
            if (hovPlay)
            {
                draw->AddCircleFilled(c, playS * 0.5f + 3.5f * sc, IM_COL32(30, 215, 96, 60), 32);
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            }
            draw->AddCircleFilled(c, playS * 0.5f, hovPlay ? IM_COL32(52, 235, 118, 255) : accent, 32);
            const ImU32 icon = IM_COL32(14, 18, 16, 255);
            if (playing)
            {
                draw->AddRectFilled(ImVec2(c.x - 5.2f * sc, c.y - 6.5f * sc), ImVec2(c.x - 1.8f * sc, c.y + 6.5f * sc), icon, 1.0f * sc);
                draw->AddRectFilled(ImVec2(c.x + 3.2f * sc, c.y - 6.5f * sc), ImVec2(c.x + 6.6f * sc, c.y + 6.5f * sc), icon, 1.0f * sc);
            }
            else
            {
                draw->AddTriangleFilled(ImVec2(c.x - 3.5f * sc, c.y - 7.0f * sc), ImVec2(c.x - 3.5f * sc, c.y + 7.0f * sc), ImVec2(c.x + 8.0f * sc, c.y), icon);
            }
            if (hovPlay && ImGui::IsMouseClicked(0)) QueueControl(ControlCommand::PlayPause);
        }

        if (skipBtn(nextX, prevY, prevS, 1, hovNext)) QueueControl(ControlCommand::Next);

        // Publish the final rect for the click-through hotzone updater.
        g_mpRectMin = pos;
        g_mpRectMax = p1;
        g_mpRectValid = true;
    }
}

#ifdef _MSC_VER
#pragma warning (disable: 26812)    // [Static Analyzer] The enum type 'xxx' is unscoped. Prefer 'enum class' over 'enum' (Enum.3). ImGui uses unscoped enum flag bitmasks heavily.
#endif

ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
bool g_SwapChainOccluded = false;
UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// Shader background: one effect, one instance, spanning the whole menu window.
static shader::ShaderSlot g_bg;

// Persistent script text box for the Executor tab (survives tab switches).
static editor::lua_editor g_executorEditor;

// -----------------------------------------------------------------------------
// MenuFonts: file-scope mirror of the menu font choices pre-loaded into the
// ImGui font atlas at startup. The Misc tab's "Menu Font" combo just picks
// an index here, and the per-frame menu drawing in ShowImgui PushFont/PopFont
// the chosen entry so the change shows up live (no atlas rebuild).
// -----------------------------------------------------------------------------
namespace MenuFonts
{
inline ImFont* Fonts[8] = {};
inline const char* Names[8] = {
"Nunito", "Verdana", "Segoe UI", "Tahoma", "Arial",
"Georgia", "Calibri", "Consolas"
};
inline int Count = 0;
}

// -----------------------------------------------------------------------------
// Menu themes: built-in presets for the menu background / panel / accent colors.
// Index 0 is "Custom" (uses Options::Misc::MenuBgColor / MenuPanelColor / accents).
// -----------------------------------------------------------------------------
namespace MenuThemes
{
struct Theme
{
const char* name;
float bg[3];
float panel[3];
float accent[3];
float accent2[3];
bool gradient;
};

inline const Theme Presets[] = {
// Custom — pure blue accent
{ "Custom",        {0.015f,0.025f,0.045f}, {0.040f,0.060f,0.100f}, {0.300f,0.550f,1.000f}, {0.200f,0.400f,0.850f}, false },
// Midnight — deep navy, electric blue + purple gradient
{ "Midnight",      {0.015f,0.020f,0.038f}, {0.038f,0.050f,0.085f}, {0.340f,0.560f,1.000f}, {0.640f,0.400f,1.000f}, true  },
// Carbon — near-black, cool white accent, monochrome
{ "Carbon",        {0.014f,0.014f,0.017f}, {0.052f,0.052f,0.060f}, {0.870f,0.880f,0.920f}, {0.520f,0.550f,0.620f}, false },
// Sunset — dark burgundy, coral + gold gradient
{ "Sunset",        {0.045f,0.022f,0.035f}, {0.090f,0.042f,0.068f}, {1.000f,0.480f,0.340f}, {1.000f,0.780f,0.300f}, true  },
// Matrix — dark forest, neon green accent
{ "Matrix",        {0.010f,0.028f,0.016f}, {0.028f,0.062f,0.038f}, {0.240f,1.000f,0.380f}, {0.580f,1.000f,0.240f}, false },
// Ice — cool blue, ice blue + pale cyan gradient
{ "Ice",           {0.018f,0.035f,0.052f}, {0.060f,0.098f,0.130f}, {0.480f,0.870f,1.000f}, {0.760f,0.940f,1.000f}, true  },
// Crimson — deep red, bright red + orange gradient
{ "Crimson",       {0.048f,0.010f,0.016f}, {0.115f,0.028f,0.048f}, {1.000f,0.200f,0.280f}, {1.000f,0.530f,0.260f}, true  },
// Lilac — dark violet, bright purple + pink gradient
{ "Lilac",         {0.032f,0.020f,0.048f}, {0.080f,0.060f,0.125f}, {0.700f,0.480f,1.000f}, {1.000f,0.560f,0.900f}, true  },
};

inline int Count = (int)(sizeof(Presets) / sizeof(Presets[0]));

// Resolve the currently-active colors (preset or custom) into out params.
inline void Resolve(const float*& bg, const float*& panel, const float*& accent, const float*& accent2, bool& gradient)
{
int idx = Options::Misc::MenuTheme;
if (idx > 0 && idx < Count)
{
bg = Presets[idx].bg;
panel = Presets[idx].panel;
accent = Presets[idx].accent;
accent2 = Presets[idx].accent2;
gradient = Presets[idx].gradient;
}
else
{
bg = Options::Misc::MenuBgColor;
panel = Options::Misc::MenuPanelColor;
accent = Options::Misc::MenuAccentColor;
accent2 = Options::Misc::MenuAccentColor2;
gradient = Options::Misc::MenuGradient;
}
}

// Feature colors are no longer driven by the menu theme. A theme change only
// restyles the menu chrome; the in-game visuals keep their own defaults and the
// user's per-feature picks. Kept as a (no-op) hook so the theme-change call site
// stays intact and the behavior is easy to restore if ever wanted.
inline void ApplyFeatureColors(const float* accent, const float* accent2)
{
// Feature colors are intentionally decoupled from the menu theme.
//
// This used to copy the theme accent (blue by default) onto every ESP/combat
// visual (Name, Box, Corner, Skeleton, Distance, Chams, FOV, ...). Because the
// default accent is blue, that made features render blue "by default" and also
// clobbered whatever the user had picked in the per-feature color pickers.
//
// Features now keep their own defaults (white) and stay under the user's
// control; switching themes only restyles the menu chrome, not the visuals.
(void)accent;
(void)accent2;
}
}

// Bootstrap guard for MenuWeather engine state. Flipped to true during
// one-time startup and re-flipped to true after a successful LoadConfig so
// the engine re-reads the freshly-loaded Options::Weather::*. Without this,
// SyncToOptions() in Update() would clobber the just-loaded Options with
// stale Engine values on subsequent frames.
inline bool g_MenuWeatherNeedsBootstrap = true;

// -----------------------------------------------------------------------------
// Menu weather (snow / rain) effect. This is a self-contained visual feature
// drawn on top of the menu via ImGui::GetWindowDrawList(). State is stored
// here with sensible defaults so it survives without needing entries in
// options.h. All options are exposed in the Misc -> Menu -> Weather panel.
//
// Performance: 200-400 particles is comfortable at 60fps. The default 150
// keeps GPU draw-call cost near-zero on top of the menu's existing draw list.
// -----------------------------------------------------------------------------
namespace MenuWeather
{
struct Particle
{
float x, y;
float vx, vy;
};

inline bool Enabled = false;
inline int  Type = 0;                 // 0 = snow, 1 = rain
inline int  Intensity = 150;          // particle count (capped 64..2000)
inline float Speed = 1.0f;            // vertical velocity multiplier
inline float Wind = 0.0f;             // horizontal drift (units per frame)
inline float Color[3] = { 1.0f, 1.0f, 1.0f };
inline float SnowSize = 1.8f;         // pixel radius of each snowflake
inline float RainThickness = 1.4f;    // pixel width of each rain streak

inline std::vector<Particle> particles;
inline std::mt19937 rng{ std::random_device{}() };
inline bool initialised = false;
inline int lastRenderedIntensity = 0;
inline float lastRenderedSpeed = -1.f;
inline float lastRenderedWind = -1.f;

inline void SeedParticle(Particle& p, bool anywhere, float maxX, float maxY)
{
p.x = static_cast<float>(rng() % std::max(1, static_cast<int>(maxX)));
p.y = anywhere
? static_cast<float>(rng() % std::max(1, static_cast<int>(maxY)))
: -10.f;
const float angleJitter = (static_cast<float>(rng() % 100) / 100.f - 0.5f) * 0.4f;
p.vx = Wind * 0.025f + angleJitter;
p.vy = Speed * (0.6f + static_cast<float>(rng() % 60) / 100.f) * 1.2f;
}

inline void RebuildParticleBuffer(float maxX, float maxY)
{
particles.clear();
const int count = std::clamp(Intensity, 0, 2000);
particles.reserve(count);
for (int i = 0; i < count; ++i)
{
Particle p;
SeedParticle(p, true, maxX, maxY);
particles.push_back(p);
}
initialised = true;
lastRenderedIntensity = Intensity;
lastRenderedSpeed = Speed;
// Mirror user-tweakable engine-side state into the
// persistent Options snapshot so JSON save/load reflects
// the latest UI changes.
Options::Weather::Enabled       = Enabled;
Options::Weather::Type          = Type;
Options::Weather::Intensity     = Intensity;
Options::Weather::Speed         = Speed;
Options::Weather::Wind          = Wind;
Options::Weather::SnowSize      = SnowSize;
Options::Weather::RainThickness = RainThickness;
for (int i = 0; i < 3; ++i)
Options::Weather::Color[i] = Color[i];
lastRenderedWind = Wind;
}

// Copy persistent Options::Weather values into engine state.
// Called exactly ONCE on the first frame (one-time bootstrap); after
// that, MenuWeather::* is the source of truth and SyncToOptions()
// runs every frame inside Update() to back the engine state into
// Options for JSON persistence.
//
// Defensive clamps keep stale configs sane (Type in {0,1}, Intensity
// in [64, 2000]).
inline void SyncFromOptions()
{
Enabled       = Options::Weather::Enabled;
Type          = (Options::Weather::Type == 0 || Options::Weather::Type == 1) ? Options::Weather::Type : 0;
Intensity     = Options::Weather::Intensity < 64 ? 64 : (Options::Weather::Intensity > 2000 ? 2000 : Options::Weather::Intensity);
Speed         = Options::Weather::Speed;
Wind          = Options::Weather::Wind;
SnowSize      = Options::Weather::SnowSize;
RainThickness = Options::Weather::RainThickness;
for (int i = 0; i < 3; ++i)
Color[i] = Options::Weather::Color[i];
}

// Engine -> Options mirror. Runs each Update so widget-driven changes
// (toggles, slider drags, color edits) flow into the persistent
// Options snapshot without the UI ever having to write to Options.
inline void SyncToOptions()
{
Options::Weather::Enabled       = Enabled;
Options::Weather::Type          = Type;
Options::Weather::Intensity     = Intensity;
Options::Weather::Speed         = Speed;
Options::Weather::Wind          = Wind;
Options::Weather::SnowSize      = SnowSize;
Options::Weather::RainThickness = RainThickness;
for (int i = 0; i < 3; ++i)
Options::Weather::Color[i] = Color[i];
}

// Public re-bootstrap entry point. Clears engine runtime state and
// re-reads the persistent Options::Weather::* values. Called after
// LoadConfig so a freshly-loaded config takes effect instead of
// being clobbered by SyncToOptions from a stale Engine snapshot.
inline void Rebootstrap()
{
particles.clear();
initialised = false;
lastRenderedIntensity = 0;
lastRenderedSpeed = -1.f;
lastRenderedWind = -1.f;
SyncFromOptions();
}

inline void Update(float maxX, float maxY)
{
SyncToOptions();
if (!Enabled)
{
if (!particles.empty())
{
particles.clear();
initialised = false;
}
return;
}

if (!initialised || lastRenderedIntensity != Intensity
|| lastRenderedSpeed != Speed || lastRenderedWind != Wind)
{
RebuildParticleBuffer(maxX, maxY);
return;
}

for (auto& p : particles)
{
p.x += p.vx;
p.y += p.vy;

if (p.y > maxY + 12.f)
SeedParticle(p, false, maxX, maxY);
if (p.x < -8.f)            p.x = maxX + 4.f;
else if (p.x > maxX + 8.f) p.x = -4.f;
}
}

inline void Render(ImDrawList* drawList, const ImVec2& origin, const ImVec2& size)
{
if (!Enabled || particles.empty()) return;

const ImU32 col = IM_COL32(
static_cast<int>(std::clamp(Color[0], 0.f, 1.f) * 255.f),
static_cast<int>(std::clamp(Color[1], 0.f, 1.f) * 255.f),
static_cast<int>(std::clamp(Color[2], 0.f, 1.f) * 255.f),
200);

// Clip to the menu interior so particles cannot leak outside the
// window or overlap game HUD.
drawList->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);

if (Type == 0) // Snow: small filled circles
{
for (const auto& p : particles)
drawList->AddCircleFilled(
ImVec2(origin.x + p.x, origin.y + p.y),
SnowSize, col, 8);
}
else // Rain: short slanted streaks
{
const float slant = Wind * 0.5f;
for (const auto& p : particles)
{
const float px = origin.x + p.x;
const float py = origin.y + p.y;
drawList->AddLine(
ImVec2(px, py),
ImVec2(px + slant, py + 8.f),
col, RainThickness);
}
}

drawList->PopClipRect();
}
}

// ── Menu click effect: sparkle/ripple burst at the cursor on menu clicks ──
// Requests are queued by UI::ClickFx() (every custom widget and the raw-ImGui
// fallback funnel through it). Drawn on the foreground draw list so the effect
// sits above the menu. All geometry is derived from a per-click seed, so no
// per-particle state has to be simulated or stored.
namespace MenuClickFx
{
struct Burst
{
    ImVec2 pos;
    double start;
    unsigned seed;
};

inline std::vector<Burst> bursts;
inline const float kDuration = 0.55f; // seconds

inline float Hash01(unsigned x)
{
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return (float)(x & 0x00FFFFFFu) / (float)0x00FFFFFFu;
}

inline ImU32 WithAlpha(ImU32 rgb, float a)
{
    int ai = (int)(a * 255.f);
    if (ai < 0) ai = 0; else if (ai > 255) ai = 255;
    return (rgb & 0x00FFFFFFu) | ((ImU32)ai << 24);
}

const ImU32 kWhiteRgb = 0x00FFFFFFu;

inline void Update()
{
    if (!UI::clickBursts.empty())
    {
        for (const auto& r : UI::clickBursts)
            bursts.push_back({ r.pos, r.time, r.seed });
        UI::clickBursts.clear();
        if (bursts.size() > 64)
            bursts.erase(bursts.begin(), bursts.begin() + (bursts.size() - 64));
    }

    const double now = ImGui::GetTime();
    bursts.erase(std::remove_if(bursts.begin(), bursts.end(),
        [now](const Burst& b) { return (now - b.start) > (double)kDuration; }),
        bursts.end());
}

inline void Render(ImDrawList* dl)
{
    if (bursts.empty()) return;

    const double now = ImGui::GetTime();
    // Neutral tint (configurable). Kept separate from the menu accent so the
    // effect stays soft/neutral and doesn't get recoloured by Rainbow Accent.
    const ImU32 accent = IM_COL32(
        (int)(std::clamp(Options::Misc::MenuClickEffectColor[0], 0.f, 1.f) * 255.f),
        (int)(std::clamp(Options::Misc::MenuClickEffectColor[1], 0.f, 1.f) * 255.f),
        (int)(std::clamp(Options::Misc::MenuClickEffectColor[2], 0.f, 1.f) * 255.f),
        255) & 0x00FFFFFFu;

    for (const auto& b : bursts)
    {
        const float u = (float)((now - b.start) / (double)kDuration);
        if (u < 0.f || u > 1.f) continue;

        const float fade = 1.f - u;
        const float ease = 1.f - fade * fade; // ease-out

        // Expanding ripple ring
        dl->AddCircle(b.pos, 3.f + ease * 30.f,
                      WithAlpha(accent, 0.75f * fade * fade), 48,
                      2.4f * fade + 0.4f);

        // Soft glow + hot core flash
        const float core = 8.f * fade;
        if (core > 0.4f)
        {
            dl->AddCircleFilled(b.pos, core * 2.6f, WithAlpha(accent, 0.20f * fade), 28);
            dl->AddCircleFilled(b.pos, core * 1.4f, WithAlpha(accent, 0.35f * fade), 28);
            dl->AddCircleFilled(b.pos, core,       WithAlpha(kWhiteRgb, 0.85f * fade), 20);
        }

        // Sparkles: 9 streaks radiating outwards
        for (int i = 0; i < 9; ++i)
        {
            const float ang = Hash01(b.seed * 131u + (unsigned)i * 17u) * 6.2831853f;
            const float spd = 26.f + Hash01(b.seed * 977u + (unsigned)i * 31u) * 30.f;
            const float dist = ease * spd;
            const ImVec2 p(b.pos.x + cosf(ang) * dist, b.pos.y + sinf(ang) * dist);
            const ImVec2 tail(b.pos.x + cosf(ang) * dist * 0.5f, b.pos.y + sinf(ang) * dist * 0.5f);
            const ImU32 c = (i % 3 == 0) ? WithAlpha(kWhiteRgb, 0.90f * fade)
                                         : WithAlpha(accent,    0.90f * fade);
            dl->AddLine(tail, p, c, 1.7f * fade + 0.3f);
            dl->AddCircleFilled(p, 1.7f * fade + 0.3f, c, 8);
        }

        // Rotating 4-point star flash
        const float starR = 15.f * fade;
        if (starR > 0.5f)
        {
            const float rot = u * 0.6f;
            for (int k = 0; k < 2; ++k)
            {
                const float a = rot + (float)k * 1.5707963f;
                const ImVec2 d(cosf(a) * starR, sinf(a) * starR);
                dl->AddLine(ImVec2(b.pos.x - d.x, b.pos.y - d.y),
                            ImVec2(b.pos.x + d.x, b.pos.y + d.y),
                            WithAlpha(kWhiteRgb, 0.70f * fade * fade), 1.3f);
            }
        }
    }
}
}



HWND FindRobloxWindow() {
DWORD pid = 0;
if (Memory)
pid = (DWORD)Memory->getProcessId();
if (pid == 0) return nullptr;

HWND result = nullptr;
EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
DWORD wpid = 0;
GetWindowThreadProcessId(hwnd, &wpid);
if (wpid == (DWORD)lParam && IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == nullptr) {
HWND* out = reinterpret_cast<HWND*>(lParam);
*out = hwnd;
return FALSE;
}
return TRUE;
}, reinterpret_cast<LPARAM>(&result));

return result;
}

bool IsGameOnTop(const std::string& expectedTitle) {
// ESP used to be gated on Roblox owning the *foreground* window, which broke
// it whenever anything else held focus - the Seraph overlay itself, a
// browser, even the menu. The gate reported false for entire sessions while
// the game was plainly on screen, so RenderESP never ran and the only things
// visible were the passes drawn outside this check (FOV fill, keybinds,
// arrows), which piled up in a corner.
//
// What ESP actually needs is "the game is up and showing", so test that:
// the Roblox window exists, is visible, and is not minimized. Foreground
// focus is irrelevant - the overlay is topmost and composites over the game
// regardless of who owns focus.
HWND rbWnd = Globals::Viewport::RobloxHWND;
if (!rbWnd || !IsWindow(rbWnd))
{
    rbWnd = FindWindowA(NULL, "Roblox");
    if (!rbWnd)
    {
        DWORD pid = Memory ? (DWORD)Memory->getProcessId() : 0;
        if (pid != 0)
        {
            HWND found = nullptr;
            EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
                DWORD wpid = 0;
                GetWindowThreadProcessId(hwnd, &wpid);
                if (wpid == (DWORD)lParam && IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == nullptr) {
                    *reinterpret_cast<HWND*>(lParam) = hwnd;
                    return FALSE;
                }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&found));
            if (found) rbWnd = found;
        }
    }
}

if (rbWnd && IsWindow(rbWnd))
    return IsWindowVisible(rbWnd) != 0 && !IsIconic(rbWnd);

// No Roblox window resolved: fall back to the old foreground/pid test so the
// gate still works if the window handle was never published.
HWND hwnd = GetForegroundWindow();
if (!hwnd) return false;

DWORD fgPid = 0;
GetWindowThreadProcessId(hwnd, &fgPid);
DWORD rbPid = Memory ? (DWORD)Memory->getProcessId() : 0;
if (rbPid != 0)
return fgPid == rbPid;

char windowTitle[256];
int length = GetWindowTextA(hwnd, windowTitle, sizeof(windowTitle));
if (length == 0) return false;

return expectedTitle == std::string(windowTitle);
}

void HideFromTaskbar(HWND hwnd);

static volatile LONG g_OverlayWheelAccum = 0;
static WNDPROC g_GameWndProc = nullptr;
static HWND g_GameWnd = nullptr;
static int g_OverlayAlpha = 255;

static LRESULT CALLBACK GameWheelHookWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_MOUSEWHEEL)
    {
        short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (delta != 0)
            InterlockedExchangeAdd(&g_OverlayWheelAccum, (LONG)((double)delta * 4096.0 / 120.0));
    }
    return CallWindowProcW(g_GameWndProc, hWnd, msg, wParam, lParam);
}

struct WheelForwarderCtx { HWND overlay; HWND best; int bestArea; };

static BOOL CALLBACK EnumWheelForwarderWindows(HWND h, LPARAM lp)
{
    WheelForwarderCtx* c = (WheelForwarderCtx*)lp;
    if (h == c->overlay || !::IsWindowVisible(h))
        return TRUE;
    DWORD pid = 0;
    ::GetWindowThreadProcessId(h, &pid);
    if (pid != ::GetCurrentProcessId())
        return TRUE;
    LONG_PTR ex = ::GetWindowLongPtrW(h, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW)
        return TRUE;
    RECT rc;
    ::GetWindowRect(h, &rc);
    int area = (rc.right - rc.left) * (rc.bottom - rc.top);
    if (area > c->bestArea)
    {
        c->best = h;
        c->bestArea = area;
    }
    return TRUE;
}

static void InstallWheelForwarder(HWND overlay)
{
    WheelForwarderCtx c = { overlay, nullptr, 0 };
    ::EnumWindows(EnumWheelForwarderWindows, (LPARAM)&c);
    if (!c.best)
        return;
    g_GameWnd = c.best;
    g_GameWndProc = (WNDPROC)::SetWindowLongPtrW(c.best, GWLP_WNDPROC, (LONG_PTR)GameWheelHookWndProc);
}

static void UninstallWheelForwarder()
{
    if (g_GameWnd && g_GameWndProc)
        ::SetWindowLongPtrW(g_GameWnd, GWLP_WNDPROC, (LONG_PTR)g_GameWndProc);
    g_GameWnd = nullptr;
    g_GameWndProc = nullptr;
}

static void ApplyOverlayWindowStyle(HWND hwnd, bool clickThrough, bool noActivate = false)
{
LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);

// Topmost + layered at all times so the ESP/menu layer is a visible overlay.
exStyle |= WS_EX_LAYERED | WS_EX_TOPMOST;

// Click-through (transparent to input) when in-game and the menu is closed;
// clickable when the menu is open. The miniplayer hotzone additionally sets
// WS_EX_NOACTIVATE so clicking its buttons never takes focus from Roblox.
if (clickThrough)
exStyle |= WS_EX_TRANSPARENT;
else
exStyle &= ~WS_EX_TRANSPARENT;

if (noActivate)
exStyle |= WS_EX_NOACTIVATE;
else
exStyle &= ~WS_EX_NOACTIVATE;

if (Options::Misc::HideFromTabs)
{
exStyle |= WS_EX_TOOLWINDOW;
exStyle &= ~WS_EX_APPWINDOW;
}
else
{
exStyle &= ~WS_EX_TOOLWINDOW;
}

SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);
SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);

// SetWindowPos can drop the layered attributes, so restore the colour key +
// uniform alpha every time the window style is touched.
::SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), (BYTE)g_OverlayAlpha, LWA_COLORKEY | LWA_ALPHA);

if (Options::Misc::HideFromTabs)
HideFromTaskbar(hwnd);
}

void SetTransparency(HWND hwnd, bool clickThrough)
{
ApplyOverlayWindowStyle(hwnd, clickThrough);
}

// -----------------------------------------------------------------------------
// Miniplayer input hotzone. With the menu closed the overlay window is
// WS_EX_TRANSPARENT, so the floating player's controls would be dead. While
// the cursor sits over the card (Roblox in the foreground, menu closed), flip
// the overlay to clickable + WS_EX_NOACTIVATE so pressing play/pause never
// takes focus away from the game. GetCursorPos is polled directly because a
// transparent window receives no mouse messages and therefore cannot observe
// the cursor entering the rect.
static void UpdateMiniplayerClickThrough(HWND hwnd, bool menuOpen)
{
    using namespace SpotifyVisualizer;

    static bool lastClickThrough = true;
    static bool lastNoActivate = false;

    bool hot = false;
    if (!menuOpen && Options::Music::Miniplayer && g_mpRectValid && IsGameOnTop("Roblox"))
    {
        POINT pt;
        if (::GetCursorPos(&pt))
            hot = pt.x >= g_mpRectMin.x && pt.x < g_mpRectMax.x &&
                  pt.y >= g_mpRectMin.y && pt.y < g_mpRectMax.y;
    }
    g_mpHotActive = hot;

    const bool clickThrough = !menuOpen && !hot;
    const bool noActivate = hot;
    if (clickThrough != lastClickThrough || noActivate != lastNoActivate)
    {
        ApplyOverlayWindowStyle(hwnd, clickThrough, noActivate);
        lastClickThrough = clickThrough;
        lastNoActivate = noActivate;
    }
}

void HideFromTaskbar(HWND hwnd)
{
ITaskbarList* taskbarList = nullptr;
if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskbarList, reinterpret_cast<void**>(&taskbarList))))
{
taskbarList->HrInit();
taskbarList->DeleteTab(hwnd);
taskbarList->Release();
}
}

void DrawNode(RobloxInstance& node)
{
const auto& children = node.GetChildren();
if (children.empty())
{
ImGui::BulletText(node.Name().c_str());
}
else
{
if (ImGui::TreeNode(node.Name().c_str()))
{
for (auto child : children)
{
DrawNode(child);
}
ImGui::TreePop();
}
}
}

static void RenderConfigTab(float ctX, float fullW)
{
const float sc = std::clamp(Options::Misc::MenuScale, 0.6f, 2.5f);
// Same two-column geometry every other tab uses: two half-width cards filling
// the content area, separated by ColGap. The tab previously used the fixed
// UI::CardW (340px), which left a wide empty strip on the right of an 800px
// content area.
const float halfW = (fullW - ctX - UI::ColGap) * 0.5f;
UI::ContentHeader("CONFIGS");
static char configNameBuffer[128] = "";
static std::vector<std::string> configsList;
static int selectedConfigIndex = -1;
static std::string configStatusMessage;
static bool configStatusSuccess = true;
static bool listInitialized = false;
static AutoloadSettings autoloadSettings;
static bool autoloadEnabled = false;

if (!listInitialized)
{
configsList = ListConfigFiles();
autoloadSettings = LoadAutoloadSettings();
autoloadEnabled = autoloadSettings.enabled;
listInitialized = true;
}

const float panelY = ImGui::GetCursorPosY();
// Both cards share the same width and the same fixed content height, and both
// start at panelY, so their headers and bottom edges line up exactly. The
// height is derived from the space actually available in the content child
// rather than a guessed constant, so the cards fill the menu.
const float cardInnerW = halfW;
const float availH = ImGui::GetContentRegionAvail().y;
const float configCardH = (availH > 240.0f * sc) ? availH : 240.0f * sc;
    ImGui::SetCursorPosX(ctX);
    if (UI::CollapsibleSection("MANAGE CONFIGS", cardInnerW, true, configCardH))
{
// The autoload controls sit at the very top of the card. Labels are drawn by
// labelsection(); the status line gets its own row with explicit spacing so it
// can never overlap the checkbox frame above it.
UI::labelsection("AUTOLOAD");
if (UI::Checkbox("Autoload on startup", &autoloadEnabled))
{
autoloadSettings.enabled = autoloadEnabled;
if (SaveAutoloadSettings(autoloadSettings))
{
configStatusSuccess = true;
configStatusMessage = autoloadEnabled ? "Autoload enabled" : "Autoload disabled";
}
else
{
configStatusSuccess = false;
configStatusMessage = Config::lastError.empty() ? "Failed to save autoload setting" : Config::lastError;
}
}

// Dedicated row for the current autoload target. labelsection() draws its text
// at PAD, so this row is indented to the same edge to sit under the section
// label rather than floating at an arbitrary offset, and it is separated from
// the checkbox above so the two can never overlap.
{
ImGui::Dummy(ImVec2(0.0f, 3.0f * sc));
const float savedX = ImGui::GetCursorPosX();
ImGui::SetCursorPosX(savedX + UI::PAD * sc);
if (autoloadSettings.configName.empty())
ImGui::TextDisabled("No autoload config set");
else
ImGui::TextColored(UI::P.accent, "Autoload: %s", autoloadSettings.configName.c_str());
ImGui::SetCursorPosX(savedX);
ImGui::Dummy(ImVec2(0.0f, 3.0f * sc));
}

if (ImGui::Button("Refresh List", ImVec2(-1, 30.0f * sc)))
configsList = ListConfigFiles();

ImGui::Dummy(ImVec2(0.0f, 2.0f * sc));

char _clsBuf[64]; snprintf(_clsBuf, sizeof(_clsBuf), "CONFIG LIST (%d)", (int)configsList.size()); UI::labelsection(_clsBuf);

const float listHeight = ImGui::GetContentRegionAvail().y;
ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.38f));
ImGui::BeginChild("##config_list_scroll", ImVec2(-1, listHeight > 24.0f ? listHeight : 24.0f), false);
for (int i = 0; i < (int)configsList.size(); i++)
{
ImGui::PushID(i);
const bool isAutoload = !autoloadSettings.configName.empty()
&& NormalizeConfigFilename(configsList[i]) == autoloadSettings.configName;
const bool isSelected = (selectedConfigIndex == i);

// Selected rows use the accent tint so the current selection is obvious at a
// glance; the autoload config is marked with a trailing asterisk.
const std::string rowLabel = isAutoload
? (configsList[i] + "  *")
: configsList[i];

if (isSelected)
{
ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(UI::P.accent.x, UI::P.accent.y, UI::P.accent.z, 0.55f));
ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(UI::P.accent.x, UI::P.accent.y, UI::P.accent.z, 0.65f));
ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(UI::P.accent.x, UI::P.accent.y, UI::P.accent.z, 0.75f));
}
else
{
ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.06f, 0.06f, 0.06f, 1.0f));
ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(UI::P.accent.x, UI::P.accent.y, UI::P.accent.z, 0.30f));
ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(UI::P.accent.x, UI::P.accent.y, UI::P.accent.z, 0.55f));
}

// Clicking a row only selects it and mirrors the name into the editor. Loading
// is deliberately left to the Load button so a stray click can't swap the whole
// config out from under you mid-game.
if (ImGui::Button(rowLabel.c_str(), ImVec2(-1, 28)))
{
selectedConfigIndex = i;
strncpy_s(configNameBuffer, configsList[i].c_str(), _TRUNCATE);
configStatusMessage.clear();
}
if (ImGui::IsItemHovered())
ImGui::SetTooltip("Click to select, then press Load");

ImGui::PopStyleColor(3);
ImGui::PopID();
}
ImGui::EndChild();
ImGui::PopStyleVar(2);
}
UI::CollapsibleEnd();

ImGui::SetCursorPosY(panelY);
    ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
    if (UI::CollapsibleSection("ACTIONS", cardInnerW, true, configCardH))
    {
        // Shared metrics so every row in this card uses one button height and
        // one spacing value; the rows previously carried ad-hoc offsets.
        const float rowH = 30.0f * sc;
        const float rowGap = 6.0f * sc;
        // Keep config action labels clear of the lower edge of the button frame.
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
        if (!configStatusMessage.empty())
{
const ImVec4 statusColor = configStatusSuccess
? ImVec4(UI::P.accent.x, UI::P.accent.y, UI::P.accent.z, 1.0f)
: ImVec4(1.0f, 0.45f, 0.45f, 1.0f);
ImGui::PushStyleColor(ImGuiCol_Text, statusColor);
ImGui::TextWrapped("%s", configStatusMessage.c_str());
ImGui::PopStyleColor();
ImGui::Dummy(ImVec2(0.0f, rowGap));
}

UI::labelsection("CONFIG NAME");
ImGui::InputText("##config_name_edit", configNameBuffer, IM_ARRAYSIZE(configNameBuffer));
ImGui::Dummy(ImVec2(0.0f, rowGap));

if (ImGui::Button("Load", ImVec2(-1, rowH)))
{
const std::string name = NormalizeConfigFilename(configNameBuffer);
if (name.empty())
{
configStatusSuccess = false;
configStatusMessage = "Enter a config name first";
}
else
{
strncpy_s(configNameBuffer, name.c_str(), _TRUNCATE);
if (LoadConfig(name))
{
configStatusSuccess = true;
configStatusMessage = "Loaded " + name;
g_MenuWeatherNeedsBootstrap = true;
}
else
{
configStatusSuccess = false;
configStatusMessage = Config::lastError.empty() ? ("Failed to load " + name) : Config::lastError;
}
}
}

ImGui::Dummy(ImVec2(0.0f, rowGap));

if (ImGui::Button("Save", ImVec2(-1, rowH)))
{
const std::string name = NormalizeConfigFilename(configNameBuffer);
if (name.empty())
{
configStatusSuccess = false;
configStatusMessage = "Enter a config name first";
}
else
{
strncpy_s(configNameBuffer, name.c_str(), _TRUNCATE);
if (SaveConfig(name))
{
configsList = ListConfigFiles();
selectedConfigIndex = -1;
for (int i = 0; i < (int)configsList.size(); i++)
{
if (configsList[i] == name)
{
selectedConfigIndex = i;
break;
}
}
configStatusSuccess = true;
configStatusMessage = "Saved " + name;
}
else
{
configStatusSuccess = false;
configStatusMessage = Config::lastError.empty() ? ("Failed to save " + name) : Config::lastError;
}
}
}

ImGui::Dummy(ImVec2(0.0f, rowGap));

if (ImGui::Button("Set Autoload", ImVec2(-1, rowH)))
{
const std::string name = NormalizeConfigFilename(configNameBuffer);
if (name.empty())
{
configStatusSuccess = false;
configStatusMessage = "Select or enter a config first";
}
else if (!std::filesystem::exists(GetConfigFilePath(name)))
{
configStatusSuccess = false;
configStatusMessage = "Config not found";
}
else
{
autoloadSettings.configName = name;
autoloadSettings.enabled = true;
autoloadEnabled = true;
if (SaveAutoloadSettings(autoloadSettings))
{
configStatusSuccess = true;
configStatusMessage = "Autoload set to " + name;
}
else
{
configStatusSuccess = false;
configStatusMessage = Config::lastError.empty() ? "Failed to save autoload" : Config::lastError;
}
}
}

ImGui::Dummy(ImVec2(0.0f, rowGap));

if (ImGui::Button("Delete", ImVec2(-1, rowH)))
{
const std::string name = NormalizeConfigFilename(configNameBuffer);
if (name.empty())
{
configStatusSuccess = false;
configStatusMessage = "Enter a config name first";
}
else
{
const std::filesystem::path fullPath = GetConfigFilePath(name);
std::error_code ec;
if (std::filesystem::exists(fullPath, ec) && !ec)
{
std::filesystem::remove(fullPath, ec);
if (ec)
{
configStatusSuccess = false;
configStatusMessage = "Delete failed: " + ec.message();
}
else
{
ClearAutoloadIfMatches(name);
autoloadSettings = LoadAutoloadSettings();
autoloadEnabled = autoloadSettings.enabled;
configNameBuffer[0] = '\0';
selectedConfigIndex = -1;
configsList = ListConfigFiles();
configStatusSuccess = true;
configStatusMessage = "Deleted " + name;
}
}
else
{
configStatusSuccess = false;
configStatusMessage = "Config not found";
}
}
}

UI::labelsection("FILE ACTIONS");

if (ImGui::Button("Import Config...", ImVec2(-1, rowH)))
{
std::string pickedPath;
if (OpenWindowsFileDialog(true, pickedPath, "*.json\0*.json\0All Files\0*.*\0", SX("Import Seraph config").c_str()))
{
const bool ok = ImportConfigFromFile(std::filesystem::path(pickedPath));
if (ok)
{
configsList = ListConfigFiles();
selectedConfigIndex = -1;
configStatusSuccess = true;
configStatusMessage = "Imported " + std::filesystem::path(pickedPath).stem().string() + ".json";
}
else
{
configStatusSuccess = false;
configStatusMessage = Config::lastError.empty() ? "Import failed" : Config::lastError;
}
}
}

ImGui::Dummy(ImVec2(0.0f, rowGap));

const std::string exportSource = configsList.empty()
? NormalizeConfigFilename(configNameBuffer)
: (selectedConfigIndex >= 0 && selectedConfigIndex < (int)configsList.size()
? NormalizeConfigFilename(configsList[selectedConfigIndex])
: NormalizeConfigFilename(configNameBuffer));

if (ImGui::Button("Export Config...", ImVec2(-1, 28)))
{
if (exportSource.empty())
{
configStatusSuccess = false;
configStatusMessage = "Select a config to export first";
}
else
{
std::string pickedPath;
const std::string suggestedName = exportSource;
if (OpenWindowsFileDialog(false, pickedPath, "*.json\0*.json\0All Files\0*.*\0", SX("Export Seraph config").c_str(), suggestedName))
{
const bool ok = ExportConfigToFile(exportSource, std::filesystem::path(pickedPath));
if (ok)
{
configStatusSuccess = true;
configStatusMessage = "Exported " + exportSource;
}
else
{
configStatusSuccess = false;
configStatusMessage = Config::lastError.empty() ? "Export failed" : Config::lastError;
}
}
}
}
}
ImGui::PopStyleVar();
UI::CollapsibleEnd();
}

// (OpenWindowsFileDialog + UTF8ToWide / WideToUTF8 live in
// Seraph/rbx/configs/configs.h)

void RenderKeybindList(ImDrawList* drawList)
{
if (!Options::Misc::KeybindList)
return;

// Every feature with a keybind state, in menu order. ToggleType convention:
// 0 = Hold, 1 = Toggle, 2 = Always On.
struct Bind { const char* name; const char* mode; };
std::vector<Bind> activeBinds;

auto push3 = [&activeBinds](const char* name, bool enabled, int toggleType, int key, bool toggled)
{
    if (!enabled)
        return;
    bool active = (toggleType == 2)
        || (toggleType == 1 ? toggled
                            : (key != 0 && (GetAsyncKeyState(key) & 0x8000) != 0));
    if (!active)
        return;
    activeBinds.push_back({ name, toggleType == 2 ? "ON" : toggleType == 1 ? "TOGGLE" : "HOLD" });
};

push3("Aimbot", Options::Aimbot::Aimbot, Options::Aimbot::ToggleType, Options::Aimbot::AimbotKey, Options::Aimbot::Toggled);
push3("Triggerbot", Options::Triggerbot::Enabled, Options::Triggerbot::ToggleType, Options::Triggerbot::TriggerbotKey, Options::Triggerbot::Toggled);
push3("Fly", Options::Fly::Enabled, Options::Fly::ToggleType, Options::Fly::FlyKey, Options::Fly::Toggled);
push3("WalkSpeed", Options::WalkSpeed::Enabled, Options::WalkSpeed::ToggleType, Options::WalkSpeed::WalkSpeedKey, Options::WalkSpeed::Toggled);
push3("Noclip", Options::Noclip::Enabled, Options::Noclip::ToggleType, Options::Noclip::NoclipKey, Options::Noclip::Toggled);
push3("Orbit", Options::Orbit::Enabled, Options::Orbit::ToggleType, Options::Orbit::OrbitKey, Options::Orbit::Toggled);
push3("Desync", Options::Desync::Enabled, Options::Desync::ToggleType, Options::Desync::DesyncKey, Options::Desync::Toggled);
push3("Ramp Fling", Options::RampFling::Enabled, Options::RampFling::ToggleType, Options::RampFling::FlingKey, Options::RampFling::Toggled);
push3("VoidHide", Options::VoidHide::Enabled, Options::VoidHide::ToggleType, Options::VoidHide::VoidHideKey, Options::VoidHide::Toggled);
push3("Bhop", Options::Bhop::Enabled, 0, Options::Bhop::BhopKey, false);

// ESP uses its own convention (ToggleType 0 = Always).
if (Options::ESP::Enabled)
{
    const bool active = (Options::ESP::ToggleType == 0)
        || (Options::ESP::ToggleType == 1 && Options::ESP::Toggled);
    if (active)
        activeBinds.push_back({ "ESP", Options::ESP::ToggleType == 0 ? "ALWAYS" : "ON" });
}

if (activeBinds.empty())
    return;

// ── Layout: near-black panel, accent spine on the left, badge pills on the right.
ImFont* font = ImGui::GetFont();
const float fs = ImGui::GetFontSize();
const float pad = 10.0f;
const float row_h = fs + 7.0f;
const float title_h = 24.0f;
const float badge_w = 56.0f;
const float badge_gap = 10.0f;

float name_w = 0.0f;
for (const auto& b : activeBinds)
    name_w = std::max(name_w, font->CalcTextSizeA(fs, FLT_MAX, 0.0f, b.name).x);

const float box_w = pad + name_w + badge_gap + badge_w + pad;
const float box_h = title_h + activeBinds.size() * row_h + 7.0f;
const ImVec2 pos(Options::Misc::KeybindListX, Options::Misc::KeybindListY);
const ImVec2 p1(pos.x + box_w, pos.y + box_h);

const int ax = (int)(main_color.x * 255), ay = (int)(main_color.y * 255), az = (int)(main_color.z * 255);
const ImU32 accent = IM_COL32(ax, ay, az, 255);

drawList->AddRectFilled(pos, p1, IM_COL32(10, 10, 10, 238), 6.0f);
drawList->AddRect(pos, p1, IM_COL32(34, 34, 34, 255), 6.0f, 0, 1.0f);
drawList->AddRectFilled(ImVec2(pos.x + 1.0f, pos.y + 7.0f), ImVec2(pos.x + 3.5f, p1.y - 7.0f), accent, 1.5f);

// Title row: accent dot + label, live count on the right, hairline separator.
const float cy = pos.y + (title_h - fs) * 0.5f;
drawList->AddCircleFilled(ImVec2(pos.x + pad + 3.0f, pos.y + title_h * 0.5f), 3.0f, accent, 12);
drawList->AddText(font, fs, ImVec2(pos.x + pad + 12.0f, cy), IM_COL32(200, 200, 205, 255), "KEYBINDS");
char cnt[12];
std::snprintf(cnt, sizeof(cnt), "%d", (int)activeBinds.size());
const ImVec2 cnt_sz = font->CalcTextSizeA(fs - 1.0f, FLT_MAX, 0.0f, cnt);
drawList->AddText(font, fs - 1.0f, ImVec2(p1.x - pad - cnt_sz.x, cy + 1.0f), IM_COL32(120, 120, 128, 255), cnt);
drawList->AddLine(ImVec2(pos.x, pos.y + title_h), ImVec2(p1.x, pos.y + title_h), IM_COL32(255, 255, 255, 14));

// Rows: name left, accent-outlined mode pill right.
float y = pos.y + title_h + 4.0f;
for (const auto& b : activeBinds)
{
    drawList->AddText(font, fs, ImVec2(pos.x + pad, y), IM_COL32(232, 232, 236, 255), b.name);

    const ImVec2 msz = font->CalcTextSizeA(fs - 2.0f, FLT_MAX, 0.0f, b.mode);
    const ImVec2 b0(p1.x - pad - badge_w, y - 2.0f);
    const ImVec2 b1(p1.x - pad, y - 2.0f + fs + 4.0f);
    drawList->AddRectFilled(b0, b1, IM_COL32(ax * 60 / 255 + 10, ay * 60 / 255 + 10, az * 60 / 255 + 10, 130), 3.0f);
    drawList->AddRect(b0, b1, IM_COL32(ax * 160 / 255, ay * 160 / 255, az * 160 / 255, 130), 3.0f, 0, 1.0f);
    drawList->AddText(font, fs - 2.0f,
        ImVec2(b0.x + (badge_w - msz.x) * 0.5f, b0.y + ((b1.y - b0.y) - msz.y) * 0.5f),
        accent, b.mode);

    y += row_h;
}
}

// ── Katana Alert ─────────────────────────────────────────────────────────
// Red animated "PARRYING" warning drawn to the center of the screen while any
// enemy has a katana out (Rivals). Heart-beat pulse + two staggered expanding
// rings that evoke the incoming deflection.
static void RenderKatanaAlert(ImDrawList* dl)
{
    const ImVec2 dims = ImGui::GetIO().DisplaySize;
    const float t = (float)ImGui::GetTime();
    const float cycle = fmodf(t * 1.15f, 1.0f);

    ImFont* font = UI::logo_font ? UI::logo_font : ImGui::GetFont();
    const float size = (font == UI::logo_font) ? font->FontSize : 25.0f;
    const char* txt = "> PARRYING <";
    ImVec2 tsz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, txt);
    const ImVec2 c = ImVec2((dims.x - tsz.x) * 0.5f, dims.y * 0.30f - tsz.y * 0.5f);
    const ImVec2 ctr = ImVec2(dims.x * 0.5f, c.y + tsz.y * 0.5f);

    // two staggered expanding rings, a fresh one every ~0.43s
    for (int i = 0; i < 2; i++)
    {
        float ph = fmodf(cycle + i * 0.5f, 1.0f);
        float rr = 26.0f + ph * 58.0f;
        int a = (int)(130.0f * (1.0f - ph));
        dl->AddCircle(ctr, rr, IM_COL32(255, 46, 60, a), 48, 2.0f);
    }

    const float pulse = (sinf(t * 6.0f) + 1.0f) * 0.5f;
    const ImU32 col = IM_COL32(255, 46, 60, (int)(230 + pulse * 25));
    dl->AddText(font, size, ImVec2(c.x + 2.5f, c.y + 2.8f), IM_COL32(12, 10, 12, 215), txt);
    dl->AddText(font, size, ImVec2(c.x - 0.8f, c.y - 0.8f), IM_COL32(80, 8, 12, 160), txt);
    dl->AddText(font, size, c, col, txt);
}

// ── Player List window (Misc -> "Player List") ────────────────────────────
// Mirrors the Explorer window look. Lists live players from the player-object
// cache; clicking a player selects them and lets you toggle Exclude / Focus /
// clear, or add/remove them as a friend. Drives the existing PlayerFilter.
static void RenderPlayerListWindow(bool* open)
{
    if (!open || !*open)
        return;

    const ImVec4 border_outer = ImVec4(0.13f, 0.13f, 0.13f, 1.f);
    constexpr float title_h = 26.f;
    constexpr float margin = 3.f;

    static ImVec2 playerListPos = ImVec2(-1, -1);
    static ImVec2 playerListSize = ImVec2(380.f, 470.f);
    static bool playerListPosInitialized = false;

    if (!playerListPosInitialized)
    {
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        playerListPos = ImVec2(center.x - 180.f, center.y - 60.f);
        playerListPosInitialized = true;
    }

    ImGui::SetNextWindowPos(playerListPos, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(playerListSize, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(280.f, 300.f), ImVec2(FLT_MAX, FLT_MAX));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_Border, border_outer);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.08f, 1.f));
    bool visible = ImGui::Begin("##playerlist_window", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();

    if (!visible)
    {
        ImGui::End();
        return;
    }

    ImVec2 wp = ImGui::GetWindowPos();
    ImVec2 ws = ImGui::GetWindowSize();
    playerListPos = wp;
    playerListSize = ws;
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // Snapshot of the live player cache (never read the shared vector unlocked).
    // Taken before the title bar so the count badge can show it too.
    std::vector<RobloxPlayer> players;
    {
        std::lock_guard<std::mutex> lock(Globals::Caches::CachedPlayerObjectsMutex);
        players = Globals::Caches::CachedPlayerObjects;
    }
    std::string localName = Globals::Roblox::LocalPlayer.address ? Globals::Roblox::LocalPlayer.Name() : std::string();
    static std::string selected;

    draw->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + title_h), IM_COL32(20, 20, 20, 255));
    const char* title = "player list";
    ImVec2 title_ts = ImGui::CalcTextSize(title);
    draw->AddText(ImVec2(wp.x + (ws.x - title_ts.x) * 0.5f, wp.y + (title_h - title_ts.y) * 0.5f), IM_COL32(230, 230, 230, 255), title);

    // Close button
    ImVec2 xsz = ImGui::CalcTextSize("X");
    float x_w = xsz.x + 14.f;

    // Player-count pill, sits left of the close button.
    {
        char cnt[16];
        std::snprintf(cnt, sizeof(cnt), "%zu", players.size());
        const ImVec2 csz = ImGui::CalcTextSize(cnt);
        const float pill_w = csz.x + 12.f;
        const ImVec2 p0(wp.x + ws.x - x_w - pill_w - 6.f, wp.y + (title_h - 14.f) * 0.5f);
        draw->AddRectFilled(p0, ImVec2(p0.x + pill_w, p0.y + 14.f),
            IM_COL32((int)(main_color.x * 60), (int)(main_color.y * 60), (int)(main_color.z * 60), 170), 7.f);
        draw->AddText(ImVec2(p0.x + (pill_w - csz.x) * 0.5f, p0.y + (14.f - csz.y) * 0.5f),
            IM_COL32(235, 235, 235, 255), cnt);
    }

    float drag_w = ws.x - x_w - 6.f;
    ImGui::SetCursorScreenPos(ImVec2(wp.x, wp.y));
    ImGui::InvisibleButton("##playerlist_drag", ImVec2(drag_w, title_h));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        ImVec2 cur = ImGui::GetWindowPos();
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        ImGui::SetWindowPos(ImVec2(cur.x + delta.x, cur.y + delta.y));
    }
    ImVec2 xmin(wp.x + ws.x - x_w, wp.y);
    ImGui::SetCursorScreenPos(xmin);
    ImGui::InvisibleButton("##playerlist_close", ImVec2(x_w, title_h));
    bool xhov = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked())
        *open = false;
    draw->AddText(ImVec2(xmin.x + 7.f, wp.y + (title_h - xsz.y) * 0.5f),
        xhov ? IM_COL32(255, 255, 255, 255) : IM_COL32(160, 160, 160, 255), "X");

    float body_top = title_h + margin;
    float body_h = ws.y - body_top - margin;

    const bool hasSelection = !selected.empty();
    const float panel_h = hasSelection ? 112.f : 0.f;
    const float list_h = body_h - (hasSelection ? panel_h : 0.f);

    ImGui::SetCursorPos(ImVec2(margin, body_top));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.18f, 0.18f, 0.18f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.06f, 0.06f, 1.f));
    ImGui::BeginChild("##playerlist_body", ImVec2(ws.x - margin * 2.f, list_h), true);

    // Row layout constants (the body child uses zero padding, so everything is
    // positioned by hand against the draw list).
    const float avail = ImGui::GetContentRegionAvail().x;
    const float row_h = 42.f;
    const float gap = 4.f;
    const float pad = 8.f;
    const float av_d = 30.f;
    ImFont* const font = ImGui::GetFont();
    const float fs = ImGui::GetFontSize();

    for (size_t i = 0; i < players.size(); i++)
    {
        const RobloxPlayer& p = players[i];
        if (p.Name.empty())
            continue;

        bool isLocal = (p.Name == localName);
        int mark = PlayerFilter::GetMark(p.Name);
        bool isFriendly = PlayerFilter::IsFriend(p.Name);

        char id[64];
        std::snprintf(id, sizeof(id), "##plrow_%zu", i);
        const ImVec2 r0 = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(id, ImVec2(avail, row_h - gap));
        const bool hovered = ImGui::IsItemHovered();
        const bool itemSel = (selected == p.Name);
        if (ImGui::IsItemClicked())
            selected = p.Name;

        const ImVec2 r1(r0.x + avail, r0.y + row_h - gap);

        // Row background: accent-tinted when selected, subtle lift on hover.
        if (itemSel)
            draw->AddRectFilled(r0, r1, IM_COL32((int)(main_color.x * 70), (int)(main_color.y * 70), (int)(main_color.z * 70), 90), 5.0f);
        else if (hovered)
            draw->AddRectFilled(r0, r1, IM_COL32(255, 255, 255, 14), 5.0f);

        // Avatar: circular headshot with a status dot for friend/focus/exclude.
        const ImVec2 avC(r0.x + pad + av_d * 0.5f, r0.y + (row_h - gap) * 0.5f);
        std::int64_t uid = Cheat::Features::PlayerAvatars::LookupUserId(p.Name);
        ID3D11ShaderResourceView* av = uid != 0 ? Cheat::Features::PlayerAvatars::Get(uid) : nullptr;
        const ImVec2 avMin(avC.x - av_d * 0.5f, avC.y - av_d * 0.5f);
        const ImVec2 avMax(avC.x + av_d * 0.5f, avC.y + av_d * 0.5f);
        if (av)
        {
            draw->AddImageRounded((ImTextureID)av, avMin, avMax, ImVec2(0, 0), ImVec2(1, 1),
                IM_COL32(255, 255, 255, 255), av_d * 0.5f);
        }
        else
        {
            draw->AddCircleFilled(avC, av_d * 0.5f, IM_COL32(42, 42, 46, 255), 24);
            char initial[2] = { p.Name.empty() ? '?' : (char)toupper((unsigned char)p.Name[0]), 0 };
            const ImVec2 isz = font->CalcTextSizeA(fs, FLT_MAX, 0.f, initial);
            draw->AddText(font, fs, ImVec2(avC.x - isz.x * 0.5f, avC.y - isz.y * 0.5f),
                IM_COL32(150, 150, 158, 255), initial);
        }
        draw->AddCircle(avC, av_d * 0.5f + 1.0f, IM_COL32(255, 255, 255, itemSel ? 70 : 32), 24, 1.2f);
        if (mark != Options::PlayerFilter::None || isFriendly)
        {
            ImU32 dcol = IM_COL32(150, 150, 150, 255);
            if (mark == Options::PlayerFilter::Exclude) dcol = IM_COL32(235, 84, 84, 255);
            else if (mark == Options::PlayerFilter::Focus) dcol = IM_COL32(80, 210, 120, 255);
            else if (isFriendly) dcol = IM_COL32(96, 165, 250, 255);
            const ImVec2 dotC(avMax.x - 2.f, avMax.y - 2.f);
            draw->AddCircleFilled(dotC, 4.5f, IM_COL32(10, 10, 10, 255), 12);
            draw->AddCircleFilled(dotC, 3.2f, dcol, 12);
        }

        // Text block (name + sub line + health bar), clipped to the row.
        const float text_x = r0.x + pad + av_d + 10.f;
        const float row_right = r0.x + avail - pad;
        const float bar_x0 = text_x;
        float bar_x1 = text_x + font->CalcTextSizeA(fs, FLT_MAX, 0.f, p.Name.c_str()).x;
        if (bar_x1 < text_x + 60.f)
            bar_x1 = text_x + 60.f;
        if (bar_x1 > row_right)
            bar_x1 = row_right;
        draw->PushClipRect(ImVec2(text_x - 2.f, r0.y - 2.f), ImVec2(row_right + 2.f, r1.y + 8.f), true);

        const ImU32 nameCol = isLocal ? IM_COL32(120, 220, 160, 255) : IM_COL32(235, 235, 238, 255);
        draw->AddText(font, fs, ImVec2(text_x, r0.y + 3.f), nameCol, p.Name.c_str());
        if (isLocal)
        {
            const float nw = font->CalcTextSizeA(fs, FLT_MAX, 0.f, p.Name.c_str()).x;
            const ImVec2 c0(text_x + nw + 6.f, r0.y + 3.f);
            const ImVec2 csz(font->CalcTextSizeA(fs - 1.f, FLT_MAX, 0.f, "YOU").x + 8.f, fs);
            draw->AddRectFilled(c0, ImVec2(c0.x + csz.x, c0.y + csz.y),
                IM_COL32((int)(main_color.x * 90), (int)(main_color.y * 90), (int)(main_color.z * 90), 170), 3.f);
            draw->AddText(font, fs - 1.f, ImVec2(c0.x + 4.f, c0.y + 1.5f), IM_COL32(255, 255, 255, 235), "YOU");
        }

        // Sub line: level + rank + team + tool, behind the rank icon when art exists.
        std::string sub;
        if (p.LevelResolved && p.Level >= 0)
        {
            char b[24];
            std::snprintf(b, sizeof(b), "Lv %lld", static_cast<long long>(p.Level));
            sub = b;
        }
        if (p.RankResolved && p.Elo >= 0)
        {
            if (!sub.empty()) sub += "  ";
            sub += RankFromElo(p.Elo);
        }
        if (!p.TeamName.empty())
        {
            if (!sub.empty()) sub += "  ";
            sub += p.TeamName;
        }
        if (!p.ToolName.empty())
        {
            if (!sub.empty()) sub += "  ";
            sub += p.ToolName;
        }

        const float sy = r0.y + 9.f + fs;
        float sx = text_x;
        if (p.RankResolved && !p.RankStem.empty())
        {
            ImTextureID rankTex = RankIcons::Get(p.RankStem);
            if (rankTex)
            {
                const float rs = (fs + 1.f > 16.f) ? 16.f : fs + 1.f;
                draw->AddImageRounded(rankTex, ImVec2(sx, sy - 1.f), ImVec2(sx + rs, sy - 1.f + rs),
                    ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255), 2.f);
                sx += rs + 5.f;
            }
        }
        if (!sub.empty())
            draw->AddText(font, fs - 1.f, ImVec2(sx, sy), IM_COL32(150, 150, 158, 255), sub.c_str());
        draw->PopClipRect();

        // Horizontal health bar under the name.
        const float barY = r0.y + 4.f + fs;
        draw->AddRectFilled(ImVec2(bar_x0, barY), ImVec2(bar_x1, barY + 3.f),
            IM_COL32(255, 255, 255, 20), 1.5f);
        const float frac = (p.MaxHealth > 0.f) ? std::clamp(p.Health / p.MaxHealth, 0.f, 1.f) : 0.f;
        const ImU32 hcol = frac > 0.5f ? IM_COL32(76, 205, 120, 255)
            : frac > 0.25f ? IM_COL32(240, 190, 60, 255)
            : IM_COL32(235, 70, 70, 255);
        const float fillW = (bar_x1 - bar_x0) * frac;
        if (fillW > 1.f)
            draw->AddRectFilled(ImVec2(bar_x0, barY), ImVec2(bar_x0 + fillW, barY + 3.f), hcol, 1.5f);

        if (hovered)
        {
            ImGui::BeginTooltip();
            ImGui::Text("%s%s", p.Name.c_str(), isLocal ? "  (you)" : "");
            if (!p.TeamName.empty()) ImGui::Text("team   %s", p.TeamName.c_str());
            if (!p.ToolName.empty()) ImGui::Text("tool   %s", p.ToolName.c_str());
            if (p.RankResolved && p.Elo >= 0) ImGui::Text("rank   %s", RankFromElo(p.Elo));
            ImGui::Text("health %.0f / %.0f", p.Health, p.MaxHealth);
            ImGui::EndTooltip();
        }
    }

    if (players.empty())
    {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.f);
        ImGui::TextDisabled("no players in cache");
    }

    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();

    // ── Selected-player action panel ────────────────────────────────────────
    if (hasSelection)
    {
        const bool selectedIsLocal = (selected == localName);

        ImGui::SetCursorPos(ImVec2(margin, body_top + list_h));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f, 6.f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.18f, 0.18f, 0.18f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f, 0.07f, 0.07f, 1.f));
        ImGui::BeginChild("##playerlist_panel", ImVec2(ws.x - margin * 2.f, panel_h - margin), true);

        int mark = PlayerFilter::GetMark(selected);
        bool isFriendly = PlayerFilter::IsFriend(selected);

        ImGui::TextUnformatted(selected.c_str());

        // Row 1: filter marks + friend toggle.
        if (mark != Options::PlayerFilter::Exclude && ImGui::SmallButton("Exclude"))
            PlayerFilter::SetMark(selected, Options::PlayerFilter::Exclude);
        ImGui::SameLine();
        if (mark != Options::PlayerFilter::Focus && ImGui::SmallButton("Focus"))
            PlayerFilter::SetMark(selected, Options::PlayerFilter::Focus);
        ImGui::SameLine();
        if (mark != Options::PlayerFilter::None && ImGui::SmallButton("Clear"))
            PlayerFilter::SetMark(selected, Options::PlayerFilter::None);
        ImGui::SameLine();
        if (isFriendly)
        {
            if (ImGui::SmallButton("Unfriend"))
                PlayerFilter::RemoveFriend(selected);
        }
        else
        {
            if (ImGui::SmallButton("Friend"))
                PlayerFilter::AddFriend(selected);
        }

        // Row 2: spectate (drives the existing AimView camera-follow loop) + fling.
        if (!selectedIsLocal)
        {
            const bool spectating = Options::ESP::AimView && Options::ESP::AimViewTarget == selected;
            if (spectating)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(main_color.x * 0.55f, main_color.y * 0.55f, main_color.z * 0.55f, 1.f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(main_color.x * 0.75f, main_color.y * 0.75f, main_color.z * 0.75f, 1.f));
                if (ImGui::SmallButton("Stop Spectating"))
                {
                    Options::ESP::AimView = false;
                    Options::ESP::AimViewTarget.clear();
                }
                ImGui::PopStyleColor(2);
            }
            else if (ImGui::SmallButton("Spectate"))
            {
                Options::ESP::AimViewTarget = selected;
                Options::ESP::AimView = true;
            }
            ImGui::SameLine();
            if (PlayerFling::Busy())
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.16f, 0.18f, 1.f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.16f, 0.18f, 1.f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.16f, 0.16f, 0.18f, 1.f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.f));
                ImGui::SmallButton("flinging...");
                ImGui::PopStyleColor(4);
            }
            else if (ImGui::SmallButton("Fling"))
            {
                PlayerFling::Start(selected);
            }
        }

        // Row 3: persistent filter options.
        ImGui::Checkbox("Exclude Friends", &Options::PlayerFilter::ExcludeFriends);
        ImGui::SameLine();
        ImGui::Checkbox("Focus Only", &Options::PlayerFilter::FocusOnly);

        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
    }

    ImGui::End();
}

void ShowImgui()
{
    OutputDebugStringA("[S] ShowImgui: START\n");
    SeraphLog("[S] ShowImgui: START, resetting overlay flags");
    Globals::overlayShouldShutdown = false;
    Globals::overlayDone = false;
    InitializeConfigPaths();
    OutputDebugStringA("[S] ShowImgui: Calling Executor::Initialize...\n");
    Executor::Initialize();
    OutputDebugStringA("[S] ShowImgui: Executor::Initialize returned\n");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ImGui_ImplWin32_EnableDpiAwareness();
    float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    // Size the overlay to the Roblox client window instead of the whole virtual
    // screen. A fullscreen "screen+1px" layered popup is a textbook overlay
    // tell; binding to the game client rect is far less conspicuous.
    // Globals::Viewport::ScreenPos / Dimensions are populated once the game is
    // attached; fall back to the primary screen until then.
    LONG winX = 0, winY = 0, winW = 0, winH = 0;
    {
        size_t width = (size_t)GetSystemMetrics(SM_CXSCREEN);
        size_t height = (size_t)GetSystemMetrics(SM_CYSCREEN);
        if (Globals::Viewport::Valid && Globals::Viewport::Dimensions.x > 0 && Globals::Viewport::Dimensions.y > 0)
        {
            winX = Globals::Viewport::ScreenPos.x;
            winY = Globals::Viewport::ScreenPos.y;
            winW = (LONG)(Globals::Viewport::Dimensions.x);
            winH = (LONG)(Globals::Viewport::Dimensions.y);
        }
        else
        {
            winX = 0; winY = 0;
            winW = (LONG)width;
            winH = (LONG)height;
        }
    }

    // Benign, non-descript window class + title so the overlay doesn't advertise
    // itself. The old "ImGui Example" class name is a widely-flagged signature.
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"Windows.UI.Core.CoreWindow", nullptr };
    ::RegisterClassExW(&wc);

    HWND hwnd = ::CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST,
        wc.lpszClassName,
        L"",
        WS_POPUP,
        winX, winY, winW, winH,
        nullptr, nullptr, wc.hInstance, nullptr);

    // Opaque swapchain + color-key transparency (this system cannot create
    // DXGI_ALPHA_MODE_PREMULTIPLIED flip-model swapchains -- every permutation
    // of SwapEffect/BufferCount/WS_EX_NOREDIRECTIONBITMAP returns
    // DXGI_ERROR_INVALID_CALL 0x887A0001). Instead we use LWA_COLORKEY:
    // pure-black (RGB 0,0,0) pixels become see-through while all drawn UI/ESP
    // (non-black) renders opaquely, so the game shows through around the GUI.
    // Avoid pure-black foreground colors in the menu/ESP.

    OutputDebugStringA("[S] ShowImgui: Window created\n");
    SeraphLog("[S] ShowImgui: Window created, hwnd=0x" + std::to_string((uintptr_t)hwnd));

    ::SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), (BYTE)g_OverlayAlpha, LWA_COLORKEY | LWA_ALPHA);

    // Publish the overlay HWND so the file-dialog helpers (configs.h)
    // can present Import/Export as modal-to-owner dialogs. This is what
    // makes the OS dialog actually clickable when the overlay is open:
    // ownership forces Windows to route mouse + focus through the
    // dialog above our WS_EX_LAYERED + WS_EX_TOPMOST overlay.
    g_OverlayHWND = hwnd;

    InstallWheelForwarder(hwnd);

    HideFromTaskbar(hwnd);

    // Apply streamproof if enabled (WDA_EXCLUDEFROMCAPTURE = 0x00000011)
    if (Options::Misc::StreamProof)
    {
        SetWindowDisplayAffinity(hwnd, 0x00000011);
    }

    if (!CreateDeviceD3D(hwnd))
    {
        OutputDebugStringA("[S] ShowImgui: CreateDeviceD3D FAILED\n");
        CleanupDeviceD3D();
        ::DestroyWindow(hwnd);
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        CoUninitialize();
        return;
    }

    OutputDebugStringA("[S] ShowImgui: D3D device created\n");
    SeraphLog("[S] ShowImgui: D3D device created");

    ::ShowWindow(hwnd, SW_HIDE);
    ::UpdateWindow(hwnd);
    HideFromTaskbar(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    OutputDebugStringA("[S] ShowImgui: ImGui context created\n");

    ImGuiIO& io = ImGui::GetIO();
    ImGui::StyleColorsDark();

ImFontConfig config;
config.MergeMode = false;
config.PixelSnapH = true;

ImFont* baseFont = io.Fonts->AddFontDefault(&config);

// Menu font list: embedded Nunito Medium (Exterium default, 21px) is index
// 0, then a curated set of Windows system fonts for the runtime "Menu Font"
// combo (Options::Misc::MenuFont) so switching never rebuilds the atlas.
struct MenuFontEntry { ImFont* font; const char* path; const char* name; };
static MenuFontEntry menuFonts[8];
int menuFontCount = 0;

auto LoadSystemFont = [&](const char* path, float size) -> ImFont*
{
const std::wstring widePath = UTF8ToWide(path);
if (widePath.empty()) return nullptr;
if (GetFileAttributesW(widePath.c_str()) == INVALID_FILE_ATTRIBUTES)
return nullptr;
return io.Fonts->AddFontFromFileTTF(path, size, &config, io.Fonts->GetGlyphRangesJapanese());
};

if (menuFontCount < (int)(sizeof(menuFonts)/sizeof(menuFonts[0]))) menuFonts[menuFontCount++] = { io.Fonts->AddFontFromMemoryTTF((void*)NunitoMedium, (int)sizeof(NunitoMedium), 21.0f, &config, io.Fonts->GetGlyphRangesCyrillic()), "", "Nunito" };
if (menuFontCount < (int)(sizeof(menuFonts)/sizeof(menuFonts[0]))) menuFonts[menuFontCount++] = { LoadSystemFont("C:\\Windows\\Fonts\\verdana.ttf", 14.0f), "C:\\Windows\\Fonts\\verdana.ttf", "Verdana" };
if (menuFontCount < (int)(sizeof(menuFonts)/sizeof(menuFonts[0]))) menuFonts[menuFontCount++] = { LoadSystemFont("C:\\Windows\\Fonts\\segoeui.ttf", 14.0f), "C:\\Windows\\Fonts\\segoeui.ttf", "Segoe UI" };
if (menuFontCount < (int)(sizeof(menuFonts)/sizeof(menuFonts[0]))) menuFonts[menuFontCount++] = { LoadSystemFont("C:\\Windows\\Fonts\\tahoma.ttf", 14.0f),  "C:\\Windows\\Fonts\\tahoma.ttf",  "Tahoma" };
if (menuFontCount < (int)(sizeof(menuFonts)/sizeof(menuFonts[0]))) menuFonts[menuFontCount++] = { LoadSystemFont("C:\\Windows\\Fonts\\arial.ttf",   14.0f), "C:\\Windows\\Fonts\\arial.ttf",   "Arial" };
if (menuFontCount < (int)(sizeof(menuFonts)/sizeof(menuFonts[0]))) menuFonts[menuFontCount++] = { LoadSystemFont("C:\\Windows\\Fonts\\georgia.ttf",  14.0f), "C:\\Windows\\Fonts\\georgia.ttf",  "Georgia" };
if (menuFontCount < (int)(sizeof(menuFonts)/sizeof(menuFonts[0]))) menuFonts[menuFontCount++] = { LoadSystemFont("C:\\Windows\\Fonts\\calibri.ttf",  14.0f), "C:\\Windows\\Fonts\\calibri.ttf",  "Calibri" };
if (menuFontCount < (int)(sizeof(menuFonts)/sizeof(menuFonts[0]))) menuFonts[menuFontCount++] = { LoadSystemFont("C:\\Windows\\Fonts\\consola.ttf",  14.0f), "C:\\Windows\\Fonts\\consola.ttf",  "Consolas" };

// Mirror the loaded fonts into the file-scope MenuFonts namespace so
// ShowImgui can switch between them at runtime via PushFont/PopFont
// (instead of rebuilding the font atlas, which would stall the renderer).
for (int i = 0; i < menuFontCount && i < (int)(sizeof(MenuFonts::Fonts)/sizeof(MenuFonts::Fonts[0])); ++i)
MenuFonts::Fonts[i] = menuFonts[i].font;
MenuFonts::Count = menuFontCount;

// Apply current font selection; clamp to the loaded count so an out-of-
// range value falls back gracefully to the first entry (Nunito).
if (Options::Misc::MenuFont >= menuFontCount || Options::Misc::MenuFont < 0)
Options::Misc::MenuFont = 0;
ImFont* font = (menuFontCount > 0 && menuFonts[Options::Misc::MenuFont].font)
? menuFonts[Options::Misc::MenuFont].font
: baseFont;
io.FontDefault = font;

// Exterium UI faces (icons + secondary text) — loaded into the same atlas
// so there is a single texture upload and no per-frame font switching stalls.
{
ImFontConfig uicfg;
uicfg.MergeMode = false;
uicfg.PixelSnapH = true;
uicfg.OversampleH = 6;
uicfg.OversampleV = 6;
const ImWchar* cyr = io.Fonts->GetGlyphRangesCyrillic();
UI::small_font = io.Fonts->AddFontFromMemoryTTF((void*)NunitoMedium, (int)sizeof(NunitoMedium), 17.0f, &uicfg, cyr);
UI::medium_font = io.Fonts->AddFontFromMemoryTTF((void*)NunitoMedium, (int)sizeof(NunitoMedium), 18.0f, &uicfg, cyr);
UI::small_icon_font = io.Fonts->AddFontFromMemoryTTF((void*)NunitoMedium, (int)sizeof(NunitoMedium), 15.0f, &uicfg, cyr);
UI::logo_font = io.Fonts->AddFontFromMemoryTTF((void*)NunitoMedium, (int)sizeof(NunitoMedium), 25.0f, &uicfg, cyr);
uicfg.OversampleH = 8;
uicfg.OversampleV = 8;
UI::icon_font = io.Fonts->AddFontFromMemoryTTF((void*)icomoon, (int)sizeof(icomoon), 18.0f, &uicfg, io.Fonts->GetGlyphRangesDefault());
UI::icon_big_font = io.Fonts->AddFontFromMemoryTTF((void*)icomoon, (int)sizeof(icomoon), 23.0f, &uicfg, io.Fonts->GetGlyphRangesDefault());
UI::arrow_icons = io.Fonts->AddFontFromMemoryTTF((void*)arrowicon, (int)sizeof(arrowicon), 18.0f, &uicfg, io.Fonts->GetGlyphRangesDefault());
}

config.MergeMode = true;
ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    // Opaque bitblt swapchain + color-key transparency: the target has no
    // per-pixel alpha, so use straight-alpha blend so ImGui's colors composite
    // correctly (premultiplied blend would look washed out on an opaque target).
    ImGui_ImplDX11_SetPremultipliedBlend(false);
    ImGui_ImplDX11_CreateDeviceObjects();

    OutputDebugStringA("[S] ShowImgui: ImGui backends initialized\n");

    ImVec4 clear_color = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    bool done = false;
    bool menu_open = false;
    static bool gMenuWasEverOpen = false;
    int tab = 0;
    int tab2 = 0;
    int lastTab = -1;
    static float sScrollTarget = 0.0f;
    static float sScrollCurrent = 0.0f;
    static bool  sScrollActive = false;
    static ImVec2 menuPos = ImVec2(-1, -1); // persisted menu window position; -1 = center on first show
    static bool menuDragging = false;
    static ImVec2 menuDragOffset = ImVec2(0, 0);

    // Overlay is a topmost, click-through ESP layer: always visible in-game.
    // Click-through so it never blocks game input; menu toggles it clickable.
    ::ShowWindow(hwnd, SW_SHOW);
    ApplyOverlayWindowStyle(hwnd, true);
OutputDebugStringA("[S] ShowImgui: Entering render loop\n");
    SeraphLog("[S] ShowImgui: Entering render loop");
    int frameCount = 0;
    LARGE_INTEGER qpcFreq;
    ::QueryPerformanceFrequency(&qpcFreq);
    LARGE_INTEGER frameStart;
    ::timeBeginPeriod(1);
    while (!done && Globals::running && !Globals::overlayShouldShutdown)
    {
        ::QueryPerformanceCounter(&frameStart);
        if (frameCount == 0) {
            OutputDebugStringA("[S] ShowImgui: First frame\n");
        }
        frameCount++;
        // Share one player-list snapshot across every pass in this frame.
        Globals::Caches::NextPlayersFrame();
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

if (g_SwapChainOccluded && g_pSwapChain->Present(0, 0) == DXGI_STATUS_OCCLUDED)
{
::Sleep(10);
continue;
}
g_SwapChainOccluded = false;

if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
{
CleanupRenderTarget();
g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
g_ResizeWidth = g_ResizeHeight = 0;
CreateRenderTarget();
}

ImGui_ImplDX11_NewFrame();
ImGui_ImplWin32_NewFrame();
    if (menu_open)
    {
        const LONG wheel = InterlockedExchange(&g_OverlayWheelAccum, 0);
        if (wheel != 0)
            ImGui::GetIO().MouseWheel += (float)wheel / 4096.0f;
    }
    else
        InterlockedExchange(&g_OverlayWheelAccum, 0);
ImGui::NewFrame();

// Pump the external executor: resumes due coroutines, fires RunService events.
Executor::Tick();

// Update player avatars
Cheat::Features::PlayerAvatars::Tick();

Cheat::Features::ServerBrowser::SetPlaceId((std::int64_t)Globals::Roblox::lastPlaceID);
Cheat::Features::ServerBrowser::Tick();

Globals::Viewport::Update();

Globals::FrameCache::Update();

Visibility::BeginFrame();

// Bootstrap from persistent Options on the first frame, and again
// after a runtime LoadConfig (Configs tab) completes. The latter
// is signalled by the Configs tab setting g_MenuWeatherNeedsBootstrap
// back to true so the engine picks up freshly-loaded values instead
// of SyncToOptions clobbering them with stale Engine state.
if (g_MenuWeatherNeedsBootstrap)
{
MenuWeather::Rebootstrap();
g_MenuWeatherNeedsBootstrap = false;
}
    // Keep Roblox global instance pointers updated/valid to prevent stale-pointer crashes
if (Globals::Roblox::DataModel.address)
{
static DWORD lastUpdateTick = 0;
DWORD currentTick = GetTickCount();
if (currentTick - lastUpdateTick > 500)
{
lastUpdateTick = currentTick;
Globals::Roblox::Workspace = Globals::Roblox::DataModel.FindFirstChildWhichIsA("Workspace");
Globals::Roblox::Players = Globals::Roblox::DataModel.FindFirstChildWhichIsA("Players");
Globals::Roblox::Camera = Globals::Roblox::Workspace.FindFirstChildWhichIsA("Camera");
Globals::Roblox::LocalPlayer = RobloxInstance(Memory->read<uintptr_t>(Globals::Roblox::Players.address + Offsets::Player::LocalPlayer));
}
}

    // Edge-detect on the 0x8000 down-state, NOT on the low bit of
    // GetAsyncKeyState. The low bit is a consumable "pressed since last call"
    // flag, and ImGui::NewFrame() above (imgui.cpp:11702) polls every key with
    // GetAsyncKeyState, clearing that bit before this line is reached - so the
    // old `& 1` test silently dropped most presses and the menu needed several
    // key hits to react. 0x8000 is a live state, not an event, so nothing else
    // can consume it. Every other hotkey in this project already works this way.
    {
        static bool menuKeyWasDown = false;

        if (Options::Misc::MenuKey != 0)
        {
            const bool menuKeyDown = (GetAsyncKeyState(Options::Misc::MenuKey) & 0x8000) != 0;

            if (menuKeyDown && !menuKeyWasDown)
            {
                menu_open = !menu_open;
                gMenuWasEverOpen = true;
                // The auxiliary windows are menu-only: closing the menu closes
                // the explorer and player list too so nothing lingers over the
                // game in click-through mode.
                if (!menu_open)
                {
                    Options::Misc::ExplorerEnabled = false;
                    Options::Misc::PlayerListEnabled = false;
                }
                // Menu open -> clickable; menu closed -> click-through ESP layer. Window stays shown.
                ApplyOverlayWindowStyle(hwnd, !menu_open);
            }

            menuKeyWasDown = menuKeyDown;
        }
        else
        {
            menuKeyWasDown = false;
        }
    }

    // Fade animation (ease-cubic-out time-based, ported from jew-dick-hack
    // ease utilities) -- smooth, non-linear menu open/close transition.
    static float fadeProgress = 0.0f;   // 0..1 (open) / 1..0 (close)
    static bool  fadeWasOpen = false;
    const float dt = ImGui::GetIO().DeltaTime;
    const float easeDur = 0.16f;        // seconds for the eased transition
    if (menu_open)
    {
        fadeWasOpen = true;
        fadeProgress = std::min(1.0f, fadeProgress + (dt / easeDur));
    }
    else
    {
        if (fadeWasOpen)
        {
            // On first frame of close, restart the timing from the current eased value.
            fadeProgress = (fadeProgress <= 0.f) ? 0.f : fadeProgress;
        }
        fadeWasOpen = false;
        fadeProgress = std::max(0.0f, fadeProgress - (dt / easeDur));
    }

    float menuAlpha = anim::ease_cubic_out(fadeProgress);

    // Skip weather physics when the menu has never been opened
    if (gMenuWasEverOpen && (menu_open || menuAlpha > 0.0f))
        MenuWeather::Update((float)ImGui::GetIO().DisplaySize.x, (float)ImGui::GetIO().DisplaySize.y);

    // Click-effect bursts are cheap and short-lived; keep pruning them even
    // while the menu is closing so nothing lingers onto the game.
    MenuClickFx::Update();

// Dynamic streamproof toggle
static bool lastStreamProofState = Options::Misc::StreamProof;
if (lastStreamProofState != Options::Misc::StreamProof)
{
if (Options::Misc::StreamProof)
{
SetWindowDisplayAffinity(hwnd, 0x00000011); // WDA_EXCLUDEFROMCAPTURE
}
else
{
SetWindowDisplayAffinity(hwnd, 0x00000000); // WDA_NONE
}
lastStreamProofState = Options::Misc::StreamProof;
}

// While the menu is closed the overlay is composited over the game with a
// uniform window alpha (LWA_ALPHA) so everything drawn in-game -- including
// an FOV fill -- blends smoothly instead of needing a dither, because a
// layered window's pure-black colour key can only key whole pixels out.
const int wantOverlayAlpha = 255;
if (wantOverlayAlpha != g_OverlayAlpha)
{
g_OverlayAlpha = wantOverlayAlpha;
::SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), (BYTE)g_OverlayAlpha, LWA_COLORKEY | LWA_ALPHA);
}

// Update accent colors from options (with rainbow + gradient support)
if (Options::Misc::RainbowAccent)
{
float t = fmodf(static_cast<float>(ImGui::GetTime()) * Options::Misc::RainbowSpeed, 6.0f);
int segment = static_cast<int>(t);
float frac = t - segment;
float r, g, b;
switch (segment)
{
case 0: r = 1; g = frac; b = 0; break;
case 1: r = 1 - frac; g = 1; b = 0; break;
case 2: r = 0; g = 1; b = frac; break;
case 3: r = 0; g = 1 - frac; b = 1; break;
case 4: r = frac; g = 0; b = 1; break;
default: r = 1; g = 0; b = 1 - frac; break;
}
main_color = ImVec4(r, g, b, 1.0f);
// Second accent is the rainbow hue shifted 180 degrees (opposite side).
float t2 = fmodf(t + 3.0f, 6.0f);
int seg2 = static_cast<int>(t2);
float frac2 = t2 - seg2;
float r2, g2, b2;
switch (seg2)
{
case 0: r2 = 1; g2 = frac2; b2 = 0; break;
case 1: r2 = 1 - frac2; g2 = 1; b2 = 0; break;
case 2: r2 = 0; g2 = 1; b2 = frac2; break;
case 3: r2 = 0; g2 = 1 - frac2; b2 = 1; break;
case 4: r2 = frac2; g2 = 0; b2 = 1; break;
default: r2 = 1; g2 = 0; b2 = 1 - frac2; break;
}
main_color2 = ImVec4(r2, g2, b2, 1.0f);
}
else
{
main_color = ImVec4(Options::Misc::MenuAccentColor[0], Options::Misc::MenuAccentColor[1], Options::Misc::MenuAccentColor[2], 1.0f);
main_color2 = ImVec4(Options::Misc::MenuAccentColor2[0], Options::Misc::MenuAccentColor2[1], Options::Misc::MenuAccentColor2[2], 1.0f);
}

// Resolve the active theme (preset or custom) and apply it.
const float* themeBg = nullptr;
const float* themePanel = nullptr;
const float* themeAccent = nullptr;
const float* themeAccent2 = nullptr;
bool themeGradient = false;
MenuThemes::Resolve(themeBg, themePanel, themeAccent, themeAccent2, themeGradient);
// Kept as a theme-change hook: ApplyFeatureColors is now a no-op, so switching
// themes no longer overwrites the in-game feature colors (they stay at their
// own defaults and the user's picks).
static int lastAppliedTheme = -1;
if (Options::Misc::MenuTheme != lastAppliedTheme)
{
MenuThemes::ApplyFeatureColors(themeAccent, themeAccent2);
lastAppliedTheme = Options::Misc::MenuTheme;
}
// Always use preset accent colors (even for Custom) to prevent old saved configs from overriding
main_color = ImVec4(themeAccent[0], themeAccent[1], themeAccent[2], 1.0f);
main_color2 = ImVec4(themeAccent2[0], themeAccent2[1], themeAccent2[2], 1.0f);
// Gradient flag follows the preset (or the custom toggle).
const bool useGradient = themeGradient;
        (void)useGradient; // Exterium palette is fixed; gradient accent line removed

if (menu_open || menuAlpha > 0.0f)
{
// No full-screen dark dim backdrop drawn here: with color-key transparency the
// opaque pitch-black fill would render as a solid dark band around the menu
// (keyed out only when exactly RGB(0,0,0)), so it was removed. This also drops
// a full-window fill per frame, fixing the jank when spamming the menu key.
// Visuals tab uses a wider layout so the ESP preview has a dedicated side panel.
        // MenuScale acts as a uniform zoom factor for the whole UI.
        const float sc = std::clamp(Options::Misc::MenuScale, 0.6f, 2.5f);
        // keep every subtab pill a uniform width, aligned inside the left rail
        // Wider layout: 187px sidebar + 800px content, two 390px cards.
        UI::sc = sc;
        UI::SidebarX = 10.0f * sc;
        UI::SidebarW = 187.0f * sc;
        UI::ContentX = 197.0f * sc;
        UI::ContentW = 800.0f * sc;
        UI::CardW = 340.0f * sc;
        const float menuWidth = 997.0f * sc;
        const float menuHeight = 680.0f * sc;
// Window frame stays a fixed size so dragging the scale slider doesn't
// resize the window under the cursor (which caused a big/small feedback loop).
// Zoom is applied to content via SetWindowFontScale + scaled positions.
auto s = ImVec2{}, p = ImVec2{}, gs = ImVec2{ menuWidth, menuHeight };

// Center on first show (offset up slightly so Roblox chat at bottom stays visible),
// otherwise keep last dragged position.
if (menuPos.x < 0.0f)
menuPos = ImVec2((io.DisplaySize.x - gs.x) * 0.5f, (io.DisplaySize.y - gs.y) * 0.5f - 30.0f * sc);
ImGui::SetNextWindowPos(menuPos);

ImGui::SetNextWindowSize(gs);
ImGui::SetNextWindowBgAlpha(menuAlpha);
ImGui::PushStyleVar(ImGuiStyleVar_Alpha, menuAlpha);
ImGui::Begin("##GUI", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove);
{
s = ImVec2(ImGui::GetWindowSize().x - ImGui::GetStyle().WindowPadding.x * 2, ImGui::GetWindowSize().y - ImGui::GetStyle().WindowPadding.y * 2);
p = ImVec2(ImGui::GetWindowPos().x + ImGui::GetStyle().WindowPadding.x, ImGui::GetWindowPos().y + ImGui::GetStyle().WindowPadding.y);
auto draw = ImGui::GetWindowDrawList();

// â”€â”€ Title-bar drag (top 25*sc px is the grab region) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        {
            static ImVec2 menuDragTarget = ImVec2(-1, -1);
            const ImVec2 titleMin = ImVec2(p.x, p.y);
            const ImVec2 titleMax = ImVec2(p.x + s.x, p.y + 25.0f * sc);
            if (ImGui::IsMouseHoveringRect(titleMin, titleMax) && !ImGui::IsAnyItemHovered())
            {
                if (ImGui::IsMouseClicked(0))
                {
                    menuDragging = true;
                    menuDragOffset = ImVec2(io.MousePos.x - menuPos.x, io.MousePos.y - menuPos.y);
                    menuDragTarget = menuPos;
                }
            }
            if (menuDragging)
            {
                if (ImGui::IsMouseDown(0))
                    menuDragTarget = ImVec2(io.MousePos.x - menuDragOffset.x, io.MousePos.y - menuDragOffset.y);
                else
                    menuDragging = false;
            }
            // Smooth easing toward target position
            if (menuDragTarget.x >= 0)
            {
                const float rate = 1.0f - expf(-io.DeltaTime * 30.0f);
                menuPos.x += (menuDragTarget.x - menuPos.x) * rate;
                menuPos.y += (menuDragTarget.y - menuPos.y) * rate;
                if (fabsf(menuPos.x - menuDragTarget.x) < 0.5f && fabsf(menuPos.y - menuDragTarget.y) < 0.5f)
                {
                    menuPos = menuDragTarget;
                    menuDragTarget = ImVec2(-1, -1);
                }
            }
        }

        // Scale widgets/text inside the menu to match the zoom factor.
        ImGui::SetWindowFontScale(sc);

        // Apply the cohesive Seraph design system: unified palette +
        // global style so every widget (built-in or UI::) matches.
        UI::ApplyStyle(main_color, main_color2,
        ImVec4(themeBg[0], themeBg[1], themeBg[2], 1.0f),
        ImVec4(themePanel[0], themePanel[1], themePanel[2], 1.0f));

        // ── Exterium chrome ─────────────────────────────────────────────────
        // Near-black window frame (13,14,16,200), 55px header with a main-
        // color accent strip at its bottom, 187px sidebar panel, centered logo,
        // grouped sidebar tabs, and a small muted status line in the sidebar
        // footer. Layout target: 827x604.
        const float time = (float)ImGui::GetTime();
        const float glow = (sinf(time * 2.3f) + 1.0f) * 0.5f;
        const ImVec4 mainA = UI::P.accent;
        const ImU32 colWin = UI::U(UI::winbg_color);
        const ImU32 colPanel = UI::U(UI::background_color);
        const ImU32 colStroke = UI::U(UI::stroke_color);
        const ImU32 colMain = ImGui::ColorConvertFloat4ToU32(mainA);
        const ImU32 colMainStr = ImGui::ColorConvertFloat4ToU32(ImVec4(mainA.x, mainA.y, mainA.z, 1.0f));

        const float headerH = 55.0f * sc;
        const float sbW = UI::SidebarW;

        // Drop shadow (soft, subtle) — drawn on window drawlist
        for (int i = 4; i >= 0; i--)
        {
            float spread = 16.0f + i * 10.0f;
            int shAlpha = (int)((11.0f - i * 1.8f) * menuAlpha);
            draw->AddRectFilled(
                ImVec2(p.x - spread, p.y - spread * 0.5f),
                ImVec2(p.x + s.x + spread, p.y + s.y + spread),
                IM_COL32(4, 4, 6, shAlpha), (13.0f + spread) * sc);
        }

        // Window frame fill (rounded, near-black translucent)
        draw->AddRectFilled(ImVec2(p.x, p.y), ImVec2(p.x + s.x, p.y + s.y),
            colWin, 16.0f * sc);

        // Header fill (rounded top only) — matches theme
        const ImVec4 headerBg = ImVec4(UI::winbg_color.x + 0.004f, UI::winbg_color.y + 0.004f, UI::winbg_color.z + 0.006f, 1.0f);
        draw->AddRectFilled(ImVec2(p.x, p.y), ImVec2(p.x + s.x, p.y + headerH),
            ImGui::ColorConvertFloat4ToU32(headerBg), 16.0f * sc, ImDrawFlags_RoundCornersTop);

        // Sidebar panel (0,55)-(187,604), near-black, bottom-left rounded
        const ImVec2 sideMin = ImVec2(p.x, p.y + headerH);
        const ImVec2 sideMax = ImVec2(p.x + sbW, p.y + s.y);
        draw->AddRectFilled(sideMin, sideMax, colPanel, 14.0f * sc,
            ImDrawFlags_RoundCornersBottom + ImDrawFlags_RoundCornersLeft);
        // right hairline of sidebar
        draw->AddLine(
            ImVec2(sideMax.x, sideMin.y),
            ImVec2(sideMax.x, sideMax.y),
            colStroke, 1.0f * sc);

        // Content cards area background (right of sidebar, below header)
        draw->AddRectFilled(
            ImVec2(p.x + sbW, p.y + headerH),
            ImVec2(p.x + s.x, p.y + s.y),
            colWin, 16.0f * sc, ImDrawFlags_RoundCornersBottomRight);

        // ── Shader-based animated background ─────────────────────────────
        // ONE effect, ONE instance, covering the entire menu window: sidebar
        // and content are the same continuous image rather than two separate
        // renders, so there is no seam down the middle and the pointer drives
        // a single field. Runs edge to edge including behind the title bar.
        {
            const int bgIndex = (Options::Misc::ShaderBackground >= 0 &&
                                 Options::Misc::ShaderBackground < shader::BG_COUNT)
                ? Options::Misc::ShaderBackground : shader::BG_COLOR_BENDS;
            const float bgAlpha = Options::Misc::ShaderBackgroundOpacity * menuAlpha;

            const ImVec2 bgMin = p;
            const ImVec2 bgMax = ImVec2(p.x + s.x, p.y + s.y);
            const ImVec2 bgSize(s.x, s.y);

            g_bg.Update(bgIndex, dt, bgSize, bgMin);

            if (ID3D11ShaderResourceView* srv = g_bg.Srv(bgIndex))
            {
                // Clipped to the window rect so the shader respects the
                // rounded corners instead of squaring them off.
                draw->PushClipRect(bgMin, bgMax, true);
                draw->AddImage(
                    (ImTextureID)srv, bgMin, bgMax,
                    ImVec2(0.f, 0.f), ImVec2(1.f, 1.f),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(1.f, 1.f, 1.f, bgAlpha)));
                draw->PopClipRect();
            }
        }

        // Logo: "SERAPH" centered in header. Drawn after the shader so the
        // title stays legible instead of being washed out by the effect.
        {
            const std::string logoText = SX("SERAPH");
            ImFont* lf = UI::logo_font ? UI::logo_font : io.FontDefault;
            const float logoSize = UI::logo_font ? lf->FontSize : 25.0f * sc;
            ImVec2 tsz = lf->CalcTextSizeA(logoSize, FLT_MAX, 0.f, logoText.c_str());
            ImVec2 c = ImVec2(p.x + (s.x - tsz.x) * 0.5f, p.y + (52.0f * sc - logoSize) * 0.5f);
            ImU32 lc = ImGui::ColorConvertFloat4ToU32(ImVec4(0.9f, 0.9f, 0.94f, 1.0f));
            draw->AddText(lf, logoSize, ImVec2(c.x + 1.2f, c.y + 1.2f),
                ImGui::ColorConvertFloat4ToU32(ImVec4(mainA.x, mainA.y, mainA.z, 0.20f)), logoText.c_str());
            draw->AddText(lf, logoSize, c, lc, logoText.c_str());
        }

// Use the chosen menu font. Fall back to ImGui's default font
// if MenuFonts hasn't populated yet (only on the very first
// frame, before pre-load completes).
ImFont* menuFont = (MenuFonts::Count > 0
&& Options::Misc::MenuFont >= 0
&& Options::Misc::MenuFont < MenuFonts::Count
&& MenuFonts::Fonts[Options::Misc::MenuFont])
? MenuFonts::Fonts[Options::Misc::MenuFont]
: io.FontDefault;
        ImGui::PushFont(menuFont);

// â”€â”€ Animated Exterium ambient background (full menu, including top bar) â”€â”€â”€â”€
{
    const float bgW = s.x - sbW;
    const ImVec2 bgOrigin = ImVec2(p.x + sbW, p.y);  // Start from top of menu (p.y), not headerH
    UI::ExteriumBG_Update(bgW, s.y);  // Full menu height
    UI::ExteriumBG_Render(draw, bgOrigin, ImVec2(bgW, s.y), menuAlpha);
}

// ═══ Sidebar footer status line (small, muted) ══════════════════════
{
    char fpsBuf[64];    sprintf_s(fpsBuf, "%d FPS", (int)ImGui::GetIO().Framerate);
    bool connected = (Globals::Roblox::LocalPlayer.address != 0);
    std::string uname = connected ? Globals::Roblox::LocalPlayer.Name() : "";
    const char* nameStr = uname.empty() ? "Not connected" : uname.c_str();
    const ImU32 muted = ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 0.30f));
    ImFont* sf = UI::small_font ? UI::small_font : ImGui::GetFont();
    // small_font is baked at a fixed 17px (the atlas is only built once, and
    // draw->AddText ignores SetWindowFontScale), so it must be scaled here by
    // hand like every other menu string. Using the raw FontSize made this
    // block taller than the gap the nav tabs leave behind.
    const float fs = 17.0f * sc;
    const float avSize = 60.0f * sc;
    // Size the block from what it actually draws, then pin its bottom edge to
    // sideMax.y. The old fixed anchor (sideMax.y - 100) assumed a two-line
    // footer, so once the avatar block was added the FPS line landed ~13px
    // past sideMax.y and drew outside the menu window entirely.
    const float blockH = 2.0f * sc + avSize + 4.0f * sc + fs + 3.0f * sc + fs;
    float yy = sideMax.y - 12.0f * sc - blockH;

// Avatar (circular profile icon)
        {
            const float avX = sideMin.x + 12.0f * sc;
            const float avY = yy - 2.0f * sc;
            const ImVec2 avCenter(avX + avSize * 0.5f, avY + avSize * 0.5f);
            const float avR = avSize * 0.5f;
            const ImU32 avBorder = ImGui::ColorConvertFloat4ToU32(UI::P.borderDim);
            draw->AddCircleFilled(avCenter, avR, ImGui::ColorConvertFloat4ToU32(ImVec4(0, 0, 0, 0.30f)));
            draw->AddCircle(avCenter, avR, avBorder, 0, 1.0f * sc);

            if (connected) {
                std::int64_t uid = Cheat::Features::PlayerAvatars::LookupUserId(uname);
                ID3D11ShaderResourceView* av = uid != 0 ? Cheat::Features::PlayerAvatars::Get(uid) : nullptr;
                if (av) {
                    const ImVec2 avMin(avX, avY);
                    const ImVec2 avMax(avX + avSize, avY + avSize);
                    draw->AddImageRounded((ImTextureID)av, avMin, avMax, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, avR);
                } else if (!uname.empty()) {
                    char ini[2] = { (char)std::toupper((unsigned char)uname[0]), 0 };
                    ImVec2 tsz = sf->CalcTextSizeA(fs, FLT_MAX, 0.f, ini);
                    ImVec2 tpos(avX + (avSize - tsz.x) * 0.5f, avY + (avSize - tsz.y) * 0.5f);
                    draw->AddText(sf, fs, tpos, muted, ini);
                }
            }
            yy += avSize + 4.0f * sc;
        }

    ImVec2 ns = sf->CalcTextSizeA(fs, FLT_MAX, 0.f, nameStr);
    draw->AddText(sf, fs, ImVec2(sideMin.x + 18.0f * sc, yy), muted, nameStr);
    yy += fs + 3.0f * sc;
    draw->AddText(sf, fs, ImVec2(sideMin.x + 18.0f * sc, yy), muted, fpsBuf);
}

        // â”€â”€ Sidebar grouped nav tabs â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        {
            // Sidebar vertical nav tabs
            {
            const float tabX = p.x + UI::SidebarX + 10.0f * sc;

            // category label helper (white @ 0.30, small font)
            auto catLabel = [&](const char* t, float y) {
                ImFont* sf = UI::small_font ? UI::small_font : ImGui::GetFont();
                const float fs = UI::small_font ? sf->FontSize : 15.0f * sc;
                draw->AddText(sf, fs, ImVec2(tabX + 2.0f * sc, y),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 0.30f)), t);
            };

            // groups: AIMBOT, VISUALS, MISC, CONFIGS
            const char* const catN[] = { "AIMBOT", "VISUALS", "MISC", "CONFIGS" };
            // per-group (row indices -> tab id, glyph, label)
            struct Row { int id; const char* glyph; const char* name; };
            const Row g0[] = { {0,"9","Aim"}, {2,"0","Rage"} };
            const Row g1[] = { {1,"8","Visuals"} };
            const Row g2[] = { {3,"1","Misc"}, {4,"5","Movement"} };
            const Row g3[] = { {5,"6","Configs"}, {6,"3","Game"}, {7,"2","Executor"}, {8,"\xE2\x99\xAC","Music"} };
            const Row* groups[4] = { g0, g1, g2, g3 };
            const int groupN[4] = { 2, 1, 2, 4 };

            float yy = sideMin.y + 15.0f * sc;
            for (int g = 0; g < 4; g++)
            {
                catLabel(catN[g], yy);
                yy += 8.0f * sc - 0.0f;
                for (int k = 0; k < groupN[g]; k++)
                {
                    const Row& row = groups[g][k];
                    if (UI::Tab(row.name, row.glyph, tab == row.id, tabX, yy)) tab = row.id;
                    yy += 40.0f * sc + 5.0f * sc;
                }
                yy += 10.0f * sc;
            }
        }

// Reset cursor to content area top (after header)
ImGui::SetCursorPos(ImVec2(UI::ContentX, 72.0f * sc));
        }


if (tab != lastTab)
{
tab2 = 0;
lastTab = tab;
sScrollTarget = 0.0f;
sScrollCurrent = 0.0f;
sScrollActive = false;
}

        // ── Content area layout constants (Exterium-style) ────────
        const float ctX = 16.0f * sc; // child-relative (padding from child left edge)
        const float fullW = s.x - UI::ContentX - 10.0f * sc;
        const float ctW = fullW - ctX; // full-width cards fit inside the child padding
        const float halfW = (fullW - ctX - UI::ColGap) * 0.5f;
        const float cardW = halfW;

        // Content area scrollable child (allows mouse wheel scrolling for all tabs)
        // Transparent ChildBg so the animated Exterium background stays visible
        // in the gaps between the buttons/cards.
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(1, 1, 1, 0.14f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(1, 1, 1, 0.24f));
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6.0f * sc);
        ImGui::BeginChild("##content_area", ImVec2(fullW, s.y - 72.0f * sc - 8.0f * sc), false,
            ImGuiWindowFlags_NoScrollWithMouse);

        // Smooth, subtle content scrolling: wheel input eases toward a target
        // instead of jumping instantly, so the cards glide rather than snap.
        {
            ImGuiWindow* cwin = ImGui::GetCurrentWindow();
            const float maxY = cwin->ScrollMax.y;
            if (GImGui->HoveredWindow == cwin && io.MouseWheel != 0.0f && maxY > 0.0f)
            {
                sScrollTarget = UI::ClampF(sScrollCurrent - io.MouseWheel * (52.0f * sc), 0.0f, maxY);
                sScrollActive = true;
            }
            if (sScrollActive)
            {
                const float rate = 1.0f - expf(-io.DeltaTime * 16.0f);
                sScrollCurrent += (sScrollTarget - sScrollCurrent) * rate;
                if (fabsf(sScrollCurrent - sScrollTarget) < 0.4f)
                {
                    sScrollCurrent = sScrollTarget;
                    sScrollActive = false;
                }
                ImGui::SetScrollY(sScrollCurrent);
            }
            else
            {
                // Follow scrollbar drags / tab switches directly.
                sScrollCurrent = cwin->Scroll.y;
            }
        }

if (tab == 0)
{
        // ── Content header + horizontal subtab bar ────────────────
        UI::ContentHeader("AIM");
        
        {
            static float sa[5] = {};
            
            ImGui::SetCursorPosX(ctX);
            if (UI::ContentSubtab("Aim", tab2 == 0, sa[0])) tab2 = 0;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("Triggerbot", tab2 == 1, sa[1])) tab2 = 1;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("Hitbox", tab2 == 2, sa[2])) tab2 = 2;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("Weapon", tab2 == 3, sa[3])) tab2 = 3;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("Autoclicker", tab2 == 4, sa[4])) tab2 = 4;
            ImGui::Dummy(ImVec2(0, 8 * sc));
        }

        if (tab2 == 0)
        {
            // ── Aimbot: General + Smoothing (left column) ──
            const float panelY = ImGui::GetCursorPosY();
            ImGui::SetCursorPosX(ctX);
            if (UI::CollapsibleSection("GENERAL", halfW))
            {
                UI::labelsection("AIMBOT");
                UI::Checkbox("Enabled", &Options::Aimbot::Aimbot);
                UI::Checkbox("Team Check", &Options::Aimbot::TeamCheck);
                UI::Checkbox("Knocked Check", &Options::Aimbot::DownedCheck);
                UI::Checkbox("Anti Katana", &Options::Rivals::AntiKatana);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rivals: skip players currently holding a katana.");
                UI::Checkbox("Sticky Aim", &Options::Aimbot::StickyAim);
                UI::Checkbox("Wall Check", &Options::Aimbot::WallCheck);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Only lock onto players that are not behind walls.");
                UI::Checkbox("Prediction", &Options::Aimbot::Prediction);

                UI::labelsection("RANGE & FOV");
                UI::SliderFloat("Range", &Options::Aimbot::Range, 1.f, 1000.f, "%.0f");
                UI::SliderFloat("FOV", &Options::Aimbot::FOV, 10.f, 360.f, "%.0f");

UI::labelsection("SMOOTHING & FEEL");
            static const char* aimingMethods[]{ "Camera", "Mouse", "Silent", "Usilent" };
            UI::Combo("Method", &Options::Aimbot::AimingType, aimingMethods, IM_ARRAYSIZE(aimingMethods), halfW);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Usilent = raycast silent aim. Pick one method; Camera, Mouse and Silent all write the camera directly and will fight each other if combined.");

            static const char* smoothnessCurves[]{ "Linear", "Ease In", "Ease Out", "Ease In-Out", "Custom" };
            UI::Combo("Curve", &Options::Aimbot::SmoothnessCurve, smoothnessCurves, IM_ARRAYSIZE(smoothnessCurves), halfW);

                UI::SliderFloat("Smoothness", &Options::Aimbot::Smoothness, 0.f, 1.f, "%.3f");

                // ── Smoothness Curve preview ──
                ImGui::Dummy(ImVec2(0, 6));
                UI::labelsection("SMOOTHNESS CURVE PREVIEW");
                ImGui::Dummy(ImVec2(0, 4));

                {
                    float avail = ImGui::GetContentRegionAvail().x;
                    ImVec2 graphSize = ImVec2(avail - 6.0f * sc, 100.0f * sc);
                    ImVec2 graphPos = ImGui::GetCursorScreenPos();
                    ImDrawList* drawList = ImGui::GetWindowDrawList();

                    drawList->AddRectFilled(graphPos, ImVec2(graphPos.x + graphSize.x, graphPos.y + graphSize.y), UI::U(UI::P.surface), 2.0f);
                    drawList->AddRect(graphPos, ImVec2(graphPos.x + graphSize.x, graphPos.y + graphSize.y), UI::U(UI::P.accentSoft), 2.0f);

                    const ImU32 gridCol = UI::U(UI::P.divider);
                    for (int i = 1; i < 4; i++)
                    {
                        float y = graphPos.y + (graphSize.y / 4.0f) * i;
                        drawList->AddLine(ImVec2(graphPos.x, y), ImVec2(graphPos.x + graphSize.x, y), gridCol, 1.0f);
                    }
                    for (int i = 1; i < 4; i++)
                    {
                        float x = graphPos.x + (graphSize.x / 4.0f) * i;
                        drawList->AddLine(ImVec2(x, graphPos.y), ImVec2(x, graphPos.y + graphSize.y), gridCol, 1.0f);
                    }

                    ImVec2 prevPoint = ImVec2(graphPos.x, graphPos.y + graphSize.y);
                    for (int i = 1; i <= 100; i++)
                    {
                        float t = i / 100.0f;
                        float value;

                        switch (Options::Aimbot::SmoothnessCurve)
                        {
                        case 0: value = t; break;
                        case 1: value = t * t; break;
                        case 2: value = sqrt(t); break;
                        case 3: value = t * t * (3.0f - 2.0f * t); break;
                        case 4:
                        {
                            float p0 = 0.0f;
                            float p1 = Options::Aimbot::CustomCurveP1[1];
                            float p2 = Options::Aimbot::CustomCurveP2[1];
                            float p3 = 1.0f;
                            float u = 1.0f - t;
                            float tt = t * t;
                            float ttt = tt * t;
                            float uu = u * u;
                            float uuu = uu * u;
                            value = uuu * p0 + 3 * uu * t * p1 + 3 * u * tt * p2 + ttt * p3;
                            break;
                        }
                        default: value = t; break;
                        }

                        ImVec2 point = ImVec2(
                            graphPos.x + t * graphSize.x,
                            graphPos.y + graphSize.y - value * graphSize.y
                        );
                        drawList->AddLine(prevPoint, point, IM_COL32(main_color.x * 255, main_color.y * 255, main_color.z * 255, 255), 2.0f);
                        prevPoint = point;
                    }

                    if (Options::Aimbot::SmoothnessCurve == 4)
                    {
                        Options::Aimbot::CustomCurveEnabled = true;

                        ImVec2 cp1Pos = ImVec2(
                            graphPos.x + Options::Aimbot::CustomCurveP1[0] * graphSize.x,
                            graphPos.y + graphSize.y - Options::Aimbot::CustomCurveP1[1] * graphSize.y);
                        ImVec2 cp2Pos = ImVec2(
                            graphPos.x + Options::Aimbot::CustomCurveP2[0] * graphSize.x,
                            graphPos.y + graphSize.y - Options::Aimbot::CustomCurveP2[1] * graphSize.y);

                        drawList->AddLine(ImVec2(graphPos.x, graphPos.y + graphSize.y), cp1Pos, IM_COL32(100, 100, 100, 150), 1.0f);
                        drawList->AddLine(cp2Pos, ImVec2(graphPos.x + graphSize.x, graphPos.y), IM_COL32(100, 100, 100, 150), 1.0f);

                        float cpRadius = 5.0f;
                        drawList->AddCircleFilled(cp1Pos, cpRadius, IM_COL32(main_color.x * 255, main_color.y * 255, main_color.z * 255, 255));
                        drawList->AddCircle(cp1Pos, cpRadius, IM_COL32(255, 255, 255, 255), 0, 1.5f);
                        drawList->AddCircleFilled(cp2Pos, cpRadius, IM_COL32(main_color.x * 255, main_color.y * 255, main_color.z * 255, 255));
                        drawList->AddCircle(cp2Pos, cpRadius, IM_COL32(255, 255, 255, 255), 0, 1.5f);

                        ImVec2 mousePos = ImGui::GetMousePos();
                        bool mouseDown = ImGui::IsMouseDown(0);
                        static int draggedPoint = -1;

                        if (mouseDown)
                        {
                            if (draggedPoint == -1)
                            {
                                float dist1 = sqrt(pow(mousePos.x - cp1Pos.x, 2) + pow(mousePos.y - cp1Pos.y, 2));
                                if (dist1 <= cpRadius + 3.0f) draggedPoint = 0;
                                float dist2 = sqrt(pow(mousePos.x - cp2Pos.x, 2) + pow(mousePos.y - cp2Pos.y, 2));
                                if (dist2 <= cpRadius + 3.0f) draggedPoint = 1;
                            }
                            if (draggedPoint == 0)
                            {
                                Options::Aimbot::CustomCurveP1[0] = std::clamp((mousePos.x - graphPos.x) / graphSize.x, 0.0f, 1.0f);
                                Options::Aimbot::CustomCurveP1[1] = std::clamp((graphPos.y + graphSize.y - mousePos.y) / graphSize.y, 0.0f, 1.0f);
                            }
                            else if (draggedPoint == 1)
                            {
                                Options::Aimbot::CustomCurveP2[0] = std::clamp((mousePos.x - graphPos.x) / graphSize.x, 0.0f, 1.0f);
                                Options::Aimbot::CustomCurveP2[1] = std::clamp((graphPos.y + graphSize.y - mousePos.y) / graphSize.y, 0.0f, 1.0f);
                            }
                        }
                        else
                        {
                            draggedPoint = -1;
                        }
                    }
                    else
                    {
                        Options::Aimbot::CustomCurveEnabled = false;
                    }

                    drawList->AddText(ImVec2(graphPos.x + 2, graphPos.y + graphSize.y + 2), IM_COL32(150, 150, 150, 255), "0.0");
                    drawList->AddText(ImVec2(graphPos.x + graphSize.x - 20, graphPos.y + graphSize.y + 2), IM_COL32(150, 150, 150, 255), "1.0");

                    ImGui::Dummy(ImVec2(graphSize.x, graphSize.y + 15));
                }

                UI::labelsection("BEHAVIOR");
                UI::Checkbox("Shake", &Options::Aimbot::Shake);
                UI::Checkbox("Stutter", &Options::Aimbot::Stutter);
                UI::Checkbox("Ignore Jump", &Options::Aimbot::IgnoreJump);

                if (Options::Aimbot::Shake)
                    UI::SliderFloat("Shake Intensity", &Options::Aimbot::ShakeIntensity, 0.1f, 10.0f, "%.1f");
                if (Options::Aimbot::Stutter)
                    UI::SliderInt("Stutter Ticks", &Options::Aimbot::StutterTicks, 1, 20);
                if (Options::Aimbot::IgnoreJump)
                    UI::SliderFloat("Jump Threshold", &Options::Aimbot::JumpThreshold, 1.f, 100.f, "%.0f");

                UI::labelsection("KEYBIND");
                UI::Bind("##aimbot_key", &Options::Aimbot::AimbotKey, &Options::Aimbot::ToggleType);
            }
            UI::CollapsibleEnd();

            // ── Aimbot: Targeting + Silent Aim (right column) ──
            ImGui::SetCursorPosY(panelY);
            ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
            if (UI::CollapsibleSection("TARGETING", cardW))
            {
                UI::labelsection("HITBOX");
                static const char* hitboxModes[]{ "Fixed Bone", "Closest Part" };
                UI::Combo("Hitbox Mode", &Options::Aimbot::HitboxMode, hitboxModes, IM_ARRAYSIZE(hitboxModes), cardW);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fixed Bone uses the Hit Part / Air Hit Part selectors.\nClosest Part aims at the body part nearest your cursor.");
                Options::Aimbot::ClosestPart = (Options::Aimbot::HitboxMode == 1);

                static const char* hitParts[]{ "Head", "Torso", "Left Arm", "Right Arm", "Left Leg", "Right Leg", "Lower Torso", "Upper Torso" };
                if (!Options::Aimbot::ClosestPart)
                {
                    UI::Combo("Hit Part", &Options::Aimbot::TargetBone, hitParts, IM_ARRAYSIZE(hitParts), cardW);
                    UI::Combo("Air Hit Part", &Options::Aimbot::AirTargetBone, hitParts, IM_ARRAYSIZE(hitParts), cardW);
                }

                static const char* priorities[]{ "Closest Part", "Crosshair", "Lowest Health", "Farthest", "Highest Health" };
                UI::Combo("Target Priority", &Options::Aimbot::TargetPriority, priorities, IM_ARRAYSIZE(priorities), cardW);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Which enemy wins when several are inside FOV.");

                UI::labelsection("SWITCHING");
                UI::SliderFloat("Switch Delay (ms)", &Options::Aimbot::TargetSwitchDelay, 0.f, 1000.f, "%.0f");

                // Shown for both silent methods. It used to live only inside the
                // Usilent panel, which left "Silent" - the method most people
                // actually use - with no way to reach this setting and silently
                // stuck on the delivery that cannot hit.
                if (Options::Aimbot::AimingType == 2 || Options::Aimbot::AimingType == 3)
                {
                    static const char* saDelivery[]{ "Camera Rotation (recommended)", "Viewport Offset (cosmetic on Rivals)", "Both" };
                    UI::Combo("Delivery", &Options::Aimbot::SilentAimDelivery, saDelivery, IM_ARRAYSIZE(saDelivery), cardW);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Which camera field the aim is written to.\n\nCamera Rotation aims the camera matrix itself. The view visibly moves, and this is the only mode measured to land shots reliably on Rivals.\n\nViewport Offset shifts Camera::Viewport, which leaves the view completely still - but on Rivals it displaces the rendered image WITHOUT changing the direction the bullet travels. Shots then only connect when the target happens to be near the centre of the screen. The arithmetic is correct; the game just does not steer the shot with it.");

                    UI::Checkbox("Fast Camera Hold", &Options::Aimbot::SilentAimFastHold);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Re-asserts the aim from a 5ms background thread instead of once per rendered frame.\n\nRaises the hit rate, because Roblox blends the camera back toward your input every frame and a single write per frame only survives that race part of the time.\n\nOFF by default. Every re-assert is a driver write that opens and closes a process handle, and that competes with the UI thread - which showed up as the menu being slow to open. If the menu or ESP feel heavy, leave this off; if single-fire shots are landing but not every time, this is what fixes it.");
                }

                // This whole panel is the configuration for AimingType 3 ("Usilent"),
                // selected in the AIMBOT "Method" dropdown above. Per the request
                // to keep it out of its own always-visible section, it only
                // appears once that method is actually chosen.
                if (Options::Aimbot::AimingType == 3)
                {
                    UI::labelsection("RAYCAST SILENT AIM (USILENT)");
                    UI::Checkbox("Enabled", &Options::Aimbot::SilentAimEnabled);
                    UI::Tooltip("Raycast-based silent aim. Uses workspace raycast for hit verification.");
                    UI::Bind("##sa_key", &Options::Aimbot::SilentAimKey, &Options::Aimbot::SilentAimToggleType);

                    static const char* saToggleTypes[]{ "Hold", "Toggle", "Always On" };
                    UI::Combo("Mode##sa", &Options::Aimbot::SilentAimToggleType, saToggleTypes, IM_ARRAYSIZE(saToggleTypes), cardW);

                    if (Options::Aimbot::SilentAimEnabled)
                    {
                        UI::SliderFloat("FOV", &Options::Aimbot::SilentAimFOV, 10.f, 300.f, "%.0f");
                        UI::SliderFloat("Smoothness", &Options::Aimbot::SilentAimSmoothness, 0.f, 50.f, "%.1f");
                        UI::Tooltip("0 = instant snap. Higher = smoother camera movement.");

                        static const char* saModes[]{ "Camera Rotation", "Mouse Move", "Both" };
                        UI::Combo("Method", reinterpret_cast<int*>(&Options::Aimbot::SilentAimMethod), saModes, IM_ARRAYSIZE(saModes), cardW);

                    UI::labelsection("HIT PART");
                    static const char* saHitModes[]{ "Fixed Part", "Closest Part", "Adaptive" };
                    UI::Combo("Hit Mode", &Options::Aimbot::SilentAimHitMode, saHitModes, IM_ARRAYSIZE(saHitModes), cardW);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fixed Part always uses Target Part.\nAdaptive keeps Target Part while it has line of sight, then steps down the body when it is behind cover.\nIndependent of the main aimbot's Hitbox Mode.");

                    if (Options::Aimbot::SilentAimHitMode != 1)
                    {
                        static const char* saBones[]{ "Head", "UpperTorso", "LowerTorso", "HumanoidRootPart" };
                        UI::Combo("Target Part", &Options::Aimbot::SilentAimTargetBone, saBones, IM_ARRAYSIZE(saBones), cardW);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rivals uses R6 rigs, so both torso options resolve to the Torso part. Head resolves to the Head part.");
                    }

                    UI::labelsection("TRACKING");
                    UI::Checkbox("Sticky Target", &Options::Aimbot::SilentAimSticky);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Keeps the current target instead of re-picking the nearest one every frame.");
                    if (Options::Aimbot::SilentAimSticky)
                    {
                        UI::SliderFloat("Switch Delay (ms)", &Options::Aimbot::SilentAimSwitchDelay, 0.f, 500.f, "%.0f");
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Minimum time before the lock may move to a different enemy.");
                    }

                    UI::Checkbox("Team Check", &Options::Aimbot::SilentAimTeamCheck);
                    UI::Checkbox("Require Visible", &Options::Aimbot::SilentAimRequireVisible);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Off by default. Needs Wall Check on too.\nThe map raycast is not tuned for Rivals geometry and can report valid targets as covered, which stops silent aim acquiring anything at all.");

                    UI::labelsection("PREDICTION");
                    UI::Checkbox("Prediction", &Options::Aimbot::SilentAimPrediction);
                    if (Options::Aimbot::SilentAimPrediction)
                    {
                        UI::SliderFloat("Projectile Speed", &Options::Aimbot::SilentAimProjectileSpeed, 0.f, 500.f, "%.0f");
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Weapon speed in studs/second. Prediction uses real time of flight (distance / speed).\nSet to 0 to use the velocity multipliers below instead.");

                        if (Options::Aimbot::SilentAimProjectileSpeed > 1.f)
                        {
                            UI::Checkbox("Drop Compensation", &Options::Aimbot::SilentAimDropCompensation);
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lifts the aim point to counter bullet drop over the flight time (Roblox gravity 196.2 studs/s^2).");
                        }
                        else
                        {
                            UI::SliderFloat("Prediction X", &Options::Aimbot::SilentAimPredictionX, 0.5f, 3.0f, "%.2f");
                            UI::SliderFloat("Prediction Y", &Options::Aimbot::SilentAimPredictionY, 0.5f, 3.0f, "%.2f");
                        }
                    }

                    UI::labelsection("ALIGNMENT");
                    UI::SliderFloat("Muzzle Compensation", &Options::Aimbot::SilentAimMuzzleComp, 0.f, 1.f, "%.2f");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rivals spawns bullets at the weapon tip rather than the camera, which sends close-range shots past the aimed part.\nBlends the aim origin from the camera toward the muzzle to correct the parallax.");

                    UI::Checkbox("Only While Firing", &Options::Aimbot::SilentAimFireOnly);
                    if (Options::Aimbot::SilentAimFireOnly)
                    {
                        UI::SliderFloat("Hold After Fire (ms)", &Options::Aimbot::SilentAimHoldMs, 0.f, 300.f, "%.0f");
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Keeps the aim applied for this long after the trigger is released, so the last shot of a burst still lands.");
                    }

                    UI::Checkbox("Hold LMB to Aim (PF)", &Options::Aimbot::SilentAimHoldLMB);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("PF only: while left mouse button is held, rotate the camera onto the target and restore it on release.");
                }
                }

                UI::labelsection("SILENT LOCK");
                UI::Checkbox("Silent Lock", &Options::Aimbot::SilentLock);
                UI::Tooltip("Silently keeps aim on the closest target. Camera Rotation writes the camera matrix; Viewport Offset shifts the hit point.");
                UI::Bind("##silentlock_key", &Options::Aimbot::SilentLockKey, &Options::Aimbot::SilentLockMode);

                if (Options::Aimbot::SilentLock)
                {
                    static const char* lockModes[]{ "Camera Rotation", "Viewport Offset" };
                    UI::Combo("Lock Mode", &Options::Aimbot::SilentLockMode, lockModes, IM_ARRAYSIZE(lockModes), cardW);
                    UI::Checkbox("Target Line", &Options::Aimbot::TargetLine);
}

		UI::labelsection("AIM INFO");
                UI::Checkbox("Enable", &Options::Aimbot::AimInfo);
                UI::Tooltip("Draws a HUD with the current target's info at the top-right corner.");
                UI::Checkbox("Name", &Options::Aimbot::AimInfoName);
                UI::Checkbox("Distance", &Options::Aimbot::AimInfoDistance);
                UI::Checkbox("Health", &Options::Aimbot::AimInfoHealth);
                UI::Checkbox("Part", &Options::Aimbot::AimInfoPart);

                ImGui::Dummy(ImVec2(0, 10));
                ImGui::Separator();
                ImGui::Dummy(ImVec2(0, 8));

                UI::labelsection("FLICKBOT");
                UI::Checkbox("Flickbot", &Options::Aimbot::Flickbot);
                UI::Checkbox("Team Check", &Options::Aimbot::FlickbotTeamCheck);
                UI::Bind("##flickbot_key", &Options::Aimbot::FlickbotKey);
                if (Options::Aimbot::Flickbot)
                {
                    UI::SliderFloat("Flick FOV", &Options::Aimbot::FlickbotFOV, 10.0f, 400.0f, "%.0f");
                    UI::Tooltip("Maximum angle to search for targets. Higher values can snap to enemies further from your crosshair.");
                    UI::SliderFloat("Smoothing", &Options::Aimbot::FlickbotSmoothing, 0.0f, 1.0f, "%.2f");
                    UI::Tooltip("Higher values create a smoother, more natural flick. 0 = instant snap.");
                }

                UI::labelsection("VISUALS");
                UI::Checkbox("Target Line", &Options::Aimbot::TargetLine);
                UI::Checkbox("Wall Check", &Options::Aimbot::WallCheck);
                if (Options::Aimbot::TargetLine)
                {
                    UI::SliderFloat("Line Thickness", &Options::Aimbot::TargetLineThickness, 0.5f, 5.0f, "%.1f");
                    UI::ColorEdit3("Line Color", Options::Aimbot::TargetLineColor, ImGuiColorEditFlags_NoInputs);
                }

                if (Options::Aimbot::Prediction)
                {
                    UI::SliderFloat("Projectile Speed", &Options::Aimbot::ProjectileSpeed, 0.f, 2000.f, "%.0f");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bullet speed in studs/second. The range to the target is divided by this to get the flight time, and the target is led by that much of its velocity.\n0 disables leading. Rough values: AR ~350, SMG ~250, shotgun ~200, sniper ~4000 (near instant).");
                    UI::SliderFloat("Prediction X", &Options::Aimbot::PredictionX, 0.01f, 10.0f, "%.2f");
                    UI::SliderFloat("Prediction Y", &Options::Aimbot::PredictionY, 0.01f, 10.0f, "%.2f");
                }
            }
            UI::CollapsibleEnd();
        }
        else if (tab2 == 1)
        {
            // ── Triggerbot: Main (left) ──
            const float panelY = ImGui::GetCursorPosY();
            ImGui::SetCursorPosX(ctX);
            if (UI::CollapsibleSection("TRIGGERBOT", halfW))
            {
                UI::labelsection("MAIN");
                UI::Checkbox("Enabled", &Options::Triggerbot::Enabled);
                UI::Checkbox("Team Check", &Options::Triggerbot::TeamCheck);
                UI::Checkbox("Knocked Check", &Options::Triggerbot::DownedCheck);
                UI::Checkbox("Wall Check", &Options::Triggerbot::WallCheck);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Don't trigger on enemies hidden behind walls/geometry.");
                UI::Checkbox("Prediction", &Options::Triggerbot::Prediction);

                static const char* tbMethods[]{ "FOV", "Hitbox" };
                UI::Combo("Triggerbot Method", &Options::Triggerbot::Method, tbMethods, IM_ARRAYSIZE(tbMethods), cardW);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hitbox = fires whenever the crosshair is over the target's actual hitbox, at any distance.\nFOV = legacy radius check around the crosshair.");

                if (Options::Triggerbot::Method == 1)
                {
                    static const char* tbTargetParts[]{ "Head", "Torso", "HumanoidRootPart", "Closest Part" };
                    UI::Combo("Target Part", &Options::Triggerbot::TargetPart, tbTargetParts, IM_ARRAYSIZE(tbTargetParts), cardW);
                    UI::Checkbox("Show Hitboxes", &Options::Triggerbot::ShowAdvancedFOV);
                }
                else
                {
                    UI::Checkbox("Advanced FOV", &Options::Triggerbot::AdvancedFOV);
                    if (Options::Triggerbot::AdvancedFOV)
                        UI::Checkbox("Show FOV", &Options::Triggerbot::ShowAdvancedFOV);
                }

                UI::labelsection("KEYBIND");
                UI::Bind("##triggerbot_key", &Options::Triggerbot::TriggerbotKey);
            }
            UI::CollapsibleEnd();

            // ── Triggerbot: Settings (right) ──
            ImGui::SetCursorPosY(panelY);
            ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
            if (UI::CollapsibleSection("SETTINGS", cardW))
            {
                UI::labelsection("BASIC");
                if (Options::Triggerbot::Method == 0)
                {
                    if (!Options::Triggerbot::AdvancedFOV)
                        UI::SliderFloat("Radius", &Options::Triggerbot::Radius, 0.1f, 50.f, "%.1f");
                    UI::SliderFloat("Range", &Options::Triggerbot::Range, 0.1f, 1000.f, "%.1f");
                }
                else
                {
                    UI::SliderFloat("Hitbox Margin", &Options::Triggerbot::PartMargin, 0.f, 50.f, "%.1f");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Extra pixels around each part's projected hitbox that still count as the crosshair being on it.");
                }
                UI::SliderInt("Delay (ms)", &Options::Triggerbot::Delay, 0, 500);

                if (Options::Triggerbot::Method == 0)
                {
                    UI::labelsection("DYNAMIC FOV");
                    UI::Checkbox("Dynamic FOV", &Options::Triggerbot::DynamicFOV);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scales the hit zone by distance so enemies close AND far are equally easy to hit.");
                    if (Options::Triggerbot::DynamicFOV)
                    {
                        UI::SliderFloat("FOV Scale", &Options::Triggerbot::DynamicFOVScale, 0.1f, 5.0f, "%.2f");
                        UI::SliderFloat("Reference Dist", &Options::Triggerbot::DynamicFOVBaseDist, 5.f, 200.f, "%.0f");
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Distance (studs) at which the FOV equals the base Radius/FOV. Closer enemies keep full size; farther enemies grow.");
                    }
                }

                if (Options::Triggerbot::Prediction)
                {
                    UI::labelsection("PREDICTION");
                    UI::SliderFloat("Prediction X", &Options::Triggerbot::PredictionX, 0.0f, 10.0f, "%.2f");
                    UI::SliderFloat("Prediction Y", &Options::Triggerbot::PredictionY, 0.0f, 10.0f, "%.2f");
                }

                // Advanced FOV sliders (FOV method only)
                if (Options::Triggerbot::AdvancedFOV && Options::Triggerbot::Method == 0)
                {
                    UI::labelsection("ADVANCED FOV (PER BONE)");
                    ImGui::Text(" HEAD");
                    UI::SliderFloat("Head FOV X", &Options::Triggerbot::HeadFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("Head FOV Y", &Options::Triggerbot::HeadFOV_Y, 0.f, 100.f, "%.1f");

                    ImGui::Text(" TORSO");
                    UI::SliderFloat("Torso FOV X", &Options::Triggerbot::TorsoFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("Torso FOV Y", &Options::Triggerbot::TorsoFOV_Y, 0.f, 100.f, "%.1f");

                    ImGui::Text(" UPPER TORSO");
                    UI::SliderFloat("U Torso FOV X", &Options::Triggerbot::UpperTorsoFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("U Torso FOV Y", &Options::Triggerbot::UpperTorsoFOV_Y, 0.f, 100.f, "%.1f");

                    ImGui::Text(" LOWER TORSO");
                    UI::SliderFloat("L Torso FOV X", &Options::Triggerbot::LowerTorsoFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("L Torso FOV Y", &Options::Triggerbot::LowerTorsoFOV_Y, 0.f, 100.f, "%.1f");

                    ImGui::Text(" LEFT ARM");
                    UI::SliderFloat("L U Arm FOV X", &Options::Triggerbot::LeftUpperArmFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("L U Arm FOV Y", &Options::Triggerbot::LeftUpperArmFOV_Y, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("L L Arm FOV X", &Options::Triggerbot::LeftLowerArmFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("L L Arm FOV Y", &Options::Triggerbot::LeftLowerArmFOV_Y, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("L Hand FOV X", &Options::Triggerbot::LeftHandFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("L Hand FOV Y", &Options::Triggerbot::LeftHandFOV_Y, 0.f, 100.f, "%.1f");

                    ImGui::Text(" RIGHT ARM");
                    UI::SliderFloat("R U Arm FOV X", &Options::Triggerbot::RightUpperArmFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("R U Arm FOV Y", &Options::Triggerbot::RightUpperArmFOV_Y, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("R L Arm FOV X", &Options::Triggerbot::RightLowerArmFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("R L Arm FOV Y", &Options::Triggerbot::RightLowerArmFOV_Y, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("R Hand FOV X", &Options::Triggerbot::RightHandFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("R Hand FOV Y", &Options::Triggerbot::RightHandFOV_Y, 0.f, 100.f, "%.1f");

                    ImGui::Text(" LEFT LEG");
                    UI::SliderFloat("L U Leg FOV X", &Options::Triggerbot::LeftUpperLegFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("L U Leg FOV Y", &Options::Triggerbot::LeftUpperLegFOV_Y, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("L L Leg FOV X", &Options::Triggerbot::LeftLowerLegFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("L L Leg FOV Y", &Options::Triggerbot::LeftLowerLegFOV_Y, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("L Foot FOV X", &Options::Triggerbot::LeftFootFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("L Foot FOV Y", &Options::Triggerbot::LeftFootFOV_Y, 0.f, 100.f, "%.1f");

                    ImGui::Text(" RIGHT LEG");
                    UI::SliderFloat("R U Leg FOV X", &Options::Triggerbot::RightUpperLegFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("R U Leg FOV Y", &Options::Triggerbot::RightUpperLegFOV_Y, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("R L Leg FOV X", &Options::Triggerbot::RightLowerLegFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("R L Leg FOV Y", &Options::Triggerbot::RightLowerLegFOV_Y, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("R Foot FOV X", &Options::Triggerbot::RightFootFOV_X, 0.f, 100.f, "%.1f");
                    UI::SliderFloat("R Foot FOV Y", &Options::Triggerbot::RightFootFOV_Y, 0.f, 100.f, "%.1f");
                }
            }
            UI::CollapsibleEnd();
        }
        else if (tab2 == 2)
        {
            // ── Hitbox Expander ──
            const float panelY = ImGui::GetCursorPosY();
            ImGui::SetCursorPosX(ctX);
            if (UI::CollapsibleSection("HITBOX EXPANDER", halfW))
            {
                UI::labelsection("MAIN");
                UI::Checkbox("Enabled", &Options::HitboxExpander::Enabled);
                UI::Checkbox("Show Hitbox", &Options::HitboxExpander::ShowHitbox);
                UI::Checkbox("Walk Through", &Options::HitboxExpander::WalkThrough);

                UI::labelsection("SETTINGS");
                UI::SliderFloat("Horizontal Size", &Options::HitboxExpander::HorizontalSize, 1.0f, 50.0f, "%.1f");
                UI::SliderFloat("Vertical Size", &Options::HitboxExpander::VerticalSize, 1.0f, 50.0f, "%.1f");
                UI::SliderFloat("Transparency", &Options::HitboxExpander::HitboxTransparency, 0.0f, 1.0f, "%.2f");
            }
            UI::CollapsibleEnd();

            // Preview panel
            ImGui::SetCursorPosY(panelY);
            ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
            // (preview rendered in ESP overlay)
        }
        else if (tab2 == 3)
        {
            // ── Weapon + FOV subtab ──
            // Current weapon selector on the left, full-width weapon profiles below.

            static const char* wpWeapons[] = {
                "Assault Rifle", "Warper", "Bow", "Burst Rifle", "Chainsaw",
                "Sniper", "Daggers", "Jump Pad", "Permafrost", "Uzi",
                "Exogun", "Maul", "Grenade", "Flare Gun", "Flashbang",
                "Freeze Ray", "Flamethrower", "Energy Rifle", "Gunblade", "Handgun",
                "Spear", "Katana", "Knife", "Medkit", "Minigun",
                "Molotov", "Paintball Gun", "RPG", "Revolver", "Riot Shield",
                "Satchel", "Scythe", "Shorty", "Shotgun", "Slingshot",
                "Smoke Grenade", "Crossbow", "Spray", "Battle Axe", "Trowel",
                "Grenade Launcher", "Warpstone", "Distortion", "Subspace Tripmine",
                "Energy Pistols", "Fists", "Grappler", "War Horn"
            };

            const float panelY = ImGui::GetCursorPosY();

            // Full-width: CURRENT WEAPON (FOV VISUALS moved to the VISUALS tab)
            ImGui::SetCursorPosX(ctX);
            if (UI::CollapsibleSection("CURRENT WEAPON", ctW))
            {
                static int curIdx = -1;
                int match = -1;
                for (int k = 0; k < IM_ARRAYSIZE(wpWeapons); k++)
                    if (Options::WeaponProfiles::CurrentWeapon == wpWeapons[k]) { match = k; break; }
                curIdx = match;
                ImGui::SetNextItemWidth(ctW - 28.0f * sc);
                if (UI::Combo("Weapon", &curIdx, wpWeapons, IM_ARRAYSIZE(wpWeapons)))
                    Options::WeaponProfiles::CurrentWeapon =
                        (curIdx >= 0 && curIdx < (int)IM_ARRAYSIZE(wpWeapons)) ? wpWeapons[curIdx] : "";

                ImGui::Spacing();
                UI::labelsection("STATUS");
                if (Options::WeaponProfiles::ActiveProfile >= 0 &&
                    Options::WeaponProfiles::ActiveProfile < static_cast<int>(Options::WeaponProfiles::Profiles.size()))
                {
                    auto& ap = Options::WeaponProfiles::Profiles[Options::WeaponProfiles::ActiveProfile];
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "Matched: %s", ap.Name[0] ? ap.Name : "?");
                }
                else
                {
                    ImGui::TextColored(UI::P.textMid, "None (add & enable a profile)");
                }
            }
            UI::CollapsibleEnd();

            // Full-width WEAPON PROFILES panel below
            ImGui::SetCursorPosY(panelY + 160 * sc + 12.0f * sc);
            ImGui::SetCursorPosX(ctX);
            if (UI::CollapsibleSection("WEAPON PROFILES", ctW))
            {
                if (UI::Button("Add Profile", ImVec2(-1, 28)))
                {
                    Options::WeaponProfile p;
                    p.Enabled = true;
                    snprintf(p.Name, sizeof(p.Name), "Weapon%d", (int)Options::WeaponProfiles::Profiles.size() + 1);
                    Options::WeaponProfiles::Profiles.push_back(p);
                    Options::WeaponProfiles::SelectedProfile = (int)Options::WeaponProfiles::Profiles.size() - 1;
                }
                ImGui::Spacing();

                if (Options::WeaponProfiles::Profiles.empty())
                {
                    ImGui::TextColored(UI::P.textMid, "Click 'Add Profile' to create per-weapon aimbot settings.");
                }
                else
                {
                    std::vector<const char*> names;
                    std::vector<std::string> stable;
                    for (auto& pro : Options::WeaponProfiles::Profiles)
                    {
                        std::string lbl = std::string(pro.Name[0] ? pro.Name : "(unnamed)") +
                            (pro.Enabled ? " [ON]" : " [OFF]");
                        stable.push_back(lbl);
                        names.push_back(stable.back().c_str());
                    }
                    int sel = Options::WeaponProfiles::SelectedProfile;
                    UI::Combo("Profile", &sel, names.data(), (int)names.size());
                    Options::WeaponProfiles::SelectedProfile = std::clamp(sel, 0, (int)Options::WeaponProfiles::Profiles.size() - 1);

                    if (sel >= 0 && sel < (int)Options::WeaponProfiles::Profiles.size())
                    {
                        auto& prof = Options::WeaponProfiles::Profiles[sel];

                        UI::labelsection("PROFILE SETTINGS");
                        UI::Checkbox("Enabled", &prof.Enabled);
                        ImGui::InputText("Weapon Name", prof.Name, sizeof(prof.Name));
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Matches the held weapon name (case-insensitive).");

                        static const char* wpMethods[]{ "Camera", "Mouse", "Silent", "Usilent" };
                        UI::Combo("Method", &prof.AimingType, wpMethods, IM_ARRAYSIZE(wpMethods));
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Usilent requires the Silent Aim checkbox below.");
                        UI::Checkbox("Silent Aim", &prof.SilentAim);
                        if (prof.SilentAim)
                        {
                            static const char* wpSilentModes[]{ "Camera", "Mouse Spoof" };
                            UI::Combo("Silent Mode", &prof.SilentAimMode, wpSilentModes, IM_ARRAYSIZE(wpSilentModes));
                        }

                        static const char* wpBones[]{ "Head", "Torso", "Upper Torso", "Lower Torso" };
                        UI::Combo("Target Bone", &prof.TargetBone, wpBones, IM_ARRAYSIZE(wpBones));
                        {
                            static const char* hitboxModes[]{ "Fixed Bone", "Closest Part" };
                            UI::Combo("Hitbox Mode", &prof.ClosestPart, hitboxModes, IM_ARRAYSIZE(hitboxModes));
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Applies to the aimbot. Silent aim uses its own Hit Mode below so this cannot discard the selected bone.");
                        }

                        if (prof.SilentAim)
                        {
                            static const char* wpSilentHitModes[]{ "Fixed Part", "Closest Part", "Adaptive" };
                            UI::Combo("Silent Hit Mode", &prof.SilentAimHitMode, wpSilentHitModes, IM_ARRAYSIZE(wpSilentHitModes));
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Adaptive keeps Target Bone while it has line of sight, then steps down the body when it is behind cover.");
                        }

                        UI::SliderFloat("Range", &prof.Range, 1.f, 1000.f, "%.0f");
                        UI::SliderFloat("FOV", &prof.FOV, 10.f, 360.f, "%.0f");
                        UI::SliderFloat("Smoothness", &prof.Smoothness, 0.f, 1.f, "%.3f");

                        UI::labelsection("CHECKS");
                        UI::Checkbox("Team Check", &prof.TeamCheck);
                        UI::Checkbox("Knocked Check", &prof.DownedCheck);
                        UI::Checkbox("Wall Check", &prof.WallCheck);
                        UI::Checkbox("Sticky Aim", &prof.StickyAim);
                        UI::Checkbox("Prediction", &prof.Prediction);
                        if (prof.Prediction)
                        {
                            UI::SliderFloat("Prediction X", &prof.PredictionX, 0.0f, 10.0f, "%.2f");
                            UI::SliderFloat("Prediction Y", &prof.PredictionY, 0.0f, 10.0f, "%.2f");
                        }
                        UI::Checkbox("Ignore Jump", &prof.IgnoreJump);
                        if (prof.IgnoreJump)
                            UI::SliderFloat("Jump Threshold", &prof.JumpThreshold, 1.0f, 100.0f, "%.1f");

                        ImGui::Spacing();
                        if (UI::Button("Delete Profile", ImVec2(-1, 24)))
                        {
                            Options::WeaponProfiles::Profiles.erase(Options::WeaponProfiles::Profiles.begin() + sel);
                            if (Options::WeaponProfiles::SelectedProfile >= (int)Options::WeaponProfiles::Profiles.size())
                                Options::WeaponProfiles::SelectedProfile = (int)Options::WeaponProfiles::Profiles.size() - 1;
                            if (Options::WeaponProfiles::SelectedProfile < 0)
                                Options::WeaponProfiles::SelectedProfile = 0;
                        }
                    }
                }
            }
            UI::CollapsibleEnd();
        }
        else if (tab2 == 4)
        {
            // ── Autoclicker (moved from Movement) ──
            const float panelY = ImGui::GetCursorPosY();
            ImGui::SetCursorPosX(ctX);
            if (UI::CollapsibleSection("AUTOCLICKER", halfW))
            {
                UI::labelsection("MAIN");
                UI::Checkbox("Enabled", &Options::Autoclicker::Enabled);
                UI::SliderFloat("CPS", &Options::Autoclicker::CPS, 1.f, 100.f, "%.0f");
                UI::Checkbox("Right Click", &Options::Autoclicker::RightClick);
                UI::Checkbox("Only On Hold (LMB)", &Options::Autoclicker::OnlyOnHold);
            }
            UI::CollapsibleEnd();
            ImGui::SetCursorPosY(panelY);
            ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
            if (UI::CollapsibleSection("BIND", cardW))
            {
                UI::labelsection("KEYBIND");
                UI::Bind("##ac_key", &Options::Autoclicker::Key, &Options::Autoclicker::ToggleType);
            }
            UI::CollapsibleEnd();
        }
}
else if (tab == 2)
{
    // ===== Rage tab =====
    // Content header + horizontal subtab bar
    UI::ContentHeader("RAGE");
    {
        static float sa[6] = {};
        ImGui::SetCursorPosX(ctX);
        if (UI::ContentSubtab("Ragebot", tab2 == 0, sa[0])) tab2 = 0;
        ImGui::SameLine(0, 6.0f * sc);
        if (UI::ContentSubtab("Rage", tab2 == 1, sa[1])) tab2 = 1;
        ImGui::SameLine(0, 6.0f * sc);
        if (UI::ContentSubtab("Anti-Aim", tab2 == 2, sa[2])) tab2 = 2;
        ImGui::SameLine(0, 6.0f * sc);
        if (UI::ContentSubtab("Desync", tab2 == 3, sa[3])) tab2 = 3;
        ImGui::SameLine(0, 6.0f * sc);
        if (UI::ContentSubtab("VoidHide", tab2 == 4, sa[4])) tab2 = 4;
        ImGui::SameLine(0, 6.0f * sc);
        if (UI::ContentSubtab("Bhop", tab2 == 5, sa[5])) tab2 = 5;
        ImGui::Dummy(ImVec2(0, 8 * sc));
    }

    if (tab2 == 0) { RenderRagebotSubtab(main_color); }
    else if (tab2 == 1) { RenderRageSubtab(main_color); }
    else if (tab2 == 2) { RenderAntiAimSubtab(main_color); }
    else if (tab2 == 3) { RenderDesyncSubtab(main_color); }
    else if (tab2 == 4) { RenderVoidHideSubtab(main_color); }
    else if (tab2 == 5) { RenderBhopSubtab(main_color); }
}
else if (tab == 1)
{
        // Content header + horizontal subtab bar
        UI::ContentHeader("VISUALS");
        {
            static float sa[6] = {};
            ImGui::SetCursorPosX(ctX);
            if (UI::ContentSubtab("ESP", tab2 == 0, sa[0])) tab2 = 0;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("Combat", tab2 == 1, sa[1])) tab2 = 1;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("World", tab2 == 2, sa[2])) tab2 = 2;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("Colours", tab2 == 3, sa[3])) tab2 = 3;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("Crosshair", tab2 == 4, sa[4])) tab2 = 4;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("FOV", tab2 == 5, sa[5])) tab2 = 5;
            ImGui::Dummy(ImVec2(0, 8 * sc));
        }

if (tab2 == 0) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::CollapsibleSection("ESP FEATURES", halfW))
{
UI::labelsection("FILTER");
UI::Checkbox("Master Enable", &Options::ESP::Enabled);
UI::Checkbox("Team Check", &Options::ESP::TeamCheck);
UI::Checkbox("Visibility Colors", &Options::ESP::VisibilityCheck);
UI::Checkbox("Visibility Bones", &Options::ESP::VisibilityChams);

UI::labelsection("PLAYER LIST");
UI::Checkbox("Player Filter", &Options::PlayerFilter::Enabled);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Focus / exclude players. Excluded players are hidden from the ESP and skipped by the aimbot.");
UI::Checkbox("Focus Only", &Options::PlayerFilter::FocusOnly);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("When at least one player is marked as Focus, the aimbot only targets focused players.");
UI::Checkbox("Exclude Friends", &Options::PlayerFilter::ExcludeFriends);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hides players in your friend list from the ESP and the aimbot.");

UI::labelsection("PLAYER INFO");
	UI::Checkbox("Names", &Options::ESP::Name);
	UI::Checkbox("Distance", &Options::ESP::Distance);
	UI::Checkbox("Health Bar", &Options::ESP::Health);
	UI::Checkbox("Health Text", &Options::ESP::HealthText);
	UI::Checkbox("HP Above Head", &Options::ESP::EnemyHealthIndicator);
	UI::Checkbox("Tool", &Options::ESP::Tool);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shows the equipped tool under the target's name.");
	if (Options::ESP::Tool)
	{
		UI::SliderFloat("Tool Size", &Options::ESP::ToolSize, 8.0f, 24.0f, "%.1f");
		UI::ColorEdit4("Tool Color", Options::ESP::ToolColor, ImGuiColorEditFlags_NoInputs);
	}

	UI::labelsection("OVERLAYS");
	UI::Checkbox("Corner ESP", &Options::ESP::CornerESP);
	UI::Checkbox("Tracers", &Options::ESP::Tracers);
	UI::Checkbox("Skeleton", &Options::ESP::Skeleton);
	if (Options::ESP::Skeleton)
	{
		UI::Checkbox("Visibility Colors", &Options::ESP::SkeletonVisibilityColor);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Opt-in per-bone visibility coloring for the skeleton.\nBones with line of sight use the Visible color, occluded bones use the Hidden color (see the Visibility section).\nOff keeps the skeleton a single Skeleton Color.");
	}
	UI::Checkbox("Head Circle", &Options::ESP::HeadCircle);
	UI::Checkbox("Head Dot", &Options::ESP::HeadDot);
	UI::Checkbox("Arrows", &Options::ESP::Arrows);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shows directional arrows for off-screen players.");
	UI::Checkbox("Radar", &Options::ESP::Radar);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shows a circular radar overlay with nearby players.");
	if (Options::ESP::Radar)
	{
		static const char* radarThemes[]{ "Classic", "Minimal", "Neon", "Compass" };
		UI::Combo("Radar Theme", &Options::ESP::RadarTheme, radarThemes, IM_ARRAYSIZE(radarThemes));
	}
	UI::Checkbox("Rig Type", &Options::ESP::RigType);
	UI::Checkbox("Local Only", &Options::ESP::LocalOnly);
	UI::Checkbox("Avatar Icon", &Options::ESP::AvatarIcon);

	// ── Model Changer ────────────────────────────────────────────────────────
	// Overlays a model from the Models folder on the local player's character.
	// The real character still drives movement and animation; the model follows.
	UI::labelsection("MODEL CHANGER");
	UI::Checkbox("Model Changer##mc", &Options::ModelChanger::Enabled);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Draws a custom model over YOUR character.\nThe character keeps moving and animating; the model follows it.\nModels are read from the Models folder (.glb preferred, .obj fallback).");
	if (Options::ModelChanger::Enabled)
	{
		const auto& mcModels = ModelChanger::GetModelList();
		if (mcModels.empty())
		{
			ImGui::TextWrapped("No .glb or .obj models found in the Models folder.");
		}
		else
		{
			std::vector<const char*> mcNames;
			mcNames.reserve(mcModels.size());
			for (const auto& m : mcModels) mcNames.push_back(m.display.c_str());
			if (Options::ModelChanger::Model < 0 || Options::ModelChanger::Model >= (int)mcNames.size())
				Options::ModelChanger::Model = 0;
			UI::Combo("Model##mc", &Options::ModelChanger::Model, mcNames.data(), (int)mcNames.size());
		}
		UI::Checkbox("Follow Facing##mc", &Options::ModelChanger::FollowFacing);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Turn the model with your character's look direction.");
		UI::Checkbox("Mirror Player##mc", &Options::ModelChanger::Animate);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pose the model from YOUR character's real parts: it walks when you\nwalk, jumps and falls with you, sits when you sit, and copies any emote\nyou perform. Works on any rig. Overrides the emote selection below\n(turn off to use emotes instead).");
		UI::Checkbox("Hide Character##mc", &Options::ModelChanger::HideCharacter);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hide YOUR character's body so only the model shows.\nWrites the character parts' Transparency (client-side).");
		UI::SliderFloat("Scale##mc", &Options::ModelChanger::Scale, 0.5f, 6.0f, "%.2f");
		UI::SliderFloat("Yaw Offset##mc", &Options::ModelChanger::YawOffset, -180.0f, 180.0f, "%.0f");
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotate the model to correct a wrong default facing (degrees).");
		UI::SliderFloat("Pitch Offset##mc", &Options::ModelChanger::PitchOffset, -90.0f, 90.0f, "%.0f");
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tilt the model if it is lying down (wrong up axis).");

		// ── Emotes (skeletal animation for rigged models) ────────────────────
		UI::Checkbox("Emotes##mc", &Options::ModelChanger::EmoteEnabled);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Animate the model's skeleton (emotes).\nOnly rigged models with skin data can animate,\ne.g. the built-in RobloxR15Rigged model.");
		if (Options::ModelChanger::Animate)
		{
			ImGui::TextWrapped("Mirror Player is on, so the model follows your movement.\nTurn it off to play the emotes below.");
		}
		if (Options::ModelChanger::EmoteEnabled && !Options::ModelChanger::Animate)
		{
			auto& em = EmoteSystem::EmoteManager::Get();
			em.Initialize();
			std::vector<const char*> emNames;
			emNames.reserve(em.Count() + 1);
			emNames.push_back("None");
			for (int i = 0; i < em.Count(); ++i) emNames.push_back(em.Name(i));
			int sel = Options::ModelChanger::Emote + 1; // 0 == None
			if (sel < 0 || sel >= (int)emNames.size()) sel = 0;
			if (UI::Combo("Emote##mc", &sel, emNames.data(), (int)emNames.size()))
				Options::ModelChanger::Emote = sel - 1;
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pick an emote to play on the model.");
			UI::SliderFloat("Emote Speed##mc", &Options::ModelChanger::EmoteSpeed, 0.25f, 3.0f, "%.2f");
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Playback speed of the selected emote.");
		}
	}

	// Rivals rank / level. Rank draws the tier icon when one exists for it and
	// falls back to text otherwise, since only division 3 of each tier has art.
	UI::labelsection("RANK / LEVEL");
	UI::Checkbox("Rank", &Options::ESP::Rank);
	if (Options::ESP::Rank)
	{
		UI::SliderFloat("Rank Icon Size", &Options::ESP::RankIconSize, 8.0f, 48.0f, "%.0f");
		UI::Checkbox("Rank Icon Background", &Options::ESP::RankIconBackground);
	}
	UI::Checkbox("Level", &Options::ESP::Level);

	// Our own leaderboard panel, shown while the key is held. Separate from
	// Roblox's real TAB list, which we do not touch.
	UI::Checkbox("Tab List", &Options::ESP::TabList);
	if (Options::ESP::TabList && (Options::ESP::Rank || Options::ESP::Level))
	{
		UI::Bind("##tab_list_key", &Options::ESP::TabListKey, &Options::ESP::TabListToggle);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("While held, the overlay draws its own player list with level and rank icon, sorted by ELO.\n\nThis is our own panel, not Roblox's real TAB list. Writing into the real one needs a Roblox string allocated inside the game process, which is not safe to do from an external read.");
	}
	// Decoding happens here rather than mid-frame, so the first enemy on screen
	// does not stall on a disk read plus texture upload.
	RankIcons::Preload();

	UI::labelsection("BOX FILL");
	UI::Checkbox("Box", &Options::ESP::Box);
	if (Options::ESP::Box)
	{
		static const char* boxTypes[]{ "None", "Normal Box", "3D Box" };
		UI::Combo("Type", &Options::ESP::BoxType, boxTypes, IM_ARRAYSIZE(boxTypes));
		UI::Checkbox("Fill", &Options::ESP::BoxFill);
		if (Options::ESP::BoxFill)
		{
			UI::Checkbox("Gradient Fill", &Options::ESP::BoxFillGradient);
			UI::SliderFloat("Fill Opacity", &Options::ESP::BoxFillOpacity, 0.0f, 1.0f, "%.2f");
			if (Options::ESP::BoxFillGradient)
			{
				static const char* fillTypes[]{ "Vertical", "Horizontal", "Four-Corner" };
				UI::Combo("Fill Type", &Options::ESP::BoxFillType, fillTypes, IM_ARRAYSIZE(fillTypes));
				UI::Checkbox("Rotate Gradient", &Options::ESP::BoxFillGradientRotate);
				if (Options::ESP::BoxFillGradientRotate)
					UI::SliderFloat("Fill Speed", &Options::ESP::BoxFillSpeed, 0.1f, 10.0f, "%.1f");
				UI::ColorEdit4("Top Color", Options::ESP::BoxFillTopColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoAlpha);
				UI::ColorEdit4("Bottom Color", Options::ESP::BoxFillBottomColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoAlpha);
			}
			else
			{
				UI::ColorEdit4("Fill Color", Options::ESP::BoxFillColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoAlpha);
			}
		}
	}

	UI::labelsection("HEALTHBAR");
	UI::Checkbox("Gradient Healthbar", &Options::ESP::GradientHealthbar);
	if (Options::ESP::GradientHealthbar)
	{
		UI::ColorEdit4("Top Color", Options::ESP::HealthbarTopColor, ImGuiColorEditFlags_NoInputs);
		UI::ColorEdit4("Middle Color", Options::ESP::HealthbarMiddleColor, ImGuiColorEditFlags_NoInputs);
		UI::ColorEdit4("Bottom Color", Options::ESP::HealthbarBottomColor, ImGuiColorEditFlags_NoInputs);
	}

	UI::labelsection("EFFECTS");
	UI::Checkbox("Glow", &Options::ESP::Glow);
	UI::Checkbox("Pulse", &Options::ESP::Pulse);
	if (Options::ESP::Pulse)
		UI::SliderFloat("Pulse Speed", &Options::ESP::PulseSpeed, 0.1f, 5.0f, "%.2f");
	UI::Checkbox("Rings", &Options::ESP::Rings);
	if (Options::ESP::Rings)
		UI::SliderFloat("Ring Radius", &Options::ESP::RingRadius, 10.0f, 150.0f, "%.0f");
	UI::Checkbox("Trails", &Options::ESP::Trails);
	if (Options::ESP::Trails)
		UI::SliderInt("Trail Length", &Options::ESP::TrailLength, 4, 60);

	UI::labelsection("NAME & IMAGE");
	static const char* nameModes[]{ "Username", "Health%" };
	UI::Combo("Name Mode", &Options::ESP::NameMode, nameModes, IM_ARRAYSIZE(nameModes));
	UI::Checkbox("Custom Image", &Options::ESP::CustomImage);
	if (Options::ESP::CustomImage)
	{
		UI::SliderFloat("Image Scale", &Options::ESP::CustomImageScale, 0.2f, 4.0f, "%.2f");
		char imgBuf[256]; strncpy_s(imgBuf, Options::ESP::CustomImagePath, sizeof(imgBuf) - 1);
		if (ImGui::InputText("Image Path", imgBuf, sizeof(imgBuf)))
			strncpy_s(Options::ESP::CustomImagePath, imgBuf, sizeof(Options::ESP::CustomImagePath) - 1);
		ImGui::SameLine();
		if (ImGui::Button("Browse", ImVec2(-1, 20)))
		{
			OPENFILENAMEA ofn = { 0 };
			char szFile[256] = { 0 };
			ofn.lStructSize = sizeof(ofn);
			ofn.hwndOwner = GetActiveWindow();
			ofn.lpstrFile = szFile;
			ofn.nMaxFile = sizeof(szFile);
			ofn.lpstrFilter = "Image Files\0*.png;*.jpg;*.jpeg;*.bmp\0All Files\0*.*\0";
			ofn.nFilterIndex = 1;
			ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
			if (GetOpenFileNameA(&ofn))
			{
				strncpy_s(Options::ESP::CustomImagePath, szFile, _TRUNCATE);
			}
		}
	}
	}
UI::CollapsibleEnd();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
if (UI::CollapsibleSection("ESP SETTINGS", cardW))
{
	UI::labelsection("BOX");
	UI::SliderFloat("Box Thickness", &Options::ESP::BoxThickness, 1.0f, 10.0f);
UI::SliderFloat("3D Box Thickness", &Options::ESP::ESP3DThickness, 1.0f, 10.0f);

UI::labelsection("LINES");
UI::SliderFloat("Tracer Thickness", &Options::ESP::TracerThickness, 1.0f, 10.0f);
UI::SliderFloat("Skeleton Thickness", &Options::ESP::SkeletonThickness, 1.0f, 10.0f);

UI::labelsection("TEXT");
UI::SliderFloat("Name Size", &Options::ESP::NameSize, 8.0f, 24.0f, "%.1f");
UI::SliderFloat("Name Thickness", &Options::ESP::NameThickness, 0.0f, 5.0f, "%.1f");
UI::SliderFloat("Distance Size", &Options::ESP::DistanceSize, 8.0f, 24.0f, "%.1f");
UI::SliderFloat("Rig Type Size", &Options::ESP::RigTypeSize, 8.0f, 24.0f, "%.1f");
UI::SliderFloat("Arrow Size", &Options::ESP::ArrowSize, 8.0f, 32.0f, "%.1f");

UI::labelsection("HEAD");
UI::SliderFloat("Circle Thickness", &Options::ESP::HeadCircleThickness, 1.0f, 10.0f);
UI::SliderFloat("Circle Size", &Options::ESP::HeadCircleScale, 0.05f, 0.20f, "%.2f");

if (Options::ESP::VisibilityCheck || Options::ESP::VisibilityChams)
{
UI::labelsection("VISIBILITY");
UI::SliderFloat("Scan Range", &Options::ESP::VisibilityMaxDistance, 100.0f, 800.0f, "%.0f");
}

UI::labelsection("KEYBIND");
static const char* toggleTypes[]{ "Hold", "Toggle" };
UI::Combo("Mode##esp", &Options::ESP::ToggleType, toggleTypes, IM_ARRAYSIZE(toggleTypes));
            UI::Bind("##esp_key", &Options::ESP::ESPKey, &Options::ESP::ToggleType);

            UI::labelsection("CHAMS");
            UI::Checkbox("Chams Enabled", &Options::Chams::Enabled);
            UI::Checkbox("Team Check", &Options::Chams::TeamCheck);
            UI::Checkbox("Fade", &Options::Chams::ChamsFade);
            if (Options::Chams::ChamsFade)
                UI::SliderInt("Fade Speed", &Options::Chams::ChamsFadeSpeed, 1, 10);
            UI::Checkbox("Gradient Fill", &Options::Chams::GradientFill);
            UI::Checkbox("Wireframe", &Options::Chams::Wireframe);
            if (Options::Chams::Wireframe)
                UI::SliderFloat("Wireframe Thickness", &Options::Chams::WireframeThickness, 0.5f, 5.0f, "%.1f");
            UI::Checkbox("Include Accessories", &Options::Chams::IncludeAccessories);
            UI::Checkbox("Gradient Fill", &Options::Chams::GradientFill);
            UI::ColorEdit4("Fill Color 1", Options::Chams::FillColor, ImGuiColorEditFlags_NoInputs);
            UI::ColorEdit4("Fill Color 2", Options::Chams::FillColor2, ImGuiColorEditFlags_NoInputs);
            UI::ColorEdit4("Outline Color", Options::Chams::OutlineColor, ImGuiColorEditFlags_NoInputs);
            }
            UI::CollapsibleEnd();
}
else if (tab2 == 1) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::CollapsibleSection("HIT FEEDBACK", halfW))
{
UI::labelsection("HITS");
UI::Checkbox("Hit Sounds", &Options::Combat::HitSounds);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Plays a sound whenever you hit someone.");
if (Options::Combat::HitSounds)
{
    auto& soundFiles = Globals::HitSounds::Files;
    if (soundFiles.empty())
    {
        ImGui::TextDisabled("No sounds found");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Place audio files in the 'hitsounds' folder next to the exe.");
    }
    else
    {
        if (Options::Combat::HitSoundType < 0 || Options::Combat::HitSoundType >= (int)soundFiles.size())
            Options::Combat::HitSoundType = 0;
        static auto hitSoundGetter = [](void*, int idx, const char** out_text) -> bool
        {
            auto& files = Globals::HitSounds::Files;
            if (idx < 0 || idx >= (int)files.size()) return false;
            *out_text = files[idx].c_str();
            return true;
        };
        ImGui::Combo("Hit Sound", &Options::Combat::HitSoundType, hitSoundGetter, nullptr, (int)soundFiles.size());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Select which sound plays on hit.");
        if (ImGui::Button("Preview", ImVec2(-1, 20)))
        {
            std::string path = Globals::HitSounds::FolderPath + "\\" + soundFiles[Options::Combat::HitSoundType];
            CombatFeedback::PlaySoundFile(path);
        }
    }
}
UI::Checkbox("Hit Notifications", &Options::Combat::HitNotifications);
UI::Checkbox("Hit Chams", &Options::Combat::HitChams);
UI::Checkbox("Hit Effects", &Options::Combat::HitEffects);
if (Options::Combat::HitEffects)
{
    static const char* hmStyles[]{ "Cross", "Circle", "Dot" };
    UI::Combo("Hitmarker Style", &Options::Combat::HitmarkerStyle, hmStyles, IM_ARRAYSIZE(hmStyles));
    UI::Checkbox("On Crosshair", &Options::Combat::HitmarkerOnCrosshair);
    UI::SliderFloat("Hitmarker Size", &Options::Combat::HitmarkerSize, 4.0f, 20.0f, "%.1f");
    UI::SliderFloat("Hitmarker Thickness", &Options::Combat::HitmarkerThickness, 1.0f, 5.0f, "%.1f");
}

UI::labelsection("TRACERS");
UI::Checkbox("Bullet Tracers", &Options::Combat::BulletTracers);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Draws tracer lines from your position to the target on hit.");
}
UI::CollapsibleEnd();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
if (UI::CollapsibleSection("HIT SETTINGS", cardW))
{
UI::labelsection("DAMAGE");
UI::SliderFloat("Min Damage", &Options::Combat::MinDamage, 1.0f, 50.0f, "%.0f");
UI::SliderFloat("Chams Duration", &Options::Combat::HitChamsDuration, 0.1f, 2.0f, "%.2fs");
UI::SliderFloat("Effect Duration", &Options::Combat::HitEffectDuration, 0.1f, 2.0f, "%.2fs");
UI::ColorEdit3("Hit Chams Color", Options::Combat::HitChamsColor, ImGuiColorEditFlags_NoInputs);
UI::ColorEdit3("Hit Effect Color", Options::Combat::HitEffectColor, ImGuiColorEditFlags_NoInputs);

if (Options::Combat::BulletTracers)
{
UI::labelsection("BULLET TRACERS");
UI::ColorEdit3("Tracer Color", Options::Combat::BulletTracerColor, ImGuiColorEditFlags_NoInputs);
UI::SliderFloat("Tracer Duration", &Options::Combat::BulletTracerDuration, 0.1f, 3.0f, "%.1fs");
UI::SliderFloat("Tracer Width", &Options::Combat::BulletTracerThickness, 0.5f, 5.0f, "%.1f");
static const char* tracerStyles[]{ "Solid", "Glow", "Dashed", "Pulse" };
UI::Combo("Style##tracer", &Options::Combat::BulletTracerStyle, tracerStyles, IM_ARRAYSIZE(tracerStyles));
}
}
UI::CollapsibleEnd();
}
else if (tab2 == 2) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::CollapsibleSection("WORLD", halfW))
{
            UI::labelsection("MAIN");
            UI::Checkbox("Enabled", &Options::World::Enabled);
            UI::Checkbox("Fullbright", &Options::World::Fullbright);
            UI::Checkbox("No Shadows", &Options::World::NoShadows);
            UI::Checkbox("Skybox Changer", &Options::World::SkyboxChanger);

            static const char* skyPresets[]{ "Default", "Blue", "Night", "Storm", "Sunset", "Grass", "Plastic", "Red", "Purple", "Pink", "Gold" };
            UI::Combo("Sky Preset", &Options::World::SkyboxPreset, skyPresets, IM_ARRAYSIZE(skyPresets));
UI::Checkbox("Rotate Skybox", &Options::World::RotateSkybox);
	if (Options::World::RotateSkybox)
		UI::SliderFloat("Rotate Speed", &Options::World::SkyboxRotateSpeed, 0.1f, 10.0f, "%.1f");

	UI::labelsection("WEATHER");
	UI::Checkbox("Enabled", &Options::World::Weather::Enabled);
	if (Options::World::Weather::Enabled)
	{
		static const char* weatherTypes[]{ "Snow", "Rain" };
		UI::Combo("Type", &Options::World::Weather::Type, weatherTypes, IM_ARRAYSIZE(weatherTypes));

		UI::SliderFloat("Intensity", &Options::World::Weather::Intensity, 50.0f, 1500.0f, "%.0f");
		UI::SliderFloat("Speed", &Options::World::Weather::Speed, 0.1f, 4.0f, "%.2f");
		UI::SliderFloat("Wind", &Options::World::Weather::Wind, -25.0f, 25.0f, "%.1f");
		UI::Checkbox("Distance Haze", &Options::World::Weather::Haze);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fades distant precipitation toward the sky haze colour.\n\nThis is the main depth cue. Without it, far particles keep full colour and the effect reads as a flat sheet of stickers instead of weather receding into the distance.");

		if (Options::World::Weather::Type == 0)
		{
			UI::SliderFloat("Snow Size", &Options::World::Weather::SnowSize, 0.4f, 4.0f, "%.2f");
			UI::ColorEdit3("Snow Colour", &Options::World::Weather::SnowColor[0]);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Near-white with only a trace of cool tint, which is what an ice crystal actually looks like. Defaults to 245,249,255.");
		}
		else
		{
			UI::SliderFloat("Rain Thickness", &Options::World::Weather::RainThickness, 0.5f, 4.0f, "%.2f");
			UI::SliderFloat("Rain Streak", &Options::World::Weather::RainStreak, 0.005f, 0.15f, "%.3f");
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shutter time. The streak is drawn along the drop's real velocity vector, so raising this lengthens the motion blur and slants it further with wind.");
			UI::ColorEdit3("Rain Colour", &Options::World::Weather::RainColor[0]);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("A raindrop is colourless water; what you see is scattered light, which reads as desaturated cool grey. Defaults to 148,156,168 - deliberately not blue.");
			UI::ColorEdit3("Splash Colour", &Options::World::Weather::SplashColor[0]);
		}
	}
}
            UI::CollapsibleEnd();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
if (UI::CollapsibleSection("LIGHTING", cardW))
{
	UI::labelsection("TIME & BRIGHTNESS");
	UI::SliderFloat("Clock Time", &Options::World::ClockTime, 0.0f, 24.0f, "%.1f");
	UI::SliderFloat("Brightness", &Options::World::Brightness, 0.0f, 5.0f, "%.1f");
	UI::Checkbox("Brightness (Separate)", &Options::World::BrightnessEnabled);
	if (Options::World::BrightnessEnabled)
		UI::SliderFloat("Brightness Value", &Options::World::BrightnessValue, 0.0f, 5.0f, "%.1f");
	UI::Checkbox("Exposure", &Options::World::Exposure);
	if (Options::World::Exposure)
		UI::SliderFloat("Exposure Value", &Options::World::ExposureValue, -2.0f, 2.0f, "%.2f");

	UI::labelsection("FOG");
	UI::Checkbox("Fog (Separate)", &Options::World::FogEnabled);
	if (Options::World::FogEnabled)
	{
		UI::SliderFloat("Fog Distance", &Options::World::FogDistance, 0.0f, 100000.0f, "%.0f");
		UI::ColorEdit4("Fog Color", Options::World::FogColor2, ImGuiColorEditFlags_NoInputs);
	}
	else
	{
		UI::SliderFloat("Fog Start", &Options::World::FogStart, 0.0f, 1000.0f, "%.0f");
		UI::SliderFloat("Fog End", &Options::World::FogEnd, 50.0f, 100000.0f, "%.0f");
		UI::ColorEdit3("Fog Color", Options::World::FogColor, ImGuiColorEditFlags_NoInputs);
	}

	UI::labelsection("AMBIENT");
	UI::Checkbox("Ambience (Separate)", &Options::World::Ambience);
	if (Options::World::Ambience)
		UI::ColorEdit4("Ambience Color", Options::World::AmbienceColor, ImGuiColorEditFlags_NoInputs);
	else
	{
		UI::ColorEdit3("Ambient", Options::World::Ambient, ImGuiColorEditFlags_NoInputs);
		UI::ColorEdit3("Outdoor Ambient", Options::World::OutdoorAmbient, ImGuiColorEditFlags_NoInputs);
	}
}
UI::CollapsibleEnd();
}
else if (tab2 == 3) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::CollapsibleSection("ESP COLOURS", halfW))
{
UI::labelsection("BOX");
UI::ColorEdit3("Box Color", Options::ESP::BoxColor, ImGuiColorEditFlags_NoInputs);
UI::ColorEdit3("3D Box Color", Options::ESP::ESP3DColor, ImGuiColorEditFlags_NoInputs);

UI::labelsection("INFO");
UI::ColorEdit3("Name Color", Options::ESP::Color, ImGuiColorEditFlags_NoInputs);
UI::ColorEdit3("Distance Color", Options::ESP::DistanceColor, ImGuiColorEditFlags_NoInputs);

	UI::labelsection("OVERLAYS");
	UI::ColorEdit3("Tracer Color", Options::ESP::TracerColor, ImGuiColorEditFlags_NoInputs);
	UI::ColorEdit3("Skeleton Color", Options::ESP::SkeletonColor, ImGuiColorEditFlags_NoInputs);
	UI::ColorEdit3("Head Circle Color", Options::ESP::HeadCircleColor, ImGuiColorEditFlags_NoInputs);
	UI::ColorEdit3("Head Dot Color", Options::ESP::HeadDotColor, ImGuiColorEditFlags_NoInputs);
	UI::ColorEdit3("Corner Color", Options::ESP::CornerColor, ImGuiColorEditFlags_NoInputs);

	UI::labelsection("BOX FILL");
	UI::ColorEdit4("Fill Color", Options::ESP::BoxFillColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoAlpha);
	UI::ColorEdit4("Fill Top Color", Options::ESP::BoxFillTopColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoAlpha);
	UI::ColorEdit4("Fill Bottom Color", Options::ESP::BoxFillBottomColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoAlpha);

	UI::labelsection("HEALTHBAR");
	UI::ColorEdit4("Health Top", Options::ESP::HealthbarTopColor, ImGuiColorEditFlags_NoInputs);
	UI::ColorEdit4("Health Middle", Options::ESP::HealthbarMiddleColor, ImGuiColorEditFlags_NoInputs);
	UI::ColorEdit4("Health Bottom", Options::ESP::HealthbarBottomColor, ImGuiColorEditFlags_NoInputs);

	UI::labelsection("EXTRA");
	UI::ColorEdit3("Rig Type Color", Options::ESP::RigTypeColor, ImGuiColorEditFlags_NoInputs);

	UI::labelsection("CHAMS");
	UI::ColorEdit4("Fill Color", Options::Chams::FillColor, ImGuiColorEditFlags_NoInputs);
	UI::ColorEdit4("Outline Color", Options::Chams::OutlineColor, ImGuiColorEditFlags_NoInputs);

	UI::labelsection("VISIBILITY");
UI::ColorEdit3("Visible", Options::ESP::VisibleColor, ImGuiColorEditFlags_NoInputs);
UI::ColorEdit3("Hidden", Options::ESP::HiddenColor, ImGuiColorEditFlags_NoInputs);
}
UI::CollapsibleEnd();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
if (UI::CollapsibleSection("FOV & MENU", cardW))
{
UI::labelsection("THEME");
{
    float availW = ImGui::GetContentRegionAvail().x;
    float cardW = (availW - 6.0f * sc) * 0.5f;
    float cardH = 52.0f * sc;
    auto* draw = ImGui::GetWindowDrawList();

    for (int t = 0; t < MenuThemes::Count; t++)
    {
        if (t % 2 == 1) ImGui::SameLine(0, 6.0f * sc);

        ImVec2 cp = ImGui::GetCursorScreenPos();
        bool sel = (Options::Misc::MenuTheme == t);

        // Card background
        ImU32 bg = sel ? ImGui::ColorConvertFloat4ToU32(ImVec4(
            UI::P.accent.x * 0.15f, UI::P.accent.y * 0.15f, UI::P.accent.z * 0.15f, 0.30f))
            : ImGui::ColorConvertFloat4ToU32(UI::P.card);
        draw->AddRectFilled(cp, ImVec2(cp.x + cardW, cp.y + cardH), bg, 7.0f * sc);

        // Border
        ImU32 border = sel ? ImGui::ColorConvertFloat4ToU32(UI::P.accent)
            : ImGui::ColorConvertFloat4ToU32(UI::P.borderDim);
        float bw = sel ? 1.5f * sc : 1.0f * sc;
        draw->AddRect(cp, ImVec2(cp.x + cardW, cp.y + cardH), border, 7.0f * sc, 0, bw);

        // Theme name
        draw->AddText(ImGui::GetFont(), 10.0f * sc,
            ImVec2(cp.x + 9.0f * sc, cp.y + 7.0f * sc),
            sel ? ImGui::ColorConvertFloat4ToU32(UI::P.accent)
                : ImGui::ColorConvertFloat4ToU32(UI::P.text),
            MenuThemes::Presets[t].name);

        // Color swatches
        float swX = cp.x + 9.0f * sc, swY = cp.y + 26.0f * sc;
        for (int c = 0; c < 4; c++)
        {
            const float* ch = (c == 0) ? MenuThemes::Presets[t].bg
                : (c == 1) ? MenuThemes::Presets[t].panel
                : (c == 2) ? MenuThemes::Presets[t].accent
                : MenuThemes::Presets[t].accent2;
            ImU32 swCol = ImGui::ColorConvertFloat4ToU32(ImVec4(ch[0], ch[1], ch[2], 1.0f));
            draw->AddRectFilled(ImVec2(swX, swY), ImVec2(swX + 13.0f * sc, swY + 13.0f * sc), swCol, 3.0f * sc);
            draw->AddRect(ImVec2(swX, swY), ImVec2(swX + 13.0f * sc, swY + 13.0f * sc),
                ImGui::ColorConvertFloat4ToU32(UI::P.borderDim), 3.0f * sc, 0, 0.5f);
            swX += 16.0f * sc;
        }

        // Gradient indicator
        if (MenuThemes::Presets[t].gradient) {
            ImVec2 gp(cp.x + cardW - 32.0f * sc, cp.y + cardH - 18.0f * sc);
            draw->AddRectFilled(gp, ImVec2(gp.x + 24.0f * sc, gp.y + 5.0f * sc),
                ImGui::ColorConvertFloat4ToU32(ImVec4(
                    MenuThemes::Presets[t].accent[0], MenuThemes::Presets[t].accent[1],
                    MenuThemes::Presets[t].accent[2], 1.0f)), 2.5f * sc);
            draw->AddRectFilled(ImVec2(gp.x + 8.0f * sc, gp.y), ImVec2(gp.x + 24.0f * sc, gp.y + 5.0f * sc),
                ImGui::ColorConvertFloat4ToU32(ImVec4(
                    MenuThemes::Presets[t].accent2[0], MenuThemes::Presets[t].accent2[1],
                    MenuThemes::Presets[t].accent2[2], 1.0f)), 2.5f * sc);
        }

        // InvisibleButton for click
        ImGui::SetCursorScreenPos(cp);
        char tid[32]; sprintf_s(tid, "##th_%d", t);
        if (ImGui::InvisibleButton(tid, ImVec2(cardW, cardH)))
            Options::Misc::MenuTheme = t;

        // Hover fill
        if (ImGui::IsItemHovered() && !sel) {
            draw->AddRectFilled(cp, ImVec2(cp.x + cardW, cp.y + cardH),
                ImGui::ColorConvertFloat4ToU32(ImVec4(UI::P.accent.x, UI::P.accent.y, UI::P.accent.z, 0.08f)), 7.0f * sc);
        }

        if (t % 2 == 0) ImGui::SameLine(0, 6.0f * sc);
        else ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f * sc);
    }
}

if (Options::Misc::MenuTheme == 0)
{
    UI::labelsection("CUSTOM COLORS");
    UI::ColorEdit3("Menu Background", Options::Misc::MenuBgColor, ImGuiColorEditFlags_NoInputs);
    UI::ColorEdit3("Panel Background", Options::Misc::MenuPanelColor, ImGuiColorEditFlags_NoInputs);
}

UI::labelsection("ACCENT");
UI::ColorEdit3("FOV Color", Options::Aimbot::FOVColor, ImGuiColorEditFlags_NoInputs);
UI::ColorEdit3("Menu Accent", Options::Misc::MenuAccentColor, ImGuiColorEditFlags_NoInputs);
main_color = ImVec4(Options::Misc::MenuAccentColor[0], Options::Misc::MenuAccentColor[1], Options::Misc::MenuAccentColor[2], 1.0f);
UI::Checkbox("Menu Gradient", &Options::Misc::MenuGradient);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Blend the two accent colors across the menu header.");
UI::ColorEdit3("Menu Accent 2", Options::Misc::MenuAccentColor2, ImGuiColorEditFlags_NoInputs);
main_color2 = ImVec4(Options::Misc::MenuAccentColor2[0], Options::Misc::MenuAccentColor2[1], Options::Misc::MenuAccentColor2[2], 1.0f);

UI::labelsection("AMBIENT BACKGROUND");
UI::Checkbox("Animated Background", &Options::Misc::ExteriumBGEnabled);
static const char* bgShapes[] = { "Square", "Circle", "Triangle", "Diamond" };
UI::Combo("Shape", &Options::Misc::ExteriumBGParticleShape, bgShapes, IM_ARRAYSIZE(bgShapes));
UI::Checkbox("Use Theme Accent", &Options::Misc::ExteriumBGUseAccent);
if (!Options::Misc::ExteriumBGUseAccent)
    ImGui::ColorEdit3("BG Color", Options::Misc::ExteriumBGColor, ImGuiColorEditFlags_NoInputs);
if (UI::SliderInt("Particles", &Options::Misc::ExteriumBGParticleCount, 8, 48, "%d"))
    Options::Misc::ExteriumBGParticleCount = ImClamp(Options::Misc::ExteriumBGParticleCount, 8, 48);
{
    static float bgSizeVal = 14.0f;
    bgSizeVal = (Options::Misc::ExteriumBGParticleMinSize + Options::Misc::ExteriumBGParticleMaxSize) * 0.5f;
    if (UI::SliderFloat("Size", &bgSizeVal, 4.0f, 40.0f, "%.0f px"))
    {
        Options::Misc::ExteriumBGParticleMinSize = bgSizeVal * 0.6f;
        Options::Misc::ExteriumBGParticleMaxSize = bgSizeVal * 1.4f;
    }
}
{
    static float bgSpeedVal = 70.0f;
    bgSpeedVal = Options::Misc::ExteriumBGParticleMaxSpeed;
    if (UI::SliderFloat("Speed", &bgSpeedVal, 20.0f, 160.0f, "%.0f"))
    {
        Options::Misc::ExteriumBGParticleMinSpeed = bgSpeedVal * 0.4f;
        Options::Misc::ExteriumBGParticleMaxSpeed = bgSpeedVal;
    }
}
{
    int bgPct = (int)(Options::Misc::ExteriumBGParticleOpacity * 500.0f);
    if (UI::SliderInt("Opacity", &bgPct, 0, 100, "%d%%"))
        Options::Misc::ExteriumBGParticleOpacity = bgPct * 0.002f;
}
UI::Checkbox("Glow", &Options::Misc::ExteriumBGParticleGlow);

UI::labelsection("FOV FILL");
UI::ColorEdit4("FOV Fill Color", Options::Aimbot::FOVFillColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoAlpha);
}
UI::CollapsibleEnd();
}

if (tab2 == 4)
{
ImGui::SetCursorPosX(ctX);
if (UI::CollapsibleSection("CROSSHAIR", fullW))
{
UI::labelsection("MAIN");
UI::Checkbox("Enabled", &Options::Crosshair::Enabled);
UI::Checkbox("Show Text", &Options::Crosshair::ShowText);

static const char* chStyles[]{ "Static", "Pulse", "Spin", "Dynamic" };
UI::Combo("Style", &Options::Crosshair::Style, chStyles, IM_ARRAYSIZE(chStyles));

static const char* chColorModes[]{ "Static", "Rainbow" };
UI::Combo("Color Mode", &Options::Crosshair::ColorMode, chColorModes, IM_ARRAYSIZE(chColorModes));

UI::labelsection("SIZE & SHAPE");
UI::SliderFloat("Size", &Options::Crosshair::Size, 1.0f, 40.0f, "%.1f");
UI::SliderFloat("Gap", &Options::Crosshair::Gap, 0.0f, 40.0f, "%.1f");
UI::SliderFloat("Thickness", &Options::Crosshair::Thickness, 1.0f, 10.0f, "%.1f");
UI::SliderFloat("Spin Speed", &Options::Crosshair::SpinSpeed, 0.0f, 360.0f, "%.0f deg/s");
UI::SliderFloat("Gap Speed", &Options::Crosshair::GapSpeed, 0.1f, 5.0f, "%.2f");
UI::SliderFloat("Opacity", &Options::Crosshair::Opacity, 0.1f, 1.0f, "%.2f");
UI::SliderFloat("Rainbow Speed", &Options::Crosshair::RainbowSpeed, 0.1f, 5.0f, "%.2f");

UI::labelsection("OPTIONS");
UI::Checkbox("Gap Tween", &Options::Crosshair::GapTween);
UI::Checkbox("Show Dot", &Options::Crosshair::ShowDot);
UI::Checkbox("Outline", &Options::Crosshair::Outline);
UI::Checkbox("T-Style", &Options::Crosshair::TStyle);

if (Options::Crosshair::ShowDot)
UI::SliderFloat("Dot Size", &Options::Crosshair::DotSize, 0.5f, 10.0f, "%.1f");
if (Options::Crosshair::Outline)
{
UI::SliderFloat("Outline Thickness", &Options::Crosshair::OutlineThickness, 0.5f, 5.0f, "%.1f");
UI::ColorEdit4("Outline Color", Options::Crosshair::OutlineColor, ImGuiColorEditFlags_NoInputs);
}

UI::labelsection("LENGTH");
static const char* lenModes[]{ "Equal 4 Lines", "Vertical Longer" };
UI::Combo("Length Mode", &Options::Crosshair::LengthMode, lenModes, IM_ARRAYSIZE(lenModes));
if (Options::Crosshair::LengthMode == 1)
UI::SliderFloat("Vertical Length", &Options::Crosshair::VLength, 0.0f, 40.0f, "%.1f");

UI::labelsection("COLOUR");
UI::ColorEdit4("Color", Options::Crosshair::Color, ImGuiColorEditFlags_NoInputs);
}
UI::CollapsibleEnd();
}
else if (tab2 == 5)
{
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::CollapsibleSection("FOV VISUALS", halfW))
{
UI::labelsection("DISPLAY");
UI::Checkbox("Show FOV", &Options::Aimbot::ShowFOV);
UI::Checkbox("Show FOV Fill", &Options::Aimbot::ShowFOVFill);
UI::Checkbox("Show FOV Text", &Options::Aimbot::ShowFOVText);

static const char* fovPositions[]{ "Screen Center", "Follow Target" };
UI::Combo("FOV Position", &Options::Aimbot::FOVPositionMode, fovPositions, IM_ARRAYSIZE(fovPositions), halfW);

static const char* fovShapes[]{ "Circle", "Square", "Triangle", "Hexagon" };
UI::Combo("FOV Shape", &Options::Aimbot::FOVShape, fovShapes, IM_ARRAYSIZE(fovShapes), halfW);

static const char* fovColorModes[]{ "Solid", "Gradient", "Shift", "Pulse" };
UI::Combo("FOV Color Mode", &Options::Aimbot::FOVColorMode, fovColorModes, IM_ARRAYSIZE(fovColorModes), halfW);

UI::Checkbox("FOV Glow", &Options::Aimbot::FOVGlow);
UI::Checkbox("FOV Breathing", &Options::Aimbot::FOVBreathing);
UI::Checkbox("FOV Spin", &Options::Aimbot::FOVSpin);
}
UI::CollapsibleEnd();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
if (UI::CollapsibleSection("STYLING", halfW))
{
UI::labelsection("STYLING");
UI::SliderFloat("FOV Thickness", &Options::Aimbot::FOVThickness, 1.0f, 10.0f, "%.1f");
if (Options::Aimbot::FOVColorMode == 1 || Options::Aimbot::FOVColorMode == 2)
    UI::SliderFloat("Gradient Speed", &Options::Aimbot::FOVGradientSpeed, 0.1f, 5.0f, "%.2f");
if (Options::Aimbot::FOVSpin)
    UI::SliderFloat("Spin Speed", &Options::Aimbot::FOVSpinSpeed, 0.1f, 5.0f, "%.2f");

UI::labelsection("COLOURS");
UI::ColorEdit3("FOV Color", Options::Aimbot::FOVColor, ImGuiColorEditFlags_NoInputs);
UI::ColorEdit4("FOV Fill", Options::Aimbot::FOVFillColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoAlpha);
UI::SliderFloat("Fill Opacity", &Options::Aimbot::FOVFillOpacity, 0.0f, 1.0f, "%.2f");
}
UI::CollapsibleEnd();
}
}
else if (tab == 3)
{
// Misc tab - Local settings only
UI::ContentHeader("MISC");
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::CollapsibleSection("MAIN", halfW))
{
UI::labelsection("LOCAL");
UI::Checkbox("Headless",       &Options::ESP::Headless);
UI::Checkbox("Show FOV",       &Options::Aimbot::ShowFOV);
UI::Checkbox("Show FOV Fill",  &Options::Aimbot::ShowFOVFill);
UI::Checkbox("Crosshair",      &Options::Crosshair::Enabled);
	UI::Checkbox("Camera FOV",     &Options::Misc::FOVEnabled);
	UI::Checkbox("Cache NPCs",     &Options::Misc::CacheNPCs);
	UI::Checkbox("Keybind List",   &Options::Misc::KeybindList);
	UI::Checkbox("Explorer",       &Options::Misc::ExplorerEnabled);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Opens the Roblox instance explorer (datamodel tree + properties / bytecode).");
	UI::Checkbox("Player List",    &Options::Misc::PlayerListEnabled);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Opens the player list window to select, exclude, focus or mark players as friends.");
	UI::Checkbox("Server Browser", &Options::Misc::ServerBrowserEnabled);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Browse all public servers of the current game: players, ping, region and one-click join.");
	UI::Checkbox("Third Person",   &Options::Misc::ThirdPerson);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Unlocks third-person camera in games that force first-person.");

	UI::labelsection("STEALTH");
	UI::Checkbox("Hide From Tabs", &Options::Misc::HideFromTabs);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Removes the overlay from Alt+Tab / Win+Tab and the taskbar (WS_EX_TOOLWINDOW).");
	UI::Checkbox("Hide Process", &Options::Misc::HideProcess);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Relaunches the cheat as a renamed copy in %TEMP% so Task Manager shows a benign name. Applies on next launch.");

	static const char* procPresets[]{ "MicrosoftEdgeUpdate", "OneDriveSetup", "SearchApp", "Widgets", "GameBar", "OneDriveStandaloneUpdater", "Custom..." };
	static int procSel = 0;
	if (strcmp(Options::Misc::ProcessName, "MicrosoftEdgeUpdate") == 0) procSel = 0;
	else if (strcmp(Options::Misc::ProcessName, "OneDriveSetup") == 0) procSel = 1;
	else if (strcmp(Options::Misc::ProcessName, "SearchApp") == 0) procSel = 2;
	else if (strcmp(Options::Misc::ProcessName, "Widgets") == 0) procSel = 3;
	else if (strcmp(Options::Misc::ProcessName, "GameBar") == 0) procSel = 4;
	else if (strcmp(Options::Misc::ProcessName, "OneDriveStandaloneUpdater") == 0) procSel = 5;
	else procSel = 6;

	if (UI::Combo("Process Name", &procSel, procPresets, IM_ARRAYSIZE(procPresets)))
	{
		if (procSel == 6) { /* keep custom */ }
		else strncpy_s(Options::Misc::ProcessName, procPresets[procSel], sizeof(Options::Misc::ProcessName) - 1);
	}
	if (procSel == 6)
	{
		ImGui::SameLine(); ImGui::SetNextItemWidth(180 * sc);
		char procBuf[64]; strncpy_s(procBuf, Options::Misc::ProcessName, sizeof(procBuf) - 1);
		if (ImGui::InputText("##proc_custom", procBuf, sizeof(procBuf)))
			strncpy_s(Options::Misc::ProcessName, procBuf, sizeof(Options::Misc::ProcessName) - 1);
	}

	char exclBuf[256]; strncpy_s(exclBuf, Options::Misc::ExclusionPath, sizeof(exclBuf) - 1);
	if (ImGui::InputText("Exclusion Path", exclBuf, sizeof(exclBuf)))
		strncpy_s(Options::Misc::ExclusionPath, exclBuf, sizeof(Options::Misc::ExclusionPath) - 1);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Folder the trace-wiper will never delete. Use it to store the cheat somewhere safe.");

	UI::Checkbox("Stream Proof", &Options::Misc::StreamProof);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Attempts to hide overlay from OBS/Discord stream capture (DWM exclusion).");
}
UI::CollapsibleEnd();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
if (UI::CollapsibleSection("SETTINGS", halfW))
{
if (Options::Misc::FOVEnabled)
UI::SliderFloat("Camera FOV", &Options::Misc::FOV, 70.f, 120.f, "%.0f");

UI::labelsection("KEYBIND LIST POSITION");
UI::SliderFloat("Position X", &Options::Misc::KeybindListX, 0.0f, 1920.0f, "%.0f");
UI::SliderFloat("Position Y", &Options::Misc::KeybindListY, 0.0f, 1080.0f, "%.0f");

UI::labelsection("MENU KEY");
UI::Bind("##menu_key", &Options::Misc::MenuKey);

UI::labelsection("MENU FONT");
UI::Combo("Font", &Options::Misc::MenuFont, MenuFonts::Names, IM_ARRAYSIZE(MenuFonts::Names));
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Live-switches the menu typography. Applies immediately.");
UI::SliderFloat("Menu Scale", &Options::Misc::MenuScale, 0.6f, 2.5f, "%.2fx");
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zooms the entire menu (text, panels and graphics). Drag the title bar to move it.");
UI::Checkbox("Menu Click Sound", &Options::Misc::MenuClickSound);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Plays a short tick when clicking menu widgets.");
if (Options::Misc::MenuClickSound)
{
    UI::SliderInt("Click Volume", &Options::Misc::MenuClickVolume, 0, 100, "%d%%");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Volume of the menu click sound (applied as a playback gain).");
    if (ImGui::IsItemDeactivatedAfterEdit()) UI::ClickPreview();
}
UI::Checkbox("Click Effect", &Options::Misc::MenuClickEffect);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Draws a sparkle burst at the cursor when you click a menu widget.");
if (Options::Misc::MenuClickEffect)
{
    ImGui::ColorEdit3("Effect Color", Options::Misc::MenuClickEffectColor, ImGuiColorEditFlags_NoInputs);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tint of the click effect (neutral white by default).");
}

UI::labelsection("CLIENT & OFFSETS");
UI::Checkbox("Auto Update Offsets", &Options::Misc::AutoUpdateOffsets);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fetch the offset dump for the Roblox build you are running, from offsets.imtheo.lol.");
UI::Checkbox("Auto Update Roblox", &Options::Misc::AutoUpdateRoblox);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("When Roblox is older than the live build, run the Roblox installer before attaching.");
UI::Checkbox("Auto Launch Roblox", &Options::Misc::AutoLaunchRoblox);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Start Roblox by itself when it is not already running.");

UI::labelsection("MENU EFFECT");
UI::Checkbox("Enable##weather", &MenuWeather::Enabled);

static const char* weatherKinds[] = { "Snow", "Rain" };
UI::Combo("Type", &MenuWeather::Type, weatherKinds, 2);
UI::SliderInt("Intensity", &MenuWeather::Intensity, 64, 2000, "%d particles");
UI::SliderFloat("Fall Speed",     &MenuWeather::Speed,         0.2f, 6.0f,  "%.2fx");
UI::SliderFloat("Wind",           &MenuWeather::Wind,         -3.f,  3.f,   "%.2fx");
UI::SliderFloat("Snow Size",      &MenuWeather::SnowSize,      0.5f, 4.0f,  "%.1f px");
UI::SliderFloat("Rain Thickness", &MenuWeather::RainThickness, 0.5f, 3.0f,  "%.1f px");
ImGui::ColorEdit3 ("Particle Color", MenuWeather::Color, ImGuiColorEditFlags_NoInputs);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Falling snowflakes or rain streaks across the menu background. Settings are saved with your config.");

ImGui::Dummy(ImVec2(0, 15));

// Shader backdrop for the content panel and the sidebar (same effect on both).
UI::labelsection("SHADER BACKGROUND");
UI::Combo("Effect", &Options::Misc::ShaderBackground,
    shader::BackgroundNames(), shader::BG_COUNT);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Animated shader shown behind the content panel and the sidebar. Settings are saved with your config.");
UI::SliderFloat("Opacity", &Options::Misc::ShaderBackgroundOpacity, 0.0f, 1.0f, "%.2f");
if (ImGui::IsItemHovered()) ImGui::SetTooltip("How strongly the shader blends over the panel background.");

ImGui::Dummy(ImVec2(0, 15));
ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.6f));
ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 0.8f));
ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
if (ImGui::Button("Unload", ImVec2(-1, 28)))
{
exit(0);
}
ImGui::PopStyleColor(3);
}
UI::CollapsibleEnd();
}
else if (tab == 4)
{
// Movement tab
        // Content header + horizontal subtab bar
        UI::ContentHeader("MOVEMENT");
        {
            static float sa[7] = {};
            ImGui::SetCursorPosX(ctX);
            if (UI::ContentSubtab("Fly", tab2 == 0, sa[0])) tab2 = 0;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("WalkSpeed", tab2 == 1, sa[1])) tab2 = 1;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("TickRate", tab2 == 2, sa[2])) tab2 = 2;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("Noclip", tab2 == 3, sa[3])) tab2 = 3;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("Ramp Fling", tab2 == 4, sa[4])) tab2 = 4;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("360 Spin", tab2 == 5, sa[5])) tab2 = 5;
            ImGui::SameLine(0, 6.0f * sc);
            if (UI::ContentSubtab("Extra", tab2 == 6, sa[6])) tab2 = 6;
            ImGui::Dummy(ImVec2(0, 8 * sc));
        }

if (tab2 == 0) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::CollapsibleSection("FLY", halfW))
{
UI::labelsection("MAIN");
UI::Checkbox("Enabled", &Options::Fly::Enabled);
}
UI::CollapsibleEnd();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
if (UI::CollapsibleSection("SETTINGS", cardW))
        {
            UI::labelsection("PARAMETERS");
            UI::SliderFloat("Fly Speed", &Options::Fly::Speed, 10.f, 200.f, "%.0f");

            UI::labelsection("KEYBIND");
            UI::Bind("##fly_key", &Options::Fly::FlyKey, &Options::Fly::ToggleType);
        }
        UI::CollapsibleEnd();
}
    else if (tab2 == 1) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::CollapsibleSection("WALKSPEED", halfW))
{
UI::labelsection("MAIN");
UI::Checkbox("Enabled", &Options::WalkSpeed::Enabled);
}
UI::CollapsibleEnd();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
if (UI::CollapsibleSection("SETTINGS", cardW))
{
UI::labelsection("PARAMETERS");
UI::SliderFloat("Walk Speed", &Options::WalkSpeed::Speed, 16.f, 1000.f, "%.0f");

UI::labelsection("KEYBIND");
UI::Bind("##walkspeed_key", &Options::WalkSpeed::WalkSpeedKey, &Options::WalkSpeed::ToggleType);
}
UI::CollapsibleEnd();
}
else if (tab2 == 2) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::CollapsibleSection("TICKRATE", halfW))
{
UI::labelsection("MAIN");
UI::Checkbox("Enabled", &Options::TickRate::Enabled);
}
UI::CollapsibleEnd();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
if (UI::CollapsibleSection("SETTINGS", cardW))
{
UI::labelsection("RATE");
UI::SliderFloat("Tick Rate", &Options::TickRate::Rate, 10.0f, 1000.0f, "%.0f");

UI::labelsection("PRESETS");
if (ImGui::Button("Default (60)", ImVec2(-1, 24))) Options::TickRate::Rate = 60.0f;
if (ImGui::Button("High (120)", ImVec2(-1, 24))) Options::TickRate::Rate = 120.0f;
if (ImGui::Button("Ultra (240)", ImVec2(-1, 24))) Options::TickRate::Rate = 240.0f;
if (ImGui::Button("Extreme (500)", ImVec2(-1, 24))) Options::TickRate::Rate = 500.0f;
if (ImGui::Button("Max (1000)", ImVec2(-1, 24))) Options::TickRate::Rate = 1000.0f;
}
UI::CollapsibleEnd();
}
else if (tab2 == 3) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::CollapsibleSection("NOCLIP", halfW))
{
UI::labelsection("MAIN");
UI::Checkbox("Enabled", &Options::Noclip::Enabled);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lets you walk through walls and solid objects.");
}
UI::CollapsibleEnd();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
if (UI::CollapsibleSection("SETTINGS", cardW))
{
UI::labelsection("TOGGLE");
static const char* noclipModes[]{ "Hold", "Toggle", "Always On" };
UI::Combo("Mode##noclip", &Options::Noclip::ToggleType, noclipModes, IM_ARRAYSIZE(noclipModes));
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hold = while key held, Toggle = press once, Always On = always noclip.");

if (Options::Noclip::ToggleType != 2)
UI::Bind("##noclip_key", &Options::Noclip::NoclipKey, &Options::Noclip::ToggleType);
}
UI::CollapsibleEnd();
}
else if (tab2 == 4) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::CollapsibleSection("RAMP FLING", halfW))
{
UI::labelsection("MAIN");
UI::Checkbox("Enabled", &Options::RampFling::Enabled);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Automatically fling when hitting ramps.");

static const char* rampModes[]{ "Hold", "Toggle", "Always On" };
UI::Combo("Mode##ramp", &Options::RampFling::ToggleType, rampModes, IM_ARRAYSIZE(rampModes));

if (Options::RampFling::ToggleType != 2)
UI::Bind("##ramp_key", &Options::RampFling::FlingKey, &Options::RampFling::ToggleType);
}
UI::CollapsibleEnd();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
if (UI::CollapsibleSection("SETTINGS", cardW))
{
UI::labelsection("PARAMETERS");
UI::SliderFloat("Fling Force", &Options::RampFling::FlingForce, 10.f, 300.f, "%.0f");
UI::SliderFloat("Min Angle", &Options::RampFling::MinAngle, 5.f, 45.f, "%.0f");
UI::SliderFloat("Max Angle", &Options::RampFling::MaxAngle, 30.f, 90.f, "%.0f");
UI::SliderFloat("Cooldown", &Options::RampFling::Cooldown, 0.1f, 2.f, "%.1fs");
UI::SliderFloat("H. Boost", &Options::RampFling::HorizontalBoost, 0.f, 2.f, "%.1f");
}
UI::CollapsibleEnd();
}
else if (tab2 == 5) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::CollapsibleSection("360 SPIN", halfW))
{
UI::labelsection("MAIN");
                UI::Checkbox("Enable 360 Spin", &Options::Spin360::Enabled);
UI::Tooltip("Spins your camera in a full 360 circle while the key is held.");
}
UI::CollapsibleEnd();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
if (UI::CollapsibleSection("SETTINGS", cardW))
{
UI::labelsection("CONTROLS");
UI::SliderFloat("Spin Speed", &Options::Spin360::Speed, 1.0f, 45.0f, "%.1f deg/tick");
UI::Bind("##spin360_key", &Options::Spin360::HotKey);
UI::Tooltip("Hold this key to continuously spin your camera.");
}
UI::CollapsibleEnd();
}
else if (tab2 == 6) {
const float panelY = ImGui::GetCursorPosY();
ImGui::SetCursorPosX(ctX);
if (UI::CollapsibleSection("CLICK TP", halfW))
{
UI::labelsection("MAIN");
UI::Checkbox("Enabled", &Options::ClickTP::Enabled);
                UI::Bind("##clicktp_key", &Options::ClickTP::Key);
                UI::Tooltip("Teleports you to the point under the cursor when the key is pressed.");
                UI::SliderFloat("Max Distance", &Options::ClickTP::MaxDistance, 50.0f, 5000.0f, "%.0f");
}
UI::CollapsibleEnd();

ImGui::SetCursorPosY(panelY);
ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
if (UI::CollapsibleSection("HIP HEIGHT", cardW))
{
UI::labelsection("MAIN");
UI::Checkbox("Enabled##hipheight", &Options::HipHeight::Enabled);
UI::Bind("##hipheight_key", &Options::HipHeight::Key);
UI::SliderFloat("Height", &Options::HipHeight::Value, 0.0f, 20.0f, "%.1f");
}
UI::CollapsibleEnd();

ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f * sc);
ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
if (UI::CollapsibleSection("FREE CAM", cardW))
{
UI::labelsection("MAIN");
UI::Checkbox("Enabled##freecam", &Options::FreeCam::Enabled);
UI::Bind("##freecam_key", &Options::FreeCam::Key);
UI::SliderFloat("Speed", &Options::FreeCam::Speed, 10.0f, 200.0f, "%.0f");
}
UI::CollapsibleEnd();

ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f * sc);
ImGui::SetCursorPosX(ctX + halfW + UI::ColGap);
if (UI::CollapsibleSection("STRETCH RES", cardW))
{
UI::labelsection("MAIN");
UI::Checkbox("Enabled##stretchres", &Options::StretchRes::Enabled);
UI::SliderFloat("Scale X", &Options::StretchRes::ScaleX, 0.5f, 2.0f, "%.2f");
UI::SliderFloat("Scale Y", &Options::StretchRes::ScaleY, 0.5f, 2.0f, "%.2f");
}
UI::CollapsibleEnd();
}
}
else if (tab == 5)
{
RenderConfigTab(ctX, fullW);
}
else if (tab == 6)
{
UI::ContentHeader("GAME");
ImGui::SetCursorPosX(ctX);
if (UI::CollapsibleSection("GAME DETECTION", fullW))
{
UI::labelsection("DETECTED GAME");
ImGui::TextDisabled("Name:");
ImGui::SameLine();
ImGui::Text("%s", Globals::Roblox::gameName.c_str());

char pid[32];
snprintf(pid, sizeof(pid), "%d", Globals::Roblox::lastPlaceID);
ImGui::TextDisabled("Place ID:");
ImGui::SameLine();
ImGui::Text("%s", pid);

UI::labelsection("CLIENT VERSION");
{
    const std::string& cliVer = RobloxVersion::GetClientVersion();
    const int cliStatus = RobloxVersion::GetStatus();
    ImGui::TextDisabled("Installed:");
    ImGui::SameLine();
    if (!cliVer.empty())
    {
        ImVec4 col = (cliStatus == RobloxVersion::Matched)
            ? ImVec4(0.3f, 1.0f, 0.4f, 1.0f)
            : (cliStatus == RobloxVersion::Mismatch
                ? ImVec4(1.0f, 0.65f, 0.2f, 1.0f)
                : ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        ImGui::TextColored(col, "%s", cliVer.c_str());
        if (ImGui::IsItemHovered())
        {
            if (cliStatus == RobloxVersion::Matched)
                ImGui::SetTooltip("Running version matches bundled offsets.");
            else if (cliStatus == RobloxVersion::Mismatch)
                ImGui::SetTooltip("Running version differs from bundled offsets (%s). Updates may be required.",
                    Offsets::ClientVersion.c_str());
        }
    }
    else
    {
        ImGui::TextDisabled("unknown");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Could not determine the installed Roblox client version.");
    }
    ImGui::TextDisabled("Offsets for:");
    ImGui::SameLine();
    ImGui::Text("%s", Offsets::ClientVersion.c_str());
}

UI::labelsection("SUPPORTED OPTIMIZATIONS");

auto gameFlag = [&](const char* label, bool on)
{
ImGui::Bullet();
ImGui::Text("%s", label);
ImGui::SameLine();
ImGui::TextColored(on ? ImVec4(0.3f, 1.0f, 0.4f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
on ? "[active]" : "[generic]");
};

gameFlag("Phantom Forces (camera-rotation silent aim)", Globals::Roblox::isPhantomForces);
gameFlag("Rivals (smoke/flash bypass)", Globals::Roblox::isRivals);
gameFlag("Overkill / Chickynoid (cursor-snap silent aim)", Globals::Roblox::isOverkill);
                gameFlag("Generic Roblox (viewport / camera aim)", !Globals::Roblox::isPhantomForces && !Globals::Roblox::isRivals && !Globals::Roblox::isOverkill);

UI::labelsection("ARSENAL GUNMODS");
UI::Checkbox("No Recoil", &Options::ArsenalGunmods::NoRecoil);
UI::Checkbox("Fast Fire Rate", &Options::ArsenalGunmods::FastFireRate);
UI::Checkbox("All Auto", &Options::ArsenalGunmods::AllAuto);
UI::Checkbox("Infinite Ammo", &Options::ArsenalGunmods::InfiniteAmmo);
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shows the current curse name on the HUD. Enabled automatically with Infinite Ammo.");
}
UI::CollapsibleEnd();

UI::labelsection("DIAGNOSTICS");
if (UI::CollapsibleSection("GAME DIAGNOSTICS", fullW, true))
{
    ImGui::Text("Game compatibility diagnostics");
    ImGui::Separator();

    if (ImGui::Button("Run Full Diagnostics"))
    {
        Diagnostics::DumpAssetIdsManual();
        Diagnostics::DumpEnvironmentManual();
    }

    ImGui::SameLine();
    if (ImGui::Button("Dump Asset IDs"))
        Diagnostics::DumpAssetIdsManual();

    ImGui::SameLine();
    if (ImGui::Button("Dump Environment"))
        Diagnostics::DumpEnvironmentManual();

    ImGui::Separator();
    ImGui::Text("Auto-diagnostics runs in background on game join.");
    ImGui::Text("Files include PlaceId in name (e.g. seraph_assetids_123456.txt)");
    ImGui::BulletText("seraph_assetids_<PlaceId>.txt");
    ImGui::BulletText("seraph_env_<PlaceId>.txt");
}
UI::CollapsibleEnd();

}
else if (tab == 7)
{
    // ---- EXECUTOR TAB ----
    // Wrapper window owns a scrollbar and lets mouse wheel fall through to it
    // (the child cards below are NoScrollWithMouse for the editor's wheel use).
    // This guarantees the whole tab is scrollable at any font scale.
    ImGui::SetCursorPosX(ctX);
    ImGui::BeginChild("##executor_tab", ImVec2(fullW, s.y - 58.0f * sc - 8.0f * sc), false);
    {
        UI::ContentHeader("EXECUTOR");
        if (UI::CollapsibleSection("SCRIPT", fullW - 16.0f * sc))
        {
            const float innerW = (fullW - 16.0f * sc) - 2.0f * 14.0f * sc; // card WindowPadding is 14
            ImGui::SetCursorPos(ImVec2(6.0f * sc, ImGui::GetCursorPosY()));
            g_executorEditor.render("##executor_script", ImVec2(innerW - 20.0f * sc, 150.0f * sc));

            ImGui::SetCursorPos(ImVec2(6.0f * sc, ImGui::GetCursorPosY()));
            if (UI::Button("EXECUTE", ImVec2(120.0f * sc, 0.0f)))
            {
                Executor::ConsolePush("[executor] EXECUTE clicked, running script...");
                Executor::ClearConsole();
                Executor::Run(g_executorEditor.get_text());
            }
            ImGui::SameLine();
            if (UI::Button("STOP", ImVec2(90.0f * sc, 0.0f)))
                Executor::Stop();
            ImGui::SameLine();
            if (UI::Button("CLEAR", ImVec2(90.0f * sc, 0.0f)))
                Executor::ClearConsole();
            ImGui::SameLine();
            static bool s_injected = false;
            if (UI::Button(s_injected ? "EJECT" : "INJECT DLL", ImVec2(110.0f * sc, 0.0f)))
            {
                if (!s_injected)
                {
                    Injector::SetLogCallback([](const std::string& msg) { Executor::ConsolePush("[injector] " + msg); });
                    
                    Executor::ConsolePush("[injector] Button clicked, starting injection...");
                    try
                    {
                        Executor::ConsolePush("[injector] Step 1: Getting module path...");
                        wchar_t exePath[MAX_PATH];
                        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
                        std::wstring dllPath = std::wstring(exePath);
                        size_t pos = dllPath.find_last_of(L'\\');
                        if (pos != std::wstring::npos)
                            dllPath = dllPath.substr(0, pos + 1) + L"SeraphExecutorDLL.dll";
                        
                        Executor::ConsolePush("[injector] Step 2: Loading DLL from " + std::string(dllPath.begin(), dllPath.end()));
                        auto dllBytes = Injector::LoadDllFromDisk(dllPath.c_str());
                        if (!dllBytes.empty())
                        {
                            Executor::ConsolePush("[injector] Step 3: DLL loaded (" + std::to_string(dllBytes.size()) + " bytes), finding Roblox...");
                            int pid = Injector::FindRobloxPID();
                            if (pid == 0)
                            {
                                Executor::ConsolePush("[injector] Failed: RobloxPlayerBeta.exe not running");
                            }
                            else
                            {
                                Executor::ConsolePush("[injector] Step 4: Roblox found (pid " + std::to_string(pid) + "), injecting...");
                                auto result = Injector::InjectDLLByName(L"RobloxPlayerBeta.exe", dllBytes);
                                if (result.success)
                                {
                                    s_injected = true;
                                    Executor::ConsolePush("[injector] Success: DLL injected at 0x" + std::to_string(result.dllBase) + " (pid " + std::to_string(result.pid) + ")");
                                }
                                else
                                {
                                    Executor::ConsolePush("[injector] Failed: " + result.error);
                                }
                            }
                        }
                        else
                        {
                            Executor::ConsolePush("[injector] Failed: SeraphExecutorDLL.dll not found at " + std::string(dllPath.begin(), dllPath.end()));
                        }
                    }
                    catch (const std::exception& e)
                    {
                        Executor::ConsolePush("[injector] C++ Exception: " + std::string(e.what()));
                    }
                    catch (...)
                    {
                        Executor::ConsolePush("[injector] Unknown crash (SEH/access violation)");
                    }
                }
                else
                {
                    Executor::ConsolePush("[injector] Eject not implemented");
                }
            }
            ImGui::SameLine();
            ImGui::TextColored(s_injected ? ImVec4(0.3f, 1.0f, 0.4f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                s_injected ? "[INJECTED]" : "[NOT INJECTED]");
        }
        UI::CollapsibleEnd();

        if (UI::CollapsibleSection("CONSOLE", fullW - 16.0f * sc))
        {
            const float innerW = (fullW - 16.0f * sc) - 2.0f * 14.0f * sc;
            ImGui::SetCursorPos(ImVec2(6.0f * sc, ImGui::GetCursorPosY()));
            if (ImGui::BeginChild("##executor_console", ImVec2(innerW - 20.0f * sc, 170.0f * sc), true))
            {
                const auto lines = Executor::ConsoleSnapshot();
                static size_t lastConsoleCount = 0;
                const bool grew = lines.size() > lastConsoleCount;
                lastConsoleCount = lines.size();
                ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x - 8.0f * sc);
                for (const auto& l : lines)
                    ImGui::TextUnformatted(l.c_str());
                ImGui::PopTextWrapPos();
                if (grew)
                    ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();
        }
        UI::CollapsibleEnd();
    }
    ImGui::EndChild();
}
    else if (tab == 8)
    {
        // ---- SPOTIFY VISUALIZER TAB ----
        ImGui::SetCursorPosX(ctX);
        ImGui::BeginChild("##spotify_tab", ImVec2(fullW, s.y - 58.0f * sc - 8.0f * sc), false);
        {
            UI::ContentHeader("SPOTIFY VISUALIZER");
            
            // Main visualizer area
            float visHeight = std::max(0.0f, (s.y - 58.0f * sc - 8.0f * sc) - 140.0f * sc);
            ImVec2 visPos = ImGui::GetCursorScreenPos();
            ImVec2 visSize(fullW, visHeight);
            
            // Render the Spotify visualizer
            SpotifyVisualizer::Render(ImGui::GetWindowDrawList(), visPos, visSize, ImGui::GetIO().DeltaTime);
            
            // Reserve space for the visualizer
            ImGui::Dummy(visSize);
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            UI::ContentHeader("MINIPLAYER");
            UI::Checkbox("Enable Miniplayer", &Options::Music::Miniplayer);
            UI::Tooltip("Compact floating player over the game: album art background,\nprev / play / next, seek bar, title and artist.\nDrag it while the menu is open to reposition it;\nin-game, hover it to use the controls.");
            if (Options::Music::Miniplayer)
            {
                const ImVec2 disp = ImGui::GetIO().DisplaySize;
                UI::SliderFloat("Position X", &Options::Music::MiniplayerX, 0.0f, disp.x, "%.0f");
                UI::SliderFloat("Position Y", &Options::Music::MiniplayerY, 0.0f, disp.y, "%.0f");
                UI::SliderFloat("Size", &Options::Music::MiniplayerSize, 0.7f, 1.6f, "%.2fx");
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            
            // Media keybinds
            UI::ContentHeader("MEDIA KEYBINDS");
            UI::Bind("Play / Pause", &Options::Misc::MediaPlayPauseKey, &Options::Misc::MediaToggleType);
            ImGui::SameLine(0, 16.0f * sc);
            UI::Bind("Next Track", &Options::Misc::MediaNextKey, &Options::Misc::MediaToggleType);
            ImGui::Spacing();
            UI::Bind("Previous Track", &Options::Misc::MediaPrevKey, &Options::Misc::MediaToggleType);
            ImGui::SameLine(0, 16.0f * sc);
            UI::Bind("Stop", &Options::Misc::MediaStopKey, &Options::Misc::MediaToggleType);
            
            ImGui::Spacing();
            UI::Tooltip("Global keybinds work system-wide via Windows Media Session API.\nDefaults: Play/Pause = Media Play/Pause key, Next = Media Next, Prev = Media Prev, Stop = Media Stop.");
        }
        ImGui::EndChild();
    }
    ImGui::EndChild(); // ##content_area
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();

ImGui::PopFont();
}
ImGui::PopStyleVar();
ImGui::End();
}

// Global click feedback: catch standard ImGui widgets (raw ImGui::Button,
// Selectable, InputText, etc.) that don't go through the UI:: helpers. A new
// ActiveId appearing on a mouse-down frame means a click landed on a widget.
// The UI:: custom widgets manage their own state so this only fills the gaps;
// ClickFx's debounce collapses any double-fire (e.g. CollapsibleSection).
if (menu_open && ImGui::IsMouseClicked(0))
{
    if (ImGuiContext* g = ImGui::GetCurrentContext())
    {
        if (g->ActiveId != 0 && g->ActiveId != g->ActiveIdPreviousFrame)
            UI::ClickFx();
    }
}

// Visual click effect, drawn above the menu.
if (menu_open || menuAlpha > 0.0f)
    MenuClickFx::Render(ImGui::GetForegroundDrawList());

// ESP Preview overlay (positioned to the right of the menu, clamped to screen)
if (tab == 1 && tab2 == 0 && Options::ESP::Enabled && menuAlpha > 0.0f && menuPos.x >= 0)
{
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 display = ImGui::GetIO().DisplaySize;
    float previewW = 320.0f * UI::sc;
    float previewH = 470.0f * UI::sc;
    float menuWidth = 997.0f * UI::sc;
    float idealX = menuPos.x + menuWidth + 12.0f;
    float maxX = display.x - previewW - 8.0f;
    float px = (idealX > maxX) ? maxX : idealX;
    if (px < 8.0f) px = 8.0f;
    const ImVec2 previewPos = ImVec2(px, menuPos.y + 16 + 52 * UI::sc);
    const ImVec2 previewSize = ImVec2(previewW, previewH);
    RenderESPPreview(dl, previewPos, previewSize);
}

// ---- MINIPLAYER: floating now-playing card, drawn above everything ----
if (Options::Music::Miniplayer)
    SpotifyVisualizer::RenderMiniplayer(ImGui::GetForegroundDrawList(), menu_open);
UpdateMiniplayerClickThrough(hwnd, menu_open);

// Published unconditionally. It used to sit inside the IsGameOnTop("Roblox")
    // block below, which meant the flag was never updated on the one frame that
    // mattered: opening the menu takes focus away from Roblox, so IsGameOnTop
    // went false, the write was skipped, and the delivery worker kept driving
    // the camera while the user was in the menu. That is what turned a single
    // toggle into twenty clicks - the game window was being fought underneath
    // the UI the whole time.
    SilentAim::s_MenuVisible = menu_open;

    FovFillWindow::Active = false;
    FovFillWindow::ExcludeFromCapture = Options::Misc::StreamProof;
    FovFillWindow::OverlayHwnd = hwnd;

    BoxFillWindow::Active = true;
    BoxFillWindow::ExcludeFromCapture = Options::Misc::StreamProof;
    BoxFillWindow::OverlayHwnd = hwnd;
    BoxFillWindow::Begin();

    if (IsGameOnTop("Roblox"))
    {
        AntiKatanaFiringBlocked();
        CombatFeedback::Update();
        if (!menu_open)
        {
            RunAimCore(ImGui::GetBackgroundDrawList());
		RunMacro();
	}
	RunTriggerbot();
	// FOV drawing is handled unconditionally by RenderAdvancedFOV below. This
	// used to call RunAimCore() again under ShowFOV, which applied the aim
	// twice per frame and advanced sticky/toggle/switch-delay state twice.
	RenderAdvancedFOV(ImGui::GetBackgroundDrawList());
	RenderCrosshair(ImGui::GetBackgroundDrawList());
	if (Options::Rivals::KatanaAlert && AnyRivalsKatanaUser())
		RenderKatanaAlert(ImGui::GetBackgroundDrawList());
	CombatFeedback::Render(ImGui::GetBackgroundDrawList());
	RenderESP(ImGui::GetBackgroundDrawList());

	// Mode Changer sits on top of the world pass: it overlays a model on the local
	// player's character and is independent of whether any ESP feature is enabled.
	ModelChanger::RenderModeChanger(ImGui::GetBackgroundDrawList());

	// Precipitation draws after the world/player pass so droplets and flakes
	// composite over the scene rather than being hidden behind ESP geometry.
	if ( Options::World::Weather::Enabled )
		WorldWeather::Render( ImGui::GetBackgroundDrawList() );

	if (Options::ESP::LodLine || (Options::ESP::AimView && !Options::ESP::AimViewTarget.empty()))
		LodLineVisual::Render(ImGui::GetBackgroundDrawList());

	if (Options::ESP::Arrows) RenderArrows(ImGui::GetBackgroundDrawList());
	if (Options::ESP::Radar) RenderRadar(ImGui::GetBackgroundDrawList());
	
	if (Options::Desync::Enabled && Options::Desync::ShowVisual)
		DesyncVisual::RenderDesyncVisual(ImGui::GetBackgroundDrawList());
	
	RenderRageGhost(ImGui::GetBackgroundDrawList());
	
	if (Options::Orbit::Enabled && Options::Orbit::ShowRadius)
		RenderOrbitRadiusVisual(ImGui::GetBackgroundDrawList());

if (menu_open && MenuWeather::Enabled)
{
const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
MenuWeather::Render(ImGui::GetBackgroundDrawList(), ImVec2(0.0f, 0.0f), displaySize);
}

RenderKeybindList(ImGui::GetBackgroundDrawList());
}

FovFillWindow::Present();
BoxFillWindow::Present();

if (Options::Misc::ExplorerEnabled)
    gui::render_explorer_window(&Options::Misc::ExplorerEnabled);

if (Options::Misc::PlayerListEnabled)
    RenderPlayerListWindow(&Options::Misc::PlayerListEnabled);

if (Options::Misc::ServerBrowserEnabled)
    Cheat::Features::ServerBrowser::RenderWindow(&Options::Misc::ServerBrowserEnabled);

// Render draw.* items produced by running scripts (before the final commit).
Executor::RenderOverlay(ImGui::GetBackgroundDrawList());

// ── Idle present gate ────────────────────────────────────────────────────
// The overlay is a fullscreen WS_EX_LAYERED window using LWA_COLORKEY, so
// every Present makes the desktop compositor re-blend the entire screen-sized
// surface against the game window underneath, with a per-pixel color-key test.
// That cost is paid by DWM, not by this process, which is why the frame loss
// never showed up in either the read counters or Seraph's own CPU time.
//
// When nothing is drawn there is nothing to present, and pushing 60 identical
// frames a second at the compositor is pure waste over the game. The draw
// data itself is the authority on that: ImGui reports the total vertex count
// for the frame, so an empty frame is detected exactly, with no list of
// features to keep in sync. Any drawn element (menu, ESP, crosshair, a script
// draw call) produces vertices and restores full-rate presenting immediately.
//
// The count only becomes valid once ImGui::Render() has finalised the frame,
// so the present decision is made after it.
static uint64_t s_idleFrames = 0;

ImGui::Render();
ImDrawData* drawData = ImGui::GetDrawData();
const bool haveContent = drawData && drawData->TotalVtxCount > 0;

// Present at ~12hz while genuinely empty instead of 60hz.
const bool skipThisPresent = !haveContent && (++s_idleFrames % 5) != 0;

if (!skipThisPresent)
{
const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
ImGui_ImplDX11_RenderDrawData(drawData);

HRESULT hr = g_pSwapChain->Present(0, 0);
g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
}

// Cap the overlay at 60fps. ESP draws straight from kernel reads every frame
// with no internal throttle, so running the loop at 240 did all of that work
// (and every ImGui pass) 4x more often than the 60hz ESP people actually see.
// This keeps ESP at exactly 60hz while returning the CPU/IO the game would
// otherwise lose to the overlay. Raise this only if you need menu smoothness
// on a high-refresh display.
const double frameBudget = 1.0 / 60.0;
for (;;)
{
    LARGE_INTEGER now;
    ::QueryPerformanceCounter(&now);
    const double elapsed = (double)(now.QuadPart - frameStart.QuadPart) / (double)qpcFreq.QuadPart;
    const double remaining = frameBudget - elapsed;
    if (remaining <= 0.0)
        break;
    // Sleep down to ~0.8ms of slack instead of spinning through the last
    // 1.5ms of every frame; the spin was burning ~1/3 of a core at 240fps.
    if (remaining >= 0.0008)
        ::Sleep((DWORD)((remaining - 0.0002) * 1000.0));
    else
        ::Sleep(0);
}
}

::timeEndPeriod(1);

SpotifyVisualizer::Shutdown();

ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g_OverlayWheelAccum = 0;

    UninstallWheelForwarder();

    gui::explorer_shutdown();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    CoUninitialize();

    Globals::overlayDone = true;
    SeraphLog("[S] ShowImgui: overlay teardown complete, overlayDone=true");
}

bool CreateDeviceD3D(HWND hWnd)
{
// Opaque bitblt-model swapchain. This system cannot create
// DXGI_ALPHA_MODE_PREMULTIPLIED flip-model swapchains (every SwapEffect /
// BufferCount / layered-style permutation returns DXGI_ERROR_INVALID_CALL),
// so transparency is done with an opaque bitblt surface + LWA_COLORKEY
// (pure black -> see-through), matching the loader's proven opaque recipe.
DXGI_SWAP_CHAIN_DESC1 sd = {};
sd.Width = 0;
sd.Height = 0;
sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
sd.Stereo = FALSE;
sd.SampleDesc.Count = 1;
sd.SampleDesc.Quality = 0;
sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
sd.BufferCount = 1;
sd.Scaling = DXGI_SCALING_STRETCH;
sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
sd.Flags = 0;

D3D_FEATURE_LEVEL featureLevel;
const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
HRESULT res = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, featureLevelArray, 2, D3D11_SDK_VERSION,
    &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
if (res != S_OK) return false;

IDXGIDevice* dxgiDevice = nullptr;
IDXGIAdapter* adapter = nullptr;
IDXGIFactory2* factory = nullptr;
res = g_pd3dDevice->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
if (res == S_OK) res = dxgiDevice->GetAdapter(&adapter);
if (res == S_OK) res = adapter->GetParent(IID_PPV_ARGS(&factory));
if (res != S_OK)
{
    if (dxgiDevice) dxgiDevice->Release();
    if (adapter) adapter->Release();
    if (factory) factory->Release();
    return false;
}

IDXGISwapChain1* baseSwapChain = nullptr;
res = factory->CreateSwapChainForHwnd(g_pd3dDevice, hWnd, &sd, nullptr, nullptr, &baseSwapChain);
if (dxgiDevice) dxgiDevice->Release();
if (adapter) adapter->Release();
factory->Release();
if (res != S_OK || !baseSwapChain) return false;

res = baseSwapChain->QueryInterface(IID_PPV_ARGS(&g_pSwapChain));
baseSwapChain->Release();
if (res != S_OK) return false;

CreateRenderTarget();
return true;
}

void CleanupDeviceD3D()
{
Executor::Shutdown();
CleanupRenderTarget();
if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
ID3D11Texture2D* pBackBuffer;
g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
pBackBuffer->Release();
}

void CleanupRenderTarget()
{
if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
return true;

switch (msg)
{
case WM_SIZE:
if (wParam == SIZE_MINIMIZED) return 0;
g_ResizeWidth = (UINT)LOWORD(lParam);
g_ResizeHeight = (UINT)HIWORD(lParam);
return 0;
case WM_SYSCOMMAND:
if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
break;
case WM_DESTROY:
::PostQuitMessage(0);
return 0;
}
return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
