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
#define ARM_DOWN 0   // 手臂下降角度
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
int IR_LL_read(); // 讀取最左側紅外線感測器
int IR_L_read();  // 讀取左側紅外線感測器
int IR_M_read();  // 讀取中間紅外線感測器
int IR_R_read();  // 讀取右側紅外線感測器
int IR_RR_read(); // 讀取最右側紅外線感測器

// --- 馬達控制 ---
// TODO: 請宣告以下函式
void motor(int L, int R); // 馬達控制 (L:左輪速度, R:右輪速度, 正值前進/負值後退)
void forward();           // 前進
void backward();          // 後退
void s_Left();            // 左轉 (右輪停止)
void s_Right();           // 右轉 (左輪停止)
void m_Left();            // 左轉 (左輪停止)
void m_Right();           // 右轉 (右輪停止)
void b_Left();            // 急左轉 (左輪反轉)
void b_Right();           // 急右轉 (右輪反轉)
void stop();              // 停止

// --- 伺服馬達控制 ---
// TODO: 請宣告以下函式 (回傳 void，無參數)
void arm_up();     // 手臂升起
void arm_down();   // 手臂下降
void claw_open();  // 爪子開啟
void claw_close(); // 爪子關閉

void pickup_object();  // 撿取物體動作
void release_object(); // 釋放物體動作

// --- 測試指令 ---
// TODO: 請宣告以下函式 (回傳 void，無參數)
// test_encoder - 編碼馬達測試 (顯示編碼器計數值)
// test_servo   - 伺服馬達測試 (手臂和爪子動作)
void test_motor(); // 馬達測試 (前進、後退、左轉、右轉)
// ====紅外線測試宣告====
void test_IR(); // 紅外線感測器測試 (顯示各感測器數值)

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
int IR_LL_read()
{
  int sensorvalue = analogRead(IR_LL_PIN);
  return (sensorvalue > IR_THRESHOLD) ? 1 : 0;
}
int IR_L_read()
{
  int sensorvalue = analogRead(IR_L_PIN);
  return (sensorvalue > IR_THRESHOLD) ? 1 : 0;
}
int IR_M_read()
{
  int sensorvalue = analogRead(IR_M_PIN);
  return (sensorvalue > IR_THRESHOLD) ? 1 : 0;
}
int IR_R_read()
{
  int sensorvalue = analogRead(IR_R_PIN);
  return (sensorvalue > IR_THRESHOLD) ? 1 : 0;
}
int IR_RR_read()
{
  int sensorvalue = analogRead(IR_RR_PIN);
  return (sensorvalue > IR_THRESHOLD) ? 1 : 0;
}
// --- 馬達控制 ---
// 功能：控制左右馬達速度 (-255~255)
// 提示：使用 ledcWrite(通道, PWM值) 控制輸出
//       正值 → 正轉通道輸出，反轉通道=0
//       負值 → 正轉通道=0，反轉通道輸出
void motor(int L, int R)
{
  if (L > 255)
    L = 255;
  else if (L < -255)
    L = -255;
  if (R > 255)
    R = 255;
  else if (R < -255)
    R = -255;

  if (L > 0) // 左輪正轉
  {
    ledcWrite(CH_L_FWD, L);
    ledcWrite(CH_L_BWD, 0);
  }
  else // 左輪反轉
  {
    ledcWrite(CH_L_FWD, 0);
    ledcWrite(CH_L_BWD, -L);
  }
  if (R > 0)
  {
    ledcWrite(CH_R_FWD, R);
    ledcWrite(CH_R_BWD, 0);
  }
  else
  {
    ledcWrite(CH_R_FWD, 0);
    ledcWrite(CH_R_BWD, -R);
  }
}
// --- 動作函式 ---
// 功能：前進、後退、左轉、右轉、停止等
// 提示：呼叫馬達控制函式，帶入適當的左右輪速度
void forward()
{
  motor(255, 255);
}
void backward()
{
  motor(-255, -255);
}
void s_Left()
{ // 左轉 (右輪停止)
  motor(125, 255);
}
void s_Right()
{ // 右轉 (左輪停止)
  motor(255, 125);
}
void m_Left()
{ // 左轉 (左輪停止)
  motor(0, 255);
}
void m_Right()
{ // 右轉 (右輪停止)
  motor(255, 0);
}
void b_Left()
{ // 急左轉 (左輪反轉)
  motor(-255, 255);
}
void b_Right()
{ // 急右轉 (右輪反轉)
  motor(255, -255);
}
void stop()
{
  motor(0, 0);
}

// --- 伺服馬達控制 ---
// 功能：手臂升降、爪子開合
// 提示：使用 arm.write(角度) 和 claw.write(角度)

void arm_up()
{
  arm.write(ARM_UP);
}
void arm_down()
{
  arm.write(ARM_DOWN);
}
void claw_open()
{
  claw.write(CLAW_OPEN);
}
void claw_close()
{
  claw.write(CLAW_CLOSE);
}

void pickup_object()
{
  claw_open();
  delay(200); // 等待爪子開啟
  arm_down();
  delay(200); // 等待手臂下降
  claw_close();
  delay(200); // 等待爪子關閉
  arm_up();
  delay(200); // 等待手臂升起
}
void release_object()
{
  arm_down();
  delay(200); // 等待手臂下降
  claw_open();
  delay(200); // 等待爪子開啟
}
// --- 測試函式 ---
// 功能：測試各元件是否正常運作
// 提示：依序執行動作並用 Serial 輸出狀態
void test_motor()
{
  Serial.println("Motor Test: Forward");
  forward();
  delay(1000);
  Serial.println("Motor Test: Backward");
  backward();
  delay(1000);
  Serial.println("Motor Test: Left");
  m_Left();
  delay(1000);
  Serial.println("Motor Test: Right");
  m_Right();
  delay(1000);
  Serial.println("Motor Test: Stop");
  stop();
  delay(500);
}
void test_IR()
{
  Serial.print("IR_LL: ");
  Serial.print(IR_LL_read());
  Serial.print(" IR_L: ");
  Serial.print(IR_L_read());
  Serial.print(" IR_M: ");
  Serial.print(IR_M_read());
  Serial.print(" IR_R: ");
  Serial.print(IR_R_read());
  Serial.print(" IR_RR: ");
  Serial.println(IR_RR_read());
  delay(100);
}
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
  pinMode(MOTOR_L_BWD, OUTPUT);           // 設定左馬達反轉腳位為輸出
  ledcSetup(CH_L_BWD, PWM_FREQ, PWM_RES); // 設定 PWM 頻率與解析度 0~255
  ledcAttachPin(MOTOR_L_BWD, CH_L_BWD);   // 將腳位綁定到 PWM 通道

  pinMode(MOTOR_L_FWD, OUTPUT);           // 設定左馬達正轉腳位為輸出
  ledcSetup(CH_L_FWD, PWM_FREQ, PWM_RES); // 設定 PWM 頻率與解析度 0~255
  ledcAttachPin(MOTOR_L_FWD, CH_L_FWD);   // 將腳位綁定到 PWM 通道

  pinMode(MOTOR_R_BWD, OUTPUT);           // 設定右馬達反轉腳位為輸出
  ledcSetup(CH_R_BWD, PWM_FREQ, PWM_RES); // 設定 PWM 頻率與解析度 0~255
  ledcAttachPin(MOTOR_R_BWD, CH_R_BWD);   // 將腳位綁定到 PWM 通道

  pinMode(MOTOR_R_FWD, OUTPUT);           // 設定右馬達正轉腳位為輸出
  ledcSetup(CH_R_FWD, PWM_FREQ, PWM_RES); // 設定 PWM 頻率與解析度 0~255
  ledcAttachPin(MOTOR_R_FWD, CH_R_FWD);   // 將腳位綁定到 PWM 通道

  // TODO: 初始化完成後，可呼叫停止函式確保馬達不會亂轉
  pickup_object();
  delay(3000);
  release_object();
}

void loop()
{
  // TODO: 在此撰寫主程式邏輯
}