// tests/test_incense_model.c —— 焚香状态机宿主测试（不依赖 ESP-IDF/LVGL）。
// 编译：cc -std=c11 -Wall -Wextra -Werror -Imain
//       tests/test_incense_model.c main/incense_model.c -o /tmp/t 然后运行。
#include "incense_model.h"

#include <stdio.h>
#include <stdlib.h>

static int g_failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

#define CHECK_EQ(a, b)                                                     \
    do {                                                                   \
        unsigned long long _va = (unsigned long long)(a);                  \
        unsigned long long _vb = (unsigned long long)(b);                  \
        if (_va != _vb) {                                                  \
            printf("FAIL %s:%d: %s == %s (got %llu, want %llu)\n",         \
                   __FILE__, __LINE__, #a, #b, _va, _vb);                  \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

static void test_defaults(void) {
    incense_model_t m;
    incense_model_defaults(&m);
    CHECK_EQ(m.version, INCENSE_MODEL_VERSION);
    CHECK_EQ(m.state, INCENSE_IDLE);
    CHECK_EQ(m.selected_index, 0);
    CHECK_EQ(m.custom_min, 30);
    CHECK_EQ(m.remaining_sec, 15U * 60U);
}

static void test_select_cycles_and_custom_clamps(void) {
    incense_model_t m;
    incense_model_defaults(&m);

    // 预设档循环：15 → 30 → 60 → 自定义（进入自定义即切换为调整语义）。
    CHECK(incense_model_select(&m, +1));
    CHECK_EQ(m.selected_index, 1);
    CHECK_EQ(m.remaining_sec, 30U * 60U);
    CHECK(incense_model_select(&m, +1));
    CHECK_EQ(m.selected_index, 2);
    CHECK_EQ(m.remaining_sec, 60U * 60U);
    CHECK(incense_model_select(&m, +1));
    CHECK_EQ(m.selected_index, INCENSE_INDEX_CUSTOM);
    CHECK_EQ(m.remaining_sec, 30U * 60U); // 自定义默认 30 分

    // 自定义项上 DOWN=+5 / UP=-5；非边界时不出档。
    CHECK(incense_model_select(&m, +1));
    CHECK_EQ(m.custom_min, 35);
    CHECK_EQ(m.selected_index, INCENSE_INDEX_CUSTOM);
    CHECK(incense_model_select(&m, -1));
    CHECK_EQ(m.custom_min, 30);
    CHECK(incense_model_select(&m, -1));
    CHECK_EQ(m.custom_min, 25);
    CHECK(incense_model_select(&m, +1));
    CHECK_EQ(m.custom_min, 30);

    // UP 推到下界 5；值截停后再 UP 循环跳出 → 60 分档。
    for (int i = 0; i < 5; i++) CHECK(incense_model_select(&m, -1));
    CHECK_EQ(m.custom_min, INCENSE_CUSTOM_MIN_MINUTES);
    CHECK_EQ(m.selected_index, INCENSE_INDEX_CUSTOM);
    CHECK_EQ(m.remaining_sec, 5U * 60U);
    CHECK(incense_model_select(&m, -1));
    CHECK_EQ(m.selected_index, INCENSE_PRESET_COUNT - 1);
    CHECK_EQ(m.remaining_sec, 60U * 60U);

    // DOWN 重新进入自定义档（保留 5 分），DOWN 推到上界 180；
    // 值截停后再 DOWN 循环跳出 → 15 分档（列表顶部）。
    CHECK(incense_model_select(&m, +1));
    CHECK_EQ(m.selected_index, INCENSE_INDEX_CUSTOM);
    CHECK_EQ(m.custom_min, INCENSE_CUSTOM_MIN_MINUTES);
    for (int i = 0; i < 35; i++) CHECK(incense_model_select(&m, +1));
    CHECK_EQ(m.custom_min, INCENSE_CUSTOM_MAX_MINUTES);
    CHECK_EQ(m.selected_index, INCENSE_INDEX_CUSTOM);
    CHECK(incense_model_select(&m, +1));
    CHECK_EQ(m.selected_index, 0);
    CHECK_EQ(m.remaining_sec, 15U * 60U);

    // 非 IDLE 不可调整。
    CHECK(incense_model_start(&m, 1000));
    CHECK(!incense_model_select(&m, +1));
}

static void test_start_tick_completes(void) {
    incense_model_t m;
    incense_model_defaults(&m);
    const uint64_t t0 = 100000;

    CHECK(incense_model_start(&m, t0));
    CHECK_EQ(m.state, INCENSE_RUNNING);
    CHECK_EQ(m.remaining_sec, 15U * 60U);
    CHECK_EQ(m.deadline_ms, t0 + 900000ULL);

    CHECK_EQ(incense_model_tick(&m, t0 + 999), INCENSE_EVENT_NONE);
    CHECK_EQ(m.remaining_sec, 15U * 60U); // ceil
    CHECK_EQ(incense_model_tick(&m, t0 + 1000), INCENSE_EVENT_NONE);
    CHECK_EQ(m.remaining_sec, 899U);
    CHECK_EQ(incense_model_tick(&m, t0 + 45000), INCENSE_EVENT_NONE);
    CHECK_EQ(m.remaining_sec, 855U);
    CHECK_EQ(incense_model_tick(&m, t0 + 900000 - 1), INCENSE_EVENT_NONE);
    CHECK_EQ(incense_model_tick(&m, t0 + 900000), INCENSE_EVENT_COMPLETE);
    CHECK_EQ(m.state, INCENSE_DONE);
    CHECK_EQ(m.remaining_sec, 0);
    CHECK_EQ(incense_model_tick(&m, t0 + 900001), INCENSE_EVENT_NONE); // DONE 稳定

    CHECK(incense_model_dismiss(&m));
    CHECK_EQ(m.state, INCENSE_IDLE);
    CHECK_EQ(m.remaining_sec, 15U * 60U);

    // 重复 start / 非 IDLE start 拒绝。
    CHECK(incense_model_start(&m, t0));
    CHECK(!incense_model_start(&m, t0 + 1));
}

static void test_pause_freezes_resume_continues(void) {
    incense_model_t m;
    incense_model_defaults(&m);
    const uint64_t t0 = 200000;

    CHECK(incense_model_start(&m, t0));
    incense_model_tick(&m, t0 + 5000);
    CHECK(incense_model_pause(&m, t0 + 5000));
    CHECK_EQ(m.state, INCENSE_PAUSED);
    CHECK_EQ(m.remaining_sec, 895U);
    CHECK_EQ(m.deadline_ms, 0);

    // 暂停期间时间流逝不影响剩余。
    CHECK_EQ(incense_model_tick(&m, t0 + 60000), INCENSE_EVENT_NONE);
    CHECK_EQ(m.remaining_sec, 895U);
    CHECK(!incense_model_pause(&m, t0 + 61000)); // 重复暂停拒绝
    CHECK(incense_model_resume(&m, t0 + 61000));
    CHECK_EQ(m.state, INCENSE_RUNNING);
    CHECK_EQ(m.deadline_ms, t0 + 61000 + 895000ULL);
    CHECK_EQ(incense_model_tick(&m, t0 + 62000), INCENSE_EVENT_NONE);
    CHECK_EQ(m.remaining_sec, 894U);

    // 剩余为 0 时不可 resume（防御）。
    CHECK(incense_model_pause(&m, t0 + 62000));
    m.remaining_sec = 0;
    CHECK(!incense_model_resume(&m, t0 + 62001));
}

static void test_abandon_confirm_flow(void) {
    incense_model_t m;
    incense_model_defaults(&m);
    const uint64_t t0 = 300000;

    CHECK(incense_model_start(&m, t0));
    incense_model_tick(&m, t0 + 1000);

    // RUNNING → 确认页；确认页内香仍在烧（deadline 不变）。
    CHECK(incense_model_request_abandon(&m, t0 + 1000));
    CHECK_EQ(m.state, INCENSE_ABANDON_CONFIRM);
    CHECK_EQ(m.confirm_deadline_ms, t0 + 1000 + INCENSE_CONFIRM_MS);
    CHECK(!incense_model_request_abandon(&m, t0 + 1001)); // 重复请求拒绝
    CHECK_EQ(incense_model_tick(&m, t0 + 3000), INCENSE_EVENT_NONE);
    CHECK_EQ(m.remaining_sec, 897U);

    // UP 取消 → 回 RUNNING，剩余按当前时刻重算。
    CHECK(incense_model_cancel_abandon(&m, t0 + 4000));
    CHECK_EQ(m.state, INCENSE_RUNNING);
    CHECK_EQ(m.remaining_sec, 896U);
    CHECK_EQ(m.confirm_deadline_ms, 0);

    // PAUSED → 确认页 → OK 确认放弃 → IDLE。
    CHECK(incense_model_pause(&m, t0 + 5000));
    CHECK(incense_model_request_abandon(&m, t0 + 5000));
    CHECK_EQ(m.state, INCENSE_ABANDON_CONFIRM);
    CHECK(incense_model_confirm_abandon(&m));
    CHECK_EQ(m.state, INCENSE_IDLE);
    CHECK_EQ(m.remaining_sec, 15U * 60U);
    CHECK_EQ(m.deadline_ms, 0);
    CHECK(!incense_model_confirm_abandon(&m)); // 非 CONFIRM 拒绝
}

static void test_confirm_timeout_returns_to_previous_state(void) {
    incense_model_t m;
    incense_model_defaults(&m);
    const uint64_t t0 = 400000;

    // RUNNING 进入确认页，超时回 RUNNING。
    CHECK(incense_model_start(&m, t0));
    CHECK(incense_model_request_abandon(&m, t0));
    CHECK_EQ(incense_model_tick(&m, t0 + INCENSE_CONFIRM_MS - 1),
             INCENSE_EVENT_NONE);
    CHECK_EQ(m.state, INCENSE_ABANDON_CONFIRM);
    CHECK_EQ(incense_model_tick(&m, t0 + INCENSE_CONFIRM_MS),
             INCENSE_EVENT_CONFIRM_TIMEOUT);
    CHECK_EQ(m.state, INCENSE_RUNNING);
    CHECK_EQ(m.remaining_sec, 895U); // 确认页停留的 5s 已计入燃烧

    // PAUSED 进入确认页，超时回 PAUSED，剩余不流逝。
    CHECK(incense_model_pause(&m, t0 + 20000));
    CHECK(incense_model_request_abandon(&m, t0 + 20000));
    CHECK_EQ(incense_model_tick(&m, t0 + 20000 + INCENSE_CONFIRM_MS),
             INCENSE_EVENT_CONFIRM_TIMEOUT);
    CHECK_EQ(m.state, INCENSE_PAUSED);
    CHECK_EQ(m.remaining_sec, 880U);

    // 确认页内燃尽：COMPLETE 优先于超时。
    CHECK(incense_model_resume(&m, t0 + 30000));
    CHECK(incense_model_request_abandon(&m, t0 + 30000));
    CHECK_EQ(incense_model_tick(&m, t0 + 30000 + 880000 + 100),
             INCENSE_EVENT_COMPLETE);
    CHECK_EQ(m.state, INCENSE_DONE);
    CHECK_EQ(m.confirm_deadline_ms, 0);
}

static void test_restore_rebuilds_from_persisted_fields(void) {
    incense_model_t m;

    // RUNNING + 剩余 > 0 → 恢复为 PAUSED（不自动起跑）。
    m = (incense_model_t){ .version = INCENSE_MODEL_VERSION,
                           .state = INCENSE_RUNNING,
                           .selected_index = 2,
                           .custom_min = 45,
                           .remaining_sec = 1234 };
    incense_model_restore(&m);
    CHECK_EQ(m.state, INCENSE_PAUSED);
    CHECK_EQ(m.remaining_sec, 1234U);
    CHECK_EQ(m.deadline_ms, 0);
    CHECK_EQ(m.confirm_deadline_ms, 0);
    CHECK(incense_model_resume(&m, 500));
    CHECK_EQ(m.deadline_ms, 500 + 1234000ULL);

    // RUNNING + 剩余 == 0 → IDLE 且剩余重置为档位值（60 分预设）。
    m = (incense_model_t){ .version = INCENSE_MODEL_VERSION,
                           .state = INCENSE_RUNNING,
                           .selected_index = 2,
                           .custom_min = 30,
                           .remaining_sec = 0 };
    incense_model_restore(&m);
    CHECK_EQ(m.state, INCENSE_IDLE);
    CHECK_EQ(m.remaining_sec, 60U * 60U);

    // ABANDON_CONFIRM 持久化后 → PAUSED。
    m = (incense_model_t){ .version = INCENSE_MODEL_VERSION,
                           .state = INCENSE_ABANDON_CONFIRM,
                           .selected_index = 3,
                           .custom_min = 25,
                           .remaining_sec = 77 };
    incense_model_restore(&m);
    CHECK_EQ(m.state, INCENSE_PAUSED);
    CHECK_EQ(m.remaining_sec, 77U);

    // DONE 持久化后（未按 OK 就断电）→ IDLE。
    m = (incense_model_t){ .version = INCENSE_MODEL_VERSION,
                           .state = INCENSE_DONE,
                           .selected_index = 0,
                           .custom_min = 30,
                           .remaining_sec = 0 };
    incense_model_restore(&m);
    CHECK_EQ(m.state, INCENSE_IDLE);
    CHECK_EQ(m.remaining_sec, 15U * 60U);

    // 字段非法 → 整体回默认。
    m = (incense_model_t){ .version = 99,
                           .state = INCENSE_RUNNING,
                           .selected_index = 2,
                           .custom_min = 30,
                           .remaining_sec = 100 };
    incense_model_restore(&m);
    CHECK_EQ(m.state, INCENSE_IDLE);
    CHECK_EQ(m.selected_index, 0);

    m = (incense_model_t){ .version = INCENSE_MODEL_VERSION,
                           .state = INCENSE_IDLE,
                           .selected_index = 3,
                           .custom_min = 200, // 越界
                           .remaining_sec = 100 };
    incense_model_restore(&m);
    CHECK_EQ(m.custom_min, 30); // 默认值
    CHECK_EQ(m.state, INCENSE_IDLE);

    m = (incense_model_t){ .version = INCENSE_MODEL_VERSION,
                           .state = INCENSE_IDLE,
                           .selected_index = 5, // 越界
                           .custom_min = 30,
                           .remaining_sec = 100 };
    incense_model_restore(&m);
    CHECK_EQ(m.selected_index, 0);
}

int main(void) {
    test_defaults();
    test_select_cycles_and_custom_clamps();
    test_start_tick_completes();
    test_pause_freezes_resume_continues();
    test_abandon_confirm_flow();
    test_confirm_timeout_returns_to_previous_state();
    test_restore_rebuilds_from_persisted_fields();

    if (g_failures != 0) {
        printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("incense model tests: all passed\n");
    return 0;
}
