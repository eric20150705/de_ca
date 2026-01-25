# ESP32 智慧小車 - AI 編程指引

ESP32 教學機器人專案（PlatformIO + Arduino），具備循跡、撿取、視覺辨識功能。主程式：[src/main.cpp](../src/main.cpp)

## 開發指令

```bash
pio run                # 編譯
pio run -t upload      # 上傳（USB 或 OTA）
pio device monitor     # Serial 監控 (9600 baud)
```

## 關鍵硬體初始化順序

**順序錯誤會導致硬體失效！** 必須遵循：

```cpp
// 1. Timer 分配（最先）
ESP32PWM::allocateTimer(0);  // arm + claw
ESP32PWM::allocateTimer(1);  // camera（GPIO25 需獨立 Timer）

// 2. 伺服馬達
arm.attach(14, 500, 2400);
claw.attach(15, 500, 2400);
camera.attach(25, 500, 2400);

// 3. 編碼器（啟用上拉）
ESP32Encoder::useInternalWeakPullResistors = puType::up;
leftEncoder.attachHalfQuad(18, 19);

// 4. 馬達 PWM（Timer 2, CH 4-7）
ledcSetup(4, 75000, 8);  // 必須用 CH 4-7，避開伺服 Timer
```

## 核心 API 速查

| 函式                                 | 說明                       |
| ------------------------------------ | -------------------------- |
| `motor(L, R)`                        | 馬達控制，-255~255         |
| `forward() / backward()`             | 直行（固定 PWM）           |
| `p_fw_v2(dist) / p_bw_v2(dist)`      | 距離控制（速度閉環，推薦） |
| `p_left(degree) / p_right(degree)`   | 角度轉向                   |
| `pickup_object() / release_object()` | 撿取/釋放物體              |
| `trail() / trail_v2()`               | 循跡（v2 用速度閉環）      |
| `IR_LL/L/M/R/RR_read()`              | 紅外線讀取，回傳 0/1       |

## 註解規範（Better Comments）

| 標記      | 用途         | 範例                               |
| --------- | ------------ | ---------------------------------- |
| `//!`     | 硬體安全警告 | `//! 正反轉不可同時輸出會短路`     |
| `//?`     | 可調參數     | `//? 偏左→增加左輪：motor(60, 55)` |
| `//*`     | 重要步驟     | `//* 階段 1：快速前進`             |
| `//TODO:` | 待實作       | `//TODO: 整合 HuskyLens`           |

**註解原則**：術語加括號說明（如「PWM（0~255）」）、數值標單位（如「c/100ms」）

## 常見問題

| 問題           | 原因           | 解決                                             |
| -------------- | -------------- | ------------------------------------------------ |
| 伺服馬達無反應 | Timer 未先分配 | `allocateTimer()` 必須在 `attach()` 前           |
| 編碼器計數異常 | 未啟用上拉電阻 | 設定 `useInternalWeakPullResistors = puType::up` |
| 車子不走直線   | 馬達機械差異   | 調整 `forward()` 的 L/R 值                       |
| 距離控制超距   | 慣性未補償     | 增加 `TOLERANCE` 或減小 `DECEL_START`            |

## 腳位對照

- **紅外線**：39/32/33/34/35（LL/L/M/R/RR）
- **編碼器**：18/19（左）、23/5（右）
- **馬達**：27/13（左正/反）、2/4（右正/反）
- **伺服**：14（手臂）、15（爪子）、25（攝像頭）

## 函式庫注意

- **HUSKYLENS**：需用 `#pragma GCC diagnostic` 包裹抑制警告
- **ESP32Servo**：必須先 `allocateTimer()`
- **I2C 共用**：OLED(0x3C) 與 HuskyLens(0x32) 可共用 SDA=21/SCL=22
