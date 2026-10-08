#include "incense_audio.h"

#include <math.h>
#include <stdint.h>

#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define TONE_SAMPLE_RATE 16000
#define TONE_CHUNK 128
#define TONE_AMPLITUDE 4200
#define FADE_SAMPLES (TONE_SAMPLE_RATE / 50) // 每个音尾部 20 ms 线性淡出

typedef enum {
    AUDIO_MSG_COMPLETE = 1,
} audio_msg_t;

static const char *TAG = "incense_audio";
static QueueHandle_t s_queue;

// 写一段指定频率的正弦音（frequency=0 为静音间隔）。尾部线性淡出避免爆音。
static void write_tone(int frequency, int duration_ms) {
    int16_t samples[TONE_CHUNK];
    int total = TONE_SAMPLE_RATE * duration_ms / 1000;
    double step = frequency > 0 ? 2.0 * M_PI * frequency / TONE_SAMPLE_RATE : 0.0;
    int written = 0;
    while (total > 0) {
        int count = total < TONE_CHUNK ? total : TONE_CHUNK;
        for (int i = 0; i < count; i++) {
            int value = 0;
            if (frequency > 0) {
                int remain = total - i;
                double fade = remain <= FADE_SAMPLES
                                  ? (double)remain / FADE_SAMPLES
                                  : 1.0;
                value = (int)(TONE_AMPLITUDE * fade *
                              sin(step * (double)(written + i)));
            }
            samples[i] = (int16_t)value;
        }
        bsp_audio_write(samples, (size_t)count * sizeof(samples[0]));
        written += count;
        total -= count;
    }
}

static void audio_task(void *arg) {
    (void)arg;
    uint8_t msg;
    bsp_audio_set_format(TONE_SAMPLE_RATE, 16, 1);
    bsp_audio_set_volume(55);
    while (true) {
        if (xQueueReceive(s_queue, &msg, portMAX_DELAY) != pdTRUE) continue;
        if (msg == AUDIO_MSG_COMPLETE) {
            for (int note = 0; note < 3; note++) {
                if (note > 0) write_tone(0, 100); // 100 ms 静音间隔
                write_tone(880, 150);
            }
        }
    }
}

void incense_audio_prepare(bool audio_ok) {
    if (!audio_ok || s_queue) return;
    s_queue = xQueueCreate(1, sizeof(uint8_t));
    if (!s_queue || xTaskCreate(audio_task, "incense_audio", 3072, NULL, 4, NULL) != pdPASS) {
        ESP_LOGW(TAG, "Audio feedback disabled: worker creation failed");
        s_queue = NULL;
    }
}

void incense_audio_play_complete(void) {
    if (!s_queue) return; // 软降级：音频不可用时静默忽略
    uint8_t msg = AUDIO_MSG_COMPLETE;
    xQueueOverwrite(s_queue, &msg);
}
