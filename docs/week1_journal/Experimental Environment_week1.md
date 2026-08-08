##            Experimental Environment

The experiments are conducted on a system equipped with an **Intel® Core™ Ultra 7 Processor 155H**. The detailed processor specifications are available from Intel's official product page:[Intel® Core™ Ultra 7 Processor 155H](https://www.intel.com/content/www/us/en/products/sku/236847/intel-core-ultra-7-processor-155h-24m-cache-up-to-4-80-ghz/specifications.html).

The processor belongs to Intel's Meteor Lake architecture and integrates **Redwood Cove Performance-cores  (P-cores)** and **Crestmont Efficient-cores (E-cores)**. The experiments mainly focus on the microarchitectural behaviors of the P-core, especially hardware prefetching and cache-based side-channel characteristics.

The experimental system configuration and hardware parameters are recorded before each experiment to ensure reproducibility.

---



## GitHub Repository

[A public GitHub repository](https://github.com/kelyazhang/cache-side-channel-project) has been created to document the experimental process, source code, and analysis records of this cache side-channel research project. The repository will be continuously updated with experimental implementations, configuration details, and research notes to improve reproducibility and transparency.

---



## Week 1 Experimental Environment Setup

### 1. Purpose of This Environment Record

This section records the hardware configuration, Linux kernel configuration, runtime controls, and verification procedures used for the Week 1 Flush+Reload experiment.

The purposes of this record are:

1. To preserve the exact experimental environment used for timing measurements.
2. To provide a repeatable setup procedure for future experiments.
3. To distinguish persistent settings from runtime-only settings.
4. To allow the supervisor to verify whether each setting is appropriate.
5. To reduce timing variance caused by CPU migration, dynamic frequency scaling, idle-state transitions, operating-system scheduling, hardware interrupts, and unintended hardware prefetching.



The Week 1 target is a trustworthy Flush+Reload timing primitive using a shared read-only mapping, `clflush`, and a fenced timestamp measurement. The measured program must remain on one physical P-core throughout the experiment.

---

### 2. Experimental Host

#### 2.1 Machine and Operating System

The experiment is performed on the following native Linux machine:

| Item                            | Recorded value                  |
| :------------------------------ | :------------------------------ |
| Hostname                        | `redwood-probe`                 |
| System vendor                   | ASUSTeK COMPUTER INC.           |
| System model                    | `NUC14RVS-B`                    |
| Processor                       | Intel Core Ultra 7 155H         |
| Processor family/model/stepping | Family 6, Model 170, Stepping 4 |
| Microcode revision              | `0x25`                          |
| Operating system                | Ubuntu 24.04.4 LTS              |
| Kernel                          | Linux `6.8.0-100-generic`       |
| Kernel preemption model         | `PREEMPT_DYNAMIC`               |
| Architecture                    | `x86_64`                        |
| Compiler                        | GCC 13.3.0                      |
| Boot mode                       | UEFI                            |
| Secure Boot                     | Disabled                        |
| Virtualization                  | None detected                   |
| NUMA topology                   | One NUMA node                   |
| Installed memory                | Approximately 15.3 GiB          |

Verification commands:

```bash
hostnamectl
uname -a
cat /etc/os-release
lscpu
systemd-detect-virt
grep -m1 microcode /proc/cpuinfo
gcc --version
```

**Reason for this configuration:**  
The project requires native Linux rather than WSL or a virtual machine. Native execution is required for reliable cache timing, direct CPU affinity control, and MSR access.

---

#### 2.2 Official Processor Topology

According to the [Intel Core Ultra 7 155H product specification](https://www.intel.com/content/www/us/en/products/sku/236847/intel-core-ultra-7-processor-155h-24m-cache-up-to-4-80-ghz/specifications.html), the processor physically contains:

| Core type                 | Official quantity |
| ------------------------- | :---------------- |
| Performance cores         | 6                 |
| Standard Efficient cores  | 8                 |
| Low Power Efficient cores | 2                 |
| Total physical cores      | 16                |
| Maximum total threads     | 22                |

The current BIOS and operating-system configuration exposes only eight logical CPUs:

```text
CPU 0-5: Performance-core logical CPUs
CPU 6-7: Low Power Efficient-core logical CPUs
```

The eight standard E-cores are not exposed to Linux and therefore cannot execute Linux programs in the current configuration.

The two Low Power E-cores remain enabled and appear as CPU 6 and CPU 7.

Verification commands:

```bash
cat /sys/devices/cpu_core/cpus
cat /sys/devices/cpu_atom/cpus

cat /sys/devices/system/cpu/possible
cat /sys/devices/system/cpu/present
cat /sys/devices/system/cpu/online
cat /sys/devices/system/cpu/offline
```

Expected output:

```text
P-core CPUs:       0-5
E-core-class CPUs: 6-7
Online CPUs:       0-7
Offline CPUs:      none
```

**Reason for identifying the core types:**  
Meteor Lake is a hybrid processor. A timing-sensitive thread must not migrate between a P-core and an E-core because the cores have different execution resources, frequencies, cache behavior, and performance characteristics.

---

#### 2.3 Active Cache Parameters

The following cache information was reported by Linux:

| Cache parameter            | Recorded value                  |
| -------------------------- | :------------------------------ |
| Cache-line size            | 64 bytes                        |
| Total L1 data cache        | 352 KiB                         |
| Total L1 instruction cache | 512 KiB                         |
| Reported L2 cache          | 14 MiB across 7 cache instances |
| Last-level cache           | 24 MiB                          |
| L2 associativity           | 16-way                          |
| L3 associativity           | 12-way                          |

Verification command:

```bash
lscpu -C
```

**Reason for recording the cache-line size:**  
Flush+Reload operates on cache lines. The selected probe address should be aligned and interpreted using the processor's 64-byte cache-line size.

---

### 3. CPU Roles and Core Pinning

#### 3.1 Current Core Allocation

The current experimental core allocation is:

| CPU   | Core type        | Assigned role                          |
| :---- | :--------------- | -------------------------------------- |
| CPU 0 | P-core           | Housekeeping and operating-system work |
| CPU 1 | P-core           | Isolated experimental core             |
| CPU 2 | P-core           | Isolated experimental core             |
| CPU 3 | P-core           | Isolated experimental core             |
| CPU 4 | P-core           | Isolated experimental core             |
| CPU 5 | P-core           | Isolated experimental core             |
| CPU 6 | Low Power E-core | Housekeeping and background work       |
| CPU 7 | Low Power E-core | Housekeeping and background work       |

The CPU 0，6 and 7 have been described  as the housekeeping cores. The verified configuration is more accurately described as:

```text
Housekeeping CPUs: 0,6,7
Isolated P-cores:  1-5
```

A normal SSH shell and ordinary background processes may run on CPUs 0, 6, or 7.

---

#### 3.2 Meaning of CPU Isolation

CPU isolation and CPU affinity are different mechanisms.

- CPU isolation keeps ordinary scheduler load away from selected cores.
- CPU affinity explicitly places an experimental thread on a selected core and prevents it from running outside its affinity mask.

The Linux scheduler will not automatically place an ordinary SSH-launched program on CPU 1-5 merely because those CPUs are isolated.

For example, this command is **not sufficient**:

```bash
./week1_probe
```

Without explicit affinity, the program inherits the CPU mask of its parent shell and may run on CPU 0, CPU 6, or CPU 7. It may therefore execute on a Low Power E-core.

The correct Week 1 invocation is:

```bash
taskset -c 5 ./week1_probe
```

When root access is required:

```bash
sudo taskset -c 5 ./week1_probe
```

Verification:

```bash
taskset -pc "$(pgrep -n week1_probe)"
```

Expected affinity:

```text
5
```

The official Linux interfaces are documented in:

- [taskset(1)](https://man7.org/linux/man-pages/man1/taskset.1.html)
- [sched_setaffinity(2)](https://man7.org/linux/man-pages/man2/sched_setaffinity.2.html)
- [pthread_setaffinity_np(3)](https://man7.org/linux/man-pages/man3/pthread_setaffinity_np.3.html)

**Reason for pinning to a single P-core:**  
It prevents migration to CPU 6 or CPU 7 and prevents migration between different P-cores. This preserves the private L1 cache, private L2 cache, TLB state, branch-prediction state, and core-local prefetcher state.

---

#### 3.3 Internal Affinity in C Programs

A program may also bind itself internally:

```c
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>

static int bind_current_thread_to_cpu(int cpu)
{
    cpu_set_t mask;

    CPU_ZERO(&mask);
    CPU_SET(cpu, &mask);

    if (sched_setaffinity(0, sizeof(mask), &mask) != 0) {
        perror("sched_setaffinity");
        return -1;
    }

    return 0;
}
```

---

### 4. SMT and Hyper-Threading

The current system reports:

```text
Model name:                              Intel(R) Core(TM) Ultra 7 155H
Thread(s) per core:                      1
Core(s) per socket:                      8
Socket(s):                               1
```

Verification:

```bash
lscpu | grep -E 'Thread|Core|Socket'
cat /sys/devices/system/cpu/smt/active
cat /sys/devices/system/cpu/smt/control

for cpu in 0 1 2 3 4 5 6 7; do
    printf "CPU %s siblings: " "$cpu"
    cat "/sys/devices/system/cpu/cpu${cpu}/topology/thread_siblings_list"
done
```

Each online CPU currently has only itself in its sibling list.

**Reason for disabling SMT:**  
A sibling hardware thread shares major physical-core resources, including execution units, private caches, queues, and parts of the prediction machinery. Disabling SMT removes this source of contention for the Week 1 single-core calibration experiment.

**Persistence:**  
The current state is controlled by firmware/BIOS exposure and remains across ordinary reboots unless the BIOS configuration is changed or reset.### 5. Historical BIOS and Verified State.

---

### 5. Persistent BIOS and GRUB Configuration

This section records the firmware and kernel boot settings that remain active across normal reboots. These settings do not need to be manually reapplied before every experiment, but their active state should still be verified after major firmware, kernel, or GRUB changes.

#### 5.1 Persistent BIOS/Firmware State

The current firmware configuration exposes the following CPU topology to Linux:

```text
CPU 0-5: six Performance cores
CPU 6-7: two Low Power Efficient cores
Standard eight-core E-core cluster: not exposed to Linux
SMT/Hyper-Threading: disabled
```

Verification:

```bash
cat /sys/devices/cpu_core/cpus
cat /sys/devices/cpu_atom/cpus
cat /sys/devices/system/cpu/online
cat /sys/devices/system/cpu/smt/active
lscpu | grep -E 'Thread|Core|Socket'
```

Expected key results:

```text
P-core CPUs:       0-5
E-core-class CPUs: 6-7
Online CPUs:       0-7
SMT active:        0
Threads per core:  1
```

The eight standard E-cores are not visible to the operating system and therefore cannot execute Linux tasks. CPU 6 and CPU 7 remain active as Low Power E-cores and are reserved for housekeeping work.

Disabling SMT prevents a sibling hardware thread from sharing private core resources with the timing-sensitive experiment, including execution units, private caches, queues, and prediction structures.

**Persistence:**  
These states are controlled by BIOS/firmware and remain active across ordinary reboots unless the firmware configuration is modified or reset.

---

#### 5.2 Applying Linux Kernel Parameters through GRUB

The persistent Linux kernel parameters are defined in:

```text
/etc/default/grub
```

The active configuration is:

```bash
GRUB_CMDLINE_LINUX_DEFAULT="quiet splash isolcpus=1-5 nohz_full=1-5 rcu_nocbs=1-5 processor.max_cstate=1 intel_idle.max_cstate=1 idle=poll"
```

After modifying the file, regenerate the GRUB configuration and reboot:

```bash
sudo update-grub
sudo reboot
```

Verify the parameters received by the running kernel:

```bash
cat /proc/cmdline
```

Expected experimental parameters:

```text
isolcpus=1-5
nohz_full=1-5
rcu_nocbs=1-5
processor.max_cstate=1
intel_idle.max_cstate=1
idle=poll
```

`quiet` and `splash` only control boot-screen output. They do not provide CPU isolation.

The kernel parameters are documented in the [Linux kernel command-line parameter reference](https://docs.kernel.org/admin-guide/kernel-parameters.html).

---

#### 5.3 CPU-Isolation Parameters

The three CPU-isolation parameters have complementary purposes:

| Parameter       | Function                                                     | Noise reduced                                                |
| --------------- | ------------------------------------------------------------ | ------------------------------------------------------------ |
| `isolcpus=1-5`  | Removes CPU 1-5 from normal scheduler load balancing         | Unrelated processes, ordinary task migration, and context switches |
| `nohz_full=1-5` | Suppresses many periodic scheduler ticks when an isolated CPU runs a single userspace task | Scheduler-tick interruptions and operating-system jitter     |
| `rcu_nocbs=1-5` | Offloads RCU callback execution from CPU 1-5                 | Deferred kernel callback execution                           |

##### Scheduler isolation

```text
isolcpus=1-5
```

Verification:

```bash
cat /sys/devices/system/cpu/isolated
```

Expected result:

```text
1-5
```

This keeps ordinary scheduler load away from the experimental P-cores. It does not automatically place the experiment on those CPUs; explicit CPU affinity is still required.

##### Full Dynticks

```text
nohz_full=1-5
```

Verification:

```bash
cat /sys/devices/system/cpu/nohz_full
```

Expected result:

```text
1-5
```

When one isolated CPU runs a single userspace experiment thread, Linux can suppress unnecessary periodic scheduler ticks. This reduces interruptions and timing outliers.

`nohz_full` does not disable all interrupts. Hardware IRQs, IPIs, NMIs, SMIs, page faults, and some unavoidable kernel events may still occur.

##### RCU callback offloading

```text
rcu_nocbs=1-5
```

Verification:

```bash
cat /proc/cmdline | grep -o 'rcu_nocbs=[^ ]*'
```

Expected result:

```text
rcu_nocbs=1-5
```

RCU callbacks perform deferred kernel cleanup. Offloading them reduces asynchronous kernel execution on CPU 1-5.

Together, these parameters establish the following layout:

```text
CPU 0, CPU 6, CPU 7:
Housekeeping CPUs for SSH, background services, kernel work, and redirected device interrupts.

CPU 1-5:
Isolated P-cores reserved for explicitly pinned experimental programs.
```

The general isolation model is described in the [Linux CPU Isolation documentation](https://docs.kernel.org/admin-guide/cpu-isolation.html).

---

#### 5.4 C-State and CPU-Idle Configuration

The following boot parameters restrict idle-state transitions:

```text
processor.max_cstate=1
intel_idle.max_cstate=1
idle=poll
```

Verification:

```bash
cat /sys/module/processor/parameters/max_cstate
cat /sys/module/intel_idle/parameters/max_cstate
cat /sys/devices/system/cpu/cpuidle/current_driver
cpupower idle-info
```

The verified state is:

```text
processor.max_cstate = 1
intel_idle.max_cstate = 1
CPUidle driver       = none
Exposed idle states  = none
```

`processor.max_cstate=1` and `intel_idle.max_cstate=1` prevent deep CPU idle states. `idle=poll` causes an idle CPU to remain active in a polling loop rather than entering a sleep state.

This removes variable wake-up latency from cache-timing measurements.

**Cost:**  
Polling increases power consumption and temperature. Thermal throttling must therefore be avoided during long experiments.

---

#### 5.5 Persistence and Scope

The following settings remain active across ordinary reboots:

```text
BIOS core exposure
SMT disabled
isolcpus=1-5
nohz_full=1-5
rcu_nocbs=1-5
processor.max_cstate=1
intel_idle.max_cstate=1
idle=poll
```

They remain active until the BIOS or GRUB configuration is modified.

The current boot configuration does not include:

```text
mitigations=off
intel_pstate=disable
nokaslr
irqaffinity=...
nosmt
```

Therefore, security mitigations remain enabled where applicable, `intel_pstate` remains active, kernel ASLR is not explicitly disabled, and IRQ placement is handled at runtime rather than by a global GRUB parameter.

---

### 6. Per-Boot Runtime Configuration

The settings in this section are runtime controls. They may be reset after reboot, driver reinitialization, or a later system configuration change.

The following procedure should be executed in order after every reboot and before formal timing measurements.

#### 6.1 Verify the Persistent Environment

First confirm that the expected CPU topology and GRUB settings are active:

```bash
cat /proc/cmdline
cat /sys/devices/system/cpu/isolated
cat /sys/devices/system/cpu/nohz_full
cat /sys/devices/system/cpu/smt/active
cat /sys/devices/cpu_core/cpus
cat /sys/devices/cpu_atom/cpus
```

Expected key results:

```text
P-core CPUs:       0-5
LP E-core CPUs:    6-7
Isolated CPUs:     1-5
Full-dynticks CPUs:1-5
SMT active:        0
```

If these results are incorrect, the experiment should not proceed until the BIOS or GRUB configuration has been corrected.

---

#### 6.2 Select the Performance Governor

Set the CPU frequency governor:

```bash
sudo cpupower frequency-set -g performance
```

Verify CPU 1-5:

```bash
for cpu in 1 2 3 4 5; do
    printf "CPU %s governor: " "$cpu"
    cat "/sys/devices/system/cpu/cpu${cpu}/cpufreq/scaling_governor"
done
```

Expected result:

```text
performance
```

Also record the active frequency driver:

```bash
cat /sys/devices/system/cpu/intel_pstate/status
```

Expected result:

```text
active
```

The `performance` policy reduces low-to-high performance-state transitions and improves timing consistency. It does not guarantee a perfectly fixed physical frequency because hardware-managed P-states, power limits, and thermal conditions may still affect the core.

Reference: [Linux `intel_pstate` documentation](https://docs.kernel.org/admin-guide/pm/intel_pstate.html).

**Reapply after reboot:** Yes.

---

#### 6.3 Disable and Verify Turbo Boost

Disable Turbo Boost through the active `intel_pstate` interface:

```bash
echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo
```

Verify:

```bash
cat /sys/devices/system/cpu/intel_pstate/no_turbo
```

Expected result:

```text
1
```

If supported, also verify that HWP dynamic boost is disabled:

```bash
cat /sys/devices/system/cpu/intel_pstate/hwp_dynamic_boost
```

Expected result:

```text
0
```

Turbo transitions depend on temperature, package power, and workload history. Disabling Turbo removes an important source of frequency-dependent timing variance.

**Reapply or verify after reboot:** Yes.

---

#### 6.4 Select the ASLR Mode

User-space ASLR is controlled through:

```text
/proc/sys/kernel/randomize_va_space
```

For development and debugging:

```bash
echo 0 | sudo tee /proc/sys/kernel/randomize_va_space
```

For realistic validation:

```bash
echo 2 | sudo tee /proc/sys/kernel/randomize_va_space
```

Verify:

```bash
cat /proc/sys/kernel/randomize_va_space
```

Interpretation:

| Value | Meaning                               |
| :---- | ------------------------------------- |
| `0`   | User-space ASLR disabled              |
| `1`   | Partial address randomization         |
| `2`   | Full user-space address randomization |

Disabling ASLR stabilizes the virtual-address layout of newly launched programs. This is useful when debugging instruction addresses, mappings, page boundaries, and PC-indexed microarchitectural behavior.

It does not guarantee identical physical-page allocation.

For Week 1:

```text
Development baseline: ASLR = 0
Realistic validation: ASLR = 2
```

The selected value must be recorded with each dataset.

Reference: [Linux kernel sysctl documentation](https://docs.kernel.org/admin-guide/sysctl/kernel.html).

**Reapply after reboot:** Yes.

---

#### 6.5 Redirect Wired-Network IRQs

The experimental machine is accessed through the wired interface:

```text
enp86s0
```

Redirect its IRQs to the housekeeping CPUs:

```bash
for irq in $(awk '/enp86s0/ {
    gsub(":", "", $1);
    print $1
}' /proc/interrupts); do
    echo 0,6-7 | sudo tee "/proc/irq/$irq/smp_affinity_list"
done
```

Verify both the configured and effective affinity:

```bash
for irq in $(awk '/enp86s0/ {
    gsub(":", "", $1);
    print $1
}' /proc/interrupts); do
    printf "IRQ %-4s configured=" "$irq"
    cat "/proc/irq/$irq/smp_affinity_list"

    printf "         effective ="
    cat "/proc/irq/$irq/effective_affinity_list"
done
```

All effective targets should be CPU 0, CPU 6, or CPU 7.

A previously verified result was:

```text
IRQ 144 configured=0,6-7  effective=6
IRQ 145 configured=0,6-7  effective=7
IRQ 146 configured=0,6-7  effective=0
IRQ 148 configured=0,6-7  effective=6
IRQ 150 configured=0,6-7  effective=7
```

IRQ numbers may change after reboot, so the command searches dynamically by interface name rather than using fixed IRQ numbers.

Check that `irqbalance` is not redistributing the IRQs:

```bash
systemctl is-active irqbalance
```

Expected result:

```text
inactive
```

Check the network interfaces:

```bash
ip -brief link
ip -brief address
```

Wi-Fi should remain down during formal measurements. If necessary:

```bash
sudo ip link set wlo1 down
```

This IRQ procedure redirects the wired-network IRQs only. It does not eliminate all hardware interrupts, IPIs, NMIs, or SMIs.

References:

- [Linux IRQ concepts](https://docs.kernel.org/core-api/irq/concepts.html)
- [Linux `/proc` IRQ affinity interface](https://docs.kernel.org/filesystems/proc.html)

**Reapply after reboot or network-driver reset:** Yes.

---

#### 6.6 Load and Configure the MSR Interface

Load the MSR kernel module:

```bash
sudo modprobe msr
```

Verify access:

```bash
ls -l /dev/cpu/5/msr
```

Read the hardware-prefetcher control register on the experimental P-cores:

```bash
for cpu in 1 2 3 4 5; do
    printf "CPU %s MSR 0x1A4 = 0x" "$cpu"
    sudo rdmsr -p "$cpu" -x 0x1a4
done
```

`MSR 0x1A4` uses disable bits:

| Bit  | Controlled prefetcher           | `0`     | `1`      |
| :--- | ------------------------------- | ------- | -------- |
| 0    | L2 hardware/streamer prefetcher | Enabled | Disabled |
| 1    | L2 adjacent-line prefetcher     | Enabled | Disabled |
| 2    | DCU next-line prefetcher        | Enabled | Disabled |
| 3    | DCU IP/L1 IP-stride prefetcher  | Enabled | Disabled |
| 6    | LLC Page Prefetcher             | Enabled | Disabled |
| 7    | Array of Pointers prefetcher    | Enabled | Disabled |

##### Week 1 Flush+Reload baseline

Basic Flush+Reload calibration does not require the AOP-specific `0x47` configuration.

For Week 1, record the current state before running the experiment:

```bash
sudo rdmsr -p 5 -x 0x1a4
```

A value of:

```text
00
```

means that the listed prefetchers are enabled.

Do not silently modify the prefetcher configuration without recording the selected state.

##### Later single-core AOP experiments

To keep only the DCU IP/L1 IP-stride prefetcher and AOP enabled on CPU 5:

```bash
sudo wrmsr -p 5 0x1a4 0x47
sudo rdmsr -p 5 -x 0x1a4
sudo rdmsr -p 5 -f 8:8 0x48
```

Expected results:

```text
MSR 0x1A4 = 47
DDPD_U     = 0
```

##### Later CPU 4/CPU 5 AOP experiments

```bash
for cpu in 4 5; do
    sudo wrmsr -p "$cpu" 0x1a4 0x47
done

for cpu in 4 5; do
    printf "CPU %s MSR 0x1A4 = 0x" "$cpu"
    sudo rdmsr -p "$cpu" -x 0x1a4

    printf "CPU %s DDPD_U = " "$cpu"
    sudo rdmsr -p "$cpu" -f 8:8 0x48
done
```

`DDPD_U` is bit 8 of `IA32_SPEC_CTRL`. A value of `0` means that user-mode data-dependent prefetching is not disabled by this bit.

Do not overwrite the complete `IA32_SPEC_CTRL` register because other bits may be controlled by the kernel.

References:

- [Intel Optimization Reference Manual update](https://cdrdv2-public.intel.com/821613/355308-Optimization-Reference-Manual-050-Changes-Doc.pdf)
- [Intel Data Dependent Prefetcher documentation](https://www.intel.com/content/www/us/en/developer/articles/technical/software-security-guidance/technical-documentation/data-dependent-prefetcher.html)
- [Intel Model-Specific Register documentation](https://cdrdv2-public.intel.com/874253/335592-090-sdm-vol-4.pdf)

**Reapply after reboot:** Yes. MSR settings are runtime-only and per logical CPU.

---

#### 6.7 Final Pre-Run Check and CPU Pinning

Before launching the experiment, confirm the selected core is a P-core and that the program will use a single-CPU affinity mask.

Run the Week 1 program on CPU 5:

```bash
taskset -c 5 ./week1_probe
```

If root access is required:

```bash
sudo taskset -c 5 ./week1_probe
```

Verify the affinity of a running process:

```bash
taskset -pc "$(pgrep -n week1_probe)"
```

Expected result:

```text
5
```

`isolcpus=1-5` keeps ordinary scheduler load away from the experimental cores. `taskset -c 5` places the actual experiment on CPU 5 and prevents it from migrating to another P-core or to CPU 6-7.

For later multi-thread experiments, affinity must be set per thread:

```text
sender thread   -> CPU 4
receiver thread -> CPU 5
```

A process-wide mask of `4,5` is insufficient for strict per-thread placement because either thread may migrate between those two CPUs.

References:

- [taskset(1)](https://man7.org/linux/man-pages/man1/taskset.1.html)
- [sched_setaffinity(2)](https://man7.org/linux/man-pages/man2/sched_setaffinity.2.html)
- [pthread_setaffinity_np(3)](https://man7.org/linux/man-pages/man3/pthread_setaffinity_np.3.html)

---

#### 6.8 Per-Boot Command Sequence

The following condensed sequence is used after each reboot:

```bash
# 1. Verify persistent topology and isolation
cat /proc/cmdline
cat /sys/devices/system/cpu/isolated
cat /sys/devices/system/cpu/nohz_full
cat /sys/devices/system/cpu/smt/active
cat /sys/devices/cpu_core/cpus
cat /sys/devices/cpu_atom/cpus

# 2. Select the performance governor
sudo cpupower frequency-set -g performance

# 3. Disable Turbo Boost
echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo

# 4. Select the required ASLR mode
# Development:
echo 0 | sudo tee /proc/sys/kernel/randomize_va_space
# Realistic validation:
# echo 2 | sudo tee /proc/sys/kernel/randomize_va_space

# 5. Keep Wi-Fi disabled
sudo ip link set wlo1 down

# 6. Redirect wired Ethernet IRQs
for irq in $(awk '/enp86s0/ {
    gsub(":", "", $1);
    print $1
}' /proc/interrupts); do
    echo 0,6-7 | sudo tee "/proc/irq/$irq/smp_affinity_list"
done

# 7. Load MSR support and record the prefetcher state
sudo modprobe msr
sudo rdmsr -p 5 -x 0x1a4

# 8. Run the program on one isolated P-core
taskset -c 5 ./week1_probe
```

The AOP-specific `wrmsr 0x47` command is applied only when the experiment explicitly requires that prefetcher configuration.

---

### 7. Software-Level Measurement and Memory Configuration

This section records the program-level controls required for the Week 1 Flush+Reload primitive. These controls are implemented in the experimental program rather than through BIOS or GRUB.

#### 7.1 Timing and Cache-Flush Capabilities

Verify the required processor capabilities:

```bash
grep -m1 '^flags' /proc/cpuinfo
```

The relevant CPU flags include:

```text
clflush
clflushopt
clwb
rdtscp
constant_tsc
nonstop_tsc
tsc_known_freq
```

Their roles are:

| Capability       | Purpose                                                      |
| ---------------- | ------------------------------------------------------------ |
| `clflush`        | Evicts the selected cache line from the cache hierarchy      |
| `rdtscp`         | Reads the timestamp counter with partial execution ordering  |
| `constant_tsc`   | Indicates a constant-rate timestamp counter                  |
| `nonstop_tsc`    | Indicates that the timestamp counter continues across idle states |
| `tsc_known_freq` | Indicates that the operating system knows the TSC frequency  |

The Week 1 timer must use one consistent fenced timestamp sequence for every sample. The selected sequence and fence placement must be documented in the source code and must not change between cached and flushed measurements.

A typical structure is:

```c
lfence();
start = rdtscp();

load_target();

lfence();
end = rdtscp();
lfence();
```

The timestamp counter provides a stable reference clock, but instruction execution time may still vary with core performance state. This is why the runtime frequency configuration remains necessary.

Reference: [Intel 64 and IA-32 Software Developer's Manual, Instruction Set Reference](https://cdrdv2-public.intel.com/812389/325383-sdm-vol-2abcd.pdf).

---

#### 7.2 Shared Read-Only Mapping

The Week 1 Flush+Reload probe uses a shared file-backed page or a shared library page.

A file-backed mapping can be created with:

```c
int fd = open(path, O_RDONLY);

void *mapping = mmap(
    NULL,
    mapping_length,
    PROT_READ,
    MAP_SHARED,
    fd,
    0
);
```

Required properties:

```text
File opened read-only
Mapping created with PROT_READ
Mapping created with MAP_SHARED
Probe address located inside the shared mapped page
Selected cache line treated as a 64-byte cache line
```

`MAP_SHARED` allows different processes mapping the same file-backed page to refer to the same physical page-cache page, which is the sharing property required by Flush+Reload.

The probe must not rely on a private anonymous mapping when demonstrating cross-process shared-page behavior.

---

#### 7.3 Memory and Page Preparation

The recorded operating-system state is:

```text
Transparent Huge Pages enabled mode: madvise
Transparent Huge Pages defrag mode:  madvise
Reserved 2 MiB huge pages:            128
hugetlbfs mount:                       /dev/hugepages
NUMA nodes:                            1
```

Verification:

```bash
cat /sys/kernel/mm/transparent_hugepage/enabled
cat /sys/kernel/mm/transparent_hugepage/defrag
grep -E 'HugePages|Hugepagesize|AnonHugePages' /proc/meminfo
mount | grep hugetlbfs
numactl --hardware
```

The basic Week 1 file-backed Flush+Reload probe does not require reserved huge pages. The existing huge-page configuration is recorded but does not need to be changed.

Before collecting timing samples, the program should:

1. Create all mappings and result arrays.
2. Touch the mapped page to resolve the initial page fault.
3. Touch every page of the result buffer.
4. Warm up the timestamp and measurement path.
5. Avoid new memory allocation inside the timing loop.

This prevents first-access page faults and allocator activity from appearing as cache-timing outliers.

Any experiment that deliberately requires 4 KiB pages or disables Transparent Huge Pages must record that change separately. Such a change is not part of the default Week 1 Flush+Reload setup.

---

#### 7.4 Measurement-Loop Rules

The timing loop should contain only the operations required for cache-state preparation, target access, and timestamp collection.

During the timing loop:

```text
No per-sample printf
No file writing
No SCP transfer
No compilation
No package installation
No sleep or usleep
No repeated open or close operations
No unnecessary system calls
```

Samples should be stored in a preallocated memory buffer and written to disk only after all timing measurements have completed.

Recommended execution sequence:

```text
Transfer source files
-> compile the program
-> configure the runtime environment
-> keep SSH idle
-> execute the timing loop
-> store samples in memory
-> write the result file after measurement
-> transfer the result file afterward
```

An idle SSH connection is acceptable, but continuous terminal output or network transfer may generate Ethernet traffic and IRQ activity.

---

#### 7.5 Week 1 Measurement Requirements

The Week 1 implementation should:

1. Map a shared read-only file or shared library page.
2. Select one 64-byte cache line.
3. Warm up the mapping and timing path.
4. Collect a large cached-access dataset.
5. Flush the same line with `clflush`.
6. Collect a large flushed-access dataset.
7. Use an identical timing sequence for both datasets.
8. Store samples in memory during measurement.
9. Plot cached and flushed latency distributions.
10. Select and document a cache-hit threshold.
11. Repeat the experiment to confirm that the distributions remain stable.

The goal is not to eliminate every possible asynchronous event. The goal is to obtain clearly separated and reproducible cache-hit and cache-miss distributions while documenting the remaining limitations.

Unavoidable events may still include:

```text
NMI
SMI
IPI
some hardware IRQs
page-table shootdowns
firmware activity
occasional kernel activity
```

These events should appear as outliers rather than changing the main cached and flushed distributions.

---

### 8. Final Week 1 Environment Summary

| Item               | Week 1 configuration                               | Persistence             |
| ------------------ | -------------------------------------------------- | ----------------------- |
| Processor          | Intel Core Ultra 7 155H                            | Hardware                |
| OS                 | Ubuntu 24.04.4 LTS, kernel `6.8.0-100-generic`     | Installed system        |
| Visible cores      | CPU 0-5 P-cores; CPU 6-7 LP E-cores                | BIOS-controlled         |
| Core roles         | CPU 0,6,7 housekeeping; CPU 1-5 isolated           | BIOS/GRUB               |
| SMT                | Disabled                                           | BIOS-controlled         |
| CPU isolation      | `isolcpus=1-5 nohz_full=1-5 rcu_nocbs=1-5`         | GRUB-persistent         |
| Idle state         | C-state limited to 1; `idle=poll`                  | GRUB-persistent         |
| Experiment core    | CPU 5 using `taskset -c 5`                         | Every launch            |
| Governor           | `performance`                                      | Set after reboot        |
| Turbo              | Disabled, `no_turbo=1`                             | Verify/set after reboot |
| ASLR               | `0` for development; `2` for realistic validation  | Set after reboot        |
| Ethernet IRQs      | Restricted to CPU 0,6,7                            | Set after reboot        |
| Wi-Fi              | Down                                               | Verify after reboot     |
| Week 1 prefetchers | Record `MSR 0x1A4`; no AOP-specific write required | Verify after reboot     |
| Cache line         | 64 bytes                                           | Hardware                |
| Timing support     | `rdtscp`, `constant_tsc`, `nonstop_tsc`            | Hardware                |
| Flush instruction  | `clflush`                                          | Hardware                |
| Shared memory      | Read-only file-backed `MAP_SHARED` mapping         | Program-level           |
| Measurement output | Buffered in memory and written after sampling      | Program-level           |

---

### 9. Official References

- [Intel Core Ultra 7 155H Product Specifications](https://www.intel.com/content/www/us/en/products/sku/236847/intel-core-ultra-7-processor-155h-24m-cache-up-to-4-80-ghz/specifications.html)
- [Intel Optimization Reference Manual Update: Redwood Cove AOP and LLCPP](https://cdrdv2-public.intel.com/821613/355308-Optimization-Reference-Manual-050-Changes-Doc.pdf)
- [Intel 64 and IA-32 Software Developer's Manual, Instruction Set Reference](https://cdrdv2-public.intel.com/812389/325383-sdm-vol-2abcd.pdf)
- [Intel 64 and IA-32 Software Developer's Manual, Model-Specific Registers](https://cdrdv2-public.intel.com/874253/335592-090-sdm-vol-4.pdf)
- [Intel Data Dependent Prefetcher Security Documentation](https://www.intel.com/content/www/us/en/developer/articles/technical/software-security-guidance/technical-documentation/data-dependent-prefetcher.html)
- [Linux Kernel Command-Line Parameters](https://docs.kernel.org/admin-guide/kernel-parameters.html)
- [Linux CPU Isolation Documentation](https://docs.kernel.org/admin-guide/cpu-isolation.html)
- [Linux `intel_pstate` Documentation](https://docs.kernel.org/admin-guide/pm/intel_pstate.html)
- [Linux IRQ Concepts](https://docs.kernel.org/core-api/irq/concepts.html)
- [Linux `/proc` IRQ Affinity Interface](https://docs.kernel.org/filesystems/proc.html)
- [Linux Kernel Sysctl Documentation](https://docs.kernel.org/admin-guide/sysctl/kernel.html)
- [taskset(1)](https://man7.org/linux/man-pages/man1/taskset.1.html)
- [sched_setaffinity(2)](https://man7.org/linux/man-pages/man2/sched_setaffinity.2.html)
- [pthread_setaffinity_np(3)](https://man7.org/linux/man-pages/man3/pthread_setaffinity_np.3.html)
