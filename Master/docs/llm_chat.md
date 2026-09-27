# LLM 对话模块（llm_chat）

面向 ESP32-S3 的 DeepSeek 对话客户端。网络、重连、上下文管理全部封装在独立 FreeRTOS 任务里，
业务层只跟两个队列打交道。

---

## 1. 文件

| 文件 | 作用 |
| --- | --- |
| `include/llm_chat.h` | 公开 API、状态码、队列消息结构体、统计结构体 |
| `src/llm_chat.cpp` | WiFi 管理任务 + 对话任务 + 请求构建/解析 |
| `src/LLMconfig.h` | 模型、密钥、端点、上下文预算、超时重试、TLS、调试开关 |
| `src/main.cpp` | 串口命令行入口（本轮用来直接验证对话） |
| `src/WiFiconfig.h` | WiFi 名称/密码 |

---

## 2. 任务与数据流

```
                     llmSubmit(text)
                           |
                    [ 用户消息队列 ]  depth = LLM_USER_QUEUE_LEN (4)
                           |
   core0  prio3   +--------v---------+        [ 回复队列 ]  depth = LLM_REPLY_QUEUE_LEN (2)
   chatTask  ----> |  等待 WiFi 就绪   |             |
                   |  建 JSON 请求体   |             |   llmPollReply()
                   |  HTTPS POST      |             v
                   |  解析 choices[0] |      业务 / UI / 串口
                   +------------------+
                           |
                    historyPush(user/assistant)   ← 多轮上下文，上限 LLM_HISTORY_TURNS 轮

   core0  prio2   wifiTask: 断线自动重连（1s → 2s → … → 15s 退避）
```

- **chatTask 钉在 core 0**：核心 1 留给 Arduino `loop()`，所以 30 秒的 TLS 阻塞也不会卡住串口输入。
- **回复队列满时丢最旧的一条**，保证最新结果一定送得到。
- **只有连接类错误会重试**（`LLM_ERR_CONNECT`，以及 5xx）。401/402/429 这类直接返回，重试没意义。

---

## 3. API

```cpp
void  llmChatBegin();                        // setup() 里调用一次：建队列 + 起两个任务
bool  llmWifiConnected();                    // 是否已联网
bool  llmSubmit(const char *userText);       // 入队一条用户消息（非阻塞）
bool  llmPollReply(llm_reply_t *out, uint32_t timeoutMs);  // 取回复，0 = 不等待
void  llmClearHistory();                     // 清空多轮历史
void  llmSetSystemPrompt(const char *p);     // 换人设，NULL = 恢复默认
int   llmHistoryCount();
void  llmGetStats(llm_stats_t *out);
const char *llmStatusName(llm_status_t st);
```

回复结构：

```cpp
typedef struct {
    llm_status_t status;    // LLM_OK / LLM_ERR_*
    int          httpCode;  // HTTP 状态码，0 = 没拿到
    uint32_t     rttMs;     // 端到端耗时（含重试）
    uint8_t      attempts;  // 实际尝试次数
    char         text[LLM_REPLY_TEXT_MAX];  // 成功=正文，失败=人类可读错误
} llm_reply_t;
```

状态码：`LLM_OK`、`LLM_ERR_NOT_READY`（等 WiFi 超时）、`LLM_ERR_QUEUE_FULL`、
`LLM_ERR_CONNECT`（TCP/TLS）、`LLM_ERR_HTTP`（非 200）、`LLM_ERR_JSON`、
`LLM_ERR_API`（响应带 error 字段）、`LLM_ERR_EMPTY`（200 但 content 为空）。

---

## 4. 串口验证（本轮）

```bash
cd D:\Project\SmartBookShelf\Master
pio run -e esp32-s3-devkitc-1 -t upload
pio device monitor            # 115200
```

上电应看到：

```
 SmartBookShelf / Master  LLM Chat Console
model    : deepseek-flash
endpoint : https://api.deepseek.com/chat/completions
thinking : off
[LLM] ready: model=deepseek-flash endpoint=... budget=6000 chars turns=6
[LLM] WiFi up: ip=192.168.x.x rssi=-58 dBm
```

然后在 monitor 里直接打字回车：

```
> 用一句话介绍你自己
... 已发送，等待回复
[LLM] POST https://.../chat/completions  body=312 B  heap=...
[LLM] HTTP 200  payload=... B  heap=...
[LLM] tokens: prompt=xx completion=xx total=xx
[LLM] OK  1834 ms  42 B  hist=2

<<< assistant  (1834 ms, 第 1 次尝试)
我是运行在 ESP32-S3 上的嵌入式 AI 助手。
```

串口命令：

| 命令 | 作用 |
| --- | --- |
| `/help` | 帮助 |
| `/clear`、`/reset` | 清空多轮历史 |
| `/sys <提示词>` | 换系统提示词；`/sys` 单独用 = 恢复默认 |
| `/stat` | 联网状态、请求统计、内存、历史条数 |

**多轮验证**：先问「我叫小明」，再问「我叫什么」，第二问应能答出，且 `/stat` 里 history 递增。
输入 `/clear` 后再问「我叫什么」，应回答不知道。

---

## 5. 配置速查（src/LLMconfig.h）

| 宏 | 默认 | 说明 |
| --- | --- | --- |
| `LLM_MODEL` | `deepseek-flash` | 也可用 `deepseek-v4-pro` |
| `LLM_ENDPOINT` | `LLM_URL "/chat/completions"` | 想走代理就改这里 |
| `LLM_MAX_TOKENS` | 4096 | 单次回复上限 |
| `LLM_TEMPERATURE` | 0.8 | 仅 `LLM_THINKING=0` 时生效 |
| `LLM_THINKING` | 0 | 1 = 开启思维链（更准更慢，temperature 被忽略） |
| `LLM_SYSTEM_PROMPT` | 简洁助手 | 决定人设 |
| `LLM_HISTORY_TURNS` | 6 | 保留最近几轮 |
| `LLM_CONTEXT_BUDGET` | 6000 | 历史字符预算，超出丢最早的几轮 |
| `LLM_HTTP_TIMEOUT_MS` | 30000 | 单次请求超时 |
| `LLM_WIFI_WAIT_MS` | 15000 | 入队后等联网的上限 |
| `LLM_RETRY_COUNT` | 1 | 连接类错误重试次数 |
| `LLM_TLS_INSECURE` | 1 | 1 = 跳过证书校验 |
| `LLM_DEBUG` | 1 | 串口打印请求细节 |

容量类宏（`LLM_USER_TEXT_MAX` / `LLM_REPLY_TEXT_MAX` / 队列深度）在 `include/llm_chat.h`，
可用 `build_flags` 覆盖。

---

## 6. 排错表

| 现象 | 原因 / 处理 |
| --- | --- |
| `[NOT_READY]` | WiFi 没连上：核对 `WiFiconfig.h`、串口看 `WiFi down` 日志、路由器是否 2.4 GHz |
| `[API] invalid_api_key` / http=401 | `LLM_API_KEY` 不对或已撤销 |
| http=402 | 账户余额不足 |
| http=429 | 触发限流；调大 `LLM_RETRY_BACKOFF_MS` 或降并发 |
| `[CONNECT] POST failed: connection refused` | DNS 失败或网络受限；改用 IP/代理端点 |
| `[JSON]` | 响应不是 JSON（多为网关错误页），看串口打印的 160 字节预览 |
| `[EMPTY]` | `finish_reason=length`：回复被 `LLM_MAX_TOKENS` 截断，调大上限或让它简短些 |
| 请求成功但中文乱码 | 串口工具编码设为 UTF-8；`monitor_speed` 115200 |
| 内存不足 / TLS 失败 | 确认 `board_build.arduino.memory_type = qio_opi` 且 PSRAM 正常 |

---

## 7. 设计取舍

- **非流式**：一次拿完整 JSON，解析简单、内存可控。后续要做「逐字上屏」再切 SSE `stream=true`。
- **`reasoning_content` 不入上下文**：按官方文档，未带 `tools` 参数时思维链不参与多轮拼接，
  因此只记日志、不存历史，省内存。
- **`setInsecure()` 默认开**：省掉根证书与时钟校验的麻烦。要上生产就设 `LLM_TLS_INSECURE=0`
  并定义 `LLM_ROOT_CA`。
- **密钥仍写在 `LLMconfig.h`**：目前仓库没有 git，若之后要提交，建议把密钥挪到被忽略的
  `src/secrets.h` 再由 `LLMconfig.h` 兜底包含。

---

## 8. 后续可接的点

1. 屏幕 UI：把 `llm_reply_t.text` 接到 LVGL 文本框，`llmSubmit()` 接输入法/语音识别结果。
2. 流式输出：`stream=true` + SSE 解析，边收边刷屏。
3. 视觉：`deepseek-flash` 支持图片输入，摄像头抓拍 → base64 上传，可做「拍书问书」。
4. 工具调用：把书架状态（在架图书、超声波测厚）做成 `tools`，让模型直接查/控制。
