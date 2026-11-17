#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <pthread.h>
#include <sys/mman.h>
#include <orbis/libkernel.h>
#include <orbis/Pad.h>

// ============================================================================
// إعدادات القيادة والقائمة التفاعلية لدركسون Logitech G29
// ============================================================================
#define STEERING_DEADZONE    3
#define PEDAL_DEADZONE       10

typedef int (*scePadReadState_t)(int32_t handle, OrbisPadData *data);
static scePadReadState_t orig_scePadReadState = NULL;

static uint8_t orig_bytes[16];
static void *target_func_addr = NULL;
static int hook_installed = 0;

// متغيرات القائمة التفاعلية والتحكم
static float current_sensitivity = 1.45f;
static int cruise_active = 0;
static int l3_was_pressed = 0;
static int dpad_up_was_pressed = 0;
static int dpad_down_was_pressed = 0;

static void sendNotification(const char *msg) {
    OrbisNotificationRequest req;
    memset(&req, 0, sizeof(OrbisNotificationRequest));
    req.type = 0;
    req.unk3 = 0;
    req.useIconImageUri = 0;
    req.targetId = -1;
    strncpy(req.message, msg, sizeof(req.message) - 1);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

// دالة الالتفاف المتدرج بناءً على الحساسية الحالية
static inline uint8_t processAdvancedSteering(uint8_t rawX) {
    int offset = (int)rawX - 128;
    if (abs(offset) <= STEERING_DEADZONE) {
        return 128;
    }

    float norm = (float)offset / 127.0f;
    float sign = (norm < 0.0f) ? -1.0f : 1.0f;
    float abs_norm = fabsf(norm);

    float curved = powf(abs_norm, 1.25f) * current_sensitivity;
    if (curved > 1.0f) curved = 1.0f;

    int final_val = 128 + (int)(sign * curved * 127.0f);
    if (final_val > 255) final_val = 255;
    if (final_val < 0)   final_val = 0;

    return (uint8_t)final_val;
}

// ============================================================================
// نظام القائمة التفاعلية والتحكم المباشر بالدركسون
// ============================================================================
static void applyInteractiveMenuAndDriving(OrbisPadData *data) {
    if (!data || !data->connected) return;

    // 1. التحكم بالحساسية عبر أزرار التوجيه (D-Pad Up / Down)
    int dpad_up = (data->buttons & ORBIS_PAD_BUTTON_UP) ? 1 : 0;
    int dpad_down = (data->buttons & ORBIS_PAD_BUTTON_DOWN) ? 1 : 0;

    char msg[64];

    if (dpad_up && !dpad_up_was_pressed) {
        current_sensitivity += 0.15f;
        if (current_sensitivity > 2.5f) current_sensitivity = 2.5f;
        snprintf(msg, sizeof(msg), "G29 Sens: %.2f (UP)", current_sensitivity);
        sendNotification(msg);
    }
    dpad_up_was_pressed = dpad_up;

    if (dpad_down && !dpad_down_was_pressed) {
        current_sensitivity -= 0.15f;
        if (current_sensitivity < 0.8f) current_sensitivity = 0.8f;
        snprintf(msg, sizeof(msg), "G29 Sens: %.2f (DOWN)", current_sensitivity);
        sendNotification(msg);
    }
    dpad_down_was_pressed = dpad_down;

    // 2. تطبيق الالتفاف
    data->leftStick.x = processAdvancedSteering(data->leftStick.x);

    // 3. تثبيت المحور العمودي
    if (abs((int)data->leftStick.y - 128) < 20) data->leftStick.y = 128;

    // 4. مثبت السرعة (زر L3)
    int l3_now = (data->buttons & ORBIS_PAD_BUTTON_L3) ? 1 : 0;
    if (l3_now && !l3_was_pressed) {
        cruise_active = !cruise_active;
        sendNotification(cruise_active ? "Cruise Control: ON" : "Cruise Control: OFF");
    }
    l3_was_pressed = l3_now;

    if (data->analogButtons.l2 > PEDAL_DEADZONE) {
        cruise_active = 0;
    }

    if (cruise_active) {
        data->analogButtons.r2 = 190;
        data->buttons |= ORBIS_PAD_BUTTON_R2;
    } else if (data->analogButtons.r2 > PEDAL_DEADZONE) {
        data->buttons |= ORBIS_PAD_BUTTON_R2;
    } else {
        data->buttons &= ~ORBIS_PAD_BUTTON_R2;
    }

    if (data->analogButtons.l2 > PEDAL_DEADZONE) {
        data->buttons |= ORBIS_PAD_BUTTON_L2;
    } else {
        data->buttons &= ~ORBIS_PAD_BUTTON_L2;
    }

    // 5. الشفرة اليسرى (L1) للفرامل اليدوية السريعة (دريفت)
    if (data->buttons & ORBIS_PAD_BUTTON_L1) {
        data->buttons |= ORBIS_PAD_BUTTON_R1;
    }
}

int hooked_scePadReadState(int32_t handle, OrbisPadData *data) {
    memcpy(target_func_addr, orig_bytes, 14);
    int ret = orig_scePadReadState(handle, data);

    uint8_t jump[14] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0 };
    *(uint64_t *)(jump + 6) = (uint64_t)hooked_scePadReadState;
    memcpy(target_func_addr, jump, 14);

    if (ret == 0 && data != NULL) {
        applyInteractiveMenuAndDriving(data);
    }

    return ret;
}

static int installPadHook(void) {
    if (hook_installed) return 0;

    int padLibHandle = sceKernelLoadStartModule("libScePad.sprx", 0, NULL, 0, NULL, NULL);
    if (padLibHandle < 0) return -1;

    if (sceKernelDlsym(padLibHandle, "scePadReadState", &target_func_addr) < 0 || !target_func_addr) {
        return -2;
    }

    orig_scePadReadState = (scePadReadState_t)target_func_addr;

    uintptr_t page_start = (uintptr_t)target_func_addr & ~(uintptr_t)0x3FFF;
    if (mprotect((void *)page_start, 0x4000, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        return -3;
    }

    memcpy(orig_bytes, target_func_addr, 14);
    uint8_t jump[14] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0 };
    *(uint64_t *)(jump + 6) = (uint64_t)hooked_scePadReadState;
    memcpy(target_func_addr, jump, 14);

    hook_installed = 1;
    return 0;
}

static void *init_thread(void *arg) {
    sceKernelUsleep(1000000);
    int status = installPadHook();
    if (status == 0) {
        sendNotification("G29 Menu Active! D-Pad Up/Down to adjust Sens.");
    } else {
        char err[64];
        snprintf(err, sizeof(err), "G29 Hook Error: %d", status);
        sendNotification(err);
    }
    return NULL;
}

__attribute__((constructor))
static void on_plugin_load(void) {
    pthread_t th;
    pthread_create(&th, NULL, init_thread, NULL);
    pthread_detach(th);
}

__attribute__((destructor))
static void on_plugin_unload(void) {
    if (hook_installed && target_func_addr && orig_bytes[0] != 0) {
        memcpy(target_func_addr, orig_bytes, 14);
        hook_installed = 0;
    }
}
