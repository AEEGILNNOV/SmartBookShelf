#include "HuskyLib.h"

// =============================================================================
// 算法（模式）表
// 只挑和“图书识别”相关的内置 CNN 算法。想加算法就在这里加一行，
// 顺便给一个没被占用的字母当串口按键。
// =============================================================================
const HuskyModeInfo HUSKY_MODE_TABLE[] = {
    {'1', ALGORITHM_OBJECT_RECOGNITION, "物体识别"},
    {'2', ALGORITHM_OBJECT_CLASSIFICATION, "物体分类"},
    {'3', ALGORITHM_SELF_LEARNING_CLASSIFICATION, "自学习分类"},
    {'4', ALGORITHM_OCR_RECOGNITION, "OCR 文字识别"},
    {'5', ALGORITHM_TAG_RECOGNITION, "视觉标签识别"},
    {'6', ALGORITHM_BARCODE_RECOGNITION, "条形码识别"},
    {'7', ALGORITHM_QRCODE_RECOGNITION, "二维码识别"},
    {'8', ALGORITHM_FACE_RECOGNITION, "人脸识别"},
};

const uint8_t HUSKY_MODE_COUNT =
    sizeof(HUSKY_MODE_TABLE) / sizeof(HUSKY_MODE_TABLE[0]);

// 默认模式：OCR 文字识别（本项目认书名用这个）。找不到就退回第 0 项。
static const eAlgorithm_t kDefaultAlgo = ALGORITHM_OCR_RECOGNITION;

// =============================================================================
// 构造 / 初始化
// =============================================================================
HuskyLib::HuskyLib() {
    _modeIndex = 0;
    for (uint8_t i = 0; i < HUSKY_MODE_COUNT; i++) {
        if (HUSKY_MODE_TABLE[i].algo == kDefaultAlgo) {
            _modeIndex = (int16_t)i;
            break;
        }
    }
    _online = false;
    _lastReadMs = 0;
    _frames = 0;
    _errors = 0;
    _recovers = 0;
}

void HuskyLib::begin(int sda, int scl) {
    Wire.begin(sda, scl);
    // 关键：默认事务超时只有 50ms，二哈拉伸时钟会导致协议包随机失败
    Wire.setTimeOut(HUSKY_I2C_TIMEOUT_MS);

    _husky.retry = HUSKY_RETRY;
    _husky.timeOutDuration = HUSKY_TIMEOUT_MS;
    _online = false;
}

bool HuskyLib::probe() {
    Wire.beginTransmission(HUSKY_I2C_ADDR);
    if (Wire.endTransmission() == 0) {
        return true;
    }
    _online = false;
    return false;
}

eAlgorithm_t HuskyLib::currentAlgo() const {
    return HUSKY_MODE_TABLE[_modeIndex].algo;
}

// 探测 -> begin -> 切算法。切算法要加载 CNN 模型，很慢且经常失败，重试几次。
bool HuskyLib::connect() {
    if (_online) {
        return true;
    }
    if (!probe()) {
        return false;
    }

    if (!_husky.begin(Wire)) {
        Serial.println("  husky.begin() 失败");
        return false;
    }
    delay(300);  // 给二哈一点握手时间

    bool switched = false;
    for (uint8_t i = 0; i < 5 && !switched; i++) {
        switched = _husky.switchAlgorithm(currentAlgo());
        if (!switched) {
            Serial.printf("  切换算法失败, 重试 %u/5 ...\n", (unsigned)(i + 1));
            delay(600);
        }
    }
    if (!switched) {
        Serial.println("  切换算法始终失败(二哈可能正忙或停在菜单里)");
        return false;
    }

    _online = true;
    _recovers++;
    // CNN 模型加载需要时间，太早读会读不到
    delay(HUSKY_ALGO_SETTLE_MS);

    // 连接后把帧计数重置成“该读了”，避免立刻又等一个周期
    _lastReadMs = millis() - HUSKY_READ_INTERVAL_MS;
    return true;
}

void HuskyLib::markOffline() {
    _online = false;
}

bool HuskyLib::online() const {
    return _online;
}

uint8_t HuskyLib::modeCount() const {
    return HUSKY_MODE_COUNT;
}

int16_t HuskyLib::modeIndex() const {
    return _modeIndex;
}

const char *HuskyLib::modeName() const {
    if (_modeIndex < 0 || _modeIndex >= (int16_t)HUSKY_MODE_COUNT) {
        return "未知";
    }
    return HUSKY_MODE_TABLE[_modeIndex].name;
}

bool HuskyLib::setModeByIndex(int16_t index) {
    if (index < 0 || index >= (int16_t)HUSKY_MODE_COUNT) {
        return false;
    }
    if (index == _modeIndex) {
        return false;
    }
    _modeIndex = index;
    // 换算法后必须重连（二哈要重新加载模型）
    _online = false;
    return true;
}

bool HuskyLib::setModeByAlpha(char alpha) {
    for (uint8_t i = 0; i < HUSKY_MODE_COUNT; i++) {
        if (HUSKY_MODE_TABLE[i].alpha == alpha) {
            return setModeByIndex((int16_t)i);
        }
    }
    return false;
}

bool HuskyLib::setModeNext() {
    int16_t next = _modeIndex + 1;
    if (next >= (int16_t)HUSKY_MODE_COUNT) {
        next = 0;
    }
    return setModeByIndex(next);
}

// -----------------------------------------------------------------------------
// 读一帧
// 返回 >=0：读到（wordNum 可能为 0，表示画面里没目标）
// 返回 -1 ：通信超时（真故障，调用方累计错误后考虑重连）
// -----------------------------------------------------------------------------
int16_t HuskyLib::readFrame(HuskyFrame &frame) {
    // 先清零，避免上一帧的数据残留
    memset(&frame, 0, sizeof(frame));
    frame.modeIndex = _modeIndex;

    if (!probe()) {
        frame.online = false;
        frame.frameCount = _frames;
        frame.errorCount = _errors;
        frame.recoverCount = _recovers;
        return -1;
    }

    // 库里 getResult() 的参数是非常量引用，所以要传变量进去
    eAlgorithm_t algo = currentAlgo();
    int8_t ret = _husky.getResult(algo);
    if (ret < 0) {
        _errors++;
        frame.online = _online;
        frame.frameCount = _frames;
        frame.errorCount = _errors;
        frame.recoverCount = _recovers;
        return -1;
    }

    _frames++;

    int16_t cached = _husky.getCachedResultNum(currentAlgo());
    if (cached < 0) {
        cached = 0;
    }
    // 二哈库在 ESP32 上最多缓存 MAX_RESULT_NUM(10) 个，别读越界
    if (cached > (int16_t)MAX_RESULT_NUM) {
        cached = (int16_t)MAX_RESULT_NUM;
    }
    if (cached > (int16_t)HUSKY_WORD_MAX) {
        cached = (int16_t)HUSKY_WORD_MAX;
    }

    int16_t num = 0;
    for (int16_t i = 0; i < cached; i++) {
        Result *res = _husky.getCachedResultByIndex(currentAlgo(), i);
        if (res == nullptr) {
            continue;
        }

        HuskyWord *out = &frame.words[num];
        // 注意：res->classID 与 res->xCenter 是同一个槽位（union），
        //       要拿目标 ID 必须用 res->ID
        out->id = (int16_t)res->ID;
        out->level = (int16_t)res->level;
        out->x = (int16_t)res->xCenter;
        out->y = (int16_t)res->yCenter;
        out->w = (int16_t)res->width;
        out->h = (int16_t)res->height;
        snprintf(out->name, sizeof(out->name), "%s", res->name.c_str());
        snprintf(out->content, sizeof(out->content), "%s",
                 res->content.c_str());
        num++;
    }

    frame.online = true;
    frame.wordNum = num;
    frame.resultNum = _husky.getCachedResultNum(currentAlgo());
    frame.learnedNum = _husky.getCachedResultLearnedNum(currentAlgo());
    frame.maxId = _husky.getCachedResultMaxID(currentAlgo());
    frame.frameCount = _frames;
    frame.errorCount = _errors;
    frame.recoverCount = _recovers;
    frame.lastFrameMs = millis();
    _lastReadMs = frame.lastFrameMs;

    return num;
}

// -----------------------------------------------------------------------------
// 学习 / 清空 / 知识库
// -----------------------------------------------------------------------------
uint8_t HuskyLib::learn() {
    if (!probe()) {
        return 0;
    }
    return _husky.learn(currentAlgo());
}

bool HuskyLib::forget() {
    if (!probe()) {
        return false;
    }
    return _husky.forget(currentAlgo());
}

bool HuskyLib::saveKnowledge(uint8_t slot) {
    if (!probe()) {
        return false;
    }
    return _husky.saveKnowledges(currentAlgo(), slot);
}

bool HuskyLib::loadKnowledge(uint8_t slot) {
    if (!probe()) {
        return false;
    }
    return _husky.loadKnowledges(currentAlgo(), slot);
}

// -----------------------------------------------------------------------------
// 调试
// -----------------------------------------------------------------------------
String HuskyLib::getAlgoParam(const char *key) {
    if (!probe()) {
        return String("");
    }
    return _husky.getAlgoParamString(currentAlgo(), String(key));
}

void HuskyLib::dumpAlgoParam() {
    if (!probe()) {
        Serial.println("未连接, 读不到算法参数");
        return;
    }
    static const char *keys[] = {"lang",     "language", "dict_type", "text_type",
                                 "ocr_lang", "show_name", "det_thres",
                                 "ocr_type"};
    Serial.printf("算法参数(%s):\n", modeName());
    for (uint8_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        String s = _husky.getAlgoParamString(currentAlgo(), String(keys[i]));
        Serial.printf("  %-10s = '%s'\n", keys[i], s.c_str());
        delay(20);  // 别把总线打满
    }
}

const char *huskyLibVersion() {
    return HUSKYLIB_VERSION;
}

// 打印当前状态：连接 / 算法 / 累计统计
void HuskyLib::printState() {
    Serial.println("=== 二哈 HUSKYLENS 2 状态 ===");
    Serial.printf("连接: %s\n", _online ? "在线" : "未连接");

    Serial.printf("算法: %s (编号 %d/%u)\n", modeName(), (int)_modeIndex,
                  (unsigned)HUSKY_MODE_COUNT);
    Serial.print("可选算法: ");
    for (uint8_t i = 0; i < HUSKY_MODE_COUNT; i++) {
        Serial.printf("%c=%s  ", HUSKY_MODE_TABLE[i].alpha,
                      HUSKY_MODE_TABLE[i].name);
    }
    Serial.println();

    Serial.printf("累计: 帧=%lu 通信错误=%lu 重连=%lu\n",
                  (unsigned long)_frames, (unsigned long)_errors,
                  (unsigned long)_recovers);

    if (_online) {
        Serial.printf("当前画面: 识别=%d 其中已学习=%d, 设备最大ID=%d\n",
                      (int)_husky.getCachedResultNum(currentAlgo()),
                      (int)_husky.getCachedResultLearnedNum(currentAlgo()),
                      (int)_husky.getCachedResultMaxID(currentAlgo()));
    }
    Serial.println("提示: OCR 只识别十字准心(画面中央)那个文字块的文字;");
    Serial.println("      未学习过的文字 ID=0, 按 l 学习, 按 w 存知识库。");
}
