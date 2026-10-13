#include "SerialCmdLib.h"

// =============================================================================
// SerialCmdLib 实现
// =============================================================================

SerialCmdLib::SerialCmdLib() {
    _itemNum = 0;
    _queue = nullptr;
    memset(_items, 0, sizeof(_items));
    memset(_buf, 0, sizeof(_buf));
    _len = 0;
}

bool SerialCmdLib::begin() {
    _queue = xQueueCreate(CMD_QUEUE_SIZE, CMD_LINE_LEN);
    if (_queue == nullptr) {
        Serial.println("串口命令库: 队列创建失败");
        return false;
    }
    return true;
}

// 注册命令：cmd 统一转大写，回调不能为空
// ⚠️ 同一个字母注册两次会被前一条遮蔽（dispatch 只认第一个匹配），
//    所以这里直接拒绝重复注册并把冲突打到串口——不然会出现
//    “帮助里有这条命令、敲下去却走了另一条”的怪事。
bool SerialCmdLib::registerCommand(char cmd, const char *help,
                                   SerialCmdCallback callback) {
    if (callback == nullptr || _itemNum >= CMD_MAX_ITEMS) {
        return false;
    }
    char upper = (char)toupper((unsigned char)cmd);

    for (uint8_t i = 0; i < _itemNum; i++) {
        if (_items[i].cmd == upper) {
            Serial.printf("串口命令库: 命令 '%c' 重复注册, 已忽略后一条 [%s]\n",
                          upper, help != nullptr ? help : "");
            return false;
        }
    }

    _items[_itemNum].cmd = upper;
    _items[_itemNum].help = help;
    _items[_itemNum].callback = callback;
    _itemNum++;
    return true;
}

void SerialCmdLib::printHelp() {
    Serial.println("--- 串口命令 ---");
    for (uint8_t i = 0; i < _itemNum; i++) {
        Serial.printf("  %c = %s\n", _items[i].cmd,
                      _items[i].help != nullptr ? _items[i].help : "");
    }
    Serial.println("  (命令要按回车结尾; 带参数写成 命令,参数)");
}

// 带参数的投递：把整行拷进队列
bool SerialCmdLib::sendCommand(const char *line) {
    if (_queue == nullptr || line == nullptr) {
        return false;
    }
    uint8_t n = (uint8_t)strlen(line);
    if (n >= CMD_LINE_LEN) {
        n = CMD_LINE_LEN - 1;
    }
    char tmp[CMD_LINE_LEN];
    memset(tmp, 0, sizeof(tmp));
    memcpy(tmp, line, n);

    if (xQueueSend(_queue, tmp, 0) != pdPASS) {
        return false;  // 队列满就丢，不阻塞调用方
    }
    return true;
}

// 解析并执行一行命令
void SerialCmdLib::dispatch(const char *line) {
    if (line == nullptr || line[0] == '\0') {
        return;
    }

    char cmd = (char)toupper((unsigned char)line[0]);
    const char *arg = "";

    // 找第一个逗号，后面的都是参数
    for (uint8_t i = 1; line[i] != '\0'; i++) {
        if (line[i] == ',') {
            arg = &line[i + 1];
            break;
        }
    }

    for (uint8_t i = 0; i < _itemNum; i++) {
        if (_items[i].cmd == cmd) {
            _items[i].callback(arg);
            return;
        }
    }

    Serial.printf("未知命令 '%c' (输入 ? 看帮助)\n", cmd);
}

// 读串口：攒够一行（换行结尾）才 dispatch
void SerialCmdLib::poll() {
    while (Serial.available()) {
        char c = (char)Serial.read();

        if (c == '\n' || c == '\r') {
            if (_len > 0) {
                _buf[_len] = '\0';
                // 先入队再执行，保证和 sendCommand 走同一条路，顺序一致
                sendCommand(_buf);
                _len = 0;
            }
            continue;
        }

        if (_len < sizeof(_buf) - 1) {
            _buf[_len++] = c;
        } else {
            // 行太长：丢弃整行，等下一个换行
            _len = 0;
        }
    }
}

void SerialCmdLib::serialCmdTaskEntry(void *pvParameters) {
    SerialCmdLib *self = (SerialCmdLib *)pvParameters;
    char line[CMD_LINE_LEN];

    for (;;) {
        self->poll();

        // 从队列里取命令行执行（来自串口或其它任务）
        while (xQueueReceive(self->_queue, line, 0) == pdPASS) {
            self->dispatch(line);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

bool SerialCmdLib::startTask() {
    // 栈/优先级/核心都在 config.h 里（CFG_STACK_CMD / CFG_PRIO_CMD / CFG_CORE_CMD），
    // 不在这里写死数字，改配置只改 config.h。
    BaseType_t ret = xTaskCreatePinnedToCore(
        serialCmdTaskEntry, "serialcmd", CFG_STACK_CMD, this, CFG_PRIO_CMD,
        nullptr, CFG_CORE_CMD);
    if (ret != pdPASS) {
        Serial.println("串口命令库: 任务创建失败");
        return false;
    }
    return true;
}

const char *serialCmdLibVersion() {
    return SERIALCMDLIB_VERSION;
}
