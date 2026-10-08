// main/incense_audio.h —— 燃尽提示音。
//
// 设计约束：按键回调/LVGL 定时器只允许入队（非阻塞红线），合成与 I2S 写入
// 全部在 worker 任务内完成。音频不可用（初始化失败/任务创建失败）时软降级
// ——播放请求被静默忽略，UI 照常运行。
#pragma once

#include <stdbool.h>

// 创建播放 worker。audio_ok=false（bsp_audio_init 失败）时只置降级标志，
// 不创建任务。重复调用安全。
void incense_audio_prepare(bool audio_ok);

// 播放燃尽提示：880 Hz 正弦三短音（各 150 ms、间隔 100 ms、尾部淡出防爆音）。
// 非阻塞；内部 xQueueOverwrite，重复请求只保留最新一次。
void incense_audio_play_complete(void);
