// main/incense_model.h —— 焚香计时器纯逻辑状态机。
//
// 本模块不依赖 ESP-IDF/LVGL，宿主可直接编译测试：
//   cc -std=c11 -Wall -Wextra -Werror -Imain
//     tests/test_incense_model.c main/incense_model.c -o /tmp/t 然后 /tmp/t。
// 时间源由调用方注入（设备上用 esp_timer_get_time()/1000），保证可测性。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define INCENSE_MODEL_VERSION 1
#define INCENSE_CONFIRM_MS 5000

// 档位表：3 个预设 + 第 4 项自定义（custom_min，5..180，步进 5）。
#define INCENSE_PRESET_COUNT 3
#define INCENSE_OPTION_COUNT 4
#define INCENSE_INDEX_CUSTOM 3
#define INCENSE_CUSTOM_MIN_MINUTES 5
#define INCENSE_CUSTOM_MAX_MINUTES 180
#define INCENSE_CUSTOM_STEP_MINUTES 5

typedef enum {
    INCENSE_IDLE = 0,        // 选档
    INCENSE_RUNNING,         // 计时中
    INCENSE_PAUSED,          // 暂停
    INCENSE_ABANDON_CONFIRM, // 放弃二次确认（确认页不冻结已暂停的香，
                             // 但 RUNNING 进入确认页时香仍在烧：deadline 继续走）
    INCENSE_DONE,            // 燃尽提示页
} incense_state_t;

typedef enum {
    INCENSE_EVENT_NONE = 0,
    INCENSE_EVENT_COMPLETE,        // 燃尽
    INCENSE_EVENT_CONFIRM_TIMEOUT, // 确认页 5s 超时，已回原状态
} incense_event_t;

typedef struct {
    /* 持久化字段（incense_store 写入 NVS 的内容）。 */
    uint16_t version;
    incense_state_t state;
    uint8_t selected_index; // 0..3，3 = 自定义
    uint8_t custom_min;     // 自定义分钟数，5..180，5 的倍数
    uint32_t remaining_sec;

    /* 下面是运行时字段，不写入 NVS。 */
    incense_state_t pre_confirm_state; // 进入确认页前的状态
    uint64_t deadline_ms;              // RUNNING 的燃尽时刻（单调毫秒）
    uint64_t confirm_deadline_ms;      // 确认页超时时刻
} incense_model_t;

void incense_model_defaults(incense_model_t *model);
// 按 NVS 恢复的字段重建一致状态：RUNNING/PAUSED/CONFIRM 且 remaining>0 → PAUSED
//（用户手动继续，避免意外起跑）；否则 IDLE。
void incense_model_restore(incense_model_t *model);

// 当前档位的分钟数（预设或自定义）。
uint8_t incense_model_minutes(const incense_model_t *model);

// IDLE 中 UP/DOWN 移动选中（4 档循环）；已选中自定义项时改为 ±5 调整分钟，
// 在 5/180 处截停；值已到边界时同方向再按则循环跳出（DOWN→15 分、UP→60 分）。
bool incense_model_select(incense_model_t *model, int direction);

bool incense_model_start(incense_model_t *model, uint64_t now_ms);
bool incense_model_pause(incense_model_t *model, uint64_t now_ms);
bool incense_model_resume(incense_model_t *model, uint64_t now_ms);
// RUNNING/PAUSED 中请求放弃 → 确认页。
bool incense_model_request_abandon(incense_model_t *model, uint64_t now_ms);
// 确认页取消（UP）→ 回原状态。
bool incense_model_cancel_abandon(incense_model_t *model, uint64_t now_ms);
// 确认页确认（OK）→ 清运行态回 IDLE。
bool incense_model_confirm_abandon(incense_model_t *model);
// DONE 提示页按 OK → IDLE。
bool incense_model_dismiss(incense_model_t *model);

// 周期驱动：更新剩余秒数，产生 COMPLETE / CONFIRM_TIMEOUT 事件。
incense_event_t incense_model_tick(incense_model_t *model, uint64_t now_ms);
