# ESP32 智慧小車教學專案 - AI 編程指引

## 專案概述

這是一個 **ESP32 智慧小車機器人競賽**教學專案，使用 PlatformIO + Arduino 框架開發。主程式位於 `src/main.cpp`。

## 硬體架構

### 感測器

| 感測器       | 腳位       | 說明                           |
| ------------ | ---------- | ------------------------------ |
| 紅外線 LL    | GPIO 39    | 最左側循線                     |
| 紅外線 L     | GPIO 32    | 左側循線                       |
| 紅外線 M     | GPIO 33    | 中間循線                       |
| 紅外線 R     | GPIO 34    | 右側循線                       |
| 紅外線 RR    | GPIO 35    | 最右側循線                     |
| 編碼器左 A/B | GPIO 18/19 | 左輪回饋                       |
| 編碼器右 A/B | GPIO 23/5  | 右輪回饋                       |
| HuskyLens    | I2C        | AI 視覺模組 (已引入但尚未使用) |

### 執行器

| 執行器   | 腳位                       | 說明                      |
| -------- | -------------------------- | ------------------------- |
| 左馬達   | GPIO 27 (正轉) / 13 (反轉) | PWM 通道 8/9              |
| 右馬達   | GPIO 2 (正轉) / 4 (反轉)   | PWM 通道 10/11            |
| 手臂伺服 | GPIO 14                    | 角度：升起 90° / 下降 15° |
| 爪子伺服 | GPIO 15                    | 角度：開啟 90° / 關閉 0°  |

## 開發工作流程

```bash
pio run              # 編譯
pio run -t upload    # 上傳至 ESP32
pio device monitor   # 開啟 Serial Monitor (9600 baud)
```

### PWM 通道分配規則

-   **Timer 0**：保留給伺服馬達 (`ESP32PWM::allocateTimer(0)`)
-   **Timer 2**：馬達 PWM 控制 (通道 8, 9, 10, 11)
-   PWM 頻率：75kHz，解析度：8-bit (0~255)

## 程式碼慣例

### 馬達控制

```cpp
motor(int L, int R);  // L/R: -255~255，正值前進、負值後退
// 重要：左右輪速度需校正，目前 forward() 使用 motor(100, 165)
```

預設動作函式：`forward()`, `backward()`, `m_Left()`, `m_Right()`, `b_Left()`, `b_Right()`, `stop()`

### 紅外線感測

```cpp
IR_X_read();  // 回傳 0 (白線) 或 1 (黑線)，閾值 IR_THRESHOLD=2000
// X = LL(最左), L(左), M(中), R(右), RR(最右)
```

### 伺服馬達

```cpp
arm_up(); arm_down();     // 手臂升降
claw_open(); claw_close(); // 爪子開合
```

### 測試函式

-   `test_encoder()` - Serial 顯示編碼器計數 (放在 loop 中持續輸出)
-   `test_servo()` - 依序測試手臂和爪子動作
-   `test_motor()` - 依序測試前進、後退、左轉、右轉

### 命名規則

-   腳位定義：`XXX_PIN`（如 `IR_LL_PIN`, `ARM_PIN`）
-   PWM 通道：`CH_X_XXX`（如 `CH_L_FWD`）
-   角度常數：`XXX_UP/DOWN/OPEN/CLOSE`
-   動作函式：使用中文註解說明用途

## 函式庫依賴

| 函式庫       | 用途        | 注意事項                               |
| ------------ | ----------- | -------------------------------------- |
| ESP32Encoder | 編碼器讀取  | 使用 `halfQuad` 模式                   |
| QuickPID     | PID 控制    | 尚未實作                               |
| ESP32Servo   | 伺服馬達    | 必須使用 Timer 0                       |
| HUSKYLENS    | AI 視覺模組 | 需用 `#pragma GCC diagnostic` 抑制警告 |

## 擴展指引

-   新增感測器：在「腳位定義」區塊添加 `#define XXX_PIN`
-   新增伺服馬達：使用 Timer 0，參考現有 `arm`/`claw` 初始化方式
-   馬達動作函式保持簡潔，複雜邏輯放在 `loop()` 或獨立函式
-   HuskyLens 使用時需注意 I2C 初始化與編譯警告抑制
