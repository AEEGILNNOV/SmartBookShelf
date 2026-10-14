#include "HuskyCam.h"

// =============================================================================
// 算法表
// 只挑和“图书识别”相关的内置 CNN 算法。想加算法就在这里加一行，
// 顺便加一个没被占用的字母当串口按键。
//   物体识别/物体分类/自学习分类：给书封面、书脊分类用
//   OCR 文字识别：认书名（本项目主用）
//   视觉标签/条码/二维码：认书上的码
//   人脸/手部/姿态识别：调试二哈本身是否正常
// =============================================================================
const HuskyAlgoInfo HUSKY_ALGO_TABLE[] = {
    {'1', ALGORITHM_OBJECT_RECOGNITION, "物体识别"},
    {'2', ALGORITHM_OBJECT_CLASSIFICATION, "物体分类"},
    {'3', ALGORITHM_SELF_LEARNING_CLASSIFICATION, "自学习分类"},
    {'4', ALGORITHM_OCR_RECOGNITION, "OCR 文字识别"},
    {'5', ALGORITHM_TAG_RECOGNITION, "视觉标签识别"},
    {'6', ALGORITHM_BARCODE_RECOGNITION, "条形码识别"},
    {'7', ALGORITHM_QRCODE_RECOGNITION, "二维码识别"},
    {'8', ALGORITHM_FACE_RECOGNITION, "人脸识别"},
};

const uint8_t HUSKY_ALGO_COUNT =
    sizeof(HUSKY_ALGO_TABLE) / sizeof(HUSKY_ALGO_TABLE[0]);

// 开机默认用哪个算法（在算法表里的下标），0 下标是物体识别。
// 本项目认书名，所以默认 OCR，在表里是第 4 项（下标 3）。
static const uint8_t kDefaultAlgoIndex = 3;

// 二哈的 I2C 地址，固定 0x50（库内部也是这个值）
static const uint8_t kHuskyI2cAddr = 0x50;

// -----------------------------------------------------------------------------
// 构造
// -----------------------------------------------------------------------------
HuskyCam::HuskyCam() {
    _algo = HUSKY_ALGO_TABLE[kDefaultAlgoIndex].algo;
    _ok = false;
}

// -----------------------------------------------------------------------------
// 初始化 I2C 总线。只做总线设置，不碰二哈（二哈不在也不会阻塞）。
// -----------------------------------------------------------------------------
void HuskyCam::begin(int sda, int scl) {
    Wire.begin(sda, scl);
    // 关键：ESP32 Wire 默认事务超时只有 50ms，二哈处理图像会拉伸时钟，
    // 默认值下协议包会随机失败。
    Wire.setTimeOut(HUSKY_I2C_TIMEOUT_MS);
    _husky.retry = HUSKY_RETRY;
    _husky.timeOutDuration = HUSKY_TIMEOUT_MS;
    _ok = false;
}

// -----------------------------------------------------------------------------
// 快速探测：二哈 I2C 地址是否应答。所有库调用之前必须先走这一步。
// -----------------------------------------------------------------------------
bool HuskyCam::probe() {
    Wire.beginTransmission(kHuskyI2cAddr);
    if (Wire.endTransmission() == 0) {
        return true;
    }
    _ok = false;
    return false;
}

// -----------------------------------------------------------------------------
// 连接：探测 -> begin -> 切算法。
// 切算法要加载 CNN 模型，耗时较长而且经常会失败，所以重试几次。
// -----------------------------------------------------------------------------
bool HuskyCam::connect() {
    if (_ok) {
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

    bool sw = false;
    for (uint8_t i = 0; i < 5 && !sw; i++) {
        sw = _husky.switchAlgorithm(_algo);
        if (!sw) {
            Serial.printf("  切换算法失败, 重试 %u/5 ...\n", (unsigned)(i + 1));
            delay(600);
        }
    }
    if (!sw) {
        Serial.println("  切换算法始终失败(二哈可能正忙或停在菜单里)");
        return false;
    }

    _ok = true;
    // 官方示例切完算法要等 5 秒再读。CNN 模型加载需要时间，太早读读不到。
    delay(4000);
    return true;
}

bool HuskyCam::connected() const {
    return _ok;
}

void HuskyCam::markOffline() {
    _ok = false;
}

eAlgorithm_t HuskyCam::algorithm() const {
    return _algo;
}

void HuskyCam::setAlgorithm(eAlgorithm_t algo) {
    if (_algo != algo) {
        _algo = algo;
        _ok = false;  // 换算法后要重新 connect 一次
    }
}

void HuskyCam::setRetry(uint8_t retry) {
    _husky.retry = retry;
}

// -----------------------------------------------------------------------------
// 读一帧结果
// 返回：>0 读到 wordNum 个结果（返回值没有别的含义，用 wordNum 判断个数）
//       -1 通信超时（真故障）
// 注意库里 getResult() 的参数是非常量引用，所以要传变量进去。
// -----------------------------------------------------------------------------
int16_t HuskyCam::readWords(HuskyWord *words, uint8_t maxNum, uint8_t &wordNum) {
    wordNum = 0;
    if (words == nullptr || maxNum == 0) {
        return -1;
    }
    if (!probe()) {
        return -1;
    }

    eAlgorithm_t algo = _algo;
    int8_t n = _husky.getResult(algo);
    if (n < 0) {
        return -1;
    }

    int16_t cached = _husky.getCachedResultNum(_algo);
    if (cached <= 0) {
        return 0;
    }

    // 库在 ESP32 上最多缓存 MAX_RESULT_NUM(10) 个结果，别读越界
    if (cached > (int16_t)MAX_RESULT_NUM) {
        cached = (int16_t)MAX_RESULT_NUM;
    }
    uint8_t num = (uint8_t)cached;
    if (num > maxNum) {
        num = maxNum;
    }

    for (uint8_t i = 0; i < num; i++) {
        Result *res = _husky.getCachedResultByIndex(_algo, (int16_t)i);
        if (res == nullptr) {
            continue;
        }

        HuskyWord *out = &words[wordNum];
        out->id = (int16_t)res->ID;  // 必须用 ID，classID 是 xCenter 的别名
        out->level = (int16_t)res->level;
        out->x = (int16_t)res->xCenter;
        out->y = (int16_t)res->yCenter;
        out->w = (int16_t)res->width;
        out->h = (int16_t)res->height;

        memset(out->name, 0, sizeof(out->name));
        memset(out->content, 0, sizeof(out->content));
        snprintf(out->name, sizeof(out->name), "%s", res->name.c_str());
        snprintf(out->content, sizeof(out->content), "%s", res->content.c_str());

        wordNum++;
    }
    return (int16_t)wordNum;
}

int16_t HuskyCam::cachedResultNum() {
    return _husky.getCachedResultNum(_algo);
}

int16_t HuskyCam::cachedLearnedNum() {
    return _husky.getCachedResultLearnedNum(_algo);
}

int16_t HuskyCam::cachedMaxId() {
    return _husky.getCachedResultMaxID(_algo);
}

// -----------------------------------------------------------------------------
// 学习当前画面中心的目标（CNN 提特征入库），返回新 ID，0 = 失败
// -----------------------------------------------------------------------------
uint8_t HuskyCam::learn() {
    if (!probe()) {
        _ok = false;
        return 0;
    }
    return _husky.learn(_algo);
}

bool HuskyCam::forget() {
    if (!probe()) {
        _ok = false;
        return false;
    }
    return _husky.forget(_algo);
}

bool HuskyCam::saveKnowledge(uint8_t slot) {
    if (!probe()) {
        _ok = false;
        return false;
    }
    return _husky.saveKnowledges(_algo, slot);
}

bool HuskyCam::loadKnowledge(uint8_t slot) {
    if (!probe()) {
        _ok = false;
        return false;
    }
    return _husky.loadKnowledges(_algo, slot);
}

String HuskyCam::getAlgoParam(const char *key) {
    if (!probe()) {
        _ok = false;
        return String("");
    }
    return _husky.getAlgoParamString(_algo, String(key));
}
