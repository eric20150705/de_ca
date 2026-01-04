#include <Arduino.h>
#include <ESP32Encoder.h>
#include <QuickPID.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
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

// ===== OLED (SSD1306) =====
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDR 0x3C
#define OLED_SDA 21
#define OLED_SCL 22
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
bool oled_ready = false;

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
int IR_LL_read();           // 讀取最左側紅外線感測器
int IR_L_read();            // 讀取左側紅外線感測器
int IR_M_read();            // 讀取中間紅外線感測器
int IR_R_read();            // 讀取右側紅外線感測器
int IR_RR_read();           // 讀取最右側紅外線感測器
void oled_init();           // OLED 初始化
void oled_show_ir_status(); // OLED 顯示紅外線狀態
// --- 馬達控制 ---
// TODO: 請宣告以下函式
void motor(int L, int R);   // 馬達控制 (L:左輪速度, R:右輪速度, 正值前進/負值後退)
void forward();             // 前進
void backward();            // 後退
void s_Left();              // 左轉 (右輪停止)
void s_Right();             // 右轉 (左輪停止)
void m_Left();              // 左轉 (左輪停止)
void m_Right();             // 右轉 (右輪停止)
void b_Left();              // 急左轉 (左輪反轉)
void b_Right();             // 急右轉 (右輪反轉)
void stop();                // 停止
void p_fw(int distance);    // 設定距離前進
void p_bw(int distance);    // 設定距離後退
void p_left(int distance);  // 設定距離左轉
void p_right(int distance); // 設定距離右轉
void p_test(int distance);  // 距離測試

// --- 伺服馬達控制 ---
// TODO: 請宣告以下函式 (回傳 void，無參數)
void arm_up();       // 手臂升起
void arm_down();     // 手臂下降
void claw_open();    // 爪子開啟
void claw_close();   // 爪子關閉
void test_encoder(); // 撿取物體動作

void pickup_object();  // 撿取物體動作
void release_object(); // 釋放物體動作

// --- 測試指令 ---
// TODO: 請宣告以下函式 (回傳 void，無參數)
// test_encoder - 編碼馬達測試 (顯示編碼器計數值)
void test_encoder()
{
  long leftCount = leftEncoder.getCount();
  long rightCount = rightEncoder.getCount();

  Serial.print("Left Encoder Count: ");
  Serial.print(leftCount);
  Serial.print("  Right Encoder Count: ");
  Serial.println(rightCount);
};
// test_servo   - 伺服馬達測試 (手臂和爪子動作)
void test_motor(); // 馬達測試 (前進、後退、左轉、右轉)
// ====紅外線測試宣告====
void test_IR(); // 紅外線感測器測試 (顯示各感測器數值)
// =========尋機功能===========
void trail(); // 尋機功能

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
// --- OLED 顯示 ---
void oled_init()
{
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR))
  {
    Serial.println("SSD1306 init failed");
    return;
  }
  oled_ready = true;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
}

void oled_show_ir_status()
{
  if (!oled_ready)
  {
    return;
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("IR:L ");
  display.print(IR_LL_read());
  display.print(" ");
  display.print(IR_L_read());
  display.print(" ");
  display.print(IR_M_read());
  display.print(" ");
  display.print(IR_R_read());
  display.print(" ");
  display.print(IR_RR_read());
  display.print(" R");
  display.println(" ");
  display.print("Encoder L:");
  display.print(leftEncoder.getCount());
  display.print(" R:");
  display.println(rightEncoder.getCount());
  display.display();
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
  motor(55, 58);
}
void backward()
{
  motor(-55, -55);
}
void s_Left()
{ // 左轉 (右輪停止)
  motor(-75, 55);
}
void s_Right()
{ // 右轉 (左輪停止)
  motor(55, -75);
}
void m_Left()
{ // 左轉 (左輪停止)
  motor(0, 55);
}
void m_Right()
{ // 右轉 (右輪停止)
  motor(55, -00);
}
void b_Left()
{ // 急左轉 (左輪反轉)
  motor(-55, 55);
}
void b_Right()
{ // 急右轉 (右輪反轉)
  motor(55, -55);
}
void stop()
{
  motor(0, 0);
  // leftEncoder.clearCount();
  // rightEncoder.clearCount();
}

void p_test(int distance)
{
  long targetCount = distance; // 假設 1 單位距離對應 1 編碼器計數值
  leftEncoder.clearCount();
  rightEncoder.clearCount();

  // 快速前進到接近目標（留20的餘量）
  motor(55, 0); // 只控制左輪
  while (true)
  {
    long leftCount = leftEncoder.getCount();
    if (leftCount >= targetCount - 20)
    {
      break;
    }
    delay(1);
  }

  // 精調階段：低速前進到目標附近
  motor(20, 0); // 只控制左輪
  delay(50);
  stop();

  // 反復調整直到誤差小於10
  const int TOLERANCE = 10;
  const int MIN_SPEED = 25; // 最小驅動速度（克服靜摩擦力）
  unsigned long maxAttempts = 100;
  unsigned long attempts = 0;

  while (attempts < maxAttempts)
  {
    long leftCount = leftEncoder.getCount();
    long error = leftCount - targetCount;

    // 誤差在容差範圍內，完成
    if (abs(error) < TOLERANCE)
    {
      break;
    }

    // 根據誤差大小動態計算調整速度
    int adjustSpeed = map(abs(error), TOLERANCE, 100, MIN_SPEED, 50);
    adjustSpeed = constrain(adjustSpeed, MIN_SPEED, 50);

    // 超過目標太多，反向調整
    if (error > TOLERANCE)
    {
      motor(-adjustSpeed, 0); // 只控制左輪
      delay(30);
      stop();
    }
    // 未達目標，繼續前進
    else if (error < -TOLERANCE)
    {
      motor(adjustSpeed, 0); // 只控制左輪
      delay(30);
      stop();
    }

    delay(50);
    attempts++;
  }

  stop();
}

void p_fw(int distance)
{
  long targetCount = distance; // 假設 1 單位距離對應 1 編碼器計數值
  leftEncoder.clearCount();
  rightEncoder.clearCount();
  forward();
  while (true)
  {
    long leftCount = leftEncoder.getCount();
    long rightCount = rightEncoder.getCount();
    if (leftCount >= targetCount || rightCount >= targetCount)
    {
      break;
    }
    delay(1);
  }
  stop();
}
void p_bw(int distance)
{
  long targetCount = distance; // 假設 1 單位距離對應 1 編碼器計數值
  leftEncoder.clearCount();
  rightEncoder.clearCount();
  backward();
  while (true)
  {
    long leftCount = leftEncoder.getCount();
    long rightCount = rightEncoder.getCount();
    if (leftCount <= -targetCount || rightCount <= -targetCount)
    {
      break;
    }
    delay(1);
  }
  stop();
}
void p_left(int distance)
{
  long targetCount = distance; // 假設 1 單位距離對應 1 編碼器計數值
  leftEncoder.clearCount();
  rightEncoder.clearCount();
  b_Left();
  while (true)
  {
    long leftCount = leftEncoder.getCount();
    long rightCount = rightEncoder.getCount();
    if (abs(leftCount) >= targetCount || abs(rightCount) >= targetCount)
    {
      break;
    }
    delay(1);
  }
  stop();
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

//=======循跡功能=========
void trail()
{
  if (IR_M_read() == 1)
  {

    if (IR_L_read() == 1 && IR_R_read() == 0)
    {
      m_Left();
    }
    else if (IR_L_read() == 0 && IR_R_read() == 1)
    {
      m_Right();
    }
    else
    {
      forward();
    }
  }
  else
  {
    if (IR_L_read() == 1 && IR_R_read() == 0)
    {
      b_Left();
    }
    else if (IR_L_read() == 0 && IR_R_read() == 1)
    {
      b_Right();
    }
  }
}
// ===== 主程式 =====
void setup()
{
  Serial.begin(9600);

  // --- OLED 初始化 ---
  oled_init();

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

  // while (!((IR_LL_read() == 1) || (IR_RR_read() == 1)))
  // {
  //   trail();
  // }
  // stop();
  // delay(1000);
  // while (!((IR_LL_read() == 1) || (IR_RR_read() == 1)))
  // {
  //   trail();
  // }
  // stop();
  // delay(1000);
  // while (!((IR_R_read() == 1) && (IR_M_read() == 1)) || ((IR_L_read() == 1) || (IR_M_read() == 1)))
  // {
  //   stop();
  //   delay(1000);
  //   b_Left();
  //   b_Left();
  // }
  p_test(1000);
  while (true)
  {
    test_encoder();
    oled_show_ir_status();
    delay(500);
  };
}

void loop()
{
  // TODO: 在此撰寫主程式邏輯
}
