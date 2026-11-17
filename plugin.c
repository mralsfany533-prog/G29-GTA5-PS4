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
// 1. إعدادات القيادة الاحترافية لدركسون Logitech G29
// ============================================================================
#define STEERING_DEADZONE    3       // منطقة ميتة دقيقة لمنع اهتزاز السيارة
#define STEERING_SENSITIVITY 1.45f   // معامل الاستجابة الأساسي
#define STEERING_CURVE       1.25f   // نعومة المنتصف وسرعة الزوايا الحادة
#define PEDAL_DEADZONE       10      // حساسية بداية دعسة البنزين والفرامل

typedef int (*scePadReadState_t)(int32_t handle, OrbisPadData *data);
static scePadReadState_t orig_scePadReadState = NULL;

static uint8_t orig_bytes[16];
static void *target_func_addr = NULL;
static int hook_installed = 0;

// متغيرات نظام مثبت السرعة (Cruise Control)
static int cruise_active = 0;
static int l3_was_pressed = 0;

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

// ============================================================================
// 2. معادلة الالتفاف المتدرج (تحاكي مود Manual Transmission على PC)
// ============================================================================
static inline uint8_t processAdvancedSteering(uint8_t rawX) {
    int offset = (int)rawX - 128;
    if (abs(offset) <= STEERING_DEADZONE) {
        return 128;
    }

    // تحويل القيمة لنطاق [-1.0 إلى 1.0]
    float norm = (float)offset / 127.0f;
    float sign = (norm < 0.0f) ? -1.0f : 1.0f;
    float abs_norm = fabsf(norm);

    // تطبيق منحنى الاستجابة المتدرج + التضخيم
    float curved = powf(abs_norm, STEERING_CURVE) * STEERING_SENSITIVITY;
    if (curved > 1.0f) curved = 1.0f;

    int final_val = 128 + (int)(sign * curved * 127.0f);
    if (final_val > 255) final_val = 255;
    if (final_val < 0)   final_val = 0;

    return (uint8_t)final_val;
}

// ============================================================================
// 3. تطبيق كافة تحسينات القيادة والدواسات والأزرار
// ============================================================================
static void applyProDrivingEnhancements(OrbisPadData *data) {
    if (!data || !data->connected) return;

    // 1. الالتفاف المتدرج الناعم
    data->leftStick.x = processAdvancedSteering(data->leftStick.x);

    // 2. تثبيت المحور العمودي للعصا اليسرى لمنع اهتزاز السيارة/الدراجة
    if (abs((int)data->leftStick.y - 128) < 20) {
        data->leftStick.y = 128;
    }

    // 3. منع اهتزاز الكاميرا (Right Stick) بالخطأ
    if (abs((int)data->rightStick.x - 128) < 12) data->rightStick.x = 128;
    if (abs((int)data->rightStick.y - 128) < 12) data->rightStick.y = 128;

    // 4. نظام مثبت السرعة (زر L3 لتفعيل/إلغاء، والفرامل تفصله تلقائياً)
    int l3_now = (data->buttons & ORBIS_PAD_BUTTON_L3) ? 1 : 0;
    if (l3_now && !l3_was_pressed) {
        cruise_active = !cruise_active;
        sendNotification(cruise_active ? "Cruise Control: ON" : "Cruise Control: OFF");
    }
    l3_was_pressed = l3_now;

    // فصل مثبت السرعة فور الضغط على الفرامل
    if (data->analogButtons.l2 > PEDAL_DEADZONE) {
        cruise_active = 0;
    }

    // 5. معالجة دواسة البنزين (R2) والفرامل (L2)
    if (cruise_active) {
        data->analogButtons.r2 = 190; // سرعة ثابتة متوسطة مريحة
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

    // 6. تسهيل الدريفت: الشفرة اليسرى (L1) تعمل كفرامل يدوية سريعة الاستجابة
    if (data->buttons & ORBIS_PAD_BUTTON_L1) {
        data->buttons |= ORBIS_PAD_BUTTON_R1;
    }
}

// ============================================================================
// 4. الدالة البديلة وحقن الذاكرة
// ============================================================================
int hooked_scePadReadState(int32_t handle, OrbisPadData *data) {
    memcpy(target_func_addr, orig_bytes, 14);
    int ret = orig_scePadReadState(handle, data);

    uint8_t jump[14] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0 };
    *(uint64_t *)(jump + 6) = (uint64_t)hooked_scePadReadState;
    memcpy(target_func_addr, jump, 14);

    if (ret == 0 && data != NULL) {
        applyProDrivingEnhancements(data);
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
        sendNotification("G29 Pro Mode Active! (L3 = Cruise)");
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
