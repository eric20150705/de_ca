#include <Arduino.h>
#include <ESP32Encoder.h>
#include <QuickPID.h>
#include <ESP32Servo.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#pragma GCC diagnostic ignored "-Wunused-variable"
#include <HUSKYLENS.h>
#pragma GCC diagnostic pop
#include <esp32-hal-ledc.h>

// ===== 編碼器物件 =====
ESP32Encoder leftEncoder;
ESP32Encoder rightEncoder;

// ===== 伺服馬達物件 =====
Servo arm;  // 手臂伺服馬達
Servo claw; // 爪子伺服馬達

// ===== 伺服馬達腳位定義 =====
#define ARM_PIN 14  // 手臂伺服馬達腳位
#define CLAW_PIN 15 // 爪子伺服馬達腳位

// ===== 伺服馬達角度設定 =====
#define ARM_UP 90    // 手臂升起角度
#define ARM_DOWN 15  // 手臂下降角度
#define CLAW_OPEN 90 // 爪子開啟角度
#define CLAW_CLOSE 0 // 爪子關閉角度

// ===== 編碼器腳位定義 =====
#define LEFT_ENCODER_A 18  // 左編碼器 A 相
#define LEFT_ENCODER_B 19  // 左編碼器 B 相
#define RIGHT_ENCODER_A 23 // 右編碼器 A 相
#define RIGHT_ENCODER_B 5  // 右編碼器 B 相

// ===== 腳位定義 =====
// 紅外線感測器腳位
#define IR_LL_PIN 39 // 最左側紅外線
#define IR_L_PIN 32  // 左側紅外線
#define IR_M_PIN 33  // 中間紅外線
#define IR_R_PIN 34  // 右側紅外線
#define IR_RR_PIN 35 // 最右側紅外線

// 馬達控制腳位
#define MOTOR_L_FWD 27 // 左馬達正轉
#define MOTOR_L_BWD 13 // 左馬達反轉
#define MOTOR_R_FWD 2  // 右馬達正轉
#define MOTOR_R_BWD 4  // 右馬達反轉

// PWM 通道 (使用 Timer 2 的通道 8-11，Timer 0 預留給伺服馬達)
#define CH_L_FWD 8  // 左馬達正轉通道 (Timer 2)
#define CH_L_BWD 9  // 左馬達反轉通道 (Timer 2)
#define CH_R_FWD 10 // 右馬達正轉通道 (Timer 2)
#define CH_R_BWD 11 // 右馬達反轉通道 (Timer 2)

// ===== 參數設定 =====
#define IR_THRESHOLD 2000 // 紅外線感測器閾值
#define PWM_FREQ 75000    // PWM 頻率
#define PWM_RES 8         // PWM 解析度 (8-bit = 0~255)

// ===== 函式前向宣告 =====
// 提示：函式宣告格式為 回傳型別 函式名稱(參數);
//       例如：void forward(); 或 int IR_M_read();

// --- 紅外線感測器 ---
// TODO: 請宣告以下函式 (回傳 int，無參數)
// IR_LL_read - 讀取最左側紅外線感測器
// IR_L_read  - 讀取左側紅外線感測器
// IR_M_read  - 讀取中間紅外線感測器
// IR_R_read  - 讀取右側紅外線感測器
// IR_RR_read - 讀取最右側紅外線感測器

// --- 馬達控制 ---
// TODO: 請宣告以下函式
// motor(int L, int R) - 馬達控制 (L:左輪速度, R:右輪速度, 正值前進/負值後退)
// forward  - 前進
// backward - 後退
// m_Left   - 左轉 (左輪停止)
// m_Right  - 右轉 (右輪停止)
// b_Left   - 急左轉 (左輪反轉)
// b_Right  - 急右轉 (右輪反轉)
// stop     - 停止

// --- 伺服馬達控制 ---
// TODO: 請宣告以下函式 (回傳 void，無參數)
// arm_up    - 手臂升起
// arm_down  - 手臂下降
// claw_open  - 爪子開啟
// claw_close - 爪子關閉

// --- 測試指令 ---
// TODO: 請宣告以下函式 (回傳 void，無參數)
// test_encoder - 編碼馬達測試 (顯示編碼器計數值)
// test_servo   - 伺服馬達測試 (手臂和爪子動作)
// test_motor   - 馬達測試 (前進、後退、左轉、右轉)

// ===== 自訂函式區 =====
// TODO: 請在此區塊建立你的自訂函式
//
// 【函式建立格式】
//   回傳型別 函式名稱(參數列表)
//   {
//       函式內容
//   }
//
// 【建議建立的函式】
//
// --- 紅外線感測器 ---
// 功能：讀取感測器數值，回傳 0 (白線) 或 1 (黑線)
// 提示：使用 analogRead(腳位) 讀取，與 IR_THRESHOLD 比較
//
// --- 馬達控制 ---
// 功能：控制左右馬達速度 (-255~255)
// 提示：使用 ledcWrite(通道, PWM值) 控制輸出
//       正值 → 正轉通道輸出，反轉通道=0
//       負值 → 正轉通道=0，反轉通道輸出
//
// --- 動作函式 ---
// 功能：前進、後退、左轉、右轉、停止等
// 提示：呼叫馬達控制函式，帶入適當的左右輪速度
//
// --- 伺服馬達控制 ---
// 功能：手臂升降、爪子開合
// 提示：使用 arm.write(角度) 和 claw.write(角度)
//
// --- 測試函式 ---
// 功能：測試各元件是否正常運作
// 提示：依序執行動作並用 Serial 輸出狀態

// ===== 主程式 =====
void setup()
{
  Serial.begin(9600);

  // --- 伺服馬達定時器分配 (Timer 0 給伺服馬達) ---
  ESP32PWM::allocateTimer(0);

  // --- 伺服馬達初始化 ---
  arm.setPeriodHertz(50);           // 標準 50Hz 伺服馬達
  arm.attach(ARM_PIN, 500, 2400);   // SG90 脈寬範圍 500~2400us
  claw.setPeriodHertz(50);          // 標準 50Hz 伺服馬達
  claw.attach(CLAW_PIN, 500, 2400); // SG90 脈寬範圍 500~2400us

  // --- 編碼器初始化 ---
  ESP32Encoder::useInternalWeakPullResistors = puType::up;
  leftEncoder.attachHalfQuad(LEFT_ENCODER_A, LEFT_ENCODER_B);
  leftEncoder.clearCount();
  rightEncoder.attachHalfQuad(RIGHT_ENCODER_A, RIGHT_ENCODER_B);
  rightEncoder.clearCount();

  // --- 紅外線感測器初始化 ---
  pinMode(IR_LL_PIN, INPUT);
  pinMode(IR_L_PIN, INPUT);
  pinMode(IR_M_PIN, INPUT);
  pinMode(IR_R_PIN, INPUT);
  pinMode(IR_RR_PIN, INPUT);

  // --- 馬達 PWM 初始化 (Timer 2，Timer 0 預留給伺服馬達) ---
  pinMode(MOTOR_L_BWD, OUTPUT);
  ledcSetup(CH_L_BWD, PWM_FREQ, PWM_RES);
  ledcAttachPin(MOTOR_L_BWD, CH_L_BWD);

  pinMode(MOTOR_L_FWD, OUTPUT);
  ledcSetup(CH_L_FWD, PWM_FREQ, PWM_RES);
  ledcAttachPin(MOTOR_L_FWD, CH_L_FWD);

  pinMode(MOTOR_R_BWD, OUTPUT);
  ledcSetup(CH_R_BWD, PWM_FREQ, PWM_RES);
  ledcAttachPin(MOTOR_R_BWD, CH_R_BWD);

  pinMode(MOTOR_R_FWD, OUTPUT);
  ledcSetup(CH_R_FWD, PWM_FREQ, PWM_RES);
  ledcAttachPin(MOTOR_R_FWD, CH_R_FWD);

  // TODO: 初始化完成後，可呼叫停止函式確保馬達不會亂轉
  // stop();
}

void loop()
{
  // TODO: 在此撰寫主程式邏輯
}
