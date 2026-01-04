# ESP32 智慧小車教學專案 - AI 編程指引

## 專案概述

ESP32 智慧小車機器人競賽教學專案，使用 **PlatformIO + Arduino** 框架。此為教學機器人，具備循跡、自動撿取、伺服控制等功能。主程式：[src/main.cpp](../src/main.cpp)

### 核心硬體系統
- **驅動系統**：雙馬達 + 編碼器反饋（左/右獨立控制）
- **感知系統**：5 線紅外線陣列（循跡）、HuskyLens AI 視覺模組
- **執行機構**：手臂伺服 + 爪子伺服（撿取物體）
- **反饋系統**：OLED 顯示器、Serial 監控
- **控制器**：ESP32 內置 PWM + LEDC 馬達驅動

## 開發指令

```bash
pio run              # 編譯
pio run -t upload    # 上傳至 ESP32（自動編譯）
pio device monitor   # Serial Monitor (9600 baud)
pio run -t monitor   # 直接開啟監控（需先編譯成功）
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

### 馬達控制（已實作）

```cpp
// 低層控制
motor(int L, int R);  // L/R: -255~255，正值前進、負值後退（無加速度控制）

// 高層動作（基於 motor() 實作）
forward()    // 直線前進 (55, 58) — 輕微左偏補償
backward()   // 直線後退 (-55, -55)
s_Left()     // 原地左轉 (-75, 55) — 左輪減速
s_Right()    // 原地右轉 (55, -75)
m_Left()     // 左轉（右輪停止） (0, 55)
m_Right()    // 右轉（左輪停止） (55, 0)
b_Left()     // 急轉左 (-55, 55) — 左輪反轉
b_Right()    // 急轉右 (55, -55)
stop()       // 停止

// 距離控制（編碼器反饋）
p_fw(int distance)    // 前進指定距離
p_bw(int distance)    // 後退指定距離
p_left(int distance)  // 左轉指定距離
// 注意：距離單位為編碼器計數值，實際距離需校準
```

### 編碼器與距離校準

- 編碼器模式：`attachHalfQuad()` — 4X 計數模式
- 初始化：`leftEncoder.clearCount()` 清除計數器
- **問題**：當前 `p_*()` 函式使用簡單計數對比，易產生超距。建議使用 PID 控制精確距離

### 伺服馬達控制（已實作）

```cpp
arm_up()      // 手臂升起 (ARM_UP=90°)
arm_down()    // 手臂下降 (ARM_DOWN=0°)
claw_open()   // 爪子開啟 (CLAW_OPEN=90°)
claw_close()  // 爪子關閉 (CLAW_CLOSE=0°)

// 複合動作
pickup_object()    // 張爪→放下手臂→夾爪→抬手臂
release_object()   // 放下手臂→張爪
```

### 測試函式（用於調試）

```cpp
test_motor()      // 順序測試前進、後退、左轉、右轉
test_IR()         // 輸出 5 路紅外線讀數 (Serial)
test_encoder()    // 輸出左右編碼器計數值
oled_show_ir_status()  // 在 OLED 顯示 IR & 編碼器狀態
```

## PWM 定時器規則（重要）

-   **Timer 0**：伺服馬達專用 — 在 `setup()` 中必須 `ESP32PWM::allocateTimer(0)` 先行分配
-   **Timer 2**：馬達 PWM (通道 8-11)
-   PWM 設定：75kHz / 8-bit (0~255)
-   **注意**：不要改變 Timer 分配，否則伺服馬達無法正常工作

## 程式結構與編程模式

### 基本架構（setup() → loop()）

- **setup()**：初始化所有硬體、編碼器、伺服、I2C、PWM 等。當前範例在 `setup()` 結尾使用 `while(true)` 陷阱用於測試
- **loop()**：主程式邏輯區（目前為空）。實現時應放入主要控制邏輯（如循跡、自主導航等）

### 常見開發模式

1. **循跡邏輯** (`trail()` 函式)
   - 讀取 5 線紅外線：LL, L, M, R, RR
   - 如果中線 (M) 在黑線上，根據兩側狀態調整方向
   - 中線不在黑線上時，執行更激進的轉向

2. **距離控制實現** (`p_fw()`, `p_bw()` 等)
   - 先清除編碼器計數：`leftEncoder.clearCount()`, `rightEncoder.clearCount()`
   - 執行動作後檢查計數是否達目標
   - **已知問題**：簡單計數對比會造成超距，建議用 PID 改進

3. **複合動作序列** (`pickup_object()`)
   - 使用 `delay()` 同步伺服動作時序
   - 典型流程：張爪 → 放下手臂 → 夾爪 → 抬手臂

### 初始化順序（重要）

```cpp
// 1. 伺服定時器分配（必須最先）
ESP32PWM::allocateTimer(0);

// 2. 伺服初始化（50Hz, 脈寬 500~2400us）
arm.attach(ARM_PIN, 500, 2400);

// 3. 編碼器初始化（使用 attachHalfQuad，啟用上拉電阻）
ESP32Encoder::useInternalWeakPullResistors = puType::up;
leftEncoder.attachHalfQuad(LEFT_ENCODER_A, LEFT_ENCODER_B);

// 4. I2C (OLED) 初始化
Wire.begin(OLED_SDA, OLED_SCL);

// 5. PWM 初始化（Timer 2，Channel 8-11）
ledcSetup(CH_L_FWD, PWM_FREQ, PWM_RES);
ledcAttachPin(MOTOR_L_FWD, CH_L_FWD);
```

如果順序錯誤（例如先初始化伺服再分配 Timer），將導致硬體無法正常工作。

## 函式庫注意事項

| 函式庫       | 版本          | 用途                    | 注意事項                                       |
| ------------ | ------------- | ----------------------- | ---------------------------------------------- |
| ESP32Encoder | ^0.11.7       | 編碼器反饋              | 使用 `attachHalfQuad()` 模式（4X 計數）        |
| ESP32Servo   | ^3.0.6        | 伺服馬達控制            | 必須先 `ESP32PWM::allocateTimer(0)`，使用 Timer 0 |
| HUSKYLENS    | master (Git)  | AI 視覺識別             | 需 `#pragma GCC diagnostic` 包裹以抑制編譯警告 |
| QuickPID     | ^3.1.9        | PID 控制（待實作）       | 已引入但尚未用於距離/速度控制                  |
| Adafruit SSD1306 | Git        | OLED 顯示                | 需 Wire.begin(SDA, SCL) 初始化 I2C              |

## 常見陷阱與解決方案

### 1. 伺服馬達無反應
- **原因**：Timer 0 未先分配，或 `attach()` 順序錯誤
- **解決**：確保 `ESP32PWM::allocateTimer(0)` 在所有伺服操作前執行

### 2. 編碼器計數異常
- **原因**：未啟用內部上拉電阻或腳位接觸不良
- **解決**：設定 `ESP32Encoder::useInternalWeakPullResistors = puType::up`

### 3. 距離控制超距
- **原因**：`p_fw()` 等函式使用簡單計數對比，無加速度/減速控制
- **建議**：改用 QuickPID 實作 PID 控制

### 4. OLED 顯示無法初始化
- **原因**：I2C 地址不對或 Wire 未初始化
- **解決**：確認接線，使用 `Wire.begin(OLED_SDA, OLED_SCL)` 指定腳位

## 測試與調試工作流

### Serial Monitor 監控
```bash
# 方式 1：直接監控
pio run -t monitor      # 需先編譯成功，自動開啟

# 方式 2：分別編譯和監控
pio run                 # 先編譯
pio device monitor      # 再開啟監控 (9600 baud)
```

### 快速測試方法
在 `setup()` 末尾使用 `while(true)` 執行測試迴圈：
```cpp
while (true) {
  test_motor();
  delay(500);
}
```

### 整合測試流程
1. 呼叫 `test_motor()` 驗證馬達方向和速度
2. 呼叫 `test_IR()` 驗證紅外線感測器是否檢測線路
3. 呼叫 `test_encoder()` 檢查編碼器計數是否正常
4. 呼叫 `oled_show_ir_status()` 驗證 OLED 顯示
5. 再執行 `trail()` 測試循跡邏輯

## 性能優化指南

### 避免阻塞 loop()
當前 `loop()` 為空，不會產生阻塞。但如需實現實時控制：
- **不要**：在 loop 中使用 `while(true)` 或長 `delay()`
- **改用**：時間戳記 + 狀態機（見 `trail()` 循跡邏輯）

### 編碼器精度改進
當前 `p_*()` 實現易超距：
```cpp
// 當前：簡單計數對比（會超距）
while (leftCount >= targetCount) { break; }

// 建議：使用 QuickPID 實現平滑減速
// PID 配置：Kp=1.5, Ki=0.3, Kd=0.5（需現場校準）
```

## 硬體校準參考

| 參數               | 當前值 | 說明                                |
| ------------------ | ------ | ----------------------------------- |
| 左馬達正轉速度     | 55     | 與右輪(58)配合實現直線前進          |
| 右馬達正轉速度     | 58     | 略高於左輪以補償機械差異             |
| IR 閾值            | 2000   | 黑線/白線辨別標準，需依環境調整      |
| 手臂 UP 角度       | 90°    | SG90 伺服馬達角度範圍 0~180°         |
| 爪子開啟角度       | 90°    | 根據爪子結構可調                     |

## 專案狀態與待實作項目

### ✅ 已完成
- 馬達 PWM 控制（前進、後退、轉向）
- 編碼器讀取與基礎距離控制
- 伺服馬達初始化和角度控制
- 紅外線感測和循跡邏輯框架
- OLED 顯示和 Serial 監控

### 🔄 部分完成
- 距離控制（`p_fw()` 等）— 易超距，建議用 PID 改進
- 循跡邏輯 (`trail()`) — 框架完成，需現場微調速度參數

### ⏳ 待實作
- **HuskyLens 整合** — 顏色/形狀識別邏輯
- **路線規劃** — 基於 IR 和 HuskyLens 的自主導航
- **状態機重構** — 用事件驅動取代 while 迴圈
- **PID 控制** — 精確距離和速度穩定

## 擴展指引

-   **新增伺服馬達**：使用 Timer 0，參考 `arm`/`claw` 初始化
-   **新增感測器**：在腳位定義區塊添加 `#define XXX_PIN`，在 `setup()` 中 `pinMode()` 初始化
-   **HuskyLens 整合**：需設定 I2C 並用 pragma 包裹以抑制編譯警告
-   **性能優化**：避免 `loop()` 中阻塞性操作（如長 `delay()`），改用毫秒計時器或非阻塞狀態機

## 命名規則與慣例

-   **腳位**：`XXX_PIN` (如 `IR_LL_PIN`, `ARM_PIN`)
-   **PWM 通道**：`CH_X_XXX` (如 `CH_L_FWD`)
-   **角度常數**：`XXX_UP/DOWN/OPEN/CLOSE`
-   **函式**：動作類用英文縮寫（`s_Left` = 原地左轉，`b_Left` = 急轉左）
