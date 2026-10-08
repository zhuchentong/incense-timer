#include "incense_ui.h"

#include <stdio.h>

#include "bsp_battery.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "incense_audio.h"
#include "incense_store.h"
#include "incense_text.h"
#include "lvgl.h"

LV_FONT_DECLARE(app_font_48);
LV_FONT_DECLARE(app_font_24);

// 全新视觉：深炭底 + 琥珀主色（与基线演示的米黄像素风无关）。
#define COLOR_BG 0x14100C
#define COLOR_PANEL 0x241C12
#define COLOR_PANEL_SEL 0x3A2C18
#define COLOR_TEXT 0xF2EDE4
#define COLOR_DIM 0x8A8177
#define COLOR_ACCENT 0xF5A623
#define COLOR_DANGER 0xD0453A

#define UI_TIMER_PERIOD_MS 250
#define UI_BATTERY_TICKS (30000 / UI_TIMER_PERIOD_MS) // 30 s

static const char *TAG = "incense_ui";

static incense_model_t *s_model;
static bool s_battery_ok;
static lv_obj_t *s_scr;
static lv_obj_t *s_battery_label;
static lv_obj_t *s_idle_page;
static lv_obj_t *s_rows[INCENSE_OPTION_COUNT];
static lv_obj_t *s_run_page;
static lv_obj_t *s_time_label;
static lv_obj_t *s_status_label;
static lv_obj_t *s_run_hint_label;
static lv_obj_t *s_confirm_page;
static lv_obj_t *s_done_page;
static lv_timer_t *s_timer;
static incense_state_t s_page;
static uint32_t s_last_sec = UINT32_MAX;
static uint32_t s_save_bucket = UINT32_MAX;
static int s_battery_soc = -1;
static uint32_t s_battery_ticks;

static uint64_t now_ms(void) {
    return (uint64_t)esp_timer_get_time() / 1000ULL;
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font,
                            uint32_t color) {
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_width(obj, 240);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    return obj;
}

static void set_page(incense_state_t state);

// ---------------------------------------------------------------------------
// 页面构建
// ---------------------------------------------------------------------------

static lv_obj_t *make_page(void) {
    lv_obj_t *page = lv_obj_create(s_scr);
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_size(page, 240, 320);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    return page;
}

static void build_idle_page(void) {
    s_idle_page = make_page();

    for (int i = 0; i < INCENSE_OPTION_COUNT; i++) {
        lv_obj_t *row = lv_obj_create(s_idle_page);
        lv_obj_set_pos(row, 20, 64 + i * 48);
        lv_obj_set_size(row, 200, 40);
        lv_obj_set_style_bg_color(row, lv_color_hex(COLOR_PANEL), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *label = make_label(row, &app_font_24, COLOR_TEXT);
        lv_obj_center(label);
        s_rows[i] = label;
    }

    lv_obj_t *hint = make_label(s_idle_page, &app_font_24, COLOR_DIM);
    lv_label_set_text(hint, INCENSE_TXT_PRESS_OK);
    lv_obj_set_pos(hint, 0, 276);
}

static void build_run_page(void) {
    s_run_page = make_page();

    s_time_label = make_label(s_run_page, &app_font_48, COLOR_TEXT);
    lv_obj_set_pos(s_time_label, 0, 116);

    s_status_label = make_label(s_run_page, &app_font_24, COLOR_ACCENT);
    lv_obj_set_pos(s_status_label, 0, 182);

    s_run_hint_label = make_label(s_run_page, &app_font_24, COLOR_DIM);
    lv_obj_set_pos(s_run_hint_label, 0, 224);
}

static void build_confirm_page(void) {
    s_confirm_page = make_page();

    lv_obj_t *question = make_label(s_confirm_page, &app_font_24, COLOR_TEXT);
    lv_label_set_text(question, INCENSE_TXT_ABANDON_Q);
    lv_obj_set_pos(question, 0, 118);

    lv_obj_t *hint = make_label(s_confirm_page, &app_font_24, COLOR_DANGER);
    lv_label_set_text(hint, INCENSE_TXT_ABANDON_HINT);
    lv_obj_set_pos(hint, 0, 172);
}

static void build_done_page(void) {
    s_done_page = make_page();

    lv_obj_t *done = make_label(s_done_page, &app_font_48, COLOR_ACCENT);
    lv_label_set_text(done, INCENSE_TXT_DONE);
    lv_obj_set_pos(done, 0, 128);
}

// ---------------------------------------------------------------------------
// 刷新
// ---------------------------------------------------------------------------

static void set_hidden(lv_obj_t *obj, bool hidden) {
    if (!obj) return;
    if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static void refresh_idle_rows(void) {
    static const char *PRESETS[INCENSE_PRESET_COUNT] = {
        INCENSE_TXT_PRESET_15, INCENSE_TXT_PRESET_30, INCENSE_TXT_PRESET_60,
    };
    for (int i = 0; i < INCENSE_OPTION_COUNT; i++) {
        lv_obj_t *row = lv_obj_get_parent(s_rows[i]);
        bool selected = s_model->selected_index == (uint8_t)i;
        lv_obj_set_style_bg_color(row,
                                  lv_color_hex(selected ? COLOR_PANEL_SEL
                                                         : COLOR_PANEL),
                                  0);
        lv_obj_set_style_border_color(
            row, lv_color_hex(selected ? COLOR_ACCENT : COLOR_PANEL), 0);
        lv_obj_set_style_border_width(row, selected ? 2 : 0, 0);
        if (i == INCENSE_INDEX_CUSTOM) {
            lv_label_set_text_fmt(s_rows[i], INCENSE_TXT_CUSTOM " %u " INCENSE_TXT_MINUTES,
                                  (unsigned)s_model->custom_min);
        } else {
            lv_label_set_text(s_rows[i], PRESETS[i]);
        }
        lv_obj_set_style_text_color(
            s_rows[i],
            lv_color_hex(selected ? COLOR_ACCENT : COLOR_TEXT), 0);
    }
}

static uint32_t displayed_seconds(void) {
    if (s_model->state == INCENSE_IDLE) {
        return (uint32_t)incense_model_minutes(s_model) * 60U;
    }
    return s_model->remaining_sec;
}

static void refresh_run_page(void) {
    uint32_t seconds = displayed_seconds();
    lv_label_set_text_fmt(s_time_label, "%02lu:%02lu",
                          (unsigned long)(seconds / 60),
                          (unsigned long)(seconds % 60));
    bool paused = s_model->state == INCENSE_PAUSED;
    lv_label_set_text(s_status_label, INCENSE_TXT_PAUSED);
    lv_label_set_text(s_run_hint_label, INCENSE_TXT_RESUME);
    set_hidden(s_status_label, !paused);
    set_hidden(s_run_hint_label, !paused);
}

static void refresh_battery(void) {
    if (!s_battery_ok) return;
    int soc = bsp_battery_soc();
    if (soc < 0) {
        // 项目规则：读不到就隐藏，绝不显示 -1。
        set_hidden(s_battery_label, true);
        return;
    }
    s_battery_soc = soc;
    lv_label_set_text_fmt(s_battery_label, "%d%%", s_battery_soc);
    set_hidden(s_battery_label, false);
}

static void refresh_all(void) {
    refresh_idle_rows();
    refresh_run_page();
    refresh_battery();
}

static void set_page(incense_state_t state) {
    if (s_page == state) {
        refresh_all();
        return;
    }
    s_page = state;
    set_hidden(s_idle_page, state != INCENSE_IDLE);
    set_hidden(s_run_page,
               state != INCENSE_RUNNING && state != INCENSE_PAUSED);
    set_hidden(s_confirm_page, state != INCENSE_ABANDON_CONFIRM);
    set_hidden(s_done_page, state != INCENSE_DONE);
    refresh_all();
}

// ---------------------------------------------------------------------------
// 按键与定时器
// ---------------------------------------------------------------------------

static void persist(void) {
    incense_store_request_save(s_model);
}

void incense_ui_on_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (!s_model || !s_scr) return;
    uint64_t now = now_ms();
    bool changed = false;

    switch (s_model->state) {
        case INCENSE_IDLE:
            if (ev == BSP_BTN_CLICK && btn == BSP_BTN_UP) {
                changed = incense_model_select(s_model, -1);
            } else if (ev == BSP_BTN_CLICK && btn == BSP_BTN_DOWN) {
                changed = incense_model_select(s_model, +1);
            } else if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) {
                changed = incense_model_start(s_model, now);
            }
            break;
        case INCENSE_RUNNING:
            if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) {
                changed = incense_model_pause(s_model, now);
            } else if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
                changed = incense_model_request_abandon(s_model, now);
            }
            break;
        case INCENSE_PAUSED:
            if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) {
                changed = incense_model_resume(s_model, now);
            } else if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
                changed = incense_model_request_abandon(s_model, now);
            }
            break;
        case INCENSE_ABANDON_CONFIRM:
            if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) {
                changed = incense_model_confirm_abandon(s_model);
            } else if (ev == BSP_BTN_CLICK && btn == BSP_BTN_UP) {
                changed = incense_model_cancel_abandon(s_model, now);
            }
            break;
        case INCENSE_DONE:
            if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) {
                changed = incense_model_dismiss(s_model);
            }
            break;
    }

    if (changed) {
        s_last_sec = UINT32_MAX;
        persist();
        set_page(s_model->state);
    }
}

static void timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (!s_model || !s_scr) return;
    uint64_t now = now_ms();
    incense_event_t event = incense_model_tick(s_model, now);

    if (event == INCENSE_EVENT_COMPLETE) {
        persist();
        incense_audio_play_complete();
        s_last_sec = UINT32_MAX;
        set_page(s_model->state); // DONE
    } else if (event == INCENSE_EVENT_CONFIRM_TIMEOUT) {
        persist();
        s_last_sec = UINT32_MAX;
        set_page(s_model->state); // 回 RUNNING/PAUSED
    }

    uint32_t seconds = displayed_seconds();
    if (seconds != s_last_sec) {
        s_last_sec = seconds;
        refresh_run_page();
        if (s_page == INCENSE_IDLE) refresh_idle_rows();
    }

    // RUNNING 中每约 60 秒分桶保存一次，限制 NVS 磨损。
    if (s_model->state == INCENSE_RUNNING) {
        uint32_t bucket = seconds / 60;
        if (bucket != s_save_bucket) {
            s_save_bucket = bucket;
            persist();
        }
    } else {
        s_save_bucket = UINT32_MAX;
    }

    if (++s_battery_ticks >= UI_BATTERY_TICKS) {
        s_battery_ticks = 0;
        refresh_battery();
    }
}

// ---------------------------------------------------------------------------
// 初始化
// ---------------------------------------------------------------------------

void incense_ui_init(incense_model_t *model, bool battery_ok) {
    s_model = model;
    s_battery_ok = battery_ok;
    s_last_sec = UINT32_MAX;
    s_save_bucket = UINT32_MAX;
    s_battery_ticks = 0;
    s_battery_soc = -1;

    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_scr, 0, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = make_label(s_scr, &app_font_24, COLOR_TEXT);
    lv_label_set_text(title, INCENSE_TXT_TITLE);
    lv_obj_set_pos(title, 0, 12);

    s_battery_label = make_label(s_scr, &app_font_24, COLOR_DIM);
    lv_obj_set_width(s_battery_label, LV_SIZE_CONTENT);
    lv_obj_align(s_battery_label, LV_ALIGN_TOP_RIGHT, -8, 12);
    set_hidden(s_battery_label, true);

    build_idle_page();
    build_run_page();
    build_confirm_page();
    build_done_page();

    set_page(s_model->state); // 恢复启动时直接落在正确页
    s_timer = lv_timer_create(timer_cb, UI_TIMER_PERIOD_MS, NULL);
    lv_screen_load(s_scr);
    ESP_LOGI(TAG, "incense UI ready (state=%d)", (int)s_model->state);
}
