# CH32V203C8T USB IAP / Bootloader 方案与开发记录

> 状态：V1.00 IAP 代码、Qt 升级页、独立构建和工厂合并工具已完成；软件测试通过，等待硬件升级与断电恢复测试。
>
> 本文件同时作为后续会话的上下文恢复记录。新会话先读“快速恢复上下文”“已完成的实施内容”和“历史进程记录”。

## 1. 快速恢复上下文

- 芯片：CH32V203C8T，64KB Flash、20KB RAM，普通 Flash 擦除扇区为 4KB。
- V1.00 APP 构建占用：Flash `38,680B`，RAM `14,344B`（含固定 2KB 栈）；APP 入口为 `0x00003100`。
- V1.00 Bootloader 构建占用：Flash `11,700B / 12KB`，剩余 `588B`；后续非必要功能放在 Qt，不再挤占 BOOT。
- 最后一个 4KB 扇区 `0x0800F000-0x0800FFFF` 已用于六枪配置，升级不得擦写。
- V0.99 六枪混合烧录和同一鼠标 USB 口的 HID 配置导入已由用户实测通过。
- 固件远端基线：`9ce98a0`（新增 USB HID 配置导入通道）。
- Qt 远端基线：`ba5d504`（新增通过鼠标 USB 接口烧录六枪配置）。
- 客户日常升级只使用鼠标现有 USB 线，不要求串口、WCH-Link/JLINK 或额外驱动。
- 旧 V0.99 板第一次安装 Bootloader 仍需工厂使用 WCH-Link/JLINK 烧录一次合并 HEX；装好以后才可永久改走 USB 升级。

## 2. 评审结论

方案可行，推荐使用：

`12KB 固定 Bootloader + 48KB 单 APP 分区 + 4KB 六枪配置区`

这满足“只有一个应用分区”的要求。48KB APP 分区内部再保留 256B 镜像头，不是第二个 APP，也不保存旧固件副本。

单 APP 的代价是没有自动回滚：升级开始后如果断电，旧 APP 已不完整，设备会停在 Bootloader 等待重新上传。但 Bootloader 自身不被擦除，因此可通过 USB 恢复，不会成为必须拆机接调试器的“真砖”。

## 3. Flash 与 RAM 规划

### 3.1 Flash 总体分区

| 区域 | 扇区 | 物理地址 | 大小 | 作用 |
|---|---:|---|---:|---|
| Bootloader | 0-2 | `0x08000000-0x08002FFF` | 12KB | 固定启动、USB 升级、校验和 APP 跳转 |
| APP 分区 | 3-14 | `0x08003000-0x0800EFFF` | 48KB | 一个应用分区，包含镜像头和 APP 本体 |
| └ 镜像头 | 3 内部 | `0x08003000-0x080030FF` | 256B | 长度、版本、CRC32、状态和失败码 |
| └ APP 本体 | 3-14 | `0x08003100-0x0800EFFF` | 48,896B | 唯一可执行应用镜像 |
| 六枪配置 | 15 | `0x0800F000-0x0800FFFF` | 4KB | 保留现有六枪 JSON 转换结果，升级禁止擦除 |

关键规则：

- Bootloader 只能擦写 `0x08003000-0x0800EFFF`。
- APP 每次链接都必须限制在 `0x00003100-0x0000EFFF`，越界时构建直接失败。
- Qt 和 Bootloader 都要做边界检查，任一方发现 Boot 区或配置区地址立即拒绝升级。
- Bootloader 量产验证稳定后，可通过 Option Bytes 对扇区 0-2 做写保护；开发期先不自动修改 Option Bytes。
- 扇区 15 的六枪配置继续沿用现有布局和 CRC，IAP 不改变其格式。

### 3.2 地址别名约定

当前 CH32 工程按 Flash 执行别名 `0x00000000` 链接，而 Flash 控制器使用物理地址 `0x08000000` 擦写。因此约定：

- APP 链接地址：`0x00003100`。
- APP 物理写入地址：`0x08003100`。
- Qt 解析 HEX 后统一转为“相对 APP 起点的 offset”；Bootloader 最终写入 `0x08003100 + offset`。
- 第一版日常升级包只接受 APP HEX，不接受包含 Bootloader 的工厂合并 HEX。
- 如兼容 `0x00003100` 和 `0x08003100` 两种 HEX 地址别名，Qt 必须先判断整份文件只使用一种别名，再统一归一化，禁止混用。

### 3.3 容量评估

- V1.00 APP Flash 占 `38,680B`，APP 本体可用 `48,896B`，剩余约 `10,216B`。
- V1.00 APP RAM 占 `14,344B`（含 2,048B 栈），预留 16B 邮箱后余量约 `6,120B`。
- V1.00 Bootloader Flash 占 `11,700B`，离 12KB 上限 `588B`；链接脚本会在超限时直接失败。
- Bootloader 和 APP 不同时运行，因此两者的 RAM 占用不叠加。

## 4. 工程和文件结构

BOOT 与 APP 使用独立工程、独立链接脚本和独立输出 HEX，这是常见且推荐的嵌入式 IAP 组织方式。第一阶段不大规模搬动已验证的 APP 目录，以免同时引入 MounRiver 工程路径问题。

建议结构：

```text
CH32V203C8T/
├─ Bootloader/          独立 Makefile、链接脚本与源码，输出 SBZFQ_BOOT.hex
├─ Common/              仅放分区、镜像头、CRC 和 USB IAP 协议公共定义
├─ Core/ Ld/ User/ ...  现有 APP 工程，输出 SBZFQ_APP.hex
├─ release/             工厂首次烧录用 SBZFQ_FACTORY.hex
└─ IAP_BOOTLOADER_方案与开发记录.md
```

稳定后如确实需要更整齐，可再把现有 APP 整体迁入 `Application/`。第一版不建议一边做 Bootloader，一边大规模移动现有 APP 文件。

输出文件分工：

| 文件 | 使用对象 | 是否可通过客户 USB 升级 |
|---|---|---|
| `SBZFQ_BOOT.hex` | 工厂/研发 | 否 |
| `SBZFQ_APP.hex` | 客户日常升级 | 是 |
| `SBZFQ_FACTORY.hex` | 新板首次生产，BOOT + APP | 否，需 WCH-Link/JLINK |

## 5. 启动与升级状态机

### 5.1 正常启动

1. CPU 每次复位都先进入 `0x00000000` 的 Bootloader。
2. Bootloader 读取并立即清除 SRAM IAP 请求邮箱。
3. 校验镜像头魔数、版本、状态、APP 长度、入口地址和不可变头 CRC32；完整 APP CRC32 已在每次升级提交前验证，正常开机不重复计算，以缩短启动延迟。
4. 镜像完全有效且没有 IAP 请求时，关闭 Bootloader 外设并跳转 APP `_start`。
5. 镜像无效、上次升级失败或存在 IAP 请求时，枚举为 USB Bootloader，等待 Qt。

正常启动时不必先枚举 Bootloader USB，因此不会让鼠标每次上电多出现一次 USB 设备切换。40KB 左右镜像的 CRC32 应在样机上实测，目标是正常启动增加的延迟不明显。

### 5.2 APP 主动进入升级

1. Qt 在当前 `1A86:FE01` 厂商 HID 接口发送 `ENTER_IAP`。
2. APP 先完成 USB ACK，避免 Qt 误判命令丢失。
3. APP 关闭中断，在 SRAM 固定邮箱写入 `MAGIC、MAGIC 反码、原因、原因反码`。
4. 延迟约 200-250ms，让 Windows 收到 ACK 后执行软件复位。
5. Bootloader 识别一次性邮箱，清除邮箱并进入升级模式。

SRAM 邮箱建议固定为 `0x20004FF0-0x20004FFF`。APP 和 Bootloader 的链接脚本都必须把栈顶下移到 `0x20004FF0`，不能让当前 2KB 栈覆盖邮箱；邮箱段应标记为 `NOLOAD/.noinit`，Bootloader 清 `.bss` 时也不能清掉它。

需要在样机上专门验证“软件复位后 SRAM 邮箱保留”。即使邮箱因上电复位消失，Bootloader 仍会依靠镜像有效性决定是否进入恢复模式。

### 5.3 Bootloader 下载流程

```text
WAIT_BEGIN
    │ BEGIN 合法
    ▼
ERASING ──逐页擦除 12 个 APP 扇区──► READY_DATA
    │                                      │ DATA + ACK offset
    │ 擦除失败                             ▼
    └────────► FAILED ◄────────────── DOWNLOADING
                                           │ END
                                           ▼
                                      VERIFYING
                                      │         │
                                  CRC 失败    CRC 成功
                                      ▼         ▼
                                    FAILED    VALID → 复位
```

- `BEGIN` 先快速确认参数，再由主循环逐页擦除，不能在 USB 控制请求回调里阻塞擦除全部 12 页。
- Qt 在擦除阶段轮询 `STATUS`，按设备返回的已擦页数显示进度。
- DATA 不在 RAM 中缓存整份固件，每包写入 Flash、读回比对成功后才推进 `expected_offset`。
- ACK 丢失后重复发送同一 offset 时，Bootloader 应读取 Flash 比较并返回当前 offset，使传输幂等。
- `END` 只有在 `expected_offset == image_size` 时才接受。
- 全镜像 CRC32 正确后，最后一步才写入 VALID 提交字；随后短暂常亮 LED 并复位。

## 6. RISC-V 中断向量和 APP 跳转

CH32V203 是 RISC-V，不应照搬 Cortex-M 的 MSP/Reset_Handler 双向量字跳转，也不使用 Cortex VTOR。

APP 链接脚本核心约束：

```ld
FLASH (rx) : ORIGIN = 0x00003100, LENGTH = 0x0000BF00
RAM (xrw)  : ORIGIN = 0x20000000, LENGTH = 0x00004FF0
```

APP 启动文件应继续由链接位置得到向量表地址：

```asm
la   t0, _vector_base
ori  t0, t0, 3
csrw mtvec, t0
```

Bootloader 跳转前需要：

1. 关闭全局中断。
2. 停止 SysTick、USB 和 Bootloader 使用的其他外设。
3. 清除 PFIC 中断使能和 pending 状态。
4. 让 USB 设备从总线上断开，避免 FE02 描述符状态带入 APP。
5. 执行 `fence` / `fence.i`。
6. 跳到别名地址 `0x00003100` 的 APP `_start`，不要直接跳 `main`。

参考性伪代码：

```c
typedef void (*app_entry_t)(void);

__attribute__((noreturn)) static void boot_jump_to_app(void)
{
    disable_global_irq();
    stop_systick();
    usb_iap_disconnect();
    pfic_disable_and_clear_all();
    __asm volatile("fence\n\tfence.i" ::: "memory");
    ((app_entry_t)0x00003100UL)();
    for (;;) {}
}
```

最终代码要以 WCH 启动文件和 PFIC 寄存器定义为准。构建后必须用 map/objdump 检查 `_start`、`_vector_base` 和 `mtvec` 装载地址确实位于 APP 分区。

## 7. USB 方案

### 7.1 设备身份

| 模式 | VID:PID | USB 形态 | 作用 |
|---|---|---|---|
| 正常 APP | `1A86:FE01` | 现有鼠标复合设备 + 厂商 HID | 正常鼠标、六枪配置、发送 `ENTER_IAP` |
| Bootloader | 建议 `1A86:FE02` | 单一厂商 HID | 只做固件升级，不模拟鼠标 |

BOOT 使用不同 PID 可避免 Windows 对两套描述符的缓存冲突，也方便 Qt 判断设备当前处于 APP 还是恢复模式。两个模式应保持同一稳定序列号，便于多设备环境下匹配同一块板。

第一版沿用已经验证过的 HID Feature Report + hidapi 路线，不要求安装驱动。若后续实测 Feature Report 速度不够，再评估同一 HID 下的 Interrupt OUT/IN 报告，不在第一版同时引入两套传输方式。

### 7.2 63B 有效载荷格式

hidapi 缓冲区共 64B，其中第 0 字节是值为 0 的 Report ID 占位；USB 有效载荷为后续 63B。

| 有效载荷偏移 | 大小 | 内容 |
|---:|---:|---|
| 0 | 4 | 魔数 `SBI1`，同时表示协议 v1 |
| 4 | 1 | 命令 |
| 5 | 1 | 非零会话号 |
| 6 | 1 | 包序号，可回绕 |
| 7 | 1 | 请求保留；响应为状态码 |
| 8 | 4 | offset；响应中为设备确认的 `expected_offset` |
| 12 | 4 | 镜像总长度 |
| 16 | 1 | 数据长度，最大 44B |
| 17 | 44 | 数据，不足补零 |
| 61 | 2 | 前 61B 的 CRC-16/CCITT-FALSE |

建议命令：`PING、STATUS、BEGIN、DATA、END、ABORT、RESET`。响应携带状态码、已擦页数、`expected_offset`、当前阶段和持久失败码。

BEGIN 数据至少包含：

- APP 长度。
- APP CRC32。
- 入口地址，第一版必须是 `0x00003100`。
- 固件版本和构建编号。
- 可选的镜像头格式版本。

## 8. 镜像头、失败标记和断电恢复

### 8.1 256B 镜像头建议字段

| 字段 | 作用 |
|---|---|
| magic / header_version / header_size | 判断格式是否合法 |
| image_size / image_crc32 | 完整镜像校验 |
| entry_address | 第一版固定 `0x00003100` |
| firmware_version / build_id | Qt 展示和售后追踪 |
| immutable_header_crc32 | 校验上述不可变字段 |
| state_word | EMPTY / DOWNLOADING / FAILED / VALID / BOOTING / CONFIRMED |
| error_code / error_offset | 持久记录失败阶段 |
| reserved | 后续协议兼容，保持 `0xFF` |

Flash 只能把位从 1 写成 0，因此状态编码必须单向提交。例如从擦除态 `0xFFFFFFFF` 开始：

- DOWNLOADING：清 bit0。
- VALID：在 DOWNLOADING 基础上再清 bit1。
- FAILED：在 DOWNLOADING 基础上改清 bit2。
- BOOTING：首次跳转 APP 前再清 bit2，表示尚未确认启动。
- CONFIRMED：APP 被 Windows 配置为 FE01 后再清 bit3。

Bootloader 只接受精确合法的状态值，其他组合一律视为无效。再次升级时擦除扇区 3，重新开始状态周期。

### 8.2 断电行为

| 场景 | 持久状态 | 下次上电 |
|---|---|---|
| 从未开始升级 | VALID | CRC 正确后进入 APP |
| 只进入 BOOT，尚未 BEGIN | 原 VALID 不变 | 可退出或重新上电回 APP |
| 擦除/下载中断电 | 无 VALID，DOWNLOADING 或空镜像头 | 停在 FE02，重新完整上传 |
| Flash 写入失败 | FAILED + 错误码/offset | 停在 FE02，Qt 显示原因并允许重试 |
| 最终 CRC32 失败 | FAILED + VERIFY 错误 | 停在 FE02，重新上传 |
| 校验成功 | 最后写 VALID | 复位并重新枚举 FE01 |
| VALID 后首次 APP 未启动成功 | BOOTING | 下次上电留在 FE02，允许重新升级 |
| APP 已成功枚举 FE01 | CONFIRMED | 后续正常直接启动 APP |

失败标记只写在 APP 分区镜像头，不能占用或擦除六枪配置区。单 APP 第一版不实现断点续传；掉电后重新从 BEGIN 完整升级，逻辑更简单可靠。

## 9. Qt 进度和用户体验

Qt 固件升级页建议按以下阶段显示：

| 阶段 | 进度 | 进度依据 |
|---|---:|---|
| 校验 HEX、发送 ENTER_IAP | 0%-5% | 本地校验和 APP ACK |
| 等待 FE02 枚举 | 5% | 发现目标 VID/PID 和序列号 |
| 擦除 APP 分区 | 5%-10% | 设备返回的 `erased_pages / 12` |
| DATA 下载 | 10%-90% | 设备 ACK 的 `expected_offset / image_size` |
| 全镜像校验与提交 | 90%-98% | Bootloader STATUS |
| FE01 重新枚举 | 98%-100% | Windows 再次发现 `1A86:FE01` |

不能按“PC 已发送字节数”计算进度；USB 超时或重发时，只有 MCU 已确认的 offset 才能推进进度。

Qt 需要：

- 后台线程执行升级，界面不能卡死。
- 选择 HEX 后先显示版本、大小、地址范围和 CRC32，再允许开始。
- 自动识别当前是 FE01 还是 FE02；设备已在失败恢复模式时直接开始 BOOT 流程。
- 每包超时后有限次重试，并以设备返回的 `expected_offset` 重新同步。
- 保存升级日志：HEX 校验、模式切换、擦除页数、最后确认 offset、CRC、失败码和重枚举结果。
- 失败时保留明确的“可重新升级”按钮，不把可恢复失败显示成设备报废。

## 10. LED 分配

Bootloader 模式复用板载 LED，不增加超过四种表现：

| LED | Bootloader 含义 |
|---|---|
| 熄灭 | 校验成功，准备复位进入 APP；正常 APP 下按 APP 自身规则显示 |
| 慢闪 | FE02 等待升级，或失败后等待重试 |
| 快闪 | 正在擦除或校验 |
| 常亮 | 正在接收并写入 DATA |

进入 APP 后继续使用现有压枪/切枪 LED 规则。BOOT 和 APP 不同时运行，因此两套含义不会冲突；精确失败原因由 Qt 显示。

## 11. 升级时间评估

当前 APP `38,680B`，每个 DATA 包最多 44B，约需：

`ceil(38680 / 44) = 880 包`

| 环节 | 估算 |
|---|---:|
| APP → BOOT 重新枚举 | 1-3s |
| 擦除 12 个 4KB 扇区 | 0.5-2s |
| 880 包下载、写入和读回 | 4-13s（按约 5-15ms/包） |
| 全镜像 CRC32 与镜像头提交 | 0.1-0.5s |
| BOOT → APP 重新枚举 | 1-3s |
| 预计总时间 | 当前 Feature Report 实测约 57-65s；后续快速页写版本再评估提速 |

建议超时：单包 1.0s、BEGIN/擦除阶段 8s、完整升级 90s。首轮硬件实测 38,680B 镜像稳定约 699B/s，45s 在约 81% 处触发了主机总超时，因此调整为 90s；后续应在至少 3 台 Windows 电脑各升级 10 次，记录 P50/P95 再调整界面提示。

## 12. 安全边界

- 所有 `offset + length` 计算先检查整数溢出，再检查是否落在 APP 本体范围。
- DATA 长度最大 44B；除最后一包外应按 Flash 写入宽度对齐，最后一包用 `0xFF` 补齐但 CRC32 只计算真实镜像长度。
- 单包 CRC16 防止传输错误，整镜像 CRC32 防止残缺镜像。
- 写入后立即读回比对，成功后才 ACK 新 offset。
- Bootloader 空闲时喂狗；擦除和 CRC 循环也必须处理看门狗。
- Bootloader 目标体积 6-10KB，链接上限硬限制为 12KB，超限必须构建失败。
- CRC 只能防误码，不能防恶意固件。如果后期要禁止第三方固件，需要另行评估数字签名、公钥存储和 Boot 体积，第一版不加入。

## 13. 已完成的实施内容

1. `Common/` 已实现分区、镜像头、邮箱、CRC16/CRC32 和 SBI1 协议。
2. APP 已平移到 `0x3100`，栈顶限制到 `0x20004FF0`，FE01 已实现 `ENTER_IAP` ACK 后延迟复位。
3. `Bootloader/` 已实现 FE02 单 HID、逐扇区擦除、44B 分片写后回读、丢 ACK 幂等确认、增量 CRC32、失败标记、LED 和 RISC-V `_start` 跳转。
4. Qt V1.00 已实现严格 HEX 解析、FE01→FE02→FE01、后台升级、设备 ACK 进度、偏移重同步、详细持久日志和日志导出；升级后只确认 FE01 重新枚举，不在 Windows 刚加载复合 HID 时强制执行 Feature Report 握手。
5. `tools/merge_factory_hex.py` 已实现 BOOT + VALID APP 工厂 HEX；客户 USB 只选择 APP HEX。
6. BOOT/APP 均已交叉编译通过，Qt 13 项测试通过；硬件已确认完整下载、CRC32、VALID、BOOT→APP 跳转和失败恢复，剩余重点是六枪配置保留及批量循环可靠性验证。

## 14. 验收清单

| 测试 | 方法 | 通过标准 |
|---|---|---|
| BOOT/APP 地址 | 检查 map/objdump | BOOT ≤12KB；APP 从 0x3100 开始且不越过 0xEFFF |
| 正常启动 | 连续冷启动/软复位 | 无异常 USB 枚举，正常进入 FE01 APP |
| 正常升级 | FE01 进入 IAP 并完整上传 | 进度单调到 100%，CRC32/VALID 成功且 FE01 重新出现 |
| 断电恢复 | 擦除及 DATA 10%/50%/90% 拔电 | 重插均出现 FE02，可重新升级 |
| 坏包/错序/坏 CRC | 人为篡改协议包 | 明确报错，offset 不错误推进 |
| 非法 HEX 地址 | 包含 Boot 或配置区记录 | Qt 和 Bootloader 均拒绝 |
| Flash 写失败 | 故障注入 | 保存失败码，不跳残缺 APP |
| 六枪配置保留 | 升级前后读取/实测切枪 | 扇区 15 内容和行为完全不变 |
| 循环可靠性 | 连续升级至少 30 次 | 无失败、无配置丢失、无异常枚举 |
| 多电脑耗时 | 3 台 Windows 电脑各 10 次 | 记录 P50/P95，校准超时和提示 |
| 旧板迁移 | 工厂合并 HEX 首刷 | 首次硬件工具安装后，后续仅 USB |

## 15. 已确认决策

以下五项均已由用户确认并按此实现：`12KB BOOT + 单 APP + 4KB 配置区`、单 APP 失败后 FE02 恢复、BOOT 使用 `1A86:FE02`、第一版使用 HID Feature Report、升级总超时按 90 秒设计。

## 16. 历史进程记录

| 日期 | 里程碑 | 结果/证据 |
|---|---|---|
| 2026-09-12 | V0.99 六枪混合烧录 | 固件 Tag `4fdafb9`；Qt Tag `5091351` |
| 2026-09-12 | 同鼠标 USB HID 配置导入 | 固件 `9ce98a0`；Qt `ba5d504`；用户实测通过 |
| 2026-09-13 | 远端备份 | 固件已推 `mouse-bridge-ch32`；Qt 已推私有 `mouse-bridge-qt` |
| 2026-09-13 | IAP 方案评审 | 只新增本 Markdown 方案，尚未实施代码，等待用户批准 |
| 2026-09-13 | V1.00 USB IAP 实现 | BOOT `11,700B`、APP `38,680B`；APP 入口 `0x3100`；Qt 13 项测试通过；待硬件测试 |
| 2026-09-14 | BOOT 独立 MounRiver 工程 | `Bootloader` 可作为 `SBZFQ_BOOT` 单独导入、编译和用 WCH-Link 下载；待用户首次实机烧录 |
| 2026-09-14 | FE02 描述符与 DATA 超时修复 | HID 报表长度修正后 Windows Code 10 消失；修复 DATA 处理后使用旧时间导致无符号下溢误报 `0x88`，并改为原子发布响应以消除 GET_REPORT 读写竞态；BOOT `0x00010001`，Flash `11,668B`；待重新烧录实测完整升级 |
| 2026-09-14 | 首轮连续 DATA 硬件验证 | BOOT `0x00010001` 无丢包推进至 31,372/38,680B，约 699B/s；设备仍处于正常下载态，失败原因仅为 Qt 45s 总超时；Qt 上限调整为 90s，待完整闭环复测 |
| 2026-09-14 | 首次完整下载后 APP 未枚举 | 旧 BOOT `0x00010001` + 旧 APP `38,680B / 0xE1E28D4B` 完成 CRC 和 VALID，但 FE01/FE02 均消失、LED 熄灭；证明传输成功不等于 APP 启动成功 |
| 2026-09-14 | APP 启动兜底与自动恢复 | APP 最多等待接收器 3s，随后无条件用默认描述符启动 FE01；BOOT 增加 VALID→BOOTING→CONFIRMED 首次启动确认，未确认 APP 下次上电自动停留 FE02；BOOT `0x00010002`/`11,740B`，APP `38,812B`/CRC32 `0x24DC4A27`，Qt 13 项测试通过，待硬件闭环验证 |
| 2026-09-14 | 复测确认 BOOT→APP 跳转失败 | BOOT `0x00010002` 下载新 APP `38,812B / 0x24DC4A27` 后 CRC/VALID 成功，仍无 FE01；断电后正确回到 FE02 慢闪，证明恢复机制正常，故障收窄到 APP 入口执行前后 |
| 2026-09-14 | 按 WCH 官方方式修正特权级跳转 | 确认 CH32V203 APP `_start` 需要在机器态写 `mstatus/mtvec/mepc` 并执行 `mret`；旧 BOOT 在用户态普通 `jalr 0x3100` 会触发特权指令异常。改为与 WCH `IAP/USB_UART/CH32V20x_IAP` 示例相同的 `Software_IRQn`→机器态 `SW_Handler`→`jr 0x3100`；BOOT `0x00010003`/`11,744B`，反汇编已确认 SW 中断向量和 `jr`，待硬件验证 |
| 2026-09-14 | BOOT→APP 与升级主链路硬件通过 | BOOT `0x00010003` 成功下载 APP `38,812B / CRC32 0x24DC4A27`，设备 CRC、VALID 和 FE01 重新枚举均成功，证明特权级跳转修复有效；Qt 曾因重枚举后立即对复合 HID 任意子接口发送 Feature Report 而误报失败，按实测决定取消升级结束握手，只以 FE01 重新出现作为重启确认；未插接收器的独立启动表现不列为本轮升级失败。 |

后续维护规则：每次开发在本表追加一行，记录日期、目标、提交号、BOOT/APP 构建体积、硬件测试结果和待办。

## 17. 相关现有文档

- `FLASH_PROFILE_LAYOUT.md`：现有六枪配置扇区和槽位规划。
- `USB_HID_JSON_IMPORT.md`：现有 FE01 厂商 HID 配置导入协议。
- `V0.99_RELEASE_NOTES.md`：V0.99 发布说明。
