#ifndef SYS_INFO_LIB_H
#define SYS_INFO_LIB_H

#include <Arduino.h>

// =============================================================================
// SysInfoLib —— 系统信息采集库
//
// 职责：
//   1) 采集堆 / PSRAM / Flash / 运行时间，填进 SystemInfo 结构体；
//   2) 把数值格式化成固定宽度字符串，直接给显示屏或串口用。
//
// 用法（典型：一个采集任务 + 一个打印/显示任务）：
//   SystemInfo info;
//   sysInfoCollect(info);           // 采集
//   SystemInfo out;
//   sysInfoDiff(last, info, out);   // 算出相对上次的变化量（可选）
//   sysInfoPrint(info);             // 串口打印
//
// 这个库不建任务、不加锁：调用方自己决定在哪个任务里采集、用互斥锁保护共享。
// =============================================================================

// 系统信息快照
struct SystemInfo {
    uint32_t chipCores;
    uint32_t cpuFreqMhz;

    uint32_t heapTotalKb;
    uint32_t heapFreeKb;
    uint32_t ramUsedKb;
    uint32_t ramUsedPct;

    uint32_t psramTotalKb;
    uint32_t psramFreeKb;
    uint32_t psramUsedKb;
    uint32_t psramUsedPct;
    bool psramFound;

    uint32_t flashUsedKb;
    uint32_t flashTotalKb;
    uint32_t flashUsedPct;

    uint32_t uptimeSeconds;
};

// 采集一次。会清零 out 再填，所以可以直接传未初始化的结构体。
void sysInfoCollect(SystemInfo &out);

// 计算两次快照的差值（用于看一段时间内堆的涨落，找内存泄漏）
void sysInfoDiff(const SystemInfo &before, const SystemInfo &after,
                 SystemInfo &out);

// 串口打印（标题用中文，值用统一单位）
void sysInfoPrint(const SystemInfo &info);

// 百分比计算，用 uint64_t 防溢出（total=0 时返回 0）
uint32_t sysInfoPercent(uint32_t used, uint32_t total);

// ---- 格式化：写进调用方给的缓冲区，不返回 String，省堆 ----
// "12.3/320.0KB (4%)" 这种一行式
void sysInfoFormatRam(const SystemInfo &info, char *buf, size_t len);
void sysInfoFormatPsram(const SystemInfo &info, char *buf, size_t len);
void sysInfoFormatFlash(const SystemInfo &info, char *buf, size_t len);

// "HH:MM:SS"
void sysInfoFormatUptime(const SystemInfo &info, char *buf, size_t len);

// 以 KB 为单位写 "1234KB"；超过 1024KB 自动写 "1.2MB"
void sysInfoFormatKb(uint32_t kb, char *buf, size_t len);

// 本库版本字符串
const char *sysInfoVersion();

#endif
