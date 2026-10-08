// main/incense_ui.h —— 焚香计时器界面。
//
// 全新设计的四页 LVGL 界面（IDLE 选档 / 运行+暂停 / 放弃确认 / 燃尽），
// 与基线演示的菜单外壳无任何共享。所有公开函数必须在 LVGL 任务内或持有
// bsp_lvgl_lock() 时调用；按键回调路径由 main.c 持锁后进入。
#pragma once

#include <stdbool.h>

#include "bsp_button.h"
#include "incense_model.h"

// 创建屏幕、页面与 250 ms 周期 lv_timer（驱动 incense_model_tick 与刷新，
// 并在 RUNNING 中每约 60 秒分桶持久化、每 30 秒刷新电量）。
// battery_ok=false 或读数为 -1 时隐藏电量标签。
void incense_ui_init(incense_model_t *model, bool battery_ok);

// 三按键事件分发（已持锁）。语义：
//   IDLE:  UP/DOWN 移动选中（自定义项上 ±5 调分钟）；OK 开始
//   RUNNING/PAUSED: OK 短按 暂停/继续；OK 长按 请求放弃
//   CONFIRM: OK 确认放弃；UP 取消返回
//   DONE:  OK 回 IDLE
void incense_ui_on_key(bsp_btn_t btn, bsp_btn_ev_t ev);
