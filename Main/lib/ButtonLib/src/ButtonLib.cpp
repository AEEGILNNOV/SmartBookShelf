#include "ButtonLib.h"

// =============================================================================
// ButtonLib 实现
// =============================================================================

// 按下/松开对应的标志表：index 0~4 -> {按下标志, 松开标志}
// index 3 = A 键(测厚起点)，index 4 = B 键(算厚度)，将来接取书/还书流程
static const uint8_t kPressFlags[ButtonLib::COUNT] = {
    BUTTON1_OPENFLAG, BUTTON2_OPENFLAG, BUTTON3_OPENFLAG, BUTTON4_OPENFLAG,
    BUTTON5_OPENFLAG
};
static const uint8_t kReleaseFlags[ButtonLib::COUNT] = {
    BUTTON1_CLOSEFLAG, BUTTON2_CLOSEFLAG, BUTTON3_CLOSEFLAG, BUTTON4_CLOSEFLAG,
    BUTTON5_CLOSEFLAG
};

ButtonLib::ButtonLib(int pin1, int pin2, int pin3, int pin4, int pin5) {
    _pins[0] = pin1;
    _pins[1] = pin2;
    _pins[2] = pin3;
    _pins[3] = pin4;
    _pins[4] = pin5;

    for (uint8_t i = 0; i < COUNT; i++) {
        _history[i] = 0x00;
        _lastFlag[i] = 0x00;
    }
    _queue = nullptr;
    _activeHigh = (BUTTON_TRUE_VALUE != 0);
}

bool ButtonLib::begin() {
    // 按下为高电平 -> 用内部下拉；按下为低电平 -> 用内部上拉
    uint8_t mode = _activeHigh ? INPUT_PULLDOWN : INPUT_PULLUP;
    for (uint8_t i = 0; i < COUNT; i++) {
        if (_pins[i] >= 0) {
            pinMode((uint8_t)_pins[i], mode);
        }
    }

    _queue = xQueueCreate(BUTTON_QUEUE_SIZE, sizeof(ButtonEvent));
    if (_queue == nullptr) {
        Serial.println("按键库: 队列创建失败");
        return false;
    }
    return true;
}

QueueHandle_t ButtonLib::queue() const {
    return _queue;
}

// 读一个引脚，返回“是否按下”
static bool readPressed(int pin, bool activeHigh) {
    if (pin < 0) {
        return false;
    }
    uint8_t level = (uint8_t)digitalRead(pin);
    bool high = (level == HIGH);
    return activeHigh ? high : !high;
}

// 消抖后如果状态变了，就发一个事件
void ButtonLib::emitIfChanged(uint8_t index) {
    if (_pins[index] < 0 || _queue == nullptr) {
        return;
    }

    // 低 BUTTON_DEBOUNCE_BITS 位全 1 = 连续按下；全 0 = 连续松开
    uint8_t stable = _history[index] & (uint8_t)((1U << BUTTON_DEBOUNCE_BITS) - 1U);
    uint8_t flag = 0x00;
    uint8_t pressed = 0;

    if (stable == 0x0F) {
        flag = kPressFlags[index];
        pressed = 1;
    } else if (stable == 0x00) {
        flag = kReleaseFlags[index];
        pressed = 0;
    } else {
        return;  // 还在跳变，不确定
    }

    if (flag == _lastFlag[index]) {
        return;  // 状态没变，不重复发
    }
    _lastFlag[index] = flag;

    ButtonEvent event;
    event.flag = flag;
    event.index = (uint8_t)(index + 1);
    event.pressed = pressed;
    event.ms = millis();

    // 队列满就丢，不阻塞按键任务
    xQueueSend(_queue, &event, 0);
}

void ButtonLib::poll() {
    for (uint8_t i = 0; i < COUNT; i++) {
        if (_pins[i] < 0) {
            continue;
        }
        // 移位历史消抖
        _history[i] <<= 1;
        _history[i] |= readPressed(_pins[i], _activeHigh) ? 0x01 : 0x00;
        emitIfChanged(i);
    }
}

bool ButtonLib::anyPressed() const {
    for (uint8_t i = 0; i < COUNT; i++) {
        if (readPressed(_pins[i], _activeHigh)) {
            return true;
        }
    }
    return false;
}

void ButtonLib::buttonTaskEntry(void *pvParameters) {
    ButtonLib *self = (ButtonLib *)pvParameters;
    for (;;) {
        self->poll();
        vTaskDelay(pdMS_TO_TICKS(BUTTON_SCAN_INTERVAL_MS));
    }
}

bool ButtonLib::startTask() {
    BaseType_t ret =
        xTaskCreatePinnedToCore(buttonTaskEntry, "button", CFG_STACK_BUTTON,
                                this, CFG_PRIO_BUTTON, nullptr,
                                CFG_CORE_BUTTON);
    if (ret != pdPASS) {
        Serial.println("按键库: 任务创建失败");
        return false;
    }
    return true;
}

const char *buttonLibVersion() {
    return BUTTONLIB_VERSION;
}
