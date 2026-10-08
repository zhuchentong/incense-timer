// main/incense_store.h —— 焚香计时器 NVS 持久化。
//
// 写入走后台 worker 任务（调用方只入队，按键回调/定时器路径永不阻塞）。
// 状态每次迁移后由 UI 调 request_save；RUNNING 中由 lv_timer 每约 60s
// 分桶保存一次 remaining_sec，限制 NVS 磨损。
#pragma once

#include <stdbool.h>

#include "incense_model.h"

// 初始化 NVS（含 NO_FREE_PAGES/NEW_VERSION_FOUND 恢复），加载并 restore
// 持久化字段到 model。失败时 model 保持默认值并置错误标志。
bool incense_store_init(incense_model_t *model);

// 请求把 model 的持久化字段写入 NVS（异步，非阻塞）。
void incense_store_request_save(const incense_model_t *model);

// 自初始化以来是否发生过 NVS 读写错误。
bool incense_store_has_error(void);
