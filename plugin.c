#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/mman.h>
#include <orbis/libkernel.h>
#include <orbis/Pad.h>

#define STEERING_DEADZONE   4
#define STEERING_GAIN       1.65f
#define PEDAL_THRESHOLD     12

typedef int (*scePadReadState_t)(int32_t handle, OrbisPadData *data);
static scePadReadState_t orig_scePadReadState = NULL;

static uint8_t orig_bytes[16];
static void *target_func_addr = NULL;
static int hook_installed = 0;

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

static inline uint8_t processSteering(uint8_t rawX) {
    int offset = (int)rawX - 128;
    if (abs(offset) < STEERING_DEADZONE) return 128;
    int scaled = 128 + (int)(offset * STEERING_GAIN);
    if (scaled > 255) scaled = 255;
    if (scaled < 0)   scaled = 0;
    return (uint8_t)scaled;
}

static void applyG29Remapping(OrbisPadData *data) {
    if (!data || !data->connected) return;
    data->leftStick.x = processSteering(data->leftStick.x);
    if (abs((int)data->leftStick.y - 128) < 15) data->leftStick.y = 128;

    if (data->analogButtons.r2 > PEDAL_THRESHOLD) data->buttons |= ORBIS_PAD_BUTTON_R2;
    else data->buttons &= ~ORBIS_PAD_BUTTON_R2;

    if (data->analogButtons.l2 > PEDAL_THRESHOLD) data->buttons |= ORBIS_PAD_BUTTON_L2;
    else data->buttons &= ~ORBIS_PAD_BUTTON_L2;
}

int hooked_scePadReadState(int32_t handle, OrbisPadData *data) {
    memcpy(target_func_addr, orig_bytes, 14);
    int ret = orig_scePadReadState(handle, data);

    uint8_t jump[14] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0 };
    *(uint64_t *)(jump + 6) = (uint64_t)hooked_scePadReadState;
    memcpy(target_func_addr, jump, 14);

    if (ret == 0 && data != NULL) applyG29Remapping(data);
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
        sendNotification("G29 Wheel Active! Enjoy GTA V.");
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
