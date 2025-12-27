# ESP32 智慧小車教學專案 - AI 編程指引

## 專案概述

ESP32 智慧小車機器人競賽教學專案，使用 **PlatformIO + Arduino** 框架。主程式：[src/main.cpp](../src/main.cpp)

## 開發指令

```bash
pio run              # 編譯
pio run -t upload    # 上傳至 ESP32
pio device monitor   # Serial Monitor (9600 baud)
```

## 硬體腳位速查

| 元件               | 腳位           | 常數/通道                         |
| ------------------ | -------------- | --------------------------------- |
| 紅外線 LL/L/M/R/RR | 39/32/33/34/35 | `IR_LL_PIN` ~ `IR_RR_PIN`         |
| 編碼器左 A/B       | 18/19          | `LEFT_ENCODER_A/B`                |
| 編碼器右 A/B       | 23/5           | `RIGHT_ENCODER_A/B`               |
| 左馬達正/反轉      | 27/13          | `CH_L_FWD(8)` / `CH_L_BWD(9)`     |
| 右馬達正/反轉      | 2/4            | `CH_R_FWD(10)` / `CH_R_BWD(11)`   |
| 手臂伺服           | 14             | `ARM_UP=90°` / `ARM_DOWN=15°`     |
| 爪子伺服           | 15             | `CLAW_OPEN=90°` / `CLAW_CLOSE=0°` |

## 核心 API

### 馬達控制

```cpp
motor(int L, int R);  // L/R: -255~255，正值前進、負值後退
// 動作函式：forward(), backward(), s_Left(), s_Right(),
//          m_Left(), m_Right(), b_Left(), b_Right(), stop()
```

### 待實作函式 (TODO)

程式碼中標記 `TODO` 的函式需要學生完成：

-   **紅外線讀取**：`IR_LL_read()` ~ `IR_RR_read()` — 回傳 0(白)/1(黑)，閾值 `IR_THRESHOLD=2000`
-   **伺服控制**：`arm_up()`, `arm_down()`, `claw_open()`, `claw_close()`
-   **測試函式**：`test_encoder()`, `test_servo()`

## PWM 定時器規則（重要）

-   **Timer 0**：伺服馬達專用 — `ESP32PWM::allocateTimer(0)`
-   **Timer 2**：馬達 PWM (通道 8-11)
-   PWM 設定：75kHz / 8-bit (0~255)

## 程式碼慣例

### 命名規則

-   腳位：`XXX_PIN` (如 `IR_LL_PIN`, `ARM_PIN`)
-   PWM 通道：`CH_X_XXX` (如 `CH_L_FWD`)
-   角度常數：`XXX_UP/DOWN/OPEN/CLOSE`

### 程式結構

1. 腳位/常數定義 → 2. 前向宣告 → 3. 自訂函式區 → 4. setup() → 5. loop()

## 函式庫注意事項

| 函式庫       | 注意事項                                       |
| ------------ | ---------------------------------------------- |
| ESP32Encoder | 使用 `attachHalfQuad()` 模式                   |
| ESP32Servo   | 必須先 `ESP32PWM::allocateTimer(0)`            |
| HUSKYLENS    | 需 `#pragma GCC diagnostic` 包裹以抑制編譯警告 |
| QuickPID     | 已引入但尚未實作                               |

## 擴展指引

-   新增伺服馬達：使用 Timer 0，參考 `arm`/`claw` 初始化
-   新增感測器：在腳位定義區塊添加 `#define XXX_PIN`
-   HuskyLens 初始化需設定 I2C 並處理編譯警告
