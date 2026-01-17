// ===== ESP32 智慧小車主程式 =====
// 功能：控制雙馬達、伺服馬達、紅外線陣列、編碼器、OLED 顯示等
// 編譯：pio run -t upload
// 監控：pio device monitor (9600 baud)

#include <Arduino.h>          // Arduino 核心庫（digitalWrite, analogRead 等）
#include <ESP32Encoder.h>     // ESP32 編碼器庫，用於讀取馬達反饋
#include <QuickPID.h>         // PID 控制庫（待實作）
#include <ESP32Servo.h>       // ESP32 伺服馬達庫，用於控制 SG90
#include <Wire.h>             // I2C 通訊庫，用於 OLED 顯示器
#include <Adafruit_GFX.h>     // Adafruit 圖形庫，提供 OLED 繪圖功能
#include <Adafruit_SSD1306.h> // Adafruit SSD1306 OLED 驅動庫
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#pragma GCC diagnostic ignored "-Wunused-variable"
#include <HUSKYLENS.h> // HuskyLens AI 視覺識別模組（待整合）
#pragma GCC diagnostic pop
#include <esp32-hal-ledc.h> // ESP32 LEDC PWM 驅動庫，用於馬達 PWM 控制

// ===== 編碼器物件 =====
// 讀取左右馬達的旋轉計數，用於距離反饋控制
ESP32Encoder leftEncoder;  // 左馬達編碼器
ESP32Encoder rightEncoder; // 右馬達編碼器

// ===== 伺服馬達物件 =====
// ESP32Servo 提供硬體 PWM 控制，精確控制角度（0~180°）
Servo arm;  // 手臂伺服馬達（負責上升/下降）
Servo claw; // 爪子伺服馬達（負責開啟/關閉）

// ===== OLED (SSD1306) 顯示器 =====
// 用於實時顯示紅外線狀態、編碼器計數、系統狀態等
#define OLED_WIDTH 128                                                // OLED 螢幕寬度（像素）
#define OLED_HEIGHT 64                                                // OLED 螢幕高度（像素）
#define OLED_RESET -1                                                 // 復位腳位（-1 表示無硬體復位腳位）
#define OLED_ADDR 0x3C                                                // OLED I2C 地址（SSD1306 標準地址）
#define OLED_SDA 21                                                   // I2C 資料線（SDA）腳位
#define OLED_SCL 22                                                   // I2C 時鐘線（SCL）腳位
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET); // OLED 物件
bool oled_ready = false;                                              // OLED 初始化標誌位

// ===== 極限速度測試結果（全域變數）=====
// 用於儲存 test_max_speed() 的測試結果，讓 oled_show_ir_status() 也能顯示
long maxSpeed_L = 0;          // 左輪極限速度（c/100ms）
long maxSpeed_R = 0;          // 右輪極限速度（c/100ms）
bool maxSpeed_tested = false; // 是否已測試過

// ===== 伺服馬達腳位定義 =====
// 使用 ESP32 的 GPIO 腳位連接 SG90 伺服馬達（PWM 控制）
#define ARM_PIN 14  // 手臂伺服馬達訊號腳位
#define CLAW_PIN 15 // 爪子伺服馬達訊號腳位

// ===== 伺服馬達角度設定 =====
// SG90 伺服馬達角度範圍 0~180°，根據機械結構調整以下角度
#define ARM_UP 90    // 手臂升起角度（0° = 最低，180° = 最高）
#define ARM_DOWN 0   // 手臂下降角度
#define CLAW_OPEN 90 // 爪子開啟角度（夾不住物體）
#define CLAW_CLOSE 0 // 爪子關閉角度（夾住物體）

// ===== 編碼器腳位定義 =====
// 編碼器輸出 A 相和 B 相訊號，ESP32 透過相位差計算旋轉方向
#define LEFT_ENCODER_A 18  // 左馬達編碼器 A 相（正交解碼）
#define LEFT_ENCODER_B 19  // 左馬達編碼器 B 相
#define RIGHT_ENCODER_A 23 // 右馬達編碼器 A 相
#define RIGHT_ENCODER_B 5  // 右馬達編碼器 B 相

// ===== 腳位定義 =====
// 紅外線感測器：5 路陣列，用於循跡和邊界檢測
#define IR_LL_PIN 39 // 最左側紅外線（GPIO39）
#define IR_L_PIN 32  // 左側紅外線（GPIO32）
#define IR_M_PIN 33  // 中間紅外線（GPIO33）
#define IR_R_PIN 34  // 右側紅外線（GPIO34）
#define IR_RR_PIN 35 // 最右側紅外線（GPIO35）

// 馬達控制腳位：每個馬達有正轉和反轉兩個腳位
#define MOTOR_L_FWD 27 // 左馬達正轉（GPIO27）
#define MOTOR_L_BWD 13 // 左馬達反轉（GPIO13）
#define MOTOR_R_FWD 2  // 右馬達正轉（GPIO2）
#define MOTOR_R_BWD 4  // 右馬達反轉（GPIO4）

// PWM 通道設定 (使用 Timer 2 的通道 8-11，Timer 0 預留給伺服馬達)
// ESP32 LEDC 有 16 個通道（0~15），分別對應 4 個定時器
#define CH_L_FWD 8  // 左馬達正轉通道（Timer 2, Channel 8）
#define CH_L_BWD 9  // 左馬達反轉通道（Timer 2, Channel 9）
#define CH_R_FWD 10 // 右馬達正轉通道（Timer 2, Channel 10）
#define CH_R_BWD 11 // 右馬達反轉通道（Timer 2, Channel 11）

// ===== 參數設定 =====
#define IR_THRESHOLD 2000 // 紅外線感測閾值：>2000 判定為黑線，<2000 為白線
#define PWM_FREQ 75000    // PWM 頻率 75kHz（高頻降低馬達噪音和脈動）
#define PWM_RES 8         // PWM 解析度 8-bit，即 0~255 共 256 階

// ===== 速度閉環控制參數 =====
// 速度控制週期（毫秒）：根據 test_max_speed() 測得的極限速度調整
// 建議：每週期計數變化 ≥ 5 較穩定
// 例如：極限 45 c/100ms → 用 20ms（約 9c）；極限 20 c/100ms → 用 50ms（約 10c）
#define SPEED_CONTROL_PERIOD 20 // 速度控制週期，單位 ms

// SPEED_KP：速度閉環比例係數
// - 作用：PWM 調整量 = (目標速度 - 實際速度) * Kp
// - 調整方法：
//   - 太大（例：5.0）→ 車子一頓一頓、抖動 → 往下調
//   - 太小（例：0.01）→ 反應慢、達不到目標速度 → 往上調
//   - 建議從 0.1 開始，逐步微調至平順
#define SPEED_KP 0.1 // 速度控制比例係數

// ===== 函式前向宣告 =====
// 提示：函式宣告格式為 回傳型別 函式名稱(參數);
//       例如：void forward(); 或 int IR_M_read();
//
// 前向宣告的目的：允許 setup() 和 loop() 在函式定義之前呼叫這些函式

// --- 紅外線感測器相關函式 ---
// 功能：讀取各路紅外線感測器，回傳 1 (黑線) 或 0 (白線)
int IR_LL_read(); // 讀取最左側紅外線感測器
int IR_L_read();  // 讀取左側紅外線感測器
int IR_M_read();  // 讀取中間紅外線感測器
int IR_R_read();  // 讀取右側紅外線感測器
int IR_RR_read(); // 讀取最右側紅外線感測器

// --- OLED 顯示相關函式 ---
void oled_init();           // 初始化 OLED 顯示器（I2C 通訊）
void oled_show_ir_status(); // 在 OLED 顯示紅外線狀態和編碼器計數

// --- 馬達控制相關函式 ---
// 低層函式：直接控制左右馬達 PWM 值
void motor(int L, int R); // 馬達控制 (L:左輪速度 -255~255, R:右輪速度 -255~255)

// 高層動作函式：基於 motor() 實現的複合動作
void forward();  // 直線前進 (左輪55，右輪58 - 補償左偏)
void backward(); // 直線後退 (左輪-55，右輪-55)
void s_Left();   // 原地左轉 (左輪-75，右輪55)
void s_Right();  // 原地右轉 (左輪55，右輪-75)
void m_Left();   // 左轉 (左輪停止，右輪55)
void m_Right();  // 右轉 (左輪55，右輪停止)
void b_Left();   // 急左轉 (左輪-55，右輪55 - 左輪反轉)
void b_Right();  // 急右轉 (左輪55，右輪-55 - 右輪反轉)
void stop();     // 停止（兩輪速度都為 0）

// 距離控制函式：根據編碼器反饋控制精確距離
void p_fw(int distance);    // 設定距離前進（易超距，建議用 PID 改進）
void p_bw(int distance);    // 設定距離後退
void p_left(int distance);  // 設定距離左轉
void p_right(int distance); // 設定距離右轉
void p_test(int distance);  // 距離測試（帶動態調整功能）

// --- 伺服馬達控制函式 ---
// 功能：控制手臂和爪子的角度，單位為度數（0~180°）
void arm_up();     // 手臂升起（寫入 ARM_UP 角度）
void arm_down();   // 手臂下降（寫入 ARM_DOWN 角度）
void claw_open();  // 爪子開啟（寫入 CLAW_OPEN 角度）
void claw_close(); // 爪子關閉（寫入 CLAW_CLOSE 角度）

// 複合伺服動作
void pickup_object();  // 撿取物體動作序列（張爪 → 下降 → 夾爪 → 上升）
void release_object(); // 釋放物體動作序列（下降 → 張爪）
void prepare_pickup(); // 打開爪子並且降下手臂，準備撿取物體

// --- 測試函式 =====
// 功能：逐一測試各硬體元件是否正常運作，結果透過 Serial 或 OLED 輸出
void test_motor();     // 馬達測試（順序執行：前進、後退、左轉、右轉、停止）
void test_IR();        // 紅外線感測器測試（輸出 5 路感測值）
void test_encoder();   // 編碼器測試（輸出左右馬達計數值）
void test_max_speed(); // 極限速度測試：PWM=255 測量左右輪最大 c/100ms
void test_speed();     // 即時速度測試：每 100ms 印出左右輪速度，用於觀察

// --- 速度閉環控制函式 ---
// 功能：根據目標速度動態調整 PWM，不受電池電量影響
void speed_control(float L_target, float R_target); // 速度閉環控制（輸入目標速度 c/週期）
void p_fw_v2(int distance);
void p_bw_v2(int distance); // 新版前進：速度閉環 + 左右同步修正

// --- 循跡功能 ---
void trail(); // 循跡邏輯（根據 IR 陣列自動調整方向以跟隨黑線）

// ===== 自訂函式實作區 =====
// 本區塊包含所有硬體控制和功能邏輯的實現

// ============ 紅外線感測器函式 ============
// 原理：紅外線感測器於黑線上反射值較小，白地反射值較大
// 讀取模式：使用 analogRead() 取得 0~4095 的類比值，與閾值比較
// 回傳值：1 表示黑線（物體），0 表示白線（空地）

int IR_LL_read()
{
  int sensorvalue = analogRead(IR_LL_PIN);     // 讀取最左側感測器的類比值
  return (sensorvalue > IR_THRESHOLD) ? 1 : 0; // 與閾值比較
}
int IR_L_read()
{
  int sensorvalue = analogRead(IR_L_PIN); // 讀取左側感測器的類比值
  return (sensorvalue > IR_THRESHOLD) ? 1 : 0;
}
int IR_M_read()
{
  int sensorvalue = analogRead(IR_M_PIN); // 讀取中間感測器（最關鍵）
  return (sensorvalue > IR_THRESHOLD) ? 1 : 0;
}
int IR_R_read()
{
  int sensorvalue = analogRead(IR_R_PIN); // 讀取右側感測器的類比值
  return (sensorvalue > IR_THRESHOLD) ? 1 : 0;
}
int IR_RR_read()
{
  int sensorvalue = analogRead(IR_RR_PIN); // 讀取最右側感測器的類比值
  return (sensorvalue > IR_THRESHOLD) ? 1 : 0;
}

// ============ OLED 顯示函式 ============
void oled_init()
{
  // 初始化 I2C 通訊：SDA=21, SCL=22
  Wire.begin(OLED_SDA, OLED_SCL);

  // 嘗試初始化 SSD1306 OLED 顯示器
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR))
  {
    Serial.println("SSD1306 init failed");
    return;
  }

  // 初始化成功，設定標誌位
  oled_ready = true;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
}

void oled_show_ir_status()
{
  // 檢查 OLED 是否初始化成功
  if (!oled_ready)
  {
    return;
  }

  // 清除並重新繪製顯示內容
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // 第一行：顯示 5 路紅外線感測值
  display.setCursor(0, 0);
  display.print("IR:L ");
  display.print(IR_LL_read()); // 最左
  display.print(" ");
  display.print(IR_L_read()); // 左
  display.print(" ");
  display.print(IR_M_read()); // 中
  display.print(" ");
  display.print(IR_R_read()); // 右
  display.print(" ");
  display.print(IR_RR_read()); // 最右
  display.print(" R");

  // 第二行：顯示編碼器計數值
  display.println(" ");
  display.print("Enc L:");
  display.print(leftEncoder.getCount()); // 左馬達計數
  display.print(" R:");
  display.println(rightEncoder.getCount()); // 右馬達計數

  // 第三行：顯示極限速度測試結果（如果有）
  if (maxSpeed_tested)
  {
    display.println("--- Max Speed ---");
    display.print("L:");
    display.print(maxSpeed_L);
    display.print(" R:");
    display.print(maxSpeed_R);
    display.println(" c/100ms");

    // 建議 c/20ms
    display.print("c/20ms: L");
    display.print(maxSpeed_L / 5);
    display.print(" R");
    display.print(maxSpeed_R / 5);
  }

  // 將緩衝區內容寫入 OLED 顯示器
  display.display();
}

// ============ 馬達控制函式 ============
// PWM 原理：LEDC 通道獨立控制，通過 ledcWrite() 設定 0~255 的輸出值
// 馬達控制：正轉通道和反轉通道中，只有一個可以有輸出（另一個必須為 0）

void motor(int L, int R)
{
  // 限制左輪速度在 -255~255 範圍內
  if (L > 255)
    L = 255;
  else if (L < -255)
    L = -255;

  // 限制右輪速度在 -255~255 範圍內
  if (R > 255)
    R = 255;
  else if (R < -255)
    R = -255;

  // ===== 左輪控制 =====
  if (L > 0) // 正轉（前進）
  {
    ledcWrite(CH_L_FWD, L);
    ledcWrite(CH_L_BWD, 0);
  }
  else // 反轉（後退）
  {
    ledcWrite(CH_L_FWD, 0);
    ledcWrite(CH_L_BWD, -L);
  }

  // ===== 右輪控制 =====
  if (R > 0) // 正轉（前進）
  {
    ledcWrite(CH_R_FWD, R);
    ledcWrite(CH_R_BWD, 0);
  }
  else // 反轉（後退）
  {
    ledcWrite(CH_R_FWD, 0);
    ledcWrite(CH_R_BWD, -R);
  }
}

// ============ 動作函式（高層馬達控制）============
// 這些函式呼叫 motor() 實現具體動作，數值已現場校準

void forward()
{
  // 直線前進：左輪 55，右輪 58（補償左偏）
  motor(55, 55);
}

void backward()
{
  // 直線後退：左右輪速度相同
  motor(-25, -25);
}

void s_Left()
{
  // 原地左轉：左輪-75，右輪55
  motor(-45, 25);
}

void s_Right()
{
  // 原地右轉：左輪55，右輪-75
  motor(25, -45);
}

void m_Left()
{
  // 左轉：左輪停止，右輪55
  motor(0, 35);
}

void m_Right()
{
  // 右轉：左輪55，右輪停止
  motor(35, -00);
}

void b_Left()
{
  // 急左轉：左輪反轉，右輪正轉
  motor(-35, 35);
}

void b_Right()
{
  // 急右轉：左輪正轉，右輪反轉
  motor(35, -35);
}

void stop()
{
  // 停止馬達
  motor(0, 0);
  // leftEncoder.clearCount();
  // rightEncoder.clearCount();
}

// ============ 距離控制函式 ============
// 利用編碼器計數實現精確距離控制
// 校準：1 編碼器計數 ≈ ? mm，需根據輪徑現場測量並調整

void p_fw(int distance)
{
  // 目標距離（編碼器計數值）
  long targetCount = distance;

  // 清除編碼器計數器
  leftEncoder.clearCount();
  rightEncoder.clearCount();

  // 階段 1：快速前進到接近目標
  const int DECEL_COUNT = 1000; // 開始減速的計數值，方便調整
  forward();
  while (true)
  {
    long leftCount = leftEncoder.getCount();
    long rightCount = rightEncoder.getCount();
    // 當計數接近目標時停止快速階段
    if ((leftCount >= targetCount - DECEL_COUNT) || (rightCount >= targetCount - DECEL_COUNT))
    {
      break;
    }
    delay(1);
  }

  // 階段 2：低速精調
  motor(20, 20); // 低速前進
  delay(50);
  stop();

  // 階段 3：反復調整至誤差範圍內
  const int TOLERANCE = 10;        // 容差範圍（±10 計數）
  const int L_MIN_SPEED = 30;      // 最小驅動速度
  const int R_MIN_SPEED = 30;      // 最小驅動速度
  unsigned long maxAttempts = 100; // 最多調整 100 次
  unsigned long attempts = 0;

  while (attempts < maxAttempts)
  {
    long leftCount = leftEncoder.getCount();
    long rightCount = rightEncoder.getCount();
    long L_error = leftCount - targetCount; // 計算誤差
    long R_error = rightCount - targetCount;

    // 誤差在容差範圍內，完成
    if ((abs(L_error) < TOLERANCE) && (abs(R_error) < TOLERANCE))
    {
      break;
    }

    // 判斷各輪是否超過或未達目標
    bool L_over = L_error > TOLERANCE;   // 左輪超過
    bool L_under = L_error < -TOLERANCE; // 左輪未達
    bool R_over = R_error > TOLERANCE;   // 右輪超過
    bool R_under = R_error < -TOLERANCE; // 右輪未達

    // 動態計算調整速度
    int L_adjustSpeed = map(abs(L_error), TOLERANCE, 100, L_MIN_SPEED, 50);
    L_adjustSpeed = constrain(L_adjustSpeed, L_MIN_SPEED, 50);
    int R_adjustSpeed = map(abs(R_error), TOLERANCE, 100, R_MIN_SPEED, 50);
    R_adjustSpeed = constrain(R_adjustSpeed, R_MIN_SPEED, 50);

    // 根據各輪狀態同時調整
    int L_speed = 0;
    int R_speed = 0;

    // 左輪修正
    if (L_over)
      L_speed = -L_adjustSpeed; // 超過 → 反轉
    else if (L_under)
      L_speed = L_adjustSpeed; // 未達 → 正轉

    // 右輪修正
    if (R_over)
      R_speed = -R_adjustSpeed; // 超過 → 反轉
    else if (R_under)
      R_speed = R_adjustSpeed; // 未達 → 正轉

    motor(L_speed, R_speed);
    delay(30);
    stop();
    delay(50);
    attempts++;
  }

  stop();
}

void p_bw(int distance)
{
  // 目標距離（編碼器計數值）
  long targetCount = distance;

  // 清除編碼器計數
  leftEncoder.clearCount();
  rightEncoder.clearCount();

  // 執行後退
  backward();

  // 監控編碼器計數，直到達到目標距離
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

void p_left(int degree)
{
  // 目標距離（編碼器計數值）
  int distance = degree * (860 / 180);
  long targetCount = distance;

  // 清除編碼器計數器
  leftEncoder.clearCount();
  rightEncoder.clearCount();

  // 階段 1：快速前進到接近目標
  const int DECEL_COUNT = 1000; // 開始減速的計數值，方便調整
  b_Left();

  while (true)
  {
    long leftCount = abs(leftEncoder.getCount());
    long rightCount = rightEncoder.getCount();
    // 當計數接近目標時停止快速階段
    if ((leftCount >= targetCount - DECEL_COUNT) || (rightCount >= targetCount - DECEL_COUNT))
    {
      break;
    }
    delay(1);
  }

  // 階段 2：低速精調
  motor(-20, 20); // 低速前進
  delay(50);
  stop();

  // 階段 3：反復調整至誤差範圍內
  const int TOLERANCE = 10;        // 容差範圍（±10 計數）
  const int L_MIN_SPEED = -50;     // 最小驅動速度
  const int R_MIN_SPEED = 30;      // 最小驅動速度
  unsigned long maxAttempts = 100; // 最多調整 100 次
  unsigned long attempts = 0;

  while (attempts < maxAttempts)
  {
    long leftCount = abs(leftEncoder.getCount());
    long rightCount = rightEncoder.getCount();
    long L_error = leftCount - targetCount; // 計算誤差
    long R_error = rightCount - targetCount;

    // 誤差在容差範圍內，完成
    if ((abs(L_error) < TOLERANCE) && (abs(R_error) < TOLERANCE))
    {
      break;
    }

    // 判斷各輪是否超過或未達目標
    bool L_over = L_error > TOLERANCE;   // 左輪超過
    bool L_under = L_error < -TOLERANCE; // 左輪未達
    bool R_over = R_error > TOLERANCE;   // 右輪超過
    bool R_under = R_error < -TOLERANCE; // 右輪未達

    // 動態計算調整速度
    int L_adjustSpeed = map(abs(L_error), TOLERANCE, 100, L_MIN_SPEED, -50);
    L_adjustSpeed = constrain(L_adjustSpeed, L_MIN_SPEED, -50);
    int R_adjustSpeed = map(abs(R_error), TOLERANCE, 100, R_MIN_SPEED, 50);
    R_adjustSpeed = constrain(R_adjustSpeed, R_MIN_SPEED, 50);

    // 根據各輪狀態同時調整
    int L_speed = 0;
    int R_speed = 0;

    // 左輪修正
    if (L_over)
      L_speed = -L_adjustSpeed; // 超過 → 反轉
    else if (L_under)
      L_speed = L_adjustSpeed; // 未達 → 正轉

    // 右輪修正
    if (R_over)
      R_speed = -R_adjustSpeed; // 超過 → 反轉
    else if (R_under)
      R_speed = R_adjustSpeed; // 未達 → 正轉

    motor(L_speed, R_speed);
    delay(30);
    stop();
    delay(20);
  }

  stop();
}

// ============ 伺服馬達控制函式 ============
// SG90 伺服馬達原理：PWM 脈寬 1ms~2ms，對應角度 0~180°
// write(角度) 直接設定目標角度，硬體自動尋位

void arm_up()
{
  // 寫入 ARM_UP 角度，使手臂升起
  arm.write(ARM_UP);
}

void arm_down()
{
  // 寫入 ARM_DOWN 角度，使手臂下降
  arm.write(ARM_DOWN);
}

void claw_open()
{
  // 寫入 CLAW_OPEN 角度，使爪子開啟
  claw.write(CLAW_OPEN);
}

void claw_close()
{
  // 寫入 CLAW_CLOSE 角度，使爪子關閉
  claw.write(CLAW_CLOSE);
}

void pickup_object()
{
  // 撿取物體的完整動作序列

  claw_close(); // 夾爪子
  delay(300);
  arm_up(); // 抬起手臂
  delay(300);
}

void release_object()
{
  // 釋放物體的動作序列
  arm_down(); // 放下手臂
  delay(300);
  claw_open(); // 張爪子
  delay(300);
}

void prepare_pickup()
{
  // 打開爪子並且降下手臂，準備撿取物體
  claw_open(); // 張爪子
  delay(300);
  arm_down(); // 放下手臂
  delay(300);
}

// ============ 測試函式 ============
// 用於驗證硬體是否正常工作

void test_motor()
{
  // 測試馬達動作序列
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
  // 輸出 5 路紅外線感測值
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

void test_max_speed()
{
  // ===== 極限速度測試 =====
  // 目的：測量 PWM=255 時左右輪的最大速度（c/100ms）
  // 用途：作為設定目標速度的參考上限
  // 注意：測量過程不輸出任何訊息，專心量測確保準確
  //       結果會顯示在 OLED 上（適合斷線測試）

  // 清除編碼器
  leftEncoder.clearCount();
  rightEncoder.clearCount();

  // 全速前進
  motor(255, 255);

  // 等待 500ms 讓速度穩定
  delay(500);

  // 測量 10 次，每次 100ms（專心量測，不輸出）
  long L_total = 0; // 左輪速度總和
  long R_total = 0; // 右輪速度總和
  const int SAMPLES = 10;

  for (int i = 0; i < SAMPLES; i++)
  {
    // 記錄起始計數
    long L_start = leftEncoder.getCount();
    long R_start = rightEncoder.getCount();

    // 等待 100ms
    delay(100);

    // 計算這 100ms 內的計數變化量（即速度 c/100ms）
    long L_speed = leftEncoder.getCount() - L_start;
    long R_speed = rightEncoder.getCount() - R_start;

    // 累加（不印出，專心量測）
    L_total += L_speed;
    R_total += R_speed;
  }

  // 停止馬達
  stop();

  // 計算平均值
  long L_avg = L_total / SAMPLES;
  long R_avg = R_total / SAMPLES;

  // === 儲存結果到全域變數 ===
  maxSpeed_L = L_avg;
  maxSpeed_R = R_avg;
  maxSpeed_tested = true;

  // === 顯示結果到 OLED ===
  if (oled_ready)
  {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // 標題
    display.setCursor(0, 0);
    display.println("=== Max Speed ===");

    // 左輪結果
    display.print("L: ");
    display.print(L_avg);
    display.println(" c/100ms");

    // 右輪結果
    display.print("R: ");
    display.print(R_avg);
    display.println(" c/100ms");

    // 分隔線
    display.println("--- Suggest ---");

    // 建議目標速度（80%）
    display.print("80%: L");
    display.print(L_avg * 80 / 100);
    display.print(" R");
    display.println(R_avg * 80 / 100);

    // 換算成 c/20ms
    display.print("c/20ms: L");
    display.print(L_avg / 5);
    display.print(" R");
    display.print(R_avg / 5);

    display.display();
  }

  // 同時也印到 Serial（如果有連線的話）
  Serial.println("=== 測量結果 ===");
  Serial.print("左輪: ");
  Serial.print(L_avg);
  Serial.print(" c/100ms | 右輪: ");
  Serial.print(R_avg);
  Serial.println(" c/100ms");
  Serial.print("建議 c/20ms: 左 ");
  Serial.print(L_avg / 5);
  Serial.print(" / 右 ");
  Serial.print(R_avg / 5);
  Serial.println("");
}

void test_speed()
{
  // ===== 即時速度測試 =====
  // 目的：每 100ms 印出左右輪的即時速度
  // 用途：學生手轉輪子或讓車跑，觀察速度變化
  // 注意：此函式會持續執行，需按 Reset 結束

  Serial.println("=== 即時速度測試 ===");
  Serial.println("每 100ms 印出速度，按 Reset 結束");
  Serial.println("");

  while (true)
  {
    // 記錄起始計數
    long L_start = leftEncoder.getCount();
    long R_start = rightEncoder.getCount();

    // 等待 100ms
    delay(100);

    // 計算速度（c/100ms）
    long L_speed = leftEncoder.getCount() - L_start;
    long R_speed = rightEncoder.getCount() - R_start;

    // 印出結果
    Serial.print("左: ");
    Serial.print(L_speed);
    Serial.print(" c/100ms | 右: ");
    Serial.print(R_speed);
    Serial.println(" c/100ms");
  }
}

// ============ 速度閉環控制函式 ============
// 原理：不再設定固定 PWM，而是設定「目標速度」
//       程式會根據實際速度動態調整 PWM
//       這樣不管電池電量如何，都能維持相同的實際速度

// 全域變數：儲存當前左右輪的 PWM 值（供閉環控制累加調整）
float L_pwm = 0; // 左輪當前 PWM（0~255）
float R_pwm = 0; // 右輪當前 PWM（0~255）

void speed_control(float L_target, float R_target)
{
  // ===== 速度閉環控制（單次呼叫）=====
  // 輸入：L_target = 左輪目標速度（c/週期）
  //       R_target = 右輪目標速度（c/週期）
  // 原理：比較「目標速度」與「實際速度」的差異（誤差）
  //       根據誤差調整 PWM：
  //       - 實際速度 < 目標 → 加大 PWM
  //       - 實際速度 > 目標 → 減小 PWM

  // --- 記錄起始計數 ---
  long L_start = leftEncoder.getCount();
  long R_start = rightEncoder.getCount();

  // --- 等待一個控制週期 ---
  delay(SPEED_CONTROL_PERIOD);

  // --- 計算實際速度（這段時間內的計數變化量）---
  long L_actual = leftEncoder.getCount() - L_start;  // 左輪實際速度
  long R_actual = rightEncoder.getCount() - R_start; // 右輪實際速度

  // --- 計算誤差（目標 - 實際）---
  // 正誤差 = 跑太慢，需要加速
  // 負誤差 = 跑太快，需要減速
  float L_error = L_target - L_actual;
  float R_error = R_target - R_actual;

  // --- 根據誤差調整 PWM（比例控制）---
  // PWM 調整量 = 誤差 × 比例係數（Kp）
  // Kp 越大，調整越激進；Kp 越小，調整越平緩
  L_pwm = L_pwm + L_error * SPEED_KP;
  R_pwm = R_pwm + R_error * SPEED_KP;

  // --- 限制 PWM 範圍（0~255）---
  L_pwm = constrain(L_pwm, -255, 255);
  R_pwm = constrain(R_pwm, -255, 255);

  // --- 輸出到馬達 ---
  motor((int)L_pwm, (int)R_pwm);
}

void p_fw_v2(int distance)
{
  // ===== 新版前進函式（速度閉環 + 同步修正 + 距離減速）=====
  // 輸入：distance = 目標距離（編碼器計數）
  // 特點：
  //   1. 速度閉環：根據目標速度動態調整 PWM，不受電量影響
  //   2. 左右同步：即時修正左右輪差異，保持直線
  //   3. 距離減速：接近目標時自動減速，避免超距
  //
  // ===== 校正流程（請依序進行）=====
  //
  // 【步驟 1】測極限速度 → 決定 BASE_SPEED
  //    - 呼叫 test_max_speed()，記錄左右輪 c/20ms
  //    - 以較慢的輪子為基準，乘 70~80% 作為 BASE_SPEED
  //    - 例：左輪 85、右輪 88 → BASE_SPEED = 85 * 0.7 ≈ 60
  //
  // 【步驟 2】關閉 SYNC_KP → 單獨調 SPEED_KP
  //    - 先把 SYNC_KP 設為 0（排除左右同步的干擾）
  //    - 觀察車子運動是否平順（不抖、不頓）
  //    - 若一頓一頓 → SPEED_KP 太大，往下調（例：5.0 → 0.5 → 0.1）
  //    - 若反應太慢 → SPEED_KP 太小，往上調
  //    - 目標：平順加速、穩定巡航
  //
  // 【步驟 3】調 MIN_SPEED（從低往高調）
  //    - 觀察減速階段是否「停了又動」（速度降太低，馬達停轉再啟動）
  //    - 若有此現象 → MIN_SPEED 太低，往上調（例：10 → 20 → 30）
  //    - 目標：減速過程平滑連續，不會中途停頓
  //
  // 【步驟 4】調 TOLERANCE（補償慣性超距）
  //    - 讓車跑完後，讀取編碼器計數，看超過目標多少
  //    - 若超距 50 → TOLERANCE 設 50（提早停止補償慣性）
  //    - 可同時調 DECEL_START：提早減速 = 減少超距
  //
  // 【步驟 5】開啟 SYNC_KP → 調到走直線不晃(尚未完成)
  //    - 確認步驟 2-4 完成後，將 SYNC_KP 設為小值（例：0.1）
  //    - 若走歪 → 加大 SYNC_KP
  //    - 若左右晃動 → SYNC_KP 太大，調小
  //    - 目標：直線行駛，不偏移也不晃
  //
  // ===== 參數說明 =====

  // --- 參數設定（根據上述流程校正後的值）---
  // 實測極限：左輪 85 c/20ms、右輪 88 c/20ms
  // 以較慢的左輪為基準，設定 70%（保守）
  const float BASE_SPEED = 60.0; // 基礎目標速度（c/週期），約 70% 極限
  const float MIN_SPEED = 20.0;  // 最低目標速度（c/週期），需高於馬達死區
  const float DECEL_START = 0.6; // 開始減速的進度（0.6 = 走 60% 後開始減速）
  const int TOLERANCE = 50;      // 到達容差（補償慣性超距）
  // const float SYNC_KP = 0.1;  // 【步驟 5】左右同步修正係數（目前關閉）

  // --- 清除編碼器 ---
  leftEncoder.clearCount();
  rightEncoder.clearCount();

  // --- 重置 PWM 累積值 ---
  L_pwm = 50; // 給一個初始 PWM，加速啟動
  R_pwm = 50;

  // --- 主控制迴圈 ---
  while (true)
  {
    // 讀取當前計數（用於計算進度）
    long L_count = leftEncoder.getCount();
    long R_count = rightEncoder.getCount();
    long avgCount = (L_count + R_count) / 2; // 平均計數（代表行進距離）

    // === 終止條件：到達目標 ===
    if (avgCount >= distance - TOLERANCE)
    {
      break;
    }

    // === 計算目標速度（距離減速）===
    float progress = (float)avgCount / distance; // 進度 0.0 ~ 1.0

    float targetSpeed;
    if (progress < DECEL_START)
    {
      targetSpeed = BASE_SPEED; // 尚未到減速點 → 全速
    }
    else
    {
      // 線性減速：用 map 將進度映射到速度
      // map(值, 原始最小, 原始最大, 目標最小, 目標最大)
      long decelStartCount = distance * DECEL_START;                                 // 開始減速的計數值
      targetSpeed = map(avgCount, decelStartCount, distance, BASE_SPEED, MIN_SPEED); // map將目前count映射到逐漸變慢的速度
    }

    // === 呼叫速度閉環控制（左右輪目標速度相同）===
    speed_control(targetSpeed, targetSpeed);

    // 【步驟 5】若要開啟左右同步修正，取消以下註解：
    // long diffError = L_count - R_count;  // 左輪 - 右輪
    // float correction = diffError * SYNC_KP;
    // speed_control(targetSpeed - correction, targetSpeed + correction);
  }

  // --- 停止 ---
  stop();
  L_pwm = 0;
  R_pwm = 0;
}
void p_bw_v2(int distance)
{
  // ===== 新版前進函式（速度閉環 + 同步修正 + 距離減速）=====
  // 輸入：distance = 目標距離（編碼器計數）
  // 特點：
  //   1. 速度閉環：根據目標速度動態調整 PWM，不受電量影響
  //   2. 左右同步：即時修正左右輪差異，保持直線
  //   3. 距離減速：接近目標時自動減速，避免超距
  //
  // ===== 校正流程（請依序進行）=====
  //
  // 【步驟 1】測極限速度 → 決定 BASE_SPEED
  //    - 呼叫 test_max_speed()，記錄左右輪 c/20ms
  //    - 以較慢的輪子為基準，乘 70~80% 作為 BASE_SPEED
  //    - 例：左輪 85、右輪 88 → BASE_SPEED = 85 * 0.7 ≈ 60
  //
  // 【步驟 2】關閉 SYNC_KP → 單獨調 SPEED_KP
  //    - 先把 SYNC_KP 設為 0（排除左右同步的干擾）
  //    - 觀察車子運動是否平順（不抖、不頓）
  //    - 若一頓一頓 → SPEED_KP 太大，往下調（例：5.0 → 0.5 → 0.1）
  //    - 若反應太慢 → SPEED_KP 太小，往上調
  //    - 目標：平順加速、穩定巡航
  //
  // 【步驟 3】調 MIN_SPEED（從低往高調）
  //    - 觀察減速階段是否「停了又動」（速度降太低，馬達停轉再啟動）
  //    - 若有此現象 → MIN_SPEED 太低，往上調（例：10 → 20 → 30）
  //    - 目標：減速過程平滑連續，不會中途停頓
  //
  // 【步驟 4】調 TOLERANCE（補償慣性超距）
  //    - 讓車跑完後，讀取編碼器計數，看超過目標多少
  //    - 若超距 50 → TOLERANCE 設 50（提早停止補償慣性）
  //    - 可同時調 DECEL_START：提早減速 = 減少超距
  //
  // 【步驟 5】開啟 SYNC_KP → 調到走直線不晃(尚未完成)
  //    - 確認步驟 2-4 完成後，將 SYNC_KP 設為小值（例：0.1）
  //    - 若走歪 → 加大 SYNC_KP
  //    - 若左右晃動 → SYNC_KP 太大，調小
  //    - 目標：直線行駛，不偏移也不晃
  //
  // ===== 參數說明 =====

  // --- 參數設定（根據上述流程校正後的值）---
  // 實測極限：左輪 85 c/20ms、右輪 88 c/20ms
  // 以較慢的左輪為基準，設定 70%（保守）
  const float BASE_SPEED = -60.0; // 基礎目標速度（c/週期），約 70% 極限
  const float MIN_SPEED = -20.0;  // 最低目標速度（c/週期），需高於馬達死區
  const float DECEL_START = 0.6;  // 開始減速的進度（0.6 = 走 60% 後開始減速）
  const int TOLERANCE = 100;      // 到達容差（補償慣性超距）
  // const float SYNC_KP = 0.1;  // 【步驟 5】左右同步修正係數（目前關閉）

  // --- 清除編碼器 ---
  leftEncoder.clearCount();
  rightEncoder.clearCount();

  // --- 重置 PWM 累積值 ---
  L_pwm = -50; // 給一個初始 PWM，加速啟動
  R_pwm = -50;

  // --- 主控制迴圈 ---
  while (true)
  {
    // 讀取當前計數（用於計算進度）
    long L_count = leftEncoder.getCount();
    long R_count = rightEncoder.getCount();
    long avgCount = abs((L_count + R_count)) / 2; // 平均計數（代表行進距離）

    // === 終止條件：到達目標 ===
    if (avgCount >= distance - TOLERANCE)
    {
      break;
    }

    // === 計算目標速度（距離減速）===
    float progress = (float)avgCount / distance; // 進度 0.0 ~ 1.0

    float targetSpeed;
    if (progress < DECEL_START)
    {
      targetSpeed = BASE_SPEED; // 尚未到減速點 → 全速
    }
    else
    {
      // 線性減速：用 map 將進度映射到速度
      // map(值, 原始最小, 原始最大, 目標最小, 目標最大)
      long decelStartCount = distance * DECEL_START;                                 // 開始減速的計數值
      targetSpeed = map(avgCount, decelStartCount, distance, BASE_SPEED, MIN_SPEED); // map將目前count映射到逐漸變慢的速度
    }

    // === 呼叫速度閉環控制（左右輪目標速度相同）===
    speed_control(targetSpeed, targetSpeed);

    // 【步驟 5】若要開啟左右同步修正，取消以下註解：
    // long diffError = L_count - R_count;  // 左輪 - 右輪
    // float correction = diffError * SYNC_KP;
    // speed_control(targetSpeed - correction, targetSpeed + correction);
  }

  // --- 停止 ---
  stop();
  L_pwm = 0;
  R_pwm = 0;
}

// ============ 循跡功能 ============
// 利用紅外線陣列自動跟隨黑線行進

void trail()
{
  // 判斷中線是否在黑線上
  if (IR_M_read() == 1) // 中線在黑線上，方向正確
  {
    // 檢查左右偏差，微調方向
    if (IR_L_read() == 1 && IR_R_read() == 0) // 向左偏
    {
      m_Left();
    }
    else if (IR_L_read() == 0 && IR_R_read() == 1) // 向右偏
    {
      m_Right();
    }
    else // 左右平衡，直線前進
    {
      forward();
    }
  }
  else // 中線不在黑線上，需要大幅轉向
  {
    // 激進轉向
    if (IR_L_read() == 1 && IR_R_read() == 0) // 黑線在左邊
    {
      b_Left();
    }
    else if (IR_L_read() == 0 && IR_R_read() == 1) // 黑線在右邊
    {
      b_Right();
    }
  }
}

// ===== 主程式 =====
// setup()：初始化所有硬體，在上傳後執行一次
// loop()：主控制迴圈，在 setup() 完成後反覆執行

void setup()
{
  // 初始化序列埠通訊，9600 baud
  Serial.begin(9600);

  // --- OLED 初始化 ---
  oled_init();

  // --- 伺服馬達定時器分配 (Timer 0 給伺服馬達) ---
  // 重要！必須在伺服初始化前執行
  ESP32PWM::allocateTimer(0);

  // --- 伺服馬達初始化 ---
  // 標準 50Hz 伺服馬達，脈寬範圍 500~2400us
  arm.setPeriodHertz(50);           // 設定頻率 50Hz
  arm.attach(ARM_PIN, 500, 2400);   // 連結至腳位 14
  claw.setPeriodHertz(50);          // 設定頻率 50Hz
  claw.attach(CLAW_PIN, 500, 2400); // 連結至腳位 15

  // --- 編碼器初始化 ---
  // 使用 attachHalfQuad()，4X 計數模式，提升精度
  ESP32Encoder::useInternalWeakPullResistors = puType::up;
  leftEncoder.attachHalfQuad(LEFT_ENCODER_A, LEFT_ENCODER_B);
  leftEncoder.clearCount();
  rightEncoder.attachHalfQuad(RIGHT_ENCODER_A, RIGHT_ENCODER_B);
  rightEncoder.clearCount();

  // --- 紅外線感測器初始化 ---
  // 設定 5 個感測器腳位為輸入模式
  pinMode(IR_LL_PIN, INPUT);
  pinMode(IR_L_PIN, INPUT);
  pinMode(IR_M_PIN, INPUT);
  pinMode(IR_R_PIN, INPUT);
  pinMode(IR_RR_PIN, INPUT);

  // --- 馬達 PWM 初始化 (Timer 2，Timer 0 預留給伺服馬達) ---
  // PWM 設定步驟：1.設定腳位為輸出  2.建立 PWM 通道  3.綁定腳位到通道

  // 左馬達反轉通道
  pinMode(MOTOR_L_BWD, OUTPUT);           // 腳位 13 為輸出
  ledcSetup(CH_L_BWD, PWM_FREQ, PWM_RES); // 通道 9，75kHz，8-bit
  ledcAttachPin(MOTOR_L_BWD, CH_L_BWD);   // 綁定

  // 左馬達正轉通道
  pinMode(MOTOR_L_FWD, OUTPUT);           // 腳位 27 為輸出
  ledcSetup(CH_L_FWD, PWM_FREQ, PWM_RES); // 通道 8，75kHz，8-bit
  ledcAttachPin(MOTOR_L_FWD, CH_L_FWD);   // 綁定

  // 右馬達反轉通道
  pinMode(MOTOR_R_BWD, OUTPUT);           // 腳位 4 為輸出
  ledcSetup(CH_R_BWD, PWM_FREQ, PWM_RES); // 通道 11，75kHz，8-bit
  ledcAttachPin(MOTOR_R_BWD, CH_R_BWD);   // 綁定

  // 右馬達正轉通道
  pinMode(MOTOR_R_FWD, OUTPUT);           // 腳位 2 為輸出
  ledcSetup(CH_R_FWD, PWM_FREQ, PWM_RES); // 通道 10，75kHz，8-bit
  ledcAttachPin(MOTOR_R_FWD, CH_R_FWD);   // 綁定

  stop(); // 初始化時停止馬達
          // TODO: 初始化完成後，可呼叫停止函式確保馬達不會亂轉

  //?==================寫主程式的地方==================
  // *馬達測試*
  // p_fw_v2(4500); // 前進 4200 計數
  // prepare_pickup();
  // pickup_object();
  // p_bw_v2(4500);
  // p_left(45); // 左轉 2200 計數
  int look = 0;
  while (true)
  {
    if ((IR_LL_read() == 1) || (IR_RR_read() == 1))
    {
      look++;
      if (look == 3)
      {
        stop();
        break;
      }
      p_fw_v2(150);
      stop();
      delay(500);
    }
    else
    {
      trail();
    }
  }

  prepare_pickup();
  delay(500);
  pickup_object();
  delay(500);
  release_object();
  delay(500);

  //?==================寫主程式的地方==================

  //! 以下不需要更動
  //* OLED：持續顯示紅外線狀態和編碼器值
  while (true)
  {
    oled_show_ir_status(); // 在 OLED 顯示紅外線狀態和編碼器值
    delay(500);            // 每 500ms 更新一次
  };
}

void loop()
{
  // 主迴圈留空，所有功能在 setup() 中完成
}