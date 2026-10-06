# freertos-watch

基于 **STM32F103C8T6 + FreeRTOS + u8g2** 的多功能桌面小手表：OLED 显示时间/日期、秒表、DHT11 温湿度、电池电压，4 键操作。
全部外设驱动自己配置，**FreeRTOS 使用纯静态分配（不用堆）**，内存占用在链接期就完全确定。

```
Flash 28 KB / 64 KB        RAM 7.4 KB / 20 KB        (Keil MDK ARMCC V5.06)
```

---

## 1. 硬件与接线

| 功能 | 引脚 | 配置说明 |
|---|---|---|
| OLED SSD1306 128x64 | **PB6 = SCL, PB7 = SDA** | 硬件 I2C1，400 kHz，地址 0x78 |
| 按键 K1 = 上 | **PB0** | 输入 + 内部上拉，**按下接 GND（低电平有效）** |
| 按键 K2 = 下 | **PB1** | 同上 |
| 按键 K3 = 确认 | **PB10** | 同上 |
| 按键 K4 = 返回 | **PB11** | 同上 |
| DHT11 数据 | **PA1** | 开漏输出 + 内部上拉（写 1 释放总线） |
| 电池检测 | **PA0** | ADC1_IN0，电池经分压后接入 |
| 状态 LED | **PC13** | 推挽输出，**低电平点亮** |
| 无源蜂鸣器 | **PA8** | TIM1_CH1 PWM |
| HSE 晶振 | PD0 / PD1 | 8 MHz |
| 调试串口（未使用） | PA9 / PA10 | USART1，保留 |

**电池分压**（默认按 1:1 计算，即 `BATT_DIVIDER_NUM = 2`）：

```
电池正极 ──┬── 100k ──┬── PA0
           │          │
          100k        (ADC 输入，几乎不吃电流)
           │
          GND
```

若电阻比例不同，改 `Core/Src/app.c` 里的 `BATT_DIVIDER_NUM` 即可（上 100k / 下 50k 则为 3）。

**按键有效电平自动识别**：固件上电时对 4 个按键引脚各采样 8 次投票，把"未按下时的电平"记为 IDLE，之后只要电平偏离 IDLE 就算按下。
所以"按下接 GND"和"按下接 VCC"两种接法都能直接用，不需要改代码。

---

## 2. 按键操作

| 键 | 引脚 | 菜单 / 通用 | 设置时间日期 | 秒表 |
|---|---|---|---|---|
| K1 | PB0 | 上移 | 选上一个字段 / 数值 +1 | 清零 |
| K2 | PB1 | 下移 | 选下一个字段 / 数值 −1 | — |
| K3 | PB10 | 进入 / 确认 | 进入或退出"修改"模式 | 开始 / 暂停 |
| K4 | PB11 | 返回上一级 | 退回（修改模式下先退出修改） | 返回 |

按住 K1 / K2 约 250 ms 会自动连发，设置数值时不用反复点。

---

## 3. 界面

```
[时间页]                      [菜单]
  09:41                         MENU
  ▓▓▓▓▓▓▓▓▓░░░░░░              ────────────────
  2024-01-01        ⚡           Stopwatch        ← 反白为当前项
                               Clock / date
                               DHT11
                               Battery
                               About
```

| 页面 | 内容 |
|---|---|
| **时间页** | `HH:MM` 大字 + 秒进度条（含 1/10 秒刻度）+ 日期 + 右下角电池图标 |
| **Clock / date** | 年 / 月 / 日 / 时 / 分 / 秒 设置，范围自动回绕，**闰年自动处理**（2 月天数） |
| **Stopwatch** | 0.1 秒精度秒表，由独立软件定时器计数 |
| **DHT11** | 温度 + 湿度，每 2 秒刷新，K3 立即重读，读失败显示 `sensor error` |
| **Battery** | 电池百分比 + 电压（4.2 V = 100%，3.3 V = 0%），每 10 秒刷新 |
| **About** | 版本信息 |

---

## 4. 编译 / 下载

**环境**：Keil MDK-ARM V5（ARMCC V5.06），需要 STM32F1xx_DFP 器件包。

1. 用 Keil 打开 `MDK-ARM/hal_freertos_u8g2.uvprojx`
2. **Rebuild** → **Download**

> 注意：如果 Keil 已经开着这个工程，增删源文件会被它用内存里的旧内容覆盖回 `.uvprojx`，所以**改工程结构前先关掉 Keil**。

命令行编译（可选）：

```powershell
& "C:\Keil_v5\UV4\UV4.exe" -b "MDK-ARM\hal_freertos_u8g2.uvprojx" -j0 -o build.log
```

---

## 5. 源码结构

```
Core/
  Inc/
    watch.h                 引脚宏、对外接口、按键码定义（换引脚只看这里）
    main.h                  HAL 句柄声明
    FreeRTOSConfig.h        FreeRTOS 配置（静态分配、任务/定时器优先级）
    stm32f1xx_hal_conf.h    HAL 模块开关（ADC / I2C / TIM / GPIO / RCC ...）
  Src/
    main.c                  启动流程，g_bootStage 逐步打点
    bsp.c                   时钟(72MHz) / GPIO / I2C / 蜂鸣器 / LED
    delay.c                 DWT 周期计数器 → us/ms 延时 + 毫秒时钟
    timebase.c              TIM4 提供 HAL 1ms 时基（供 I2C 超时用）
    oled.c                  OLED 初始化、刷屏、I2C 卡死自动恢复
    app.c                   两个任务 + 全部界面 + 按键处理 + 电池 ADC
    stm32f1xx_it.c          中断向量（SysTick / SVC / PendSV / TIM4）
driver/
    driver_dht11.c/.h       DHT11 驱动（每个等待都有超时，绝不无限阻塞）
u8g2/                       u8g2 图形库（显示缓冲 1024 B，静态分配）
Middlewares/Third_Party/FreeRTOS/   FreeRTOS V10.0.1 内核
Drivers/                    STM32F1 HAL 库 + CMSIS
MDK-ARM/                    Keil 工程
mytasks/                    旧版任务文件（内容为空，保留仅供对照）
```

`mytasks/*.c` 与旧的 `Core/Src/gpio.c`、`i2c.c`、`tim.c`、`usart.c`、`freertos.c`、`stm32f1xx_hal_msp.c`、
`stm32f1xx_hal_timebase_tim.c` 都是重写前 CubeMX 生成的文件，**现在内容为空、不实现任何功能**，
在工程里保留只是为了兼容原有的工程分组结构。

---

## 6. 设计要点（踩过的坑）

| 现象 | 原因 | 现在的做法 |
|---|---|---|
| 时间永远 `00:00` | FreeRTOS 软件定时器任务默认优先级 2，而应用任务用了 `osPriorityNormal`(24)，定时器任务**被饿死**，走秒回调一次都没执行 | `configTIMER_TASK_PRIORITY = 7`，应用任务 = 8，定时器任务严格更高 |
| 任务 / 定时器 / 队列静默失效 | 堆只有 3072 B，`xTaskCreate` 返回 NULL 后没有任何提示 | 改为**纯静态分配**（`configSUPPORT_DYNAMIC_ALLOCATION = 0`），并检查所有创建结果 |
| 按键乱跳、切页面立刻弹回 | ISR 发送的 `Key_data` 未初始化（另外 3 个标志位是栈垃圾）；任务取出后不清标志、切任务时不清队列 | 队列项由 `s_emptyKey` 全零构造；每轮处理完清零；切换任务前 `xQueueReset()` |
| 按键无反应 / 行为相反 | 旧固件用上升沿 EXTI，与本模块"按下接 GND（低电平）"相反 | 改为 **5 ms 轮询 + 3 次一致消抖**，并**自动识别有效电平** |
| OLED 卡死（画面定格） | I2C 总线被从机拉住后 STM32 进入 BUSY，之后每次传输都失败，MCU 还在跑但屏不动 | `u8x8_byte_hw_i2c()` 统计失败次数，连续 3 帧失败则 **`Oled_Recover()`**：释放外设 → 手动打 9 个 SCL 脉冲 + STOP → 重新 `HAL_I2C_Init` → 重新初始化 SSD1306 |
| 启动流程莫名卡死 | CubeMX 生成的初始化里含大量可能阻塞的 HAL 调用 | 启动路径只做寄存器级初始化，`g_bootStage` 逐步打点，配合调试器一眼定位 |

---

## 7. 调试用变量（Keil Watch 窗口直接看）

| 变量 | 含义 |
|---|---|
| `g_bootStage` | 启动进度：`0x01`=HAL_Init 完，`0x02`=时钟，`0x03`=GPIO，`0x04`=I2C，`0x05`=蜂鸣器，`0x06`=RTOS 对象，`0x07`=调度器启动，`0x20`=**已进入 UI 任务** |
| `g_uiLoopCount` | UI 循环计数（约 20/秒） |
| `g_i2cErrorCount` | I2C 传输失败累计次数 |
| `g_keyTaskRuns` | 按键任务计数（5 ms 一次） |
| `g_keyTaskPress` | 已识别的按键次数 |
| `g_keyTaskLevel` | K1~K4 原始电平，bit0~bit3，1 = 高 |
| `g_keyIdleLevel` | 上电时测得的"松开"电平 |

LED 行为：PC13 约 1 Hz 闪烁表示调度器在运行；HardFault 时会快闪。

---

## 8. License

- 本项目代码：可自由使用与修改。
- `u8g2/`：u8g2 图形库，BSD-2-Clause，见 u8g2 官方仓库。
- `Middlewares/Third_Party/FreeRTOS/`：FreeRTOS 内核，MIT。
- `Drivers/`：STMicroelectronics HAL / CMSIS，遵循 ST 的许可条款。
- `driver/driver_dht11.c` 源自百问网（100ask）例程，遵循其 GPL-3.0 许可。
