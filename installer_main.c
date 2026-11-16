#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <orbis/libkernel.h>
#include <orbis/Sysmodule.h>

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

void copyFile(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return; }

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        fwrite(buf, 1, n, out);
    }
    fclose(in);
    fclose(out);
}

int main(void) {
    // التأكد من وجود مجلد الإضافات في GoldHEN
    mkdir("/data/GoldHEN", 0777);
    mkdir("/data/GoldHEN/plugins", 0777);

    // 1. نسخ ملف الإضافة من مجلد التطبيق إلى مجلد إضافات GoldHEN
    copyFile("/app0/G29_GTA5.prx", "/data/GoldHEN/plugins/G29_GTA5.prx");

    // 2. تسجيل الإضافة تلقائياً لنسخ لعبة GTA V (الأمريكية والأوروبية)
    FILE *ini = fopen("/data/GoldHEN/plugins.ini", "a");
    if (ini) {
        fprintf(ini, "\n[CUSA00419]\n/data/GoldHEN/plugins/G29_GTA5.prx\n");
        fprintf(ini, "\n[CUSA00411]\n/data/GoldHEN/plugins/G29_GTA5.prx\n");
        fclose(ini);
    }

    // 3. إرسال إشعار على شاشة PS4 بنجاح التثبيت
    sendNotification("G29 GTA5 Plugin Installed! Restart Game.");
    return 0;
}
