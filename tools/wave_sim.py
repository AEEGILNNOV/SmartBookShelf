# -*- coding: utf-8 -*-
# HC-SR04 换算与超时余量核对（对应 UltraLib.cpp 的 _distanceMm 计算与 config.h 的超时）
NUM, DEN = 343, 2000          # 距离(mm) = pulseUs * 343 / 2000
TIMEOUT_US = 38000            # CFG_WAVE_TIMEOUT_US
MIN_MM, MAX_MM = 20, 4000     # CFG_WAVE_MIN_MM / MAX_MM

fails, passes = [], 0
def check(ok, what, got=None, want=None):
    global passes
    if ok: passes += 1
    else: fails.append("%s (got=%s want=%s)" % (what, got, want))

def mm_from_us(us): return (us * NUM) // DEN
def us_from_mm(mm): return (mm * DEN) // NUM

# 1) 手册标称点：声速 343m/s 时，1m 往返约 5.83ms -> 1000mm
check(abs(mm_from_us(5831) - 1000) <= 1, "5831us -> 约 1000mm", mm_from_us(5831), 1000)

# 2) 量程边界
for mm in (20, 100, 500, 1000, 2000, 4000):
    us = us_from_mm(mm)
    got = mm_from_us(us)
    check(abs(got - mm) <= 2, "%dmm 往返换算自洽" % mm, got, mm)

# 3) 超时余量：最远 4000mm 的往返时间必须明显小于超时时间
us_4000 = us_from_mm(4000)
check(us_4000 < TIMEOUT_US, "4000mm 往返(%dus) < 超时(%dus)" % (us_4000, TIMEOUT_US),
      us_4000, TIMEOUT_US)
print("  4000mm 往返=%dus, 超时=%dus, 余量=%dus (%.1fms)"
      % (us_4000, TIMEOUT_US, TIMEOUT_US - us_4000, (TIMEOUT_US - us_4000)/1000.0))

# 4) 测量间隔必须大于手册建议的 60ms 吗？这里用 100ms，核对不会重叠
MEASURE_MS = 100
check(MEASURE_MS * 1000 > us_4000, "测量间隔 100ms 大于最远往返时间(%dus)" % us_4000,
      MEASURE_MS * 1000, us_4000)

# 5) 模拟状态机的时间线：ECHO 在 200us 后拉高，持续 5831us
trig_us, rise_us, fall_us = 10, 200, 200 + 5831
check(trig_us == 10, "TRIG 脉冲宽度 10us", trig_us, 10)
pulse = fall_us - rise_us
check(mm_from_us(pulse) == 1000, "状态机量到的高电平 -> 1000mm", mm_from_us(pulse), 1000)

# 6) 状态机参考实现：验证三种结局（正常/没拉高/没拉低）
class Sr04:
    IDLE, TRIG, RISE, FALL = 0,1,2,3
    def __init__(s, echo_high_at=None, echo_low_at=None, timeout=TIMEOUT_US):
        s.st, s.timeout, s.t0 = s.IDLE, timeout, 0
        s.echo_high_at, s.echo_low_at = echo_high_at, echo_low_at
        s.pulse, s.dist, s.err, s.valid, s.ready = 0, 0, 0, False, False
    def start(s, now):
        if s.st != s.IDLE: return False
        s.ready = s.valid = False; s.err = 0; s.pulse = s.dist = 0
        s.t0 = now; s.st = s.TRIG; return True
    def update(s, now):
        if s.st == s.TRIG:
            if now - s.t0 >= 10: s.wait0 = now; s.st = s.RISE
        elif s.st == s.RISE:
            if s.echo_high_at is not None and now >= s.echo_high_at:
                s.rise0 = now; s.st = s.FALL
            elif now - s.wait0 > s.timeout:
                s.err = 1; s.valid = False; s.ready = True; s.st = s.IDLE
        elif s.st == s.FALL:
            if s.echo_low_at is not None and now >= s.echo_low_at:
                s.pulse = now - s.rise0
                s.dist = (s.pulse * NUM) // DEN
                s.valid = True; s.err = 0; s.ready = True; s.st = s.IDLE
            elif now - s.wait0 > s.timeout:
                s.err = 2; s.valid = False; s.ready = True; s.st = s.IDLE

def run(sensor, steps=200000):
    t = 1000000
    sensor.start(t)
    while not sensor.ready and steps > 0:
        t += 1; sensor.update(t); steps -= 1
    return sensor, t

# 正常：200us 后拉高，5831us 后拉低
s, _ = run(Sr04(echo_high_at=1000000+200, echo_low_at=1000000+200+5831))
check(s.valid and s.err == 0, "正常回波: valid", (s.valid, s.err), (True, 0))
check(abs(s.dist - 1000) <= 2, "正常回波: 距离约 1000mm", s.dist, 1000)

# 没拉高：应该报 err=1
s, _ = run(Sr04(echo_high_at=None))
check((not s.valid) and s.err == 1, "ECHO 不拉高: err=1", (s.valid, s.err), (False, 1))

# 拉高不拉低：应该报 err=2
s, _ = run(Sr04(echo_high_at=1000000+200, echo_low_at=None))
check((not s.valid) and s.err == 2, "ECHO 不拉低: err=2", (s.valid, s.err), (False, 2))

# 超量程：回波 30ms（约 5145mm）会被量程过滤掉
s, _ = run(Sr04(echo_high_at=1000000+200, echo_low_at=1000000+200+30000))
in_range = MIN_MM <= s.dist <= MAX_MM
check((not in_range), "超量程读数会被过滤(%dmm)" % s.dist, in_range, False)

print("通过 %d 项，失败 %d 项" % (passes, len(fails)))
for f in fails: print("  [FAIL] " + f)
print("RESULT=" + ("ALL_PASS" if not fails else "HAS_FAILURE"))