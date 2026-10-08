#include "incense_model.h"

#include <stddef.h>
#include <string.h>

static const uint8_t PRESET_MINUTES[INCENSE_PRESET_COUNT] = { 15, 30, 60 };

static uint32_t seconds_until(uint64_t deadline_ms, uint64_t now_ms) {
    if (now_ms >= deadline_ms) return 0;
    return (uint32_t)((deadline_ms - now_ms + 999) / 1000);
}

static void update_running_remaining(incense_model_t *model, uint64_t now_ms) {
    if (now_ms >= model->deadline_ms) {
        model->remaining_sec = 0;
        return;
    }
    model->remaining_sec = seconds_until(model->deadline_ms, now_ms);
}

void incense_model_defaults(incense_model_t *model) {
    if (!model) return;
    memset(model, 0, sizeof(*model));
    model->version = INCENSE_MODEL_VERSION;
    model->state = INCENSE_IDLE;
    model->selected_index = 0;
    model->custom_min = 30;
    model->remaining_sec = (uint32_t)PRESET_MINUTES[0] * 60U;
}

void incense_model_restore(incense_model_t *model) {
    if (!model) return;

    if (model->version != INCENSE_MODEL_VERSION ||
        model->selected_index >= INCENSE_OPTION_COUNT ||
        model->custom_min < INCENSE_CUSTOM_MIN_MINUTES ||
        model->custom_min > INCENSE_CUSTOM_MAX_MINUTES ||
        (model->custom_min % INCENSE_CUSTOM_STEP_MINUTES) != 0 ||
        model->state > INCENSE_DONE) {
        incense_model_defaults(model);
        return;
    }

    model->deadline_ms = 0;
    model->confirm_deadline_ms = 0;
    model->pre_confirm_state = INCENSE_IDLE;

    switch (model->state) {
        case INCENSE_RUNNING:
        case INCENSE_PAUSED:
        case INCENSE_ABANDON_CONFIRM:
            // 断电续记：恢复为已暂停，由用户手动继续，避免意外自动起跑。
            model->state = model->remaining_sec > 0 ? INCENSE_PAUSED : INCENSE_IDLE;
            break;
        case INCENSE_DONE:
        case INCENSE_IDLE:
            model->state = INCENSE_IDLE;
            break;
    }

    if (model->state == INCENSE_IDLE && model->remaining_sec == 0) {
        model->remaining_sec = (uint32_t)incense_model_minutes(model) * 60U;
    }
}

uint8_t incense_model_minutes(const incense_model_t *model) {
    if (!model) return 0;
    if (model->selected_index == INCENSE_INDEX_CUSTOM) return model->custom_min;
    if (model->selected_index >= INCENSE_PRESET_COUNT) return PRESET_MINUTES[0];
    return PRESET_MINUTES[model->selected_index];
}

bool incense_model_select(incense_model_t *model, int direction) {
    if (!model || model->state != INCENSE_IDLE || direction == 0) return false;

    if (model->selected_index == INCENSE_INDEX_CUSTOM) {
        // 自定义项：UP/DOWN 直接 ±5 调整，5/180 处截停；值已到边界时按同方向
        // 再按则循环跳出（避免用户被困在自定义档），DOWN 回 15 分档、UP 回 60 分档。
        int minutes = (int)model->custom_min +
                      (direction > 0 ? INCENSE_CUSTOM_STEP_MINUTES
                                     : -INCENSE_CUSTOM_STEP_MINUTES);
        if (minutes >= INCENSE_CUSTOM_MIN_MINUTES &&
            minutes <= INCENSE_CUSTOM_MAX_MINUTES) {
            model->custom_min = (uint8_t)minutes;
            model->remaining_sec = (uint32_t)minutes * 60U;
            return true;
        }
        model->selected_index = direction > 0 ? 0 : INCENSE_PRESET_COUNT - 1;
        model->remaining_sec = (uint32_t)incense_model_minutes(model) * 60U;
        return true;
    }

    int index = (int)model->selected_index + (direction > 0 ? 1 : -1);
    if (index < 0) index = INCENSE_OPTION_COUNT - 1;
    if (index >= INCENSE_OPTION_COUNT) index = 0;
    model->selected_index = (uint8_t)index;
    model->remaining_sec = (uint32_t)incense_model_minutes(model) * 60U;
    return true;
}

bool incense_model_start(incense_model_t *model, uint64_t now_ms) {
    if (!model || model->state != INCENSE_IDLE) return false;
    model->remaining_sec = (uint32_t)incense_model_minutes(model) * 60U;
    model->deadline_ms = now_ms + (uint64_t)model->remaining_sec * 1000ULL;
    model->state = INCENSE_RUNNING;
    return true;
}

bool incense_model_pause(incense_model_t *model, uint64_t now_ms) {
    if (!model || model->state != INCENSE_RUNNING) return false;
    update_running_remaining(model, now_ms);
    model->state = INCENSE_PAUSED;
    model->deadline_ms = 0;
    return true;
}

bool incense_model_resume(incense_model_t *model, uint64_t now_ms) {
    if (!model || model->state != INCENSE_PAUSED || model->remaining_sec == 0) {
        return false;
    }
    model->deadline_ms = now_ms + (uint64_t)model->remaining_sec * 1000ULL;
    model->state = INCENSE_RUNNING;
    return true;
}

bool incense_model_request_abandon(incense_model_t *model, uint64_t now_ms) {
    if (!model) return false;
    if (model->state != INCENSE_RUNNING && model->state != INCENSE_PAUSED) {
        return false;
    }
    model->pre_confirm_state = model->state;
    model->confirm_deadline_ms = now_ms + INCENSE_CONFIRM_MS;
    model->state = INCENSE_ABANDON_CONFIRM;
    return true;
}

bool incense_model_cancel_abandon(incense_model_t *model, uint64_t now_ms) {
    if (!model || model->state != INCENSE_ABANDON_CONFIRM) return false;
    model->confirm_deadline_ms = 0;
    model->state = model->pre_confirm_state;
    model->pre_confirm_state = INCENSE_IDLE;
    if (model->state == INCENSE_RUNNING) {
        // 确认页不暂停燃烧：按当前时刻重算剩余。
        update_running_remaining(model, now_ms);
        if (model->remaining_sec == 0) {
            model->deadline_ms = 0;
            model->state = INCENSE_DONE;
        }
    }
    return true;
}

bool incense_model_confirm_abandon(incense_model_t *model) {
    if (!model || model->state != INCENSE_ABANDON_CONFIRM) return false;
    model->state = INCENSE_IDLE;
    model->pre_confirm_state = INCENSE_IDLE;
    model->deadline_ms = 0;
    model->confirm_deadline_ms = 0;
    model->remaining_sec = (uint32_t)incense_model_minutes(model) * 60U;
    return true;
}

bool incense_model_dismiss(incense_model_t *model) {
    if (!model || model->state != INCENSE_DONE) return false;
    model->state = INCENSE_IDLE;
    model->remaining_sec = (uint32_t)incense_model_minutes(model) * 60U;
    return true;
}

incense_event_t incense_model_tick(incense_model_t *model, uint64_t now_ms) {
    if (!model) return INCENSE_EVENT_NONE;

    if (model->state == INCENSE_RUNNING) {
        if (now_ms >= model->deadline_ms) {
            model->remaining_sec = 0;
            model->deadline_ms = 0;
            model->state = INCENSE_DONE;
            return INCENSE_EVENT_COMPLETE;
        }
        model->remaining_sec = seconds_until(model->deadline_ms, now_ms);
        return INCENSE_EVENT_NONE;
    }

    if (model->state == INCENSE_ABANDON_CONFIRM) {
        // 确认页里香仍在烧（pre_confirm_state == RUNNING 时）：燃尽优先上报。
        if (model->pre_confirm_state == INCENSE_RUNNING) {
            if (now_ms >= model->deadline_ms) {
                model->remaining_sec = 0;
                model->deadline_ms = 0;
                model->confirm_deadline_ms = 0;
                model->state = INCENSE_DONE;
                return INCENSE_EVENT_COMPLETE;
            }
            model->remaining_sec = seconds_until(model->deadline_ms, now_ms);
        }
        if (now_ms >= model->confirm_deadline_ms) {
            model->confirm_deadline_ms = 0;
            model->state = model->pre_confirm_state;
            model->pre_confirm_state = INCENSE_IDLE;
            return INCENSE_EVENT_CONFIRM_TIMEOUT;
        }
    }

    return INCENSE_EVENT_NONE;
}
