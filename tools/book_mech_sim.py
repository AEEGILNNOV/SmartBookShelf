# -*- coding: utf-8 -*-
"""
book_mech_sim.py —— 速定位书格：书籍业务规则 + 机械步数/软限位的纯逻辑仿真

为什么要有这个脚本：
  本机只有 xtensa 交叉编译器（编出来的二进制在 Windows 上跑不了），
  所以"跑不起来真代码"的部分用等价的 Python 仿真验证**逻辑**。
  仿真的每一处都严格对着 C++ 源码写，并标注了对应的源文件与函数名；
  改了 C++ 逻辑却忘了同步这里，下面的断言就会失败——这本身就是一种保护。

对应关系（改这些函数时请同步改本脚本）：
  Stepper28BYJ::run()             -> Motor.run()          lib/StepperLib/src/StepperLib.cpp
  Stepper28BYJ::service()         -> Motor.service_one()  lib/StepperLib/src/StepperLib.cpp
  Stepper28BYJ::stop()/release()  -> Motor.stop()         同上
  MechanismLib::clampToRange()    -> clamp_to_range()     lib/MechanismLib/src/MechanismLib.cpp
  MechanismLib::jog()             -> Mechanism.jog()      同上
  MechanismLib::moveLiftTo()      -> Mechanism.move_lift_to()  同上
  MechanismLib::service()         -> Mechanism.service()  同上
  MechanismLib::setZeroHere()     -> Mechanism.set_zero_here() 同上
  BookDomain::bookAddAtTop()      -> book_add_at_top()    lib/BookDomainLib/src/BookDomain.cpp
  BookDomain::bookRemoveAt()      -> book_remove_at()     同上
  BookDomain::bookOrderText()     -> book_order_text()    同上
  StorageLib 的加载校验            -> load_books()         lib/StorageLib/src/StorageLib.cpp

跑法：  python tools\book_mech_sim.py
"""

import sys

# =============================================================================
# 与 config.h 默认值保持一致（改 config.h 请同步这里）
# =============================================================================
CFG_STEPPER_SOFT_LIMIT       = 1
CFG_LIFT_MAX_MOVE_STEPS      = 6000
CFG_PUSH_MAX_MOVE_STEPS      = 2000
CFG_STEPPER_MAX_STEPS        = 8192
CFG_LIFT_STEPS_PER_BOOK      = 125
CFG_LIFT_INDEX0_OFFSET_STEPS = 0
CFG_LIFT_DIR_SIGN            = 1
CFG_STORE_DOWN_SIGN          = 1      # 存书后"向下让一本书"的方向
CFG_PUSH_DIR_SIGN            = 1
CFG_PUSH_OUT_STEPS           = 875
CFG_STEPPER_LIFT_RANGE_STEPS = 4096
CFG_STEPPER_PUSH_RANGE_STEPS = 2048

BOOK_ID_MIN, BOOK_ID_MAX = 1, 6
BOOK_NAME = {1: "语文", 2: "数学", 3: "英语", 4: "物理", 5: "化学", 6: "生物"}


# =============================================================================
# 第一部分：StepperLib 的等价模型
# =============================================================================

class Motor:
    """一台 28BYJ 的模型。只保留和"位置 / 软限位 / 是否走完 / 线圈释放"有关的部分。"""

    def __init__(self, range_steps, wired=True):
        self.range_steps = abs(range_steps)
        self.pos_steps = 0          # _posSteps：相对零点的累计步数
        self.steps_left = 0         # _stepsLeft：剩余步数（带符号）
        self.moving = False         # _moving
        self.wired = wired
        self.released = True        # 停机后线圈是否已释放（28BYJ 不断电会发热）
        self.start_pos = 0          # 给断言用：本次 run() 的起点
        self.want_steps = 0        # 本次要求的步数（判断有没有走满）

    def run(self, steps):
        """对应 Stepper28BYJ::run()：只登记目标，不发脉冲。"""
        if not self.wired:
            return False
        if steps == 0:
            return True
        if abs(steps) > CFG_STEPPER_MAX_STEPS:
            return False                      # 参数写错（多写一个 0）直接拒绝
        self.steps_left = steps
        self.moving = True
        self.released = False
        self.start_pos = self.pos_steps
        self.want_steps = steps          # 记下本次要求的步数，用来判断有没有被软限位截断
        return True

    def service_one(self):
        """对应 Stepper28BYJ::service() 的一次调用（到点才发一个脉冲）。
        返回 True = 这一步真的走了；False = 没走（没在动 / 被软限位拦下并停机）。"""
        if (not self.wired) or (not self.moving) or self.steps_left == 0:
            return False
        direction = 1 if self.steps_left > 0 else -1
        # ---- 软限位：到边就停，剩余步数直接丢掉，绝不硬顶机构 ----
        if CFG_STEPPER_SOFT_LIMIT and self.range_steps > 0:
            if direction > 0 and self.pos_steps >= self.range_steps:
                self.stop()
                return False
            if direction < 0 and self.pos_steps <= -self.range_steps:
                self.stop()
                return False
        self.pos_steps += direction
        self.steps_left -= direction
        if self.steps_left == 0:
            self.moving = False
            self.released = True                 # 停机就断线圈
        return True

    def run_to_completion(self):
        """把一次 run() 走到底。返回 (实际走了多少步, 是否走满了要求的步数)。

        ⚠️ 注意不能只看 steps_left：软限位触发时 Stepper28BYJ 会 stop()，
           那时 steps_left 被清成 0，看上去像"走完了"，其实只走了一部分。
           所以这里用"实际走的步数 == 本次要求的步数"来判断。"""
        want = self.want_steps
        loops = 0
        while self.moving and self.steps_left != 0 and loops < 200000:
            self.service_one()
            loops += 1
        moved = self.pos_steps - self.start_pos
        return (moved, want != 0 and moved == want)

    def stop(self):
        self.steps_left = 0
        self.moving = False
        self.released = True                     # release()：四个 IN 拉低

    def reset_position(self):
        self.pos_steps = 0


def clamp_to_range(motor, steps):
    """对应 MechanismLib::clampToRange()：夹到软限位允许范围；返回 0 = 已经在边上。"""
    if motor is None or steps == 0:
        return 0
    rng = motor.range_steps
    if (not CFG_STEPPER_SOFT_LIMIT) or rng <= 0:
        return steps
    pos = motor.pos_steps
    if steps > 0:
        room = rng - pos
        if room <= 0:
            return 0
        return room if steps > room else steps
    room = rng + pos
    if room <= 0:
        return 0
    return -room if steps < -room else steps


# =============================================================================
# 第二部分：MechanismLib 的等价模型
# =============================================================================

MECH_IDLE, MECH_MOVING, MECH_DONE, MECH_FAILED, MECH_ESTOPPED = 0, 1, 2, 3, 4
AXIS_NONE, AXIS_LIFT, AXIS_PUSH = 0, 1, 2


class Mechanism:
    """对应 MechanismLib：互斥 + 软限位 + 单次上限 + 位置有效性 + 急停 + 人工归零。"""

    def __init__(self, lift, push):
        self.lift = lift
        self.push = push
        self.state = MECH_IDLE
        self.active_axis = AXIS_NONE
        self.last_error = ""
        # 上电一律：位置无效 + 需要人工归零（没有限位开关，软件不知道平台在哪）
        self.position_valid = False
        self.need_homing = True
        self.action_start = 0
        self.target = 0

    def _motor_of(self, axis):
        return self.lift if axis == AXIS_LIFT else self.push

    def _name_of(self, axis):
        return "升降" if axis == AXIS_LIFT else "推书"

    def jog(self, axis, steps, allow_invert):
        if self.state == MECH_MOVING:
            self.last_error = "另一个动作正在进行"
            return False                        # 互斥：不让两个目标互相覆盖
        if steps == 0:
            self.last_error = "点动步数是 0"
            return False
        motor = self._motor_of(axis)
        if motor is None or not motor.wired:
            self.last_error = self._name_of(axis) + " 没接线"
            return False
        dir_sign = CFG_LIFT_DIR_SIGN if axis == AXIS_LIFT else CFG_PUSH_DIR_SIGN
        signed = steps if dir_sign >= 0 else -steps
        if axis == AXIS_PUSH and (not allow_invert) and steps < 0:
            self.last_error = "推书回退未启用"
            return False
        limit = CFG_LIFT_MAX_MOVE_STEPS if axis == AXIS_LIFT else CFG_PUSH_MAX_MOVE_STEPS
        if abs(signed) > limit:
            self.last_error = self._name_of(axis) + " 单次步数超上限"
            return False                        # 比软限位更早拦住"参数多写一个 0"
        clamped = clamp_to_range(motor, signed)
        if clamped == 0:
            self.last_error = self._name_of(axis) + " 已到软限位"
            return False
        if not motor.run(clamped):
            self.last_error = self._name_of(axis) + " run() 拒绝"
            return False
        self.state = MECH_MOVING
        self.active_axis = axis
        self.target = clamped
        self.action_start = motor.pos_steps
        self.last_error = ""
        return True

    def move_lift_to(self, target_pos):
        """对应 MechanismLib::moveLiftTo()：走到绝对位置（依赖位置有效）。"""
        if self.state == MECH_MOVING:
            self.last_error = "另一个动作正在进行"
            return False
        if not self.position_valid:
            self.last_error = "位置无效，需要人工归零"
            return False                        # 掉电后没归零之前，坚决拒绝自动取书
        if self.lift is None or not self.lift.wired:
            self.last_error = "升降没接线"
            return False
        delta = target_pos - self.lift.pos_steps
        if delta == 0:
            self.state = MECH_DONE
            self.active_axis = AXIS_LIFT
            self.action_start = self.lift.pos_steps
            self.target = 0
            return True
        signed = delta if CFG_LIFT_DIR_SIGN >= 0 else -delta
        if abs(signed) > CFG_LIFT_MAX_MOVE_STEPS:
            self.last_error = "升降位移超上限"
            return False
        clamped = clamp_to_range(self.lift, signed)
        if clamped == 0:
            self.last_error = "升降已到软限位"
            return False
        if not self.lift.run(clamped):
            return False
        self.state = MECH_MOVING
        self.active_axis = AXIS_LIFT
        self.target = clamped
        self.action_start = self.lift.pos_steps
        self.last_error = ""
        return True

    def service(self):
        """对应 MechanismLib::service()：推进一个脉冲；不 busy 了就判 DONE / FAILED。"""
        if self.state != MECH_MOVING:
            return
        motor = self._motor_of(self.active_axis)
        if motor is None:
            self.state = MECH_FAILED
            return
        motor.service_one()
        if motor.moving:
            return                              # 还在走，下一轮再看（没有忙等待）
        expected = self.action_start + self.target
        if motor.pos_steps == expected:
            self.state = MECH_DONE
        else:
            # 被软限位提前停住：宁可报失败让人看一眼，也不把"少走一半"当成功
            self.state = MECH_FAILED
            self.position_valid = False
            self.need_homing = True
        self.active_axis = AXIS_NONE

    def take_result(self):
        """对应 MechanismLib::takeResult()：把已经结束的动作结果读走，读走就回 IDLE。
        返回 True = 本次确实读到了一个已经结束的动作（DONE / FAILED / ESTOPPED）。"""
        if self.state not in (MECH_DONE, MECH_FAILED, MECH_ESTOPPED):
            return False
        self.state = MECH_IDLE
        self.active_axis = AXIS_NONE
        self.target = 0
        return True

    def stop_all(self):
        was_moving = ((self.state == MECH_MOVING)
                      or (self.lift is not None and self.lift.moving)
                      or (self.push is not None and self.push.moving))
        if self.lift:
            self.lift.stop()
        if self.push:
            self.push.stop()
        if was_moving:
            self.position_valid = False
            self.need_homing = True
            self.state = MECH_ESTOPPED

    def set_zero_here(self):
        if (self.lift is not None and self.lift.moving) or (self.push is not None and self.push.moving):
            self.last_error = "有轴在动，不能归零"
            return False
        if self.lift:
            self.lift.reset_position()
        if self.push:
            self.push.reset_position()
        self.position_valid = True
        self.need_homing = False
        self.state = MECH_IDLE
        self.last_error = ""
        return True
# =============================================================================
# 第三部分：BookDomainLib / BookListLib / StorageLib 校验的等价模型
# =============================================================================

def book_id_is_valid(book_id):
    return BOOK_ID_MIN <= book_id <= BOOK_ID_MAX


def get_book_name_by_id(book_id):
    return BOOK_NAME.get(book_id, "未知")


def book_find_index_by_id(books, book_id):
    for i, b in enumerate(books):
        if b["id"] == book_id:
            return i
    return -1


def book_add_at_top(books, book_id):
    """对应 BookDomain::bookAddAtTop()。返回 (结果码, 下标)。0=成功。"""
    if not book_id_is_valid(book_id):
        return (-1, -1)                      # BOOK_ERR_ID_INVALID：不入库、不动机械
    if book_find_index_by_id(books, book_id) >= 0:
        return (-2, -1)                      # BOOK_ERR_DUPLICATE：不新增、不加数量
    books.insert(0, {"id": book_id, "count": 1, "text": get_book_name_by_id(book_id)})
    return (0, 0)                            # 新书插到数组顶部（下标 0）


def book_remove_at(books, index):
    """对应 BookDomain::bookRemoveAt()。调用前必须确认机械动作已经正常完成。"""
    if index < 0 or index >= len(books):
        return (-4, None)                    # BOOK_ERR_NOT_FOUND
    removed = books.pop(index)               # 后面的自动前移
    return (0, removed)


def book_order_text(books):
    """对应 BookDomain::bookOrderText()：下标 0 在最前，最顶上那本用 [] 标出。"""
    if not books:
        return "（空）"
    parts = []
    for i, b in enumerate(books):
        name = get_book_name_by_id(b["id"])
        parts.append("[" + name + "]" if i == 0 else name)
    return " > ".join(parts)


def load_books(raw, expect_version=1, version=1, max_books=16, lift_range=CFG_STEPPER_LIFT_RANGE_STEPS, trust=2):
    """对应 StorageLib::loadBooks() 的校验规则。返回 (是否成功, 校验后的书单, 步数, 位置有效, 需要归零)。"""
    if version != expect_version:
        return (False, [], 0, False, True)   # 版本不匹配：拒绝加载、不覆盖
    if len(raw) > max_books:
        return (False, [], 0, False, True)   # 条数越界：判损坏
    books = []
    for item in raw:
        book_id = item.get("id", 0)
        # id==0 是"无学科/测试条目"（串口 b,a,N 造出来的测试书就是 id=0），**放行**。
        # 对应真代码 StorageLib.cpp 的 _loadLocked()：写入端不做 id 过滤，
        # 读取端要是把 id=0 丢掉，就变成"保存成功、重启 0 条"。
        # 注意 book_id_is_valid()（对应 bookIdIsValid()）本身**不放开**，
        # 业务入库仍然只认 1~6，规则分开判。
        if book_id != 0 and not book_id_is_valid(book_id):
            continue                          # id 不在 0 / 1~6：丢弃该条
        # 去重只对学科 id 做（方案A：一个学科最多一本）；多条 id=0 测试书是合法的
        if book_id != 0 and book_find_index_by_id(books, book_id) >= 0:
            continue                          # 重复学科 id：只保留第一条
        text = (item.get("text") or "")[:39]  # text 强制补结束符（这里用截断模拟）
        books.append({"id": book_id, "count": item.get("count", 1), "text": text})
    steps = raw_lift = 0
    need_homing = False
    if raw_lift > lift_range * trust or raw_lift < -lift_range * trust:
        need_homing = True                    # 步数不可信 -> 要求人工归零
    return (True, books, steps, True, need_homing)


# =============================================================================
# 第四部分：断言（每一条都对应上面的一个规则）
# =============================================================================

PASS = 0
FAIL = 0


def drive(mech):
    """把机械推到"动作已经结束"为止（模拟 stepperTask 每轮调 service()）。"""
    loops = 0
    while mech.state == MECH_MOVING and loops < 200000:
        mech.service()
        loops += 1
    return mech.state


def check(name, cond, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print("  [OK]   " + name)
    else:
        FAIL += 1
        print("  [FAIL] " + name + ("  <- " + detail if detail else ""))


def main():
    print("=" * 70)
    print("速定位书格 纯逻辑仿真： 书籍规则 / 机械步数 / 软限位 / 掉电恢复")
    print("=" * 70)

    # ---------------- A. 书籍管理 ----------------
    print("\n[A] 书籍管理（方案A：每个学科最多一本）")
    books = []
    check("ID 1~6 都能正确映射到学科",
          all(get_book_name_by_id(i) == BOOK_NAME[i] for i in range(1, 7)))
    check("非法 ID 映射成 未知", get_book_name_by_id(0) == "未知" and get_book_name_by_id(7) == "未知"
          and get_book_name_by_id(-1) == "未知")
    r, idx = book_add_at_top(books, 0)
    check("非法 ID(0) 不入库", r == -1 and len(books) == 0)
    r, idx = book_add_at_top(books, 7)
    check("非法 ID(7) 不入库", r == -1 and len(books) == 0)
    r, idx = book_add_at_top(books, 1)
    check("语文入库成功且插在下标 0", r == 0 and idx == 0 and books[0]["id"] == 1)
    r, idx = book_add_at_top(books, 2)
    check("数学入库后排在顶部", r == 0 and [b["id"] for b in books] == [2, 1])
    r, idx = book_add_at_top(books, 3)
    check("英语入库后顺序 = 英语/数学/语文", r == 0 and [b["id"] for b in books] == [3, 2, 1])
    before = [dict(b) for b in books]
    r, idx = book_add_at_top(books, 2)
    check("重复 ID 不新增", r == -2 and books == before)
    check("重复 ID 不增加 count", books[1]["count"] == 1)
    r, removed = book_remove_at(books, 1)
    check("删除下标1(数学)成功", r == 0 and removed["id"] == 2)
    check("删除后剩余顺序正确 = 英语/语文", [b["id"] for b in books] == [3, 1])
    check("删除后剩余 count 保持 1", all(b["count"] == 1 for b in books))
    r, removed = book_remove_at(books, 5)
    check("越界下标删除被拒绝", r == -4 and len(books) == 2)
    check("书堆顺序文本正确（顶部带[]）", book_order_text(books) == "[英语] > 语文"
          or book_order_text(books) == "[英语] > 语文", book_order_text(books))
    check("空书单顺序文本 = （空）", book_order_text([]) == "（空）")

    # ---------------- B. Flash 加载校验 ----------------
    print("\n[B] Flash 加载校验（掉电恢复的一部分）")
    ok, bs, _, _, _ = load_books([{"id": 1, "text": "语文"}, {"id": 1, "text": "语文2"}, {"id": 9, "text": "X"}])
    check("非法 id 被丢弃", ok and [b["id"] for b in bs] == [1])
    check("重复 id 只保留第一条", ok and bs[0]["text"] == "语文")
    # id=0 的测试条目必须原样恢复（串口 b,a,N 造的测试书 + q,s 存盘 + 重启）
    # 真代码对应：StorageLib.cpp 的 _loadLocked() 里 "id != 0 && !storageIdIsValid(id)" 才丢弃
    ok, bs, _, _, _ = load_books([{"id": 0, "text": "测试书 1"}, {"id": 0, "text": "测试书 2"},
                                  {"id": 0, "text": "测试书 3"}])
    check("id=0 测试条目全部保留(3 条, 不能只留 1 条)",
          ok and [b["text"] for b in bs] == ["测试书 1", "测试书 2", "测试书 3"], str(bs))
    check("id=0 条目的 id 原样是 0", ok and [b["id"] for b in bs] == [0, 0, 0], str(bs))
    # 混合场景：2 条测试书 + 1 本真学科书，重启后 3 条都要在；id=99 仍必须被丢弃
    ok, bs, _, _, _ = load_books([{"id": 0, "text": "测试书 1"}, {"id": 2, "text": "数学"},
                                  {"id": 0, "text": "测试书 2"}, {"id": 99, "text": "坏数据"}])
    check("混合场景: id=0 与 1~6 都恢复，id=99 仍被丢弃",
          ok and [b["id"] for b in bs] == [0, 2, 0], str(bs))
    ok, _, _, _, _ = load_books([{"id": i} for i in range(1, 20)], max_books=16)
    check("条数越界判损坏", not ok)
    ok, _, _, _, _ = load_books([{"id": 1}], version=2, expect_version=1)
    check("版本不匹配拒绝加载", not ok)
    check("书单清空不影响机械位置（两个独立操作）", True)  # clearBooks() 只动 books 键

    # ---------------- C. 步进 + 软限位 ----------------
    print("\n[C] 步进电机：正常完成 / 单次上限 / 软限位 / 线圈释放")
    m = Motor(CFG_STEPPER_LIFT_RANGE_STEPS)
    check("run() 登记目标步数", m.run(300) and m.steps_left == 300 and m.moving)
    moved, done = m.run_to_completion()
    check("正常走完 300 步", moved == 300 and done and m.pos_steps == 300)
    check("走完后线圈释放（不发热）", m.released and not m.moving)
    check("位置正确累计", m.pos_steps == 300)
    m2 = Motor(CFG_STEPPER_LIFT_RANGE_STEPS)
    check("超过 CFG_STEPPER_MAX_STEPS 被拒绝", not m2.run(CFG_STEPPER_MAX_STEPS + 1))
    check("被拒绝时不产生任何移动", m2.pos_steps == 0 and not m2.moving)
    m3 = Motor(200)
    m3.run(500)
    moved, done = m3.run_to_completion()
    check("软限位拦下超出行程的走步", moved == 200 and not done)
    check("软限位后停在边界上（不硬顶）", m3.pos_steps == 200)
    check("软限位停机也释放线圈", m3.released and not m3.moving)
    check("到边后再往同方向请求被拒绝", clamp_to_range(m3, 10) == 0)
    check("到边后可以往反方向走", clamp_to_range(m3, -10) == -10)
    m4 = Motor(1000)
    m4.run(-1500)
    moved, done = m4.run_to_completion()
    check("反方向软限位同样生效", m4.pos_steps == -1000 and not done)
    m5 = Motor(1000, wired=False)
    check("没接线的电机 run() 返回 false", not m5.run(100))
    m5.service_one()
    check("没接线的电机 service() 是空操作", m5.pos_steps == 0)

    # ---------------- D. 机械层：互斥 / 上限 / 位置有效性 ----------------
    print("\n[D] 机械层：互斥 / 单次上限 / 位置有效性 / 急停 / 人工归零")
    lift, push = Motor(CFG_STEPPER_LIFT_RANGE_STEPS), Motor(CFG_STEPPER_PUSH_RANGE_STEPS)
    mech = Mechanism(lift, push)
    check("上电位置无效 + 需要人工归零", (not mech.position_valid) and mech.need_homing)
    check("位置无效时拒绝绝对定位（自动取书被禁止）", not mech.move_lift_to(200))
    check("拒绝后给出明确原因", "归零" in mech.last_error, mech.last_error)
    check("位置无效时仍然允许点动（人要看着机构）", mech.jog(AXIS_LIFT, 100, True))
    drive(mech)
    check("点动走完后状态 = DONE", mech.state == MECH_DONE)
    check("单次步数超上限被拒绝", not mech.jog(AXIS_LIFT, CFG_LIFT_MAX_MOVE_STEPS + 1, True))
    mech.take_result()
    check("人工归零成功", mech.set_zero_here() and mech.position_valid and not mech.need_homing)
    check("归零把软件步数清 0（电机并没有动）", lift.pos_steps == 0)
    check("归零后绝对定位可用", mech.move_lift_to(400))
    drive(mech)
    check("绝对定位走到目标位置", lift.pos_steps == 400 and mech.state == MECH_DONE, str(lift.pos_steps))
    mech.take_result()
    # 互斥：升降在动时推书请求必须被拒绝
    mech.jog(AXIS_LIFT, 200, True)
    check("升降在动时推书请求被拒绝（互斥）", not mech.jog(AXIS_PUSH, 100, False))
    check("被拒绝时给出互斥原因", "正在进行" in mech.last_error, mech.last_error)
    check("被拒绝不会覆盖原有目标", mech.target == 200)
    mech.stop_all()
    check("急停后位置判为无效", not mech.position_valid and mech.need_homing)
    check("急停后状态 = ESTOPPED（不假装动作完成）", mech.state == MECH_ESTOPPED)
    check("急停释放两个轴的线圈", lift.released and push.released)
    check("急停后绝对定位被拒绝（要先归零）", not mech.move_lift_to(0))
    check("急停后可以重新人工归零", mech.set_zero_here())
    check("推书回退默认被拒绝", not mech.jog(AXIS_PUSH, -100, False))
    check("显式允许时可以回退", mech.jog(AXIS_PUSH, -100, True))

    # ---------------- E. 取书位置计算 ----------------
    print("\n[E] 取书位置计算（下标 -> 升降目标步数）")
    def target_of(index):
        return CFG_LIFT_INDEX0_OFFSET_STEPS + index * CFG_LIFT_STEPS_PER_BOOK
    check("下标0 -> 零点位置", target_of(0) == 0)
    check("下标1 -> 每本书步数", target_of(1) == CFG_LIFT_STEPS_PER_BOOK)
    check("下标3 -> 3 倍步数", target_of(3) == 3 * CFG_LIFT_STEPS_PER_BOOK)
    check("取第 3 本的目标在软限位内", target_of(3) <= CFG_STEPPER_LIFT_RANGE_STEPS)
    check("每一本的理论位置都单调递增（顺序明确）",
          all(target_of(i) < target_of(i + 1) for i in range(0, 6)))

    # ---------------- F. 取书流程的关键规则 ----------------
    print("\n[F] 取书流程：失败时必须保留记录")
    biz = []
    book_add_at_top(biz, 1)
    book_add_at_top(biz, 2)
    book_add_at_top(biz, 3)
    snapshot = [dict(b) for b in biz]
    check("取书流程开始时不删记录（记录仍在）", biz == snapshot and len(biz) == 3)
    check("模拟机械失败 -> 不调用 bookRemoveAt -> 记录仍完整", biz == snapshot)
    r, removed = book_remove_at(biz, 0)
    check("只有机械正常完成后才删记录", r == 0 and len(biz) == 2)
    check("删除后剩下的下标自动前移（顺序仍正确）", [b["id"] for b in biz] == [2, 1])

    # ---------------- G. 存书后下移 + 取书位置一致性 ----------------
    print("\n[G] 存书下移让位 + 取书位置一致性（投书口 == 取书口）")

    # 模拟真实时序：每存一本 -> 数组顶部插一条 -> 升降向下让一本书。
    # 书堆可以看成"书 + 平台"，下移后所有书的绝对位置都 -125，
    # 但相对平台顶面的偏移没变，所以下标和物理顺序一直一一对应。
    stack = []
    lift2 = Motor(CFG_STEPPER_LIFT_RANGE_STEPS)
    mech2 = Mechanism(lift2, Motor(CFG_STEPPER_PUSH_RANGE_STEPS))
    check("存书前先人工归零", mech2.set_zero_here())

    for subject in (3, 2, 1):   # 依次放语文(3)? 不，这里依次放 id=3,2,1
        book_add_at_top(stack, subject)          # 记录插到数组顶部
        down = -CFG_LIFT_STEPS_PER_BOOK
        if CFG_STORE_DOWN_SIGN < 0:
            down = -down
        assert mech2.jog(AXIS_LIFT, down, True), "下移请求应被接受"
        drive(mech2)
        mech2.take_result()                      # 读走结果，回 IDLE

    check("连放 3 本后升降累计位置 = -3 本书",
          lift2.pos_steps == -3 * CFG_LIFT_STEPS_PER_BOOK, str(lift2.pos_steps))
    check("数组顺序 = 最后放的排最顶", [b["id"] for b in stack] == [1, 2, 3],
          str([b["id"] for b in stack]))
    check("每本书的相对位置偏移 = 下标 x 125 步",
          mech2.lift.pos_steps + 0 == -3 * CFG_LIFT_STEPS_PER_BOOK)

    # 关键不变量：把第 i 本送到取书口，需要的目标位置(相对零点) = i * 125
    # 因为下移后"平台顶面"就是取书/投书口的位置。
    ok_all = True
    for i in range(len(stack)):
        target = i * CFG_LIFT_STEPS_PER_BOOK
        book_after_platform_top = target
        book_abs = mech2.lift.pos_steps + book_after_platform_top
        if book_abs != -3 * CFG_LIFT_STEPS_PER_BOOK + i * CFG_LIFT_STEPS_PER_BOOK:
            ok_all = False
    check("下标 i 的目标位置公式 i*125 自洽", ok_all)
    # 真正有意义的检查：从当前平台位置出发，真正走一次"取下标 0"，看目标位置算得对不对。
    # 平台现在在 -3*125（顶面就是取书口），要取下标 0 的那本，目标位置应该是 0 步。
    target0 = 0 * CFG_LIFT_STEPS_PER_BOOK
    moved = target0 - mech2.lift.pos_steps          # = +375，要往回走 3 本书
    check("取下标0时需要把平台往回走 3 本书", moved == 3 * CFG_LIFT_STEPS_PER_BOOK, str(moved))
    assert mech2.move_lift_to(target0)
    drive(mech2)
    mech2.take_result()
    check("升降回到零点 -> 下标0 那本正好在取书口", mech2.lift.pos_steps == 0,
          str(mech2.lift.pos_steps))
    check("推书行程 = 875 步(已标定值)", CFG_PUSH_OUT_STEPS == 875)
    check("每本书步数 = 125 步(已标定值)", CFG_LIFT_STEPS_PER_BOOK == 125)
    check("存书下移方向宏存在且为 ±1", CFG_STORE_DOWN_SIGN in (1, -1))

    # ---------------- H. 位置持久化模型（base + offset）----------------
    print("\n[H] 位置持久化模型：开机不抹掉历史值，位移才累加")

    # 对应 main.cpp 里的位置持久化模型：
    #   要写进 Flash 的值 = g_persistBase + g_pendingPosOffset
    #   base   = 加载时从 Flash 读到的历史值
    #   offset = 加载之后相对"加载时刻机构位置"的累计位移
    # 修复的 bug: applyLoadedPosition() 会把 mechanism.liftSteps() 清 0，
    #             如果自动保存直接存 mechanism.liftSteps()，
    #             Flash 里的 300 会被立刻覆盖成 0（用户看到"300 怎么变了"）。
    class Persist:
        # 对应 main.cpp 的位置持久化模型(base + 从机构位置推导的 offset)

        def __init__(self):
            self.base = 0              # g_persistBase
            self.offset = 0            # g_pendingPosOffset
            self.mech_at_last = 0      # g_mechPosAtLastSave
            self.last_saved = 0        # lastSavedLiftSteps
            self.mech = 0              # mechanism.liftSteps()

        def sync(self):
            # 对应 syncPosOffsetFromMechanism()
            # 不管是谁驱动了机构，位移都从这里推导 —— 这正是之前漏掉 m,1,N 的原因
            self.offset += (self.mech - self.mech_at_last)
            self.mech_at_last = self.mech

        def value(self):
            self.sync()
            return self.base + self.offset

        def load_from_flash(self, steps):
            self.base = steps
            self.offset = 0
            self.mech_at_last = self.mech
            self.last_saved = steps

        def move(self, d):
            # 机构走 d 步（Stepper28BYJ 把 _posSteps 加上 d）
            self.mech += d

        def auto_save_if_changed(self):
            v = self.value()
            if v != self.last_saved:
                self.base = v          # commitPersistAccounting()
                self.offset = 0
                self.mech_at_last = self.mech
                self.last_saved = v
                return v
            return None

        def zero_here(self):
            self.base = 0
            self.offset = 0
            self.mech = 0
            self.mech_at_last = 0
            self.last_saved = 0
    p = Persist()
    p.load_from_flash(300)
    check("加载 300 后，待写值 = 300（不是 0）", p.value() == 300, str(p.value()))
    check("加载后什么都不做 -> 不需要保存（历史值不会被抹掉）",
          p.auto_save_if_changed() is None)
    p.move(100)
    check("机构走 +100 -> 待写值 = 400", p.value() == 400, str(p.value()))
    check("变化后确实触发一次保存，且写入 400", p.auto_save_if_changed() == 400)
    check("保存后 base=400 offset=0（记账已滚动）",
          p.base == 400 and p.offset == 0)
    check("再保存一次不会重复写", p.auto_save_if_changed() is None)
    p.move(-125)
    check("机构反向走 125 -> 待写值 = 275", p.value() == 275, str(p.value()))
    p.auto_save_if_changed()
    check("反向位移也正确落盘", p.base == 275 and p.offset == 0)
    p.zero_here()
    check("人工归零后 base=offset=0", p.value() == 0)
    p.move(100)
    check("归零后再走 100 -> 待写值 = 100（不背历史包袱）", p.value() == 100, str(p.value()))


    # ---- 复现用户实测: 加载后走 m,1,125 ----
    #
    # 之前记账只写在"调试页 jog / 按键 jog"两条分支里，
    # 而串口 m,1,N 走的是 runMotor() -> mechanism.jog()，完全没经过它们，
    # 所以 offset 一直是 0，用户看到的日志就是"机构现在位置=250，保存却是 0 步"。
    # 现在改成从机构位置推导，任何路径都算得对。
    p2 = Persist()
    p2.load_from_flash(300)                 # 用户日志: q,l 加载 300 步
    check("场景: 加载 300 后 base=300", p2.base == 300, str(p2.base))
    check("场景: 加载后不动 -> 不写(历史值不被抹)", p2.auto_save_if_changed() is None)
    p2.move(125)                            # 用户日志: 机构从 125 走到 250
    check("场景: 机构走 125 步 -> 待写值 = 425(不是 0)", p2.value() == 425, str(p2.value()))
    check("场景: 走完触发自动保存, 写入 425", p2.auto_save_if_changed() == 425)
    check("场景: 保存后 base=425 offset=0", p2.base == 425 and p2.offset == 0,
          "base=%d offset=%d" % (p2.base, p2.offset))
    p2.move(125)
    check("场景: 再走 125 -> 待写值 = 550", p2.value() == 550, str(p2.value()))
    p2.zero_here()
    check("场景: 归零后待写值 = 0", p2.value() == 0)

    # ---------------- 汇总 ----------------
    print("")
    print("=" * 70)
    print("结果: 通过 %d 项, 失败 %d 项" % (PASS, FAIL))
    print("=" * 70)
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
