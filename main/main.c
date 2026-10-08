// main/main.c —— 焚香计时器：初始化 + 直进应用 + 按键分发。
//
// 启动顺序：显示/LVGL（失败即退出）→ 背光 100% → 按键 → 音频/电量（软失败
// 不阻塞）→ NVS 恢复 → 创建界面。按键语义见 incense_ui.h。
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "incense_audio.h"
#include "incense_model.h"
#include "incense_store.h"
#include "incense_ui.h"

static const char *TAG = "main";

static incense_model_t s_model;

// 按键回调运行在 button 组件的共享 esp_timer 任务里：只做有界分发，
// LVGL 访问全部在持锁之后（回调内绝不做音频/NVS 等慢操作——非阻塞红线）。
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    if (!bsp_lvgl_lock(500)) return;
    incense_ui_on_key(btn, ev);
    bsp_lvgl_unlock();
}

void app_main(void) {
    ESP_LOGI(TAG, "焚香计时器启动");

    // 屏幕是本应用的 UI 载体，失败没有界面可言——打清日志后退出。
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败，应用无法继续");
        return;
    }
    bsp_display_backlight(100);

    // 按键：无按键则应用不可操作，但保留界面显示以暴露状态。
    esp_err_t button_err = bsp_button_init(on_key, NULL);
    if (button_err != ESP_OK) {
        ESP_LOGE(TAG, "按键初始化失败: %s", esp_err_to_name(button_err));
    }

    // 音频/电量软失败：提示音与电量显示降级，计时功能不受影响。
    bool audio_ok = bsp_audio_init() == ESP_OK;
    if (!audio_ok) ESP_LOGW(TAG, "音频初始化失败，燃尽提示音不可用");
    incense_audio_prepare(audio_ok);

    bool battery_ok = bsp_battery_init() == ESP_OK;
    if (!battery_ok) ESP_LOGW(TAG, "电量计初始化失败，电量显示不可用");

    // NVS 初始化 + 断电续记恢复（RUNNING/PAUSED → 恢复为已暂停）。
    if (!incense_store_init(&s_model)) {
        ESP_LOGW(TAG, "NVS 不可用，本次运行不持久化");
    }

    if (bsp_lvgl_lock(1000)) {
        incense_ui_init(&s_model, battery_ok);
        bsp_lvgl_unlock();
    }

    ESP_LOGI(TAG, "焚香计时器就绪: Button=%d Audio=%d Battery=%d State=%d",
             button_err == ESP_OK, audio_ok, battery_ok, (int)s_model.state);
}
