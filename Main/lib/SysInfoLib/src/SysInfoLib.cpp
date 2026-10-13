#include "SysInfoLib.h"

#include "BoardConfig.h"

// =============================================================================
// SysInfoLib 实现
// =============================================================================

// 百分比计算，用 uint64_t 防溢出
uint32_t sysInfoPercent(uint32_t used, uint32_t total) {
    if (total == 0) {
        return 0;
    }
    return (uint32_t)(((uint64_t)used * 100U) / total);
}

// 采集系统信息
void sysInfoCollect(SystemInfo &out) {
    // 清零，避免漏填的字段带出脏数据
    memset(&out, 0, sizeof(out));

    out.chipCores = ESP.getChipCores();
    out.cpuFreqMhz = ESP.getCpuFreqMHz();

    uint32_t heapTotal = ESP.getHeapSize();
    uint32_t heapFree = ESP.getFreeHeap();
    out.heapTotalKb = heapTotal / 1024;
    out.heapFreeKb = heapFree / 1024;
    out.ramUsedKb = (heapTotal - heapFree) / 1024;
    out.ramUsedPct = sysInfoPercent(heapTotal - heapFree, heapTotal);

    // PSRAM 可能不存在，必须先判断
    out.psramFound = psramFound();
    if (out.psramFound) {
        uint32_t psramTotal = ESP.getPsramSize();
        uint32_t psramFree = ESP.getFreePsram();
        out.psramTotalKb = psramTotal / 1024;
        out.psramFreeKb = psramFree / 1024;
        out.psramUsedKb = (psramTotal - psramFree) / 1024;
        out.psramUsedPct = sysInfoPercent(psramTotal - psramFree, psramTotal);
    }

    uint32_t sketchUsed = ESP.getSketchSize();
    uint32_t sketchFree = ESP.getFreeSketchSpace();
    out.flashUsedKb = sketchUsed / 1024;
    out.flashTotalKb = (sketchUsed + sketchFree) / 1024;
    out.flashUsedPct = sysInfoPercent(sketchUsed, sketchUsed + sketchFree);

    out.uptimeSeconds = millis() / 1000UL;
}

// 求两次快照的差值。uptimeSeconds 保留 after 的值，不做差。
void sysInfoDiff(const SystemInfo &before, const SystemInfo &after,
                 SystemInfo &out) {
    memset(&out, 0, sizeof(out));

    out.chipCores = after.chipCores;
    out.cpuFreqMhz = after.cpuFreqMhz;

    out.heapTotalKb = after.heapTotalKb;
    out.heapFreeKb = after.heapFreeKb;
    out.heapTotalKb = after.heapTotalKb;
    out.ramUsedKb = after.ramUsedKb - before.ramUsedKb;
    out.ramUsedPct = after.ramUsedPct;

    out.psramFound = after.psramFound;
    out.psramTotalKb = after.psramTotalKb;
    out.psramFreeKb = after.psramFreeKb;
    out.psramUsedKb = after.psramUsedKb - before.psramUsedKb;
    out.psramUsedPct = after.psramUsedPct;

    out.flashUsedKb = after.flashUsedKb - before.flashUsedKb;
    out.flashTotalKb = after.flashTotalKb;
    out.flashUsedPct = after.flashUsedPct;

    out.uptimeSeconds = after.uptimeSeconds;
}

// 串口打印
void sysInfoPrint(const SystemInfo &info) {
    Serial.println("=== ESP32-S3 N16R8 系统信息 ===");
    Serial.printf("芯片: %u 核  %u MHz\n", (unsigned)info.chipCores,
                  (unsigned)info.cpuFreqMhz);

    Serial.printf("RAM: %u/%u KB (%u%%)\n", (unsigned)info.ramUsedKb,
                  (unsigned)info.heapTotalKb, (unsigned)info.ramUsedPct);

    if (info.psramFound) {
        Serial.printf("PSRAM: %u/%u KB (%u%%)\n", (unsigned)info.psramUsedKb,
                      (unsigned)info.psramTotalKb, (unsigned)info.psramUsedPct);
    } else {
        Serial.println(
            "PSRAM: 未找到(检查 board_build.arduino.memory_type = qio_opi)");
    }

    Serial.printf("Flash: %u/%u KB (%u%%)\n", (unsigned)info.flashUsedKb,
                  (unsigned)info.flashTotalKb, (unsigned)info.flashUsedPct);

    char uptime[16];
    sysInfoFormatUptime(info, uptime, sizeof(uptime));
    Serial.printf("运行时间: %s\n", uptime);
}

// -----------------------------------------------------------------------------
// 格式化
// -----------------------------------------------------------------------------

// 以 KB 为单位格式化，超过 1024KB 自动换成 MB，保留一位小数
void sysInfoFormatKb(uint32_t kb, char *buf, size_t len) {
    if (buf == nullptr || len == 0) {
        return;
    }
    if (kb >= 1024U) {
        snprintf(buf, len, "%u.%uMB", (unsigned)(kb / 1024U),
                 (unsigned)((kb % 1024U) * 10U / 1024U));
    } else {
        snprintf(buf, len, "%uKB", (unsigned)kb);
    }
}

void sysInfoFormatRam(const SystemInfo &info, char *buf, size_t len) {
    if (buf == nullptr || len == 0) {
        return;
    }
    snprintf(buf, len, "%u/%uKB (%u%%)", (unsigned)info.ramUsedKb,
             (unsigned)info.heapTotalKb, (unsigned)info.ramUsedPct);
}

void sysInfoFormatPsram(const SystemInfo &info, char *buf, size_t len) {
    if (buf == nullptr || len == 0) {
        return;
    }
    if (!info.psramFound) {
        snprintf(buf, len, "N/A");
        return;
    }
    char used[16];
    char total[16];
    sysInfoFormatKb(info.psramUsedKb, used, sizeof(used));
    sysInfoFormatKb(info.psramTotalKb, total, sizeof(total));
    snprintf(buf, len, "%s/%s (%u%%)", used, total,
             (unsigned)info.psramUsedPct);
}

void sysInfoFormatFlash(const SystemInfo &info, char *buf, size_t len) {
    if (buf == nullptr || len == 0) {
        return;
    }
    char used[16];
    char total[16];
    sysInfoFormatKb(info.flashUsedKb, used, sizeof(used));
    sysInfoFormatKb(info.flashTotalKb, total, sizeof(total));
    snprintf(buf, len, "%s/%s (%u%%)", used, total,
             (unsigned)info.flashUsedPct);
}

void sysInfoFormatUptime(const SystemInfo &info, char *buf, size_t len) {
    if (buf == nullptr || len == 0) {
        return;
    }
    uint32_t s = info.uptimeSeconds;
    snprintf(buf, len, "%02u:%02u:%02u", (unsigned)(s / 3600U),
             (unsigned)((s / 60U) % 60U), (unsigned)(s % 60U));
}

const char *sysInfoVersion() {
    return SYSINFOLIB_VERSION;
}
