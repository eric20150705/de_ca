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
pio run -t upload    # 上傳至 ESP32（透過 OTA 無線燒入）
pio device monitor   # Serial Monitor (9600 baud)
pio run -t monitor   # 直接開啟監控（需先編譯成功）
```

### OTA 無線燒入

- **初次燒入**：必須使用 USB 連接燒入（建立 WiFi 連線）
- **後續更新**：可透過 WiFi 無線燒入（IP：192.168.50.158）
- **OTA 窗口**：ESP32 重啟後 7 秒內監聽 OTA 請求
- **WiFi 憑證**：`Singular_AI` / `Singular#1234`（在 [main.cpp](../src/main.cpp) 中修改）
- **注意**：OTA 傳輸開始後會自動延長窗口直到完成

## 硬體腳位速查

| 元件               | 腳位           | 常數/通道                                                       |
| ------------------ | -------------- | --------------------------------------------------------------- |
| 紅外線 LL/L/M/R/RR | 39/32/33/34/35 | `IR_LL_PIN` ~ `IR_RR_PIN`                                       |
| 編碼器左 A/B       | 18/19          | `LEFT_ENCODER_A/B`                                              |
| 編碼器右 A/B       | 23/5           | `RIGHT_ENCODER_A/B`                                             |
| 左馬達正/反轉      | 27/13          | `CH_L_FWD(4)` / `CH_L_BWD(5)`                                   |
| 右馬達正/反轉      | 2/4            | `CH_R_FWD(6)` / `CH_R_BWD(7)`                                   |
| 手臂伺服           | 14             | `ARM_UP=90°` / `ARM_DOWN=0°`                                    |
| 爪子伺服           | 15             | `CLAW_OPEN=80°` / `CLAW_CLOSE=0°`                               |
| **攝像頭伺服**     | **25**         | **`CAMERA_FRONT=90°` / `CAMERA_LEFT=170°` / `CAMERA_RIGHT=0°`** |

## Better Comments 使用規範

本專案使用 Better Comments 擴展進行視覺化註解標記，讓程式碼更易於閱讀和維護：

| 標記      | 顏色    | 用途                     | 範例                               |
| --------- | ------- | ------------------------ | ---------------------------------- |
| `//!`     | 🔴 紅色 | 安全警告、硬體損壞風險   | `//! 正反轉不可同時輸出會短路`     |
| `//?`     | 🔵 藍色 | 可調參數、需要校準的數值 | `//? 偏左→增加左輪：motor(60, 55)` |
| `//*`     | 🟢 綠色 | 重要步驟、核心概念       | `//* 階段 1：快速前進到接近目標`   |
| `//TODO:` | 🟠 橙色 | 待實作功能               | `//TODO: 整合 HuskyLens 顏色識別`  |

### 註解撰寫原則

- **術語白話化**：首次出現加括號說明（如「PWM（快速開關控制速度，0~255）」）
- **圖像化比喻**：用生活經驗說明（如「像汽車定速巡航」「像計步器」）
- **調參指引**：告訴學生怎麼調、調什麼、看什麼結果
- **單位明確**：所有數值註明單位（如「c/100ms」「度°」「毫秒ms」）

## 核心 API

### 馬達控制（已實作）

```cpp
// 低層控制
motor(int L, int R);  // L/R: -255~255，正值前進、負值後退（像水龍頭控制）

// 高層動作（基於 motor() 實作）
forward()    // 直線前進 (55, 55) — 可調整補償偏移
backward()   // 直線後退 (-25, -25) — 簡易版，無速度閉環
s_Left()     // 差速左轉 (25, 45)
s_Right()    // 差速右轉 (45, 25)
m_Left()     // 左轉 (0, 35)
m_Right()    // 右轉 (35, 0)
b_Left()     // 急轉左 (-55, 55)
b_Right()    // 急轉右 (55, -55)
stop()       // 停止

// 距離控制（編碼器反饋 + 速度閉環）
p_fw_v2(int distance)  // 新版前進：速度閉環 + 距離減速（推薦使用）
p_bw_v2(int distance)  // 新版後退：速度閉環 + 距離減速（推薦使用）
p_left(int distance)   // 左轉指定角度（單位：度°，需轉換為計數值）
p_right(int distance)  // 右轉指定角度（單位：度°，需轉換為計數值）
// 注意：distance 單位為編碼器計數值（約 0.5mm/count），角度需乘轉換係數
```

### 編碼器與距離校準

- 編碼器模式：`attachHalfQuad()` — 4X 計數模式（像計步器記錄輪子轉動）
- 初始化：`leftEncoder.clearCount()` 清除計數器
- **速度閉環**：`p_fw_v2()` / `p_bw_v2()` 已實作速度閉環控制，避免超距問題
- **調參流程**：
    1. 執行 `test_max_speed()` 測量極限速度
    2. 設定 `BASE_SPEED` 為極限的 70%
    3. 調整 `SPEED_KP` 直到動作平順不抖動

### 伺服馬達控制（已實作）

```cpp
arm_up()      // 手臂升起 (ARM_UP=90°)
arm_down()    // 手臂下降 (ARM_DOWN=0°)
claw_open()   // 爪子開啟 (CLAW_OPEN=80°)
claw_close()  // 爪子關閉 (CLAW_CLOSE=0°)

// 攝像頭視角控制（新增）
camera_front()  // 攝像頭轉向正前方（90°）
camera_left()   // 攝像頭轉向左側（170°）
camera_right()  // 攝像頭轉向右側（0°）

// 複合動作
pickup_object()    // 張爪→放下手臂→夾爪→抬手臂
release_object()   // 放下手臂→張爪
```

### 測試函式（用於調試）

```cpp
test_motor()           // 順序測試前進、後退、左轉、右轉
test_IR()              // 輸出 5 路紅外線讀數 (Serial)
test_encoder()         // 輸出左右編碼器計數值
test_max_speed()       // 極限速度測試：PWM=255 測量左右輪最大速度
test_speed()           // 即時速度測試：每 100ms 印出左右輪速度
test_camera()          // 攝像頭伺服測試：前→左→前→右→前（間隔 1 秒）
oled_show_ir_status()  // 在 OLED 顯示 IR & 編碼器狀態及極限速度
```

## PWM 定時器規則（重要）

### Timer 與 Channel 固定對應關係

ESP32 LEDC 的 16 個通道**不是任意分配**，而是固定綁定到 4 個 Timer：

| Timer       | 控制的 Channels | 當前用途              | 頻率  |
| ----------- | --------------- | --------------------- | ----- |
| **Timer 0** | 0, 1, 8, 9      | arm + claw 伺服       | 50Hz  |
| **Timer 1** | 2, 3, 10, 11    | camera 伺服           | 50Hz  |
| **Timer 2** | 4, 5, 12, 13    | **馬達 PWM (CH 4-7)** | 75kHz |
| **Timer 3** | 6, 7, 14, 15    | （保留未使用）        | -     |

### ⚠️ 關鍵限制

1. **同一 Timer 的所有 Channels 必須使用相同頻率**
2. ESP32Servo 使用 Timer 0 (Channels 0, 1) 和 Timer 1 (Channel 2)
3. 馬達 PWM **必須避開 Channels 0-3, 8-11**，使用 Timer 2 或 Timer 3 的通道

### 初始化順序

```cpp
// 1. 先分配所有 ESP32Servo 的 Timer
ESP32PWM::allocateTimer(0); // arm + claw
ESP32PWM::allocateTimer(1); // camera

// 2. 再初始化伺服馬達
arm.attach(ARM_PIN, 500, 2400);
claw.attach(CLAW_PIN, 500, 2400);
camera.attach(CAMERA_PIN, 500, 2400);

// 3. 最後初始化馬達 PWM（使用 Timer 2 的 Channels 4-7）
ledcSetup(CH_L_FWD, 75000, 8); // Channel 4
ledcSetup(CH_L_BWD, 75000, 8); // Channel 5
ledcSetup(CH_R_FWD, 75000, 8); // Channel 6
ledcSetup(CH_R_BWD, 75000, 8); // Channel 7
```

- **注意**：GPIO 25 屬於 ADC2，在使用 WiFi (OTA) 時需使用獨立 Timer 避免干擾

## 程式結構與編程模式

### 基本架構（setup() → loop()）

- **setup()**：初始化所有硬體、編碼器、伺服、I2C、PWM 等。當前範例在 `setup()` 結尾使用 `while(true)` 陷阱用於測試
- **loop()**：主程式邏輯區（目前為空）。實現時應放入主要控制邏輯（如循跡、自主導航等）

### 常見開發模式

1. **循跡邏輯** (`trail()` 函式)
    - 讀取 5 線紅外線：LL, L, M, R, RR
    - 如果中線 (M) 在黑線上，根據兩側狀態調整方向
    - 中線不在黑線上時，執行更激進的轉向

2. **距離控制實現** (`p_fw_v2()`, `p_bw_v2()`, `p_left()`, `p_right()`)
    - 先清除編碼器計數：`leftEncoder.clearCount()`, `rightEncoder.clearCount()`
    - **三階段控制**：快速前進 → 低速精調 → 反復微調至容差範圍
    - **速度閉環**：根據目標速度動態調整 PWM，補償電池電量變化
    - **距離減速**：接近目標時自動減速（DECEL_START = 0.6）

3. **複合動作序列** (`pickup_object()`)
    - 使用 `delay()` 同步伺服動作時序
    - 典型流程：張爪 → 放下手臂 → 夾爪 → 抬手臂

### 初始化順序（重要）

```cpp
// 1. 伺服定時器分配（必須最先）
ESP32PWM::allocateTimer(0); // Timer 0 給 arm & claw
ESP32PWM::allocateTimer(1); // Timer 1 給 camera（獨立 Timer 避免 GPIO25/WiFi 衝突）

// 2. 伺服初始化（50Hz, 脈寬 500~2400us）
arm.attach(ARM_PIN, 500, 2400);    // 連結至 Timer 0
claw.attach(CLAW_PIN, 500, 2400);  // 連結至 Timer 0
camera.attach(CAMERA_PIN, 500, 2400); // 連結至 Timer 1

// 3. 編碼器初始化（使用 attachHalfQuad，啟用上拉電阻）
ESP32Encoder::useInternalWeakPullResistors = puType::up;
leftEncoder.attachHalfQuad(LEFT_ENCODER_A, LEFT_ENCODER_B);

// 4. I2C (OLED) 初始化
Wire.begin(OLED_SDA, OLED_SCL);

// 5. PWM 初始化（Timer 2，Channel 4-7）
ledcSetup(CH_L_FWD, PWM_FREQ, PWM_RES);
ledcAttachPin(MOTOR_L_FWD, CH_L_FWD);
```

如果順序錯誤（例如先初始化伺服再分配 Timer），將導致硬體無法正常工作。

## 函式庫注意事項

| 函式庫           | 版本         | 用途               | 注意事項                                          |
| ---------------- | ------------ | ------------------ | ------------------------------------------------- |
| ESP32Encoder     | ^0.11.7      | 編碼器反饋         | 使用 `attachHalfQuad()` 模式（4X 計數）           |
| ESP32Servo       | ^3.0.6       | 伺服馬達控制       | 必須先 `ESP32PWM::allocateTimer(0)`，使用 Timer 0 |
| HUSKYLENS        | master (Git) | AI 視覺識別        | 需 `#pragma GCC diagnostic` 包裹以抑制編譯警告    |
| QuickPID         | ^3.1.9       | PID 控制（待實作） | 已引入但尚未用於距離/速度控制                     |
| Adafruit SSD1306 | Git          | OLED 顯示          | 需 Wire.begin(SDA, SCL) 初始化 I2C                |

## 常見陷阱與解決方案

### 1. 伺服馬達無反應

- **原因**：Timer 0 未先分配，或 `attach()` 順序錯誤
- **解決**：確保 `ESP32PWM::allocateTimer(0)` 在所有伺服操作前執行

### 2. 編碼器計數異常

- **原因**：未啟用內部上拉電阻或腳位接觸不良
- **解決**：設定 `ESP32Encoder::useInternalWeakPullResistors = puType::up`

### 3. 距離控制超距（已改善）

- **解決方案**：`p_fw_v2()` / `p_bw_v2()` 已實作速度閉環 + 距離減速
- **仍超距**：調整 `TOLERANCE`（慣性補償）或 `DECEL_START`（提早減速）
- **QuickPID**：已引入但未使用，可用於更精確控制

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

### 速度閉環已實作

`p_fw_v2()` / `p_bw_v2()` 已實現速度閉環控制：

```cpp
// 速度閉環控制流程
1. 執行 test_max_speed() 測量極限速度（c/100ms）
2. 設定 BASE_SPEED 為極限的 70%（保守安全值）
3. 調整 SPEED_KP（比例係數）：
   - 太大（5.0）→ 抖動 → 往下調
   - 太小（0.01）→ 反應慢 → 往上調
   - 建議從 0.1 開始微調
4. 調整 MIN_SPEED 避免馬達死區（通常 > 20）
5. 調整 TOLERANCE 補償慣性超距
```

## 硬體校準參考

| 參數           | 當前值 | 說明                            |
| -------------- | ------ | ------------------------------- |
| 左馬達正轉速度 | 55     | 與右輪(58)配合實現直線前進      |
| 右馬達正轉速度 | 58     | 略高於左輪以補償機械差異        |
| IR 閾值        | 2000   | 黑線/白線辨別標準，需依環境調整 |
| 手臂 UP 角度   | 90°    | SG90 伺服馬達角度範圍 0~180°    |
| 爪子開啟角度   | 80°    | 根據爪子結構可調                |
| 攝像頭前視角度 | 90°    | 正中心位置                      |
| 攝像頭左視角度 | 170°   | 最大左轉                        |
| 攝像頭右視角度 | 0°     | 最大右轉                        |

## 常見問題排查

### 問題 1：車子不走直線

**症狀**：前進時偏左或偏右
**原因**：左右馬達機械差異、輪子摩擦力不同
**解決方法**：

1. 調整 `forward()` 函式中的 PWM 值
2. 偏左 → 增加左輪：`motor(60, 55)`
3. 偏右 → 增加右輪：`motor(55, 60)`
4. 微調至直線行駛

### 問題 2：距離控制超距

**症狀**：`p_fw_v2()` 執行後超過目標距離
**原因**：慣性過大、減速不夠早
**解決方法**：

1. 增加 `TOLERANCE` 值補償慣性（例：50 → 100）
2. 減小 `DECEL_START` 提早減速（例：0.6 → 0.5）
3. 減小 `BASE_SPEED` 降低整體速度

### 問題 3：馬達不轉或抖動

**症狀**：馬達無反應或一頓一頓
**原因**：PWM 值太小（馬達死區）或 SPEED_KP 太大
**解決方法**：

1. 檢查 `MIN_SPEED` 是否高於馬達啟動閾值（通常 > 20）
2. 若抖動：減小 `SPEED_KP`（例：0.1 → 0.05）
3. 若反應慢：增加 `SPEED_KP`（例：0.1 → 0.2）

### 問題 4：紅外線誤判

**症狀**：白地判成黑線，或黑線判成白地
**原因**：`IR_THRESHOLD` 不符合場地環境
**解決方法**：

1. 執行 `test_IR()` 查看實際讀值
2. 白地通常 < 500，黑線通常 > 2500
3. 設定閾值在中間：`(500+2500)/2 ≈ 1500`

### 問題 5：伺服馬達無反應

**症狀**：`arm_up()` 或 `claw_open()` 或 `camera_front()` 無效
**原因**：Timer 未正確分配
**解決方法**：

1. 確認 `setup()` 中有 `ESP32PWM::allocateTimer(0)` 和 `ESP32PWM::allocateTimer(1)`
2. 必須在所有 `.attach()` 之前執行
3. 檢查腳位連接是否正確（arm=14, claw=15, camera=25）

## 專案狀態與待實作項目

### ✅ 已完成

- 馬達 PWM 控制（前進、後退、轉向）
- 編碼器讀取與速度閉環距離控制（`p_fw_v2()`, `p_bw_v2()`）
- 三個伺服馬達初始化和角度控制（arm, claw, camera）
- 攝像頭視角控制系統（前/左/右三方向）
- 雙 Timer 架構（Timer 0: arm+claw, Timer 1: camera）
- 紅外線感測和循跡邏輯框架
- OLED 顯示和 Serial 監控
- Better Comments 註解系統
- OTA 無線燒入功能
- 速度閉環控制系統（`speed_control()`）
- 極限速度測試工具（`test_max_speed()`）

### 🔄 部分完成

- 距離控制（`p_left()` / `p_right()`）— 已實作三階段控制，可能需現場微調
- 循跡邏輯 (`trail()`) — 框架完成，需現場微調速度參數

### ⏳ 待實作

- **HuskyLens 整合** — 顏色/形狀識別邏輯
- **路線規劃** — 基於 IR 和 HuskyLens 的自主導航
- **狀態機重構** — 用事件驅動取代 while 迴圈
- **進階 PID** — 整合 QuickPID 庫實現更精確控制

## 擴展指引

- **新增伺服馬達**：使用 Timer 0，參考 `arm`/`claw` 初始化
- **新增感測器**：在腳位定義區塊添加 `#define XXX_PIN`，在 `setup()` 中 `pinMode()` 初始化
- **HuskyLens 整合**：需設定 I2C 並用 pragma 包裹以抑制編譯警告
- **性能優化**：避免 `loop()` 中阻塞性操作（如長 `delay()`），改用毫秒計時器或非阻塞狀態機

## 命名規則與慣例

- **腳位**：`XXX_PIN` (如 `IR_LL_PIN`, `ARM_PIN`)
- **PWM 通道**：`CH_X_XXX` (如 `CH_L_FWD`)
- **角度常數**：`XXX_UP/DOWN/OPEN/CLOSE`
- **函式**：動作類用英文縮寫（`s_Left` = 差速左轉，`b_Left` = 急轉左）
