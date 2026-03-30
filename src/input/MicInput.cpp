#include "MicInput.hpp"

#ifdef MIC_ENABLED

#include <driver/i2s_std.h>
#include <WebSocketsServer.h>
#include <Logger.h>

static const char* TAG = "Mic";

#ifndef MIC_SD
#define MIC_SD 43
#endif
#ifndef MIC_SCK
#define MIC_SCK 6
#endif
#ifndef MIC_WS
#define MIC_WS 5
#endif

namespace {

static constexpr int SAMPLE_RATE = 16000;
static constexpr int DMA_BUF_COUNT = 16;
static constexpr int DMA_BUF_LEN = 256;
// I2S reads stereo frames (L+R interleaved), so we need 2x for N mono samples
static constexpr int MONO_SAMPLES = 512;
static constexpr int STEREO_SAMPLES = MONO_SAMPLES * 2;  // L,R,L,R,...

// Output smoothing: fast attack, slow decay
static constexpr float ATTACK = 0.4f;
static constexpr float DECAY = 0.05f;

// History ring buffer for percentile-based auto-gain
// ~125 RMS readings/sec at 16kHz/128 samples, so 128 entries ≈ 1 second window
static constexpr size_t HISTORY_SIZE = 128;
static constexpr float FLOOR_PERCENTILE = 0.10f;  // 10th percentile = noise floor
static constexpr float CEIL_PERCENTILE  = 0.90f;  // 90th percentile = peak ceiling
static constexpr float MIN_RANGE = 30.0f;          // minimum range to avoid div-by-tiny

static float rmsHistory[HISTORY_SIZE] = {};
static size_t historyIdx = 0;
static size_t historyCount = 0;

static float smoothedVolume = 0.0f;
static i2s_chan_handle_t rx_chan = nullptr;

// Insertion sort into a temp buffer — cheap at 128 elements
static float sortBuf[HISTORY_SIZE];

// Debug audio stream WebSocket on port 82
static WebSocketsServer* debugWs = nullptr;
static bool hasDebugClient = false;

// Raw stereo read buffer (32-bit per sample for INMP441) and extracted mono output
static int32_t stereoBuf[STEREO_SAMPLES];
static int16_t lastSamples[MONO_SAMPLES];  // left channel, downshifted to 16-bit
static size_t lastMonoCount = 0;

static void getPercentiles(float& floor, float& ceil) {
    size_t n = historyCount;
    if (n == 0) { floor = 0; ceil = MIN_RANGE; return; }

    memcpy(sortBuf, rmsHistory, n * sizeof(float));

    // Simple insertion sort, 128 floats is ~8K comparisons worst case
    for (size_t i = 1; i < n; i++) {
        float key = sortBuf[i];
        size_t j = i;
        while (j > 0 && sortBuf[j - 1] > key) {
            sortBuf[j] = sortBuf[j - 1];
            j--;
        }
        sortBuf[j] = key;
    }

    floor = sortBuf[(size_t)(FLOOR_PERCENTILE * (n - 1))];
    ceil  = sortBuf[(size_t)(CEIL_PERCENTILE * (n - 1))];
}

}  // namespace

namespace MicInput {

void init() {
    // Channel config
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = DMA_BUF_COUNT;
    chan_cfg.dma_frame_num = DMA_BUF_LEN;

    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &rx_chan);
    if (err != ESP_OK) {
        LOG_INFO(TAG, "I2S new channel failed: %d", err);
        return;
    }

    // Standard mode config for INMP441
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        // INMP441 outputs 24-bit data in 32-bit slots; use 32-bit width, stereo mode
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)MIC_SCK,
            .ws = (gpio_num_t)MIC_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = (gpio_num_t)MIC_SD,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(rx_chan, &std_cfg);
    if (err != ESP_OK) {
        LOG_INFO(TAG, "I2S std mode init failed: %d", err);
        i2s_del_channel(rx_chan);
        rx_chan = nullptr;
        return;
    }

    err = i2s_channel_enable(rx_chan);
    if (err != ESP_OK) {
        LOG_INFO(TAG, "I2S channel enable failed: %d", err);
        i2s_del_channel(rx_chan);
        rx_chan = nullptr;
        return;
    }

    LOG_INFO(TAG, "I2S mic initialized (SD=%d, SCK=%d, WS=%d)", MIC_SD, MIC_SCK, MIC_WS);
}

void loop() {
    if (!rx_chan) return;

    lastMonoCount = 0;
    size_t bytesRead = 0;
    esp_err_t err = i2s_channel_read(rx_chan, stereoBuf, sizeof(stereoBuf), &bytesRead, 5);
    if (bytesRead == 0) return;

    // Extract left channel from interleaved 32-bit stereo (L32,R32,L32,R32,...)
    // INMP441 outputs 24-bit data left-justified in 32-bit slot: [D23..D0, 0, 0, 0, 0, 0, 0, 0, 0]
    // Shift right by 16 to fit into int16_t (keeps top 16 of 24 data bits)
    size_t stereoCount = bytesRead / sizeof(int32_t);
    size_t monoCount = stereoCount / 2;
    for (size_t i = 0; i < monoCount; i++) {
        lastSamples[i] = (int16_t)(stereoBuf[i * 2] >> 16);
    }
    lastMonoCount = monoCount;

    // Compute RMS on mono samples
    float sumSq = 0.0f;
    for (size_t i = 0; i < monoCount; i++) {
        float s = static_cast<float>(lastSamples[i]);
        sumSq += s * s;
    }
    float rms = sqrtf(sumSq / monoCount);

    // Push into history ring buffer
    rmsHistory[historyIdx] = rms;
    historyIdx = (historyIdx + 1) % HISTORY_SIZE;
    if (historyCount < HISTORY_SIZE) historyCount++;

    // Get percentile-based floor and ceiling
    float floor, ceil;
    getPercentiles(floor, ceil);

    // Normalize: subtract floor, divide by dynamic range
    float range = fmaxf(ceil - floor, MIN_RANGE);
    float raw = fmaxf(rms - floor, 0.0f) / range;
    raw = fminf(raw, 1.0f);

    // Exponential smoothing: fast attack, slow decay
    float alpha = (raw > smoothedVolume) ? ATTACK : DECAY;
    smoothedVolume += alpha * (raw - smoothedVolume);
}

float getVolume() {
    return smoothedVolume;
}

void initDebugStream() {
    debugWs = new WebSocketsServer(82);
    debugWs->begin();
    debugWs->onEvent([](uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
        if (type == WStype_CONNECTED) {
            hasDebugClient = true;
            // Send sample rate as text so the browser knows
            String sr = String(SAMPLE_RATE);
            debugWs->sendTXT(num, sr);
            LOG_INFO(TAG, "Debug stream client %d connected", num);
        } else if (type == WStype_DISCONNECTED) {
            // Check if any clients remain
            hasDebugClient = false;
            for (uint8_t i = 0; i < 4; i++) {
                if (debugWs->clientIsConnected(i)) { hasDebugClient = true; break; }
            }
            LOG_INFO(TAG, "Debug stream client %d disconnected", num);
        }
    });
    LOG_INFO(TAG, "Debug audio stream on port 82");
}

void loopDebugStream() {
    if (!debugWs) return;
    debugWs->loop();

    if (!hasDebugClient || lastMonoCount == 0) return;

    debugWs->broadcastBIN((uint8_t*)lastSamples, lastMonoCount * sizeof(int16_t));
}

}  // namespace MicInput

#endif // MIC_ENABLED
