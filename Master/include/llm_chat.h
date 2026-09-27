#ifndef LLM_CHAT_H
#define LLM_CHAT_H

/**
 * llm_chat —— ESP32-S3 上的大模型对话客户端（DeepSeek / OpenAI 兼容接口）
 *
 * 设计要点：
 *   - 全部网络操作跑在独立的 FreeRTOS 任务里，调用方只跟两个队列打交道，不阻塞业务；
 *   - WiFi 断线由内部任务自动重连，调用方只需要看 llmWifiConnected()；
 *   - 任务内部维护多轮对话历史（最近 LLM_HISTORY_TURNS 轮），自动按字符预算裁剪；
 *   - 任何失败都会以 llm_reply_t.status + 文本错误码的形式回给调用方，绝不静默丢失。
 *
 * 典型用法：
 *     llmChatBegin();                       // setup() 里调用一次
 *     llmSubmit("这本书讲什么？");           // 需要回答时入队
 *     llm_reply_t r;
 *     if (llmPollReply(&r, 0)) { ...使用 r.text... }
 */

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

/* ---------------- 容量参数（可在调用前用 -D 覆盖） ---------------- */
#ifndef LLM_USER_TEXT_MAX
#define LLM_USER_TEXT_MAX 512          // 单条用户消息最大字节数（UTF-8）
#endif
#ifndef LLM_REPLY_TEXT_MAX
#define LLM_REPLY_TEXT_MAX 2048        // 单条回复最大字节数（UTF-8）
#endif
#ifndef LLM_USER_QUEUE_LEN
#define LLM_USER_QUEUE_LEN 4           // 待发送消息队列深度
#endif
#ifndef LLM_REPLY_QUEUE_LEN
#define LLM_REPLY_QUEUE_LEN 2          // 待取回复队列深度
#endif

/* ---------------- 状态码 ---------------- */
typedef enum {
    LLM_OK = 0,             // 成功
    LLM_ERR_NOT_READY,      // 等 WiFi 超时 / 未联网
    LLM_ERR_QUEUE_FULL,     // 发送队列满（当前由 llmSubmit 返回 false 表达，此码备用）
    LLM_ERR_CONNECT,        // TCP/TLS 建连失败或请求中断
    LLM_ERR_HTTP,           // 服务器返回非 200（见 httpCode / text）
    LLM_ERR_JSON,           // 响应不是合法 JSON
    LLM_ERR_API,            // 响应里有 error 字段（鉴权/额度/参数错误等）
    LLM_ERR_EMPTY,          // 200 但 content 为空
} llm_status_t;

/* ---------------- 队列消息 ---------------- */
typedef struct {
    char text[LLM_USER_TEXT_MAX];
} llm_user_msg_t;

typedef struct {
    llm_status_t status;                  // 本次请求结果
    int          httpCode;                // HTTP 状态码，0 表示没拿到
    uint32_t     rttMs;                   // 端到端耗时（含重试）
    uint8_t      attempts;                // 实际尝试次数
    char         text[LLM_REPLY_TEXT_MAX]; // 成功时为回复正文，失败时为错误描述
} llm_reply_t;

typedef struct {
    bool     wifiConnected;
    int8_t   rssi;
    uint32_t requests;      // 累计请求次数
    uint32_t okCount;       // 成功次数
    uint32_t errCount;      // 失败次数
    uint32_t lastRttMs;     // 最近一次耗时
    int      lastHttpCode;  // 最近一次 HTTP 状态码
    uint32_t heapFree;      // 内部 RAM 剩余
    uint32_t psramFree;     // PSRAM 剩余
} llm_stats_t;

/* ---------------- 生命周期 ---------------- */
/** 建立队列、启动 WiFi 任务与对话任务。必须在 WiFi.begin 之前/之后都行，只需调用一次。 */
void llmChatBegin();

/* ---------------- 发送 / 接收 ---------------- */
/** 用户是否已联网（拿到 IP）。 */
bool llmWifiConnected();

/** 把一条用户消息放进发送队列（不阻塞，队列满返回 false）。 */
bool llmSubmit(const char *userText);

/**
 * 取一条回复。
 * @param timeoutMs 0 = 不等待；portMAX_DELAY 也可以传。
 * @return 取到返回 true，超时返回 false。
 */
bool llmPollReply(llm_reply_t *out, uint32_t timeoutMs);

/* ---------------- 对话上下文控制 ---------------- */
/** 清空多轮历史（下一条消息将是一次全新对话）。线程安全。 */
void llmClearHistory();

/** 修改系统提示词（传 NULL 恢复 LLM_SYSTEM_PROMPT）。线程安全，下一条消息生效。 */
void llmSetSystemPrompt(const char *prompt);

/** 当前历史条数（user + assistant 合计）。 */
int  llmHistoryCount();

/* ---------------- 辅助 ---------------- */
const char *llmStatusName(llm_status_t status);
void llmGetStats(llm_stats_t *out);

#endif // LLM_CHAT_H
