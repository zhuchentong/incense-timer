// main/incense_text.h —— 焚香计时器全部 UI 字符串的唯一来源。
//
// 红线：界面不允许出现本文件之外的中文。新增字符串时必须：
//   1. 把新字符追加进 assets/fonts/incense_chars.txt；
//   2. 用 assets/fonts/generate_fonts.sh 重新生成两个字库；
//   3. tests/test_incense_text.c 会强制校验覆盖，缺失即编译期宿主测试失败。
#pragma once

#define INCENSE_TXT_TITLE        "焚香计时"
#define INCENSE_TXT_PRESET_15    "15 分"
#define INCENSE_TXT_PRESET_30    "30 分"
#define INCENSE_TXT_PRESET_60    "60 分"
#define INCENSE_TXT_CUSTOM       "自定义"
#define INCENSE_TXT_MINUTES      "分钟"
#define INCENSE_TXT_PRESS_OK     "按 OK 开始"
#define INCENSE_TXT_PAUSED       "已暂停"
#define INCENSE_TXT_RESUME       "继续"
#define INCENSE_TXT_ABANDON_Q    "放弃本次焚香？"
#define INCENSE_TXT_ABANDON_HINT "OK 确认  UP 返回"
#define INCENSE_TXT_DONE         "香已燃尽"
