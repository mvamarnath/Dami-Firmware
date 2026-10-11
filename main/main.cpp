#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>

// Verified XH-S3E-AI-V1.0 Pinout
#define I2S_MIC_WS 4
#define I2S_MIC_SCK 5
#define I2S_MIC_SD 6

#define I2S_SPK_WS 16
#define I2S_SPK_SCK 15
#define I2S_SPK_SD 7

#define SAMPLE_RATE 16000
#define MIC_GAIN                                                               \
  8.0f // Multiplier to make your voice louder through the speaker

static const char *TAG = "XH_LOOPBACK";

i2s_chan_handle_t rx_handle = NULL;
i2s_chan_handle_t tx_handle = NULL;

// ==========================================
// 1. I2S Hardware Initialization
// ==========================================
void init_i2s(void) {
  // ---- MICROPHONE (RX) ----
  i2s_chan_config_t rx_chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  ESP_ERROR_CHECK(i2s_new_channel(&rx_chan_cfg, NULL, &rx_handle));

  i2s_std_config_t rx_std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                      I2S_SLOT_MODE_MONO),
      .gpio_cfg =
          {
              .mclk = I2S_GPIO_UNUSED,
              .bclk = (gpio_num_t)I2S_MIC_SCK,
              .ws = (gpio_num_t)I2S_MIC_WS,
              .dout = I2S_GPIO_UNUSED,
              .din = (gpio_num_t)I2S_MIC_SD,
              .invert_flags = {.mclk_inv = false,
                               .bclk_inv = false,
                               .ws_inv = false},
          },
  };
  rx_std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &rx_std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));

  // ---- SPEAKER (TX) ----
  i2s_chan_config_t tx_chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  tx_chan_cfg.auto_clear = true;
  ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, &tx_handle, NULL));

  i2s_std_config_t tx_std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                      I2S_SLOT_MODE_MONO),
      .gpio_cfg =
          {
              .mclk = I2S_GPIO_UNUSED,
              .bclk = (gpio_num_t)I2S_SPK_SCK,
              .ws = (gpio_num_t)I2S_SPK_WS,
              .dout = (gpio_num_t)I2S_SPK_SD,
              .din = I2S_GPIO_UNUSED,
              .invert_flags = {.mclk_inv = false,
                               .bclk_inv = false,
                               .ws_inv = false},
          },
  };
  tx_std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &tx_std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
}

// ==========================================
// 2. The Real-Time Loopback Task
// ==========================================
void loopback_task(void *arg) {
  size_t bytes_read;
  size_t bytes_written;

  // Read 32-bit data from Mic, Write 16-bit data to Speaker
  int32_t mic_buffer[256];
  int16_t spk_buffer[256];

  ESP_LOGI(TAG, "🎤 Loopback Started! Speak into the microphone.");

  for (;;) {
    // 1. Fetch live audio from the microphone
    if (i2s_channel_read(rx_handle, mic_buffer, sizeof(mic_buffer), &bytes_read,
                         portMAX_DELAY) == ESP_OK) {
      int samples_read = bytes_read / sizeof(int32_t);

      for (int i = 0; i < samples_read; i++) {
        // Shift bits to extract PCM, then apply digital gain
        int32_t shifted = mic_buffer[i] >> 14;
        float amplified = (float)shifted * MIC_GAIN;

        // Clamp the audio to 16-bit limits to prevent digital static/wrapping
        if (amplified > 32767.0f)
          amplified = 32767.0f;
        if (amplified < -32768.0f)
          amplified = -32768.0f;

        // Push the safe value into the speaker buffer
        spk_buffer[i] = (int16_t)amplified;
      }

      // 2. Blast it immediately out of the speaker
      i2s_channel_write(tx_handle, spk_buffer, samples_read * sizeof(int16_t),
                        &bytes_written, portMAX_DELAY);
    }
  }
}

// ==========================================
// 3. Main Entry Point
// ==========================================
extern "C" void app_main(void) {
  ESP_LOGI(TAG, "╔══════════════════════════════╗");
  ESP_LOGI(TAG, "║    Audio Loopback Test       ║");
  ESP_LOGI(TAG, "╚══════════════════════════════╝");

  init_i2s();
  xTaskCreate(loopback_task, "loopback", 8192, NULL, 5, NULL);
}