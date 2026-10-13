# -*- coding: utf-8 -*-
# StepperLib 状态机的等价实现 + 逐项验证。
# 要点：单步 run(1) 走完后会 release() 把线圈拉低（对 28BYJ 是必须的，否则发热），
#       所以验证拍序必须在“还在走、每一步之后”采样，而不是走完再采。
SEQ8 = [0x01, 0x03, 0x02, 0x06, 0x04, 0x0C, 0x08, 0x09]
MIN_US, MAX_US = 800, 4000
MAX_STEPS = 8192

class Stepper:
    def __init__(self, wired=True, wave_mode=True, soft_limit=True):
        self.wired, self.wave_mode, self.soft_limit = wired, wave_mode, soft_limit
        self.seq_len = 8 if wave_mode else 4
        self.stride = 1 if wave_mode else 2
        self.delay, self.steps_left, self.pos, self.seq_index = 1500, 0, 0, 0
        self.moving, self.range, self.mask = False, 0, 0
        self.last_us, self.now = 0, 1000000   # 真机 setup 跑完时 micros() 早不是 0
    def set_delay(self, us): self.delay = max(MIN_US, min(MAX_US, us))
    def set_range(self, r): self.range = abs(r)
    def run(self, steps):
        if not self.wired: return False
        if steps == 0: return True
        if abs(steps) > MAX_STEPS: return False
        self.steps_left, self.moving = steps, True
        return True
    def release(self): self.mask = 0
    def stop(self): self.steps_left, self.moving = 0, False; self.release()
    def service(self):
        if not self.wired or not self.moving or self.steps_left == 0: return
        if self.now - self.last_us < self.delay: return
        self.last_us = self.now
        d = 1 if self.steps_left > 0 else -1
        if self.soft_limit and self.range > 0:
            if d > 0 and self.pos >= self.range: self.stop(); return
            if d < 0 and self.pos <= -self.range: self.stop(); return
        self.seq_index = (self.seq_index + d) % self.seq_len
        self.mask = SEQ8[self.seq_index * self.stride]
        self.pos += d
        self.steps_left -= d
        if self.steps_left == 0:
            self.moving = False
            self.release()
    def samples(self, steps):
        """边推进边采样每一步之后的线圈状态（还在走的时候）"""
        assert self.run(steps)
        out = []
        guard = 0
        while self.moving and guard < 200000:
            self.now += self.delay
            self.service()
            out.append(self.mask)     # 关键：走一步就采一次
            guard += 1
        return out
    def finish(self, steps):
        self.samples(steps); return self.pos

def parse_motor_cmd(line):
    idx = -1
    if line and line[0] in "123":
        first = int(line[0])
        if 1 <= first <= 3 and "," in line:
            return line.split(",", 1)[1], first - 1
    return line, idx

fails, passes = [], 0
def check(ok, what, got=None, want=None):
    global passes
    if ok: passes += 1
    else: fails.append("%s (got=%s want=%s)" % (what, got, want))

# 1) 八拍拍序（采前 8 个样本，第 9 步之后的 release 不算）
# 采样从 _seqIndex=0 的“下一步”开始，所以期望序列是循环左移一位；
# 最后一步走完会 release()，所以只比较前 N-1 个样本。
m = Stepper(); m.set_delay(MIN_US)
s = m.samples(9)[:8]
check(s == SEQ8[1:] + SEQ8[:1], "八拍拍序 = A-AB-B-BC-C-CD-D-DA(循环移位)",
      s, SEQ8[1:] + SEQ8[:1])

# 2) 四拍只落单相
m = Stepper(wave_mode=False); m.set_delay(MIN_US)
s = m.samples(5)[:4]
check(s == [0x02, 0x04, 0x08, 0x01], "四拍只落单相(A-B-C-D 循环)",
      s, [2,4,8,1])

# 3) 方向：正转的拍序和反转的拍序互为逆序
m = Stepper(); m.set_delay(MIN_US)
fwd = m.samples(5)[:4]
m2 = Stepper(); m2.set_delay(MIN_US)
bwd = m2.samples(-5)[:4]      # 多走一步，丢掉最后 release 后的 0
check(fwd == SEQ8[1:5], "正转拍序前进", fwd, SEQ8[1:5])
check(bwd == [SEQ8[7], SEQ8[6], SEQ8[5], SEQ8[4]],
      "反转拍序后退(逆序)", bwd, [SEQ8[7], SEQ8[6], SEQ8[5], SEQ8[4]])

# 4) 步数与位置
m = Stepper(); m.set_delay(MIN_US)
check(m.finish(100) == 100, "走 100 步 position", m.pos, 100)
check(m.steps_left == 0 and not m.moving, "走完清零", (m.steps_left, m.moving), (0, False))
m.finish(-40)
check(m.pos == 60, "再反转 40 步 position", m.pos, 60)
m.run(1000); check(m.moving, "run 后 busy", m.moving, True)
m.stop(); check(not m.moving, "stop 后 busy", m.moving, False)
check(m.pos == 60, "stop 不改位置", m.pos, 60)
check(m.mask == 0, "停机后线圈全低", m.mask, 0)
m.finish(3)
check(m.mask == 0, "走完后自动断电(release)", m.mask, 0)

# 5) 软限位
m = Stepper(); m.set_delay(MIN_US); m.set_range(50)
m.samples(200)
check(m.pos == 50, "正向撞限位停 +50", m.pos, 50)
check(m.steps_left == 0, "撞限位后丢弃剩余步数", m.steps_left, 0)
m.samples(-200)
check(m.pos == -50, "反向撞限位停 -50", m.pos, -50)

# 6) 非法/边界
m = Stepper()
check(m.run(0) is True, "0 步算成功(不转)", None, None)
check(m.run(MAX_STEPS) is True, "恰好最大步数接受", None, None)
check(m.run(MAX_STEPS+1) is False, "超最大步数拒绝", None, None)
check(m.run(-(MAX_STEPS+1)) is False, "负向超限拒绝", None, None)

# 7) 没接线
m = Stepper(wired=False)
m.service()
check(m.run(100) is False, "没接线 run() false", None, None)
check(m.mask == 0, "没接线不碰引脚", m.mask, 0)

# 8) 速度夹取
m = Stepper()
m.set_delay(10);    check(m.delay == MIN_US, "速度下限夹到 800", m.delay, MIN_US)
m.set_delay(999999); check(m.delay == MAX_US, "速度上限夹到 4000", m.delay, MAX_US)

# 9) 命令解析
cases = [("1,300","300",0), ("1,-300","-300",0), ("2,300","300",1), ("3,300","300",2),
         ("300","300",-1), ("-300","-300",-1), ("z","z",-1), ("2,z","z",1),
         ("s1500","s1500",-1), ("?","?",-1)]
for line, wb, wi in cases:
    b, i = parse_motor_cmd(line)
    check(b == wb and i == wi, "解析 [%s]" % line, (b,i), (wb,wi))

print("通过 %d 项，失败 %d 项" % (passes, len(fails)))
for f in fails: print("  [FAIL] " + f)
print("RESULT=" + ("ALL_PASS" if not fails else "HAS_FAILURE"))