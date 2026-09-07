# Week 3 Version A：AES-128 软件 T-table Flush+Reload 受控同步验证

本代码包用于先完成 Week 3 的 Version A 基线实验：

```text
attacker flush
        ↓
公开 START
        ↓
victim 执行 AddRoundKey + AES 第一轮 T-table
        ↓
公开 ROUND_DONE
        ↓
attacker reload 并保存原始 ticks
        ↓
公开 MEASURE_DONE
        ↓
victim 完成 AES 第 2～10 轮并保存 ciphertext
```

这是一套“受控同步验证程序”，不是完全非协作攻击程序。它的目的，是先验证：

1. 两个独立进程能否共享同一份 T-table 页面；
2. SMT 兄弟逻辑处理器之间能否完成 Flush+Reload；
3. 第一轮访问是否能在 cache-line 时间中留下稳定痕迹；
4. victim 是否仍然完成完整 AES-128 加密并产生正确 ciphertext；
5. raw CSV 是否能被拉回离线主机，进行后续密钥候选恢复。

## 1. 实验约束和威胁模型

实验机器使用原生 Ubuntu，不在 WSL 或虚拟机中采集 TSC 数据。

本机已经确认的 SMT 拓扑：

```text
physical core 4
├── logical CPU 3：attacker
└── logical CPU 4：victim
```

攻击者能力：

- 可以映射 victim 使用的共享只读 T-table 文件；
- 可以对共享 T-table cache line 执行 `clflush`；
- 可以 reload 并记录访问 ticks；
- 知道每条 trace 的 plaintext；
- 可以使用公开同步信号对齐实验窗口。

攻击者不能力：

- attacker 二进制不包含 `victim_key`；
- attacker 不映射 victim 的私有 key 或 round key；
- attacker 不读取 key；
- attacker 运行阶段不执行 threshold 判断、hit/miss 分类、候选评分或密钥恢复。

重要说明：本版本的 `ROUND_DONE` 是受控实验装置提供的同步信号。victim 在第一轮完成后暂停，使后续 AES 轮次不会污染本轮测量。报告中必须把这一点作为实验假设写出，不能把 Version A 结果描述成完全非协作攻击。

## 2. 文件结构

```text
week3_versionA_aes_fr/
├── aes.c                 软件 AES-128、S-box、Te0~Te3、key expansion
├── aes.h                 AES 接口和 victim 侧 round-key 结构
├── shared.h              POSIX shared memory 控制结构和四阶段状态
├── timing.h              rdtscp+lfence+mov、clflush、mfence、NOP
├── victim.c              victim 进程：私有 key、完整 AES、公开同步
├── attacker.c            attacker 进程：flush/reload，只保存原始数据
├── offline_recover.py    实验结束后离线恢复高 4 bit 的脚本
├── Makefile
└── README.md
```

运行阶段产生：

```text
ttable_shared.bin        共享 T-table 文件
trace_timings.csv        原始 trace CSV，保持原始格式
victim_timing.csv        victim 第一轮、剩余 AES 和同步窗口的 TSC 记录
```

## 3. 编译

在 Ubuntu 实验机上执行：

```bash
cd ~/week3_versionA_aes_fr
make clean
make
```

代码没有链接 OpenSSL，也没有调用 AES-NI。victim 使用本地 C 代码生成标准 AES T-table。

建议编译后检查反汇编：

```bash
objdump -d victim | grep -E 'aesenc|aesenclast|vaesenc|vaesenclast'
```

正常情况下不应有输出。T-table 访问应能在反汇编中看到基于索引的内存 load。

## 4. WSL → 实验机：传输代码

以下命令在你的 Ubuntu WSL 窗口中执行。把用户名、实验机地址替换成自己的值：

```bash
scp -r week3_versionA_aes_fr USER@EXPERIMENT_HOST:~/
```

登录实验机：

```bash
ssh USER@EXPERIMENT_HOST
```

进入目录：

```bash
cd ~/week3_versionA_aes_fr
```

注意：真正的计时和运行必须在原生 Ubuntu 实验机完成，不要在 WSL 内直接运行攻击程序。

## 5. 检查 SMT 拓扑和运行环境

在实验机执行：

```bash
lscpu -e=CPU,CORE,SOCKET,NODE
cat /sys/devices/system/cpu/cpu3/topology/thread_siblings_list
cat /sys/devices/system/cpu/cpu4/topology/thread_siblings_list
```

确认 CPU 3 和 CPU 4 是同一个 physical core 的 SMT sibling。

检查当前进程可用 CPU：

```bash
taskset -pc $$
```

如果实验机使用了 CPU 隔离配置，确认 CPU 3 和 CPU 4 没有被其他高负载任务占用。

## 6. 启动实验：两个终端窗口

### 终端 1：先启动 victim

必须先启动 victim，因为 victim 负责创建控制页和初始化共享 T-table：

```bash
cd ~/week3_versionA_aes_fr

taskset -c 3,4 ./victim \
  --table ./ttable_shared.bin \
  --ctrl /aes_fr_versionA_ctrl \
  --traces 10000 \
  --cpu 4
```

看到下面类似信息后，不要关闭窗口：

```text
victim ready: cpu=4, traces=10000, table=./ttable_shared.bin
victim AES self-test passed; master key is private to victim code path.
```

### 终端 2：再启动 attacker

新开一个 SSH 终端：

```bash
ssh USER@EXPERIMENT_HOST
cd ~/week3_versionA_aes_fr

taskset -c 3,4 ./attacker \
  --table ./ttable_shared.bin \
  --ctrl /aes_fr_versionA_ctrl \
  --traces 10000 \
  --cpu 3 \
  --csv trace_timings.csv \
  --sync-csv victim_timing.csv \
  --seed 0x12345678
```

attacker 正常启动后会显示：

```text
attacker ready: cpu=3, traces=10000, raw=trace_timings.csv, sync=victim_timing.csv
runtime mode: raw collection only; no threshold, hit/miss, or key recovery is executed.
```

两个进程结束后，终端 1 和终端 2 都应正常返回 shell，且目录中出现：

```bash
ls -lh trace_timings.csv victim_timing.csv ttable_shared.bin
wc -l trace_timings.csv victim_timing.csv
```

正常情况下：

```text
trace_timings.csv  行数 = 10001，包括表头
victim_timing.csv  行数 = 10001，包括表头
```

## 7. 原始 CSV 格式

`trace_timings.csv` 的表头保持原来 demo 的格式：

```text
trace,
pt00,...,pt15,
ct00,...,ct15,
te0_line00_ticks,...,te0_line15_ticks,
te1_line00_ticks,...,te1_line15_ticks,
te2_line00_ticks,...,te2_line15_ticks,
te3_line00_ticks,...,te3_line15_ticks
```

每一行是一条 trace：

```text
1 次已知 plaintext AES-128 加密
+
4 张 T-table、共 64 条 cache line 的原始 reload ticks
```

运行过程中 attacker 不判断：

- 哪个 ticks 是 hit；
- 哪个 ticks 是 miss；
- 哪个 line 对应哪个 key guess；
- 哪个 key guess 得分最高。

这些都留到离线主机完成。

`victim_timing.csv` 用于观察同步验证阶段的粗略时间：

```text
first_round_cycles
```

表示 victim 从第一轮开始到第一轮结束的 TSC 差值；

```text
attacker_measurement_window_cycles
```

表示 victim 在 `ROUND_DONE` 后等待 attacker 完成 reload 的窗口长度；

```text
rest_aes_cycles
```

表示 victim 收到 `MEASURE_DONE` 后执行剩余 AES 轮次的 TSC 差值；

```text
trace_wall_cycles
```

表示从第一轮开始到完整 AES 完成的总墙钟周期，其中包含 attacker reload 等待时间。

## 8. 将 CSV 拉回 WSL 离线主机

在 WSL 窗口执行：

```bash
mkdir -p week3_results

scp USER@EXPERIMENT_HOST:~/week3_versionA_aes_fr/trace_timings.csv \
  ./week3_results/

scp USER@EXPERIMENT_HOST:~/week3_versionA_aes_fr/victim_timing.csv \
  ./week3_results/
```

可以先检查文件：

```bash
wc -l week3_results/trace_timings.csv
head -n 2 week3_results/trace_timings.csv
```

## 9. WSL 离线恢复第一版结果

注意：标准紧凑型 T-table 中每条 cache line 有 16 个 4B 表项，因此 Version A 的离线脚本只能恢复每个 key byte 的高 4 bit，并输出 16 个低位候选。

阈值必须先根据当前 SMT/AES 环境的 hit-vs-miss 校准结果确定。假设离线选择出的阈值是 180 ticks：

```bash
python3 offline_recover.py \
  --csv week3_results/trace_timings.csv \
  --threshold 180 \
  --out week3_results/recovery_high_nibbles.csv
```

如果需要用 victim 的真实 key 做离线验证，可以在离线主机执行：

```bash
python3 offline_recover.py \
  --csv week3_results/trace_timings.csv \
  --threshold 180 \
  --true-key 2b7e151628aed2a6abf7158809cf4f3c \
  --out week3_results/recovery_high_nibbles.csv
```

真实 key 只用于离线 verifier，不参与实验机上的 attacker 运行。

## 10. 当前 Version A 的正确结论

本版本应报告为：

1. 在 CPU 3/4 同一 physical core 的 SMT 条件下，attacker 和 victim 可以通过共享 T-table 页面完成 Flush+Reload；
2. victim 确实完成了完整 AES-128 的 10 轮计算；
3. attacker 能够保存每条 trace 的 plaintext、ciphertext 和 64 条 cache-line reload 时间；
4. 第一轮 T-table 访问与 `plaintext_byte XOR key_byte` 存在可分析关系；
5. 由于紧凑型 T-table 的一条 cache line 包含 16 个元素，第一轮 cache-line 观测直接暴露的是 `index >> 4`，因此只能直接恢复 key byte 的高 4 bit；
6. 该版本的 `ROUND_DONE` 是实验同步假设，不是完全非协作攻击；
7. 完整 AES 密文验证和密钥候选分析在实验结束后离线完成。

不要把高 4 bit 的结果写成已经恢复了完整 128-bit AES key。Version A 的作用是先把共享页面、SMT、同步、完整 AES 和原始数据采集链路验证正确，为之后的弱同步攻击和更高分辨率恢复做基础。

## 11. 常见问题

### attacker 启动时提示 shm_open failed

说明 victim 没有先启动，或者 victim 已经结束。重新清理目录后，先运行 victim，再运行 attacker：

```bash
rm -f ttable_shared.bin trace_timings.csv victim_timing.csv
```

### attacker 和 victim 运行后没有退出

检查两个终端是否使用了相同的：

```text
--ctrl
--traces
```

并确认 CPU 3 和 CPU 4 没有被系统限制或绑定失败。

### CSV 行数不完整

不要在实验中按 Ctrl+C。若进程被中断，本版本会留下已经写入的前缀数据，但最后一条 trace 可能不完整。删除输出文件后重新运行。

### 为什么程序不在运行阶段输出 recovered key

这是有意设计的。attacker 运行阶段只保存原始测量数据，不执行阈值分类和密钥恢复，避免在线分析路径影响 Flush+Reload 时序，也符合“实验机采集、离线主机分析”的流程。
