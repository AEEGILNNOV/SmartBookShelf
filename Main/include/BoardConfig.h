#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

// =============================================================================
// SmartBookShelf / Main —— BoardConfig.h（兼容壳，内容已全部搬到 config.h）
//
// 以前引脚和板级参数都在这个文件里；现在**唯一配置来源是 include/config.h**，
// 本文件只保留一行 include，作用有两个：
//   1. 老代码里的 #include "BoardConfig.h" 继续有效，不用一次性改所有库；
//   2. 新代码请直接 #include "config.h"，不要再往这个文件里加东西。
//
// 改配置 -> 请打开 Main/include/config.h
// =============================================================================

#include "config.h"

#endif
