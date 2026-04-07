#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_PWMServoDriver.h>
#include <DHT.h>
#include <math.h>
#include <string.h>

namespace hw
{
constexpr uint8_t tftSck = 18;
constexpr uint8_t tftMosi = 23;
constexpr uint8_t tftCs = 25;
constexpr uint8_t tftDc = 17;
constexpr uint8_t tftRst = 16;
constexpr uint8_t tftBl = 26;

constexpr uint8_t i2cSda = 21;
constexpr uint8_t i2cScl = 22;
constexpr uint8_t oledAddr = 0x3C;
constexpr uint8_t pcaAddr = 0x40;

constexpr uint8_t dhtPin = 13;
constexpr uint8_t ldrPin = 12;
constexpr uint8_t btn1Pin = 2;
constexpr uint8_t btn2Pin = 4;

constexpr uint8_t rgbChR = 0;
constexpr uint8_t rgbChG = 1;
constexpr uint8_t rgbChB = 2;

constexpr uint16_t tftWidth = 160;
constexpr uint16_t tftHeight = 128;
constexpr uint16_t oledWidth = 128;
constexpr uint16_t oledHeight = 64;
} // namespace hw

enum class ButtonWiringType : uint8_t
{
  AutoByIdle,
  PullUpActiveLow,
  PullDownActiveHigh
};

constexpr uint32_t kRenderIntervalMs = 33;
constexpr uint32_t kBootDurationMs = 2200;
constexpr uint32_t kLongPressMs = 1000;
constexpr uint32_t kPanicHoldMs = 5000;
constexpr uint32_t kInputGuardMs = 120;
constexpr uint32_t kErrorScreenMs = 1500;
constexpr uint32_t kToastMs = 1400;
constexpr uint32_t kGreenPulseMs = 1200;
constexpr uint8_t kTopScores = 3;
constexpr uint8_t kPasswordLength = 4;
constexpr uint32_t kButtonDebounceMs = 40;
constexpr char kDefaultPassword[] = "1120";
constexpr bool kBypassPasswordLock = true;
constexpr bool kBypassPasswordForButtonTest = false;
constexpr ButtonWiringType kBtn1Wiring = ButtonWiringType::AutoByIdle;
constexpr ButtonWiringType kBtn2Wiring = ButtonWiringType::AutoByIdle;
constexpr uint8_t kTftInitMode = INITR_GREENTAB;
constexpr uint8_t kTftRotation = 1;
constexpr int16_t kFooterY = 116;

constexpr uint16_t color565(uint8_t r, uint8_t g, uint8_t b)
{
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

constexpr uint16_t COLOR_BG = color565(5, 12, 22);
constexpr uint16_t COLOR_PANEL = color565(18, 31, 49);
constexpr uint16_t COLOR_ACCENT = color565(27, 157, 255);
constexpr uint16_t COLOR_WARN = color565(255, 180, 0);
constexpr uint16_t COLOR_DANGER = color565(240, 70, 60);
constexpr uint16_t COLOR_GOOD = color565(50, 210, 110);
constexpr uint16_t COLOR_TEXT = ST77XX_WHITE;
constexpr uint16_t COLOR_DIM = color565(140, 165, 190);

template <typename T> T clampValue(T value, T lower, T upper)
{
  if (value < lower)
  {
    return lower;
  }
  if (value > upper)
  {
    return upper;
  }
  return value;
}

enum class AppState : uint8_t
{
  Boot,
  LockScreen,
  MainMenu,
  SensorMonitor,
  SelfTest,
  GamesMenu,
  Leaderboard,
  GameRunning,
  GameResult
};

enum class GameId : uint8_t
{
  Breakout,
  Pong,
  FlappyBird,
  ShieldSword,
  BalloonBattle,
  GridBattle,
  PoleClimb,
  Racing,
  DuckHunt,
  QuickDraw,
  Count
};

enum class LdrState : uint8_t
{
  Unknown,
  Ok,
  Warning
};

struct HardwareProfile
{
  uint8_t tftSck;
  uint8_t tftMosi;
  uint8_t tftCs;
  uint8_t tftDc;
  uint8_t tftRst;
  uint8_t tftBl;
  uint8_t i2cSda;
  uint8_t i2cScl;
  uint8_t dhtPin;
  uint8_t ldrPin;
  uint8_t btn1Pin;
  uint8_t btn2Pin;
  uint8_t oledAddr;
  uint8_t pcaAddr;
  uint8_t rgbChR;
  uint8_t rgbChG;
  uint8_t rgbChB;
  bool backlightActiveHigh;
};

struct InputSnapshot
{
  bool btn1Down = false;
  bool btn2Down = false;
  bool btn1RawHigh = false;
  bool btn2RawHigh = false;
  bool btn1DownEdge = false;
  bool btn2DownEdge = false;
  bool btn1Pressed = false;
  bool btn2Pressed = false;
  bool btn1Long = false;
  bool btn2Long = false;
  bool panicCombo = false;
};

struct PasswordInputState
{
  int8_t digits[kPasswordLength] = {-1, -1, -1, -1};
  uint8_t cursor = 0;
  uint8_t filledCount = 0;
  uint8_t currentValue = 0;
  bool isComplete = false;
};

struct SensorSnapshot
{
  float temperatureC = NAN;
  float humidityPct = NAN;
  int lightRaw = 0;
  int lightPct = 0;
  bool dhtOk = false;
};

struct SelfTestReport
{
  bool tftOk = false;
  bool oledOk = false;
  bool pcaOk = false;
  bool dhtOk = false;
  LdrState ldrState = LdrState::Unknown;
  bool buttonsOk = false;
};

struct LeaderboardEntry
{
  uint32_t score = 0;
  uint32_t durationMs = 0;
  uint8_t difficulty = 0;
};

struct ToastState
{
  bool active = false;
  char message[28] = {};
  uint16_t color = COLOR_ACCENT;
  uint32_t untilMs = 0;
};

struct GameResult
{
  GameId gameId = GameId::Breakout;
  uint8_t difficulty = 0;
  uint32_t score = 0;
  uint32_t durationMs = 0;
  bool completed = false;
  uint8_t winner = 0;
};

struct GameDescriptor
{
  GameId id;
  const char *title;
  bool implemented;
  uint8_t difficultyCount;
  const char *const *difficultyNames;
};

const char *const kBaseDifficultyNames[] = {"Base"};
const char *const kPongDifficultyNames[] = {"Easy", "Normal", "Hard"};

const GameDescriptor kGameDescriptors[] = {
    {GameId::Breakout, "Breakout", true, 3, kPongDifficultyNames},
    {GameId::Pong, "Pong", true, 3, kPongDifficultyNames},
    {GameId::FlappyBird, "Flappy Bird", true, 3, kPongDifficultyNames},
    {GameId::ShieldSword, "Shield & Sword", false, 1, kBaseDifficultyNames},
    {GameId::BalloonBattle, "Balloon Battle", true, 3, kPongDifficultyNames},
    {GameId::GridBattle, "Grid Battle", false, 1, kBaseDifficultyNames},
    {GameId::PoleClimb, "Pole Climb", true, 3, kPongDifficultyNames},
    {GameId::Racing, "Racing", true, 3, kPongDifficultyNames},
    {GameId::DuckHunt, "Duck Hunt", true, 3, kPongDifficultyNames},
    {GameId::QuickDraw, "Quick Draw", true, 3, kPongDifficultyNames},
};

const char *const kMainMenuItems[] = {
    "Sensors",
    "Games",
    "Scores",
    "Self Test",
};

const HardwareProfile Hardware = {
    hw::tftSck, hw::tftMosi, hw::tftCs, hw::tftDc, hw::tftRst, hw::tftBl,
    hw::i2cSda, hw::i2cScl, hw::dhtPin, hw::ldrPin, hw::btn1Pin, hw::btn2Pin,
    hw::oledAddr, hw::pcaAddr, hw::rgbChR, hw::rgbChG, hw::rgbChB, true,
};

Adafruit_ST7735 tft = Adafruit_ST7735(Hardware.tftCs, Hardware.tftDc, Hardware.tftRst);
Adafruit_SSD1306 oled(hw::oledWidth, hw::oledHeight, &Wire, -1);
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(Hardware.pcaAddr, Wire);
DHT dht(Hardware.dhtPin, DHT11);
Preferences prefs;

class ButtonTracker
{
public:
  void begin(uint8_t pin, ButtonWiringType wiring)
  {
    _pin = pin;
    _wiring = wiring;
    pinMode(_pin, (_wiring == ButtonWiringType::PullDownActiveHigh) ? INPUT_PULLDOWN : INPUT_PULLUP);
    delay(2);
    _idleHigh = rawLevelHigh();
    _lastRaw = pressedFromLevel(_idleHigh);
    _stableDown = _lastRaw;
    _lastChangeMs = millis();
    _downSinceMs = _lastChangeMs;
  }

  void update(uint32_t now, bool &shortPress, bool &longPress, bool &down, bool &rawHigh, bool &downEdge)
  {
    shortPress = false;
    longPress = false;
    downEdge = false;

    rawHigh = rawLevelHigh();
    const bool raw = pressedFromLevel(rawHigh);
    if (raw != _lastRaw)
    {
      _lastRaw = raw;
      _lastChangeMs = now;
    }

    if ((now - _lastChangeMs) >= _debounceMs && raw != _stableDown)
    {
      _stableDown = raw;
      if (_stableDown)
      {
        _downSinceMs = now;
        _longTriggered = false;
        downEdge = true;
      }
      else if (!_longTriggered)
      {
        shortPress = true;
      }
    }

    if (_stableDown && !_longTriggered && (now - _downSinceMs) >= kLongPressMs)
    {
      _longTriggered = true;
      longPress = true;
    }

    down = _stableDown;
  }

private:
  bool rawLevelHigh() const
  {
    return digitalRead(_pin) == HIGH;
  }

  bool pressedFromLevel(bool rawHigh) const
  {
    switch (_wiring)
    {
    case ButtonWiringType::AutoByIdle:
      return rawHigh != _idleHigh;
    case ButtonWiringType::PullDownActiveHigh:
      return rawHigh;
    case ButtonWiringType::PullUpActiveLow:
    default:
      return !rawHigh;
    }
  }

  uint8_t _pin = 0;
  ButtonWiringType _wiring = ButtonWiringType::AutoByIdle;
  bool _idleHigh = true;
  bool _lastRaw = false;
  bool _stableDown = false;
  bool _longTriggered = false;
  uint32_t _lastChangeMs = 0;
  uint32_t _downSinceMs = 0;
  static constexpr uint32_t _debounceMs = kButtonDebounceMs;
};

class InputService
{
public:
  void begin()
  {
    _btn1.begin(Hardware.btn1Pin, kBtn1Wiring);
    _btn2.begin(Hardware.btn2Pin, kBtn2Wiring);
  }

  InputSnapshot update(uint32_t now)
  {
    InputSnapshot input;
    _btn1.update(now, input.btn1Pressed, input.btn1Long, input.btn1Down, input.btn1RawHigh, input.btn1DownEdge);
    _btn2.update(now, input.btn2Pressed, input.btn2Long, input.btn2Down, input.btn2RawHigh, input.btn2DownEdge);

    if (_requireRelease)
    {
      if (!input.btn1Down && !input.btn2Down && now >= _suppressUntilMs)
      {
        _requireRelease = false;
      }
      else
      {
        return {};
      }
    }

    if (now < _suppressUntilMs)
    {
      return {};
    }

    if (input.btn1Down && input.btn2Down)
    {
      if (_comboStartMs == 0)
      {
        _comboStartMs = now;
      }
      if (!_comboLatched && (now - _comboStartMs) >= kPanicHoldMs)
      {
        input.panicCombo = true;
        _comboLatched = true;
      }
    }
    else
    {
      _comboStartMs = 0;
      _comboLatched = false;
    }

    return input;
  }

  void suppressUntilRelease(uint32_t now, uint32_t durationMs = kInputGuardMs)
  {
    _suppressUntilMs = now + durationMs;
    _requireRelease = true;
    _comboStartMs = 0;
    _comboLatched = false;
  }

private:
  ButtonTracker _btn1;
  ButtonTracker _btn2;
  uint32_t _comboStartMs = 0;
  bool _comboLatched = false;
  uint32_t _suppressUntilMs = 0;
  bool _requireRelease = false;
};

class StorageService
{
public:
  void begin()
  {
    prefs.begin("rec_console", false);
  }

  void load()
  {
    _password = prefs.getString("password", kDefaultPassword);
    _darkRaw = prefs.getInt("darkRaw", 4000);
    _brightRaw = prefs.getInt("brightRaw", 200);
    if (!prefs.isKey("password"))
    {
      prefs.putString("password", _password);
    }
    else if (_password == "1234")
    {
      _password = kDefaultPassword;
      prefs.putString("password", _password);
    }
    if (!prefs.isKey("darkRaw"))
    {
      prefs.putInt("darkRaw", _darkRaw);
    }
    if (!prefs.isKey("brightRaw"))
    {
      prefs.putInt("brightRaw", _brightRaw);
    }
  }

  const String &password() const
  {
    return _password;
  }

  int darkRaw() const
  {
    return _darkRaw;
  }

  int brightRaw() const
  {
    return _brightRaw;
  }

  void saveCalibration(int darkRaw, int brightRaw)
  {
    _darkRaw = darkRaw;
    _brightRaw = brightRaw;
    prefs.putInt("darkRaw", _darkRaw);
    prefs.putInt("brightRaw", _brightRaw);
  }

  LeaderboardEntry loadEntry(GameId gameId, uint8_t difficulty, uint8_t rank) const
  {
    LeaderboardEntry entry;
    LeaderboardEntry table[kTopScores];
    loadTable(gameId, difficulty, table);
    if (rank < kTopScores)
    {
      entry = table[rank];
    }
    return entry;
  }

  int8_t submitResult(const GameResult &result)
  {
    LeaderboardEntry table[kTopScores];
    loadTable(result.gameId, result.difficulty, table);

    LeaderboardEntry candidate;
    candidate.score = result.score;
    candidate.durationMs = result.durationMs;
    candidate.difficulty = result.difficulty;

    for (uint8_t i = 0; i < kTopScores; ++i)
    {
      if (better(candidate, table[i]) || table[i].score == 0)
      {
        for (int j = kTopScores - 1; j > i; --j)
        {
          table[j] = table[j - 1];
        }
        table[i] = candidate;
        saveTable(result.gameId, result.difficulty, table);
        return static_cast<int8_t>(i);
      }
    }
    return -1;
  }

private:
  bool better(const LeaderboardEntry &lhs, const LeaderboardEntry &rhs) const
  {
    if (lhs.score != rhs.score)
    {
      return lhs.score > rhs.score;
    }
    return lhs.durationMs > rhs.durationMs;
  }

  void makeKey(GameId gameId, uint8_t difficulty, char *buffer, size_t size) const
  {
    snprintf(buffer, size, "lb_%u_%u", static_cast<unsigned>(gameId), difficulty);
  }

  void loadTable(GameId gameId, uint8_t difficulty, LeaderboardEntry table[kTopScores]) const
  {
    for (uint8_t i = 0; i < kTopScores; ++i)
    {
      table[i] = {};
    }

    char key[16];
    makeKey(gameId, difficulty, key, sizeof(key));
    const size_t expectedSize = sizeof(LeaderboardEntry) * kTopScores;
    if (prefs.getBytesLength(key) == expectedSize)
    {
      prefs.getBytes(key, table, expectedSize);
    }
  }

  void saveTable(GameId gameId, uint8_t difficulty, const LeaderboardEntry table[kTopScores])
  {
    char key[16];
    makeKey(gameId, difficulty, key, sizeof(key));
    prefs.putBytes(key, table, sizeof(LeaderboardEntry) * kTopScores);
  }

  String _password = kDefaultPassword;
  int _darkRaw = 4000;
  int _brightRaw = 200;
};

class SensorService
{
public:
  void begin(int darkRaw, int brightRaw)
  {
    _darkRaw = darkRaw;
    _brightRaw = brightRaw;
    dht.begin();
    pinMode(Hardware.ldrPin, INPUT);
    analogReadResolution(12);
    analogSetPinAttenuation(Hardware.ldrPin, ADC_11db);

    const int initial = analogRead(Hardware.ldrPin);
    for (uint8_t i = 0; i < 16; ++i)
    {
      _samples[i] = initial;
    }
    _snapshot.lightRaw = initial;
    _snapshot.lightPct = computePct(initial);
  }

  void update(uint32_t now)
  {
    if (_lastLightSampleMs == 0 || (now - _lastLightSampleMs) >= 100)
    {
      _lastLightSampleMs = now;
      sampleLight();
    }

    if (_lastDhtSampleMs == 0 || (now - _lastDhtSampleMs) >= 1000)
    {
      _lastDhtSampleMs = now;
      const float temp = dht.readTemperature();
      const float humidity = dht.readHumidity();
      if (!isnan(temp) && !isnan(humidity))
      {
        _snapshot.temperatureC = temp;
        _snapshot.humidityPct = humidity;
        _lastDhtValidMs = now;
      }
    }

    _snapshot.dhtOk = _lastDhtValidMs != 0 && (now - _lastDhtValidMs) <= 3000;
  }

  const SensorSnapshot &snapshot() const
  {
    return _snapshot;
  }

  void setCalibration(int darkRaw, int brightRaw)
  {
    _darkRaw = darkRaw;
    _brightRaw = brightRaw;
    _snapshot.lightPct = computePct(_snapshot.lightRaw);
  }

private:
  void sampleLight()
  {
    _samples[_sampleIndex] = analogRead(Hardware.ldrPin);
    _sampleIndex = (_sampleIndex + 1) % 16;

    long sum = 0;
    for (uint8_t i = 0; i < 16; ++i)
    {
      sum += _samples[i];
    }

    _snapshot.lightRaw = static_cast<int>(sum / 16);
    _snapshot.lightPct = computePct(_snapshot.lightRaw);
  }

  int computePct(int raw) const
  {
    if (_darkRaw == _brightRaw)
    {
      return 0;
    }

    const float ratio = static_cast<float>(_darkRaw - raw) / static_cast<float>(_darkRaw - _brightRaw);
    return clampValue(static_cast<int>(ratio * 100.0f + 0.5f), 0, 100);
  }

  SensorSnapshot _snapshot;
  int _samples[16] = {};
  uint8_t _sampleIndex = 0;
  uint32_t _lastLightSampleMs = 0;
  uint32_t _lastDhtSampleMs = 0;
  uint32_t _lastDhtValidMs = 0;
  int _darkRaw = 4000;
  int _brightRaw = 200;
};

class RgbService
{
public:
  void begin(bool available)
  {
    _available = available;
    if (_available)
    {
      setColor(0, 0, 0);
    }
  }

  void setColor(uint16_t r, uint16_t g, uint16_t b)
  {
    if (!_available)
    {
      return;
    }
    pwm.setPWM(Hardware.rgbChR, 0, clampValue<uint16_t>(r, 0, 4095));
    pwm.setPWM(Hardware.rgbChG, 0, clampValue<uint16_t>(g, 0, 4095));
    pwm.setPWM(Hardware.rgbChB, 0, clampValue<uint16_t>(b, 0, 4095));
  }

private:
  bool _available = false;
};

class IGame
{
public:
  virtual ~IGame() {}
  virtual GameId id() const = 0;
  virtual void enter(uint8_t difficulty, uint32_t nowMs) = 0;
  virtual void update(uint32_t nowMs, const InputSnapshot &input, const SensorSnapshot &sensors) = 0;
  virtual void renderTft(Adafruit_ST7735 &display) = 0;
  virtual void renderOledOverlay(Adafruit_SSD1306 &display, const SensorSnapshot &sensors) = 0;
  virtual bool isFinished() const = 0;
  virtual GameResult result() const = 0;
};

bool circleRectCollision(float cx, float cy, float radius, float rx, float ry, float rw, float rh, float &normalX, float &normalY)
{
  const float closestX = clampValue(cx, rx, rx + rw);
  const float closestY = clampValue(cy, ry, ry + rh);
  const float dx = cx - closestX;
  const float dy = cy - closestY;
  if ((dx * dx + dy * dy) > (radius * radius))
  {
    return false;
  }

  if (fabsf(dx) > fabsf(dy))
  {
    normalX = (dx >= 0.0f) ? 1.0f : -1.0f;
    normalY = 0.0f;
  }
  else if (fabsf(dy) > 0.0f)
  {
    normalX = 0.0f;
    normalY = (dy >= 0.0f) ? 1.0f : -1.0f;
  }
  else
  {
    normalX = 0.0f;
    normalY = -1.0f;
  }

  return true;
}

void drawGameSensorOverlay(Adafruit_SSD1306 &display, const char *headline, uint32_t primaryValue, const char *primaryLabel, const SensorSnapshot &sensors);
void drawCenteredText(Adafruit_GFX &display, int16_t centerY, const char *text, uint16_t color, uint8_t textSize);

class BreakoutGame : public IGame
{
public:
  GameId id() const override
  {
    return GameId::Breakout;
  }

  void enter(uint8_t difficulty, uint32_t nowMs) override
  {
    _difficulty = clampValue<uint8_t>(difficulty, 0, 2);
    _startMs = nowMs;
    _lastUpdateMs = nowMs;
    _finished = false;
    _completed = false;
    _score = 0;
    _powerUntilMs = 0;
    _ballRadius = kBaseRadius;
    _ballSpeed = (_difficulty == 0) ? 86.0f : (_difficulty == 1) ? 118.0f
                                                                  : 138.0f;
    _basePaddleWidth = (_difficulty == 0) ? 42.0f : (_difficulty == 1) ? 30.0f
                                                                        : 24.0f;
    _bowlStrength = (_difficulty == 0) ? 0.95f : (_difficulty == 1) ? 0.72f
                                                                     : 0.48f;
    _activeRows = (_difficulty == 0) ? 3 : (_difficulty == 1) ? 4
                                                               : 5;
    _specialTarget = (_difficulty == 0) ? 2 : (_difficulty == 1) ? 3
                                                                  : 4;
    _paddleWidth = _basePaddleWidth;
    _paddleX = (hw::tftWidth - _paddleWidth) * 0.5f;
    _ballX = hw::tftWidth * 0.5f;
    _ballY = hw::tftHeight - 24.0f;
    _ballVx = _ballSpeed * 0.62f;
    _ballVy = -_ballSpeed * 0.78f;

    for (uint8_t r = 0; r < kMaxRows; ++r)
    {
      for (uint8_t c = 0; c < kCols; ++c)
      {
        _bricks[r][c] = r < _activeRows;
        _special[r][c] = false;
      }
    }

    randomSeed(micros());
    uint8_t placed = 0;
    uint8_t attempts = 0;
    while (placed < _specialTarget && attempts < 32)
    {
      ++attempts;
      const uint8_t pick = random(0, _activeRows * kCols);
      const uint8_t row = pick / kCols;
      const uint8_t col = pick % kCols;
      if (!_special[row][col])
      {
        _special[row][col] = true;
        ++placed;
      }
    }
  }

  void update(uint32_t nowMs, const InputSnapshot &input, const SensorSnapshot &) override
  {
    if (_finished)
    {
      return;
    }

    float dt = (nowMs - _lastUpdateMs) / 1000.0f;
    dt = clampValue(dt, 0.0f, 0.05f);
    _lastUpdateMs = nowMs;

    const float paddleSpeed = (_difficulty == 0) ? 188.0f : (_difficulty == 1) ? 164.0f
                                                                                : 148.0f;
    if (input.btn1Down && !input.btn2Down)
    {
      _paddleX -= paddleSpeed * dt;
    }
    if (input.btn2Down && !input.btn1Down)
    {
      _paddleX += paddleSpeed * dt;
    }
    _paddleX = clampValue(_paddleX, 2.0f, hw::tftWidth - _paddleWidth - 2.0f);

    if (_powerUntilMs != 0 && nowMs > _powerUntilMs)
    {
      _ballRadius = kBaseRadius;
      _powerUntilMs = 0;
    }

    _ballX += _ballVx * dt;
    _ballY += _ballVy * dt;

    if (_ballX <= _ballRadius || _ballX >= (hw::tftWidth - _ballRadius))
    {
      _ballX = clampValue(_ballX, _ballRadius, static_cast<float>(hw::tftWidth - _ballRadius));
      _ballVx = -_ballVx;
    }
    if (_ballY <= _ballRadius + 14)
    {
      _ballY = _ballRadius + 14;
      _ballVy = fabsf(_ballVy);
    }

    float nx = 0.0f;
    float ny = 0.0f;
    if (circleRectCollision(_ballX, _ballY, _ballRadius, _paddleX, kPaddleY, _paddleWidth, kPaddleH, nx, ny) && _ballVy > 0.0f)
    {
      applyPaddleBounce();
    }

    bool brickHit = false;
    for (uint8_t r = 0; r < _activeRows && !brickHit; ++r)
    {
      for (uint8_t c = 0; c < kCols && !brickHit; ++c)
      {
        if (!_bricks[r][c])
        {
          continue;
        }

        const float brickX = 8.0f + c * (kBrickW + 2.0f);
        const float brickY = 20.0f + r * (kBrickH + 3.0f);
        if (circleRectCollision(_ballX, _ballY, _ballRadius, brickX, brickY, kBrickW, kBrickH, nx, ny))
        {
          _bricks[r][c] = false;
          brickHit = true;
          reflect(nx, ny);
          _score += _special[r][c] ? 25 : 10;
          if (_special[r][c])
          {
            _ballRadius = (_difficulty == 0) ? 6.0f : 5.0f;
            _powerUntilMs = nowMs + 7000;
          }
        }
      }
    }

    if (_ballY > hw::tftHeight + 8)
    {
      finish(nowMs, false);
      return;
    }

    bool anyBrickLeft = false;
    for (uint8_t r = 0; r < _activeRows && !anyBrickLeft; ++r)
    {
      for (uint8_t c = 0; c < kCols && !anyBrickLeft; ++c)
      {
        anyBrickLeft = _bricks[r][c];
      }
    }
    if (!anyBrickLeft)
    {
      finish(nowMs, true);
    }
  }

  void renderTft(Adafruit_ST7735 &display) override
  {
    display.fillScreen(COLOR_BG);
    display.fillRect(0, 0, hw::tftWidth, 14, COLOR_PANEL);
    display.setCursor(4, 3);
    display.setTextColor(COLOR_TEXT);
    display.setTextSize(1);
    display.print("Breakout");
    display.setCursor(hw::tftWidth - 44, 3);
    display.print(_score);
    display.setCursor(4, kFooterY);
    display.setTextColor(COLOR_DIM);
    display.print(kPongDifficultyNames[_difficulty]);

    for (uint8_t r = 0; r < _activeRows; ++r)
    {
      for (uint8_t c = 0; c < kCols; ++c)
      {
        if (!_bricks[r][c])
        {
          continue;
        }
        const int x = 8 + c * (kBrickW + 2);
        const int y = 20 + r * (kBrickH + 3);
        const uint16_t fill = _special[r][c] ? COLOR_WARN : COLOR_ACCENT;
        display.fillRoundRect(x, y, kBrickW, kBrickH, 2, fill);
      }
    }

    drawPrediction(display);
    drawBowlPaddle(display);
    display.fillCircle(static_cast<int>(_ballX), static_cast<int>(_ballY), static_cast<int>(_ballRadius), COLOR_GOOD);
    if (_powerUntilMs != 0)
    {
      display.setTextColor(COLOR_WARN);
      display.setCursor(hw::tftWidth - 28, kFooterY);
      display.print("BIG");
    }
  }

  void renderOledOverlay(Adafruit_SSD1306 &display, const SensorSnapshot &sensors) override
  {
    drawGameSensorOverlay(display, "BREAKOUT", _score, "Score:", sensors);
  }

  bool isFinished() const override
  {
    return _finished;
  }

  GameResult result() const override
  {
    GameResult result;
    result.gameId = id();
    result.difficulty = _difficulty;
    result.score = _score;
    result.durationMs = _durationMs;
    result.completed = _completed;
    return result;
  }

private:
  void applyPaddleBounce()
  {
    const float paddleCenter = _paddleX + (_paddleWidth * 0.5f);
    float normalized = (_ballX - paddleCenter) / (_paddleWidth * 0.5f);
    normalized = clampValue(normalized, -1.0f, 1.0f);

    // Bowl paddle: edge hits bend back toward the center instead of flinging outward.
    float bowlCurve = -normalized * (0.25f + fabsf(normalized) * 0.75f);
    bowlCurve *= _bowlStrength;

    _ballVx = bowlCurve * (_ballSpeed * 0.85f);
    const float vySquared = max(36.0f, (_ballSpeed * _ballSpeed) - (_ballVx * _ballVx));
    _ballVy = -sqrtf(vySquared);

    _ballY = kPaddleY - _ballRadius - 1.0f;
  }

  void reflect(float nx, float ny)
  {
    const float dot = (_ballVx * nx) + (_ballVy * ny);
    _ballVx -= 2.0f * dot * nx;
    _ballVy -= 2.0f * dot * ny;
  }

  void normalizeBallSpeed(float speed)
  {
    const float magnitude = sqrtf(_ballVx * _ballVx + _ballVy * _ballVy);
    if (magnitude > 0.001f)
    {
      _ballVx = (_ballVx / magnitude) * speed;
      _ballVy = (_ballVy / magnitude) * speed;
    }
  }

  void finish(uint32_t nowMs, bool completed)
  {
    _finished = true;
    _completed = completed;
    _durationMs = nowMs - _startMs;
  }

  void drawPrediction(Adafruit_ST7735 &display)
  {
    float x = _ballX;
    float y = _ballY;
    float vx = _ballVx * 0.12f;
    float vy = _ballVy * 0.12f;
    int prevX = static_cast<int>(x);
    int prevY = static_cast<int>(y);

    for (uint8_t i = 0; i < 18; ++i)
    {
      x += vx;
      y += vy;
      if (x <= _ballRadius || x >= (hw::tftWidth - _ballRadius))
      {
        vx = -vx;
      }
      if (y <= _ballRadius + 14)
      {
        vy = -vy;
      }
      const int nextX = static_cast<int>(x);
      const int nextY = static_cast<int>(y);
      const uint8_t fade = static_cast<uint8_t>(clampValue<int>(255 - i * 12, 36, 255));
      const uint16_t lineColor = color565(fade, clampValue<int>(fade - 38, 18, 220), 0);
      display.drawLine(prevX, prevY, nextX, nextY, lineColor);
      prevX = nextX;
      prevY = nextY;
    }
  }

  void drawBowlPaddle(Adafruit_ST7735 &display)
  {
    const int x = static_cast<int>(_paddleX);
    const int w = static_cast<int>(_paddleWidth);
    const int paddleY = static_cast<int>(kPaddleY);
    for (int i = 0; i < w; ++i)
    {
      const float t = (w <= 1) ? 0.0f : (static_cast<float>(i) / static_cast<float>(w - 1)) * 2.0f - 1.0f;
      const int topOffset = static_cast<int>((1.0f - (t * t)) * 2.2f);
      const int topY = paddleY + topOffset;
      display.drawFastVLine(x + i, topY, (paddleY + kPaddleH) - topY, ST77XX_WHITE);
      if ((i % 2) == 0)
      {
        display.drawPixel(x + i, topY, COLOR_DIM);
      }
    }
  }

  static constexpr uint8_t kMaxRows = 5;
  static constexpr uint8_t kCols = 6;
  static constexpr float kBrickW = 22.0f;
  static constexpr float kBrickH = 10.0f;
  static constexpr float kBaseRadius = 3.0f;
  static constexpr float kPaddleY = hw::tftHeight - 14.0f;
  static constexpr int kPaddleH = 6;

  bool _bricks[kMaxRows][kCols] = {};
  bool _special[kMaxRows][kCols] = {};
  bool _finished = false;
  bool _completed = false;
  uint8_t _difficulty = 0;
  uint8_t _activeRows = 4;
  uint8_t _specialTarget = 3;
  uint32_t _startMs = 0;
  uint32_t _lastUpdateMs = 0;
  uint32_t _durationMs = 0;
  uint32_t _powerUntilMs = 0;
  uint32_t _score = 0;
  float _ballSpeed = 126.0f;
  float _ballRadius = kBaseRadius;
  float _basePaddleWidth = 28.0f;
  float _bowlStrength = 0.72f;
  float _paddleX = 0.0f;
  float _paddleWidth = 28.0f;
  float _ballX = 0.0f;
  float _ballY = 0.0f;
  float _ballVx = 0.0f;
  float _ballVy = 0.0f;
};

class PongGame : public IGame
{
public:
  GameId id() const override
  {
    return GameId::Pong;
  }

  void enter(uint8_t difficulty, uint32_t nowMs) override
  {
    _difficulty = clampValue<uint8_t>(difficulty, 0, 2);
    _startMs = nowMs;
    _lastUpdateMs = nowMs;
    _playerY = 46.0f;
    _aiY = 46.0f;
    _playerScore = 0;
    _aiScore = 0;
    _finished = false;
    _completed = false;
    _rallyCount = 0;
    _smashFlashUntilMs = 0;
    _paddleH = (_difficulty == 0) ? 30 : (_difficulty == 1) ? 24
                                                             : 20;
    _smashThreshold = (_difficulty == 0) ? 5 : (_difficulty == 1) ? 4
                                                                   : 3;
    resetBall(random(0, 2) == 0 ? -1.0f : 1.0f);
  }

  void update(uint32_t nowMs, const InputSnapshot &input, const SensorSnapshot &) override
  {
    if (_finished)
    {
      return;
    }

    float dt = (nowMs - _lastUpdateMs) / 1000.0f;
    dt = clampValue(dt, 0.0f, 0.05f);
    _lastUpdateMs = nowMs;

    const float playerSpeed = (_difficulty == 0) ? 126.0f : (_difficulty == 1) ? 112.0f
                                                                                : 104.0f;
    if (input.btn1Down && !input.btn2Down)
    {
      _playerY -= playerSpeed * dt;
    }
    if (input.btn2Down && !input.btn1Down)
    {
      _playerY += playerSpeed * dt;
    }
    _playerY = clampValue(_playerY, 16.0f, static_cast<float>(hw::tftHeight - _paddleH - 4));

    const float aiSpeed = (_difficulty == 0) ? 42.0f : (_difficulty == 1) ? 70.0f
                                                                           : 98.0f;
    const float aiLag = (_difficulty == 0) ? 8.0f : (_difficulty == 1) ? 4.0f
                                                                        : 0.0f;
    const float aiTarget = _ballY - (_paddleH * 0.5f) + aiLag;
    if (_aiY < aiTarget)
    {
      _aiY += aiSpeed * dt;
    }
    else if (_aiY > aiTarget)
    {
      _aiY -= aiSpeed * dt;
    }
    _aiY = clampValue(_aiY, 16.0f, static_cast<float>(hw::tftHeight - _paddleH - 4));

    _ballX += _ballVx * dt;
    _ballY += _ballVy * dt;

    if (_ballY <= kRadius + 14 || _ballY >= (hw::tftHeight - kRadius - 1))
    {
      _ballVy = -_ballVy;
      _ballY = clampValue(_ballY, kRadius + 14, static_cast<float>(hw::tftHeight - kRadius - 1));
    }

    float nx = 0.0f;
    float ny = 0.0f;
    if (circleRectCollision(_ballX, _ballY, kRadius, 8.0f, _playerY, 4.0f, _paddleH, nx, ny) && _ballVx < 0.0f)
    {
      reflect(nx, ny);
      ++_rallyCount;
      const float hitOffset = ((_ballY - _playerY) / _paddleH) - 0.5f;
      if (_rallyCount >= _smashThreshold && fabsf(hitOffset) > 0.18f)
      {
        _ballVx = fabsf(_ballVx) + ((_difficulty == 0) ? 22.0f : 30.0f);
        _ballVy = hitOffset * ((_difficulty == 0) ? 78.0f : 92.0f);
        _smashFlashUntilMs = nowMs + 700;
        _rallyCount = 0;
      }
      else
      {
        _ballVx = fabsf(_ballVx) + ((_difficulty == 0) ? 4.0f : 8.0f);
        _ballVy += hitOffset * 34.0f;
      }
    }

    if (circleRectCollision(_ballX, _ballY, kRadius, hw::tftWidth - 12.0f, _aiY, 4.0f, _paddleH, nx, ny) && _ballVx > 0.0f)
    {
      reflect(nx, ny);
      ++_rallyCount;
      _ballVx = -fabsf(_ballVx) - ((_difficulty == 0) ? 3.0f : 7.0f);
      _ballVy += ((_ballY - _aiY) / _paddleH - 0.5f) * 24.0f;
    }

    if (_ballX < -6.0f)
    {
      ++_aiScore;
      if (_aiScore >= 5)
      {
        finish(nowMs, false);
      }
      else
      {
        resetBall(1.0f);
      }
    }
    else if (_ballX > hw::tftWidth + 6.0f)
    {
      ++_playerScore;
      if (_playerScore >= 5)
      {
        finish(nowMs, true);
      }
      else
      {
        resetBall(-1.0f);
      }
    }
  }

  void renderTft(Adafruit_ST7735 &display) override
  {
    display.fillScreen(COLOR_BG);
    display.fillRect(0, 0, hw::tftWidth, 14, COLOR_PANEL);
    display.setTextColor(COLOR_TEXT);
    display.setCursor(4, 3);
    display.print("Pong");
    display.setCursor(hw::tftWidth - 48, 3);
    display.print(_playerScore);
    display.print(":");
    display.print(_aiScore);

    for (int y = 18; y < hw::tftHeight; y += 8)
    {
      display.drawFastVLine(hw::tftWidth / 2, y, 4, COLOR_DIM);
    }

    display.fillRoundRect(8, static_cast<int>(_playerY), 4, _paddleH, 2, COLOR_ACCENT);
    display.fillRoundRect(hw::tftWidth - 12, static_cast<int>(_aiY), 4, _paddleH, 2, COLOR_WARN);
    display.fillCircle(static_cast<int>(_ballX), static_cast<int>(_ballY), static_cast<int>(kRadius), ST77XX_WHITE);

    display.setTextColor(COLOR_DIM);
    display.setCursor(54, kFooterY);
    display.print(kPongDifficultyNames[_difficulty]);
    if (_smashFlashUntilMs > millis())
    {
      display.setTextColor(COLOR_WARN);
      display.setCursor(4, kFooterY);
      display.print("SMASH");
    }
  }

  void renderOledOverlay(Adafruit_SSD1306 &display, const SensorSnapshot &sensors) override
  {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print("PONG ");
    display.print(kPongDifficultyNames[_difficulty]);
    display.setCursor(0, 12);
    display.print("Score:");
    display.print(_playerScore);
    display.print("-");
    display.print(_aiScore);
    display.setCursor(0, 24);
    display.print("T:");
    if (sensors.dhtOk)
    {
      display.print(static_cast<int>(sensors.temperatureC));
      display.print("C H:");
      display.print(static_cast<int>(sensors.humidityPct));
      display.print("%");
    }
    else
    {
      display.print("-- H:--");
    }
    display.setCursor(0, 36);
    display.print("L:");
    display.print(sensors.lightPct);
    display.print("%");
    display.display();
  }

  bool isFinished() const override
  {
    return _finished;
  }

  GameResult result() const override
  {
    GameResult result;
    result.gameId = id();
    result.difficulty = _difficulty;
    result.score = static_cast<uint32_t>(_playerScore) * 100 + (_completed ? 500 : 0);
    result.durationMs = _durationMs;
    result.completed = _completed;
    return result;
  }

private:
  void reflect(float nx, float ny)
  {
    const float dot = (_ballVx * nx) + (_ballVy * ny);
    _ballVx -= 2.0f * dot * nx;
    _ballVy -= 2.0f * dot * ny;
  }

  void resetBall(float direction)
  {
    _ballX = hw::tftWidth * 0.5f;
    _ballY = hw::tftHeight * 0.5f;
    const float speed = (_difficulty == 0) ? 56.0f : (_difficulty == 1) ? 78.0f
                                                                         : 96.0f;
    _ballVx = speed * direction;
    _ballVy = random(-25, 26);
    _rallyCount = 0;
    _smashFlashUntilMs = 0;
  }

  void finish(uint32_t nowMs, bool completed)
  {
    _finished = true;
    _completed = completed;
    _durationMs = nowMs - _startMs;
  }

  static constexpr float kRadius = 3.0f;
  bool _finished = false;
  bool _completed = false;
  uint8_t _difficulty = 0;
  uint8_t _smashThreshold = 4;
  uint32_t _startMs = 0;
  uint32_t _lastUpdateMs = 0;
  uint32_t _durationMs = 0;
  uint32_t _smashFlashUntilMs = 0;
  uint8_t _playerScore = 0;
  uint8_t _aiScore = 0;
  uint8_t _rallyCount = 0;
  int _paddleH = 24;
  float _playerY = 46.0f;
  float _aiY = 46.0f;
  float _ballX = 0.0f;
  float _ballY = 0.0f;
  float _ballVx = 0.0f;
  float _ballVy = 0.0f;
};

class FlappyBirdGame : public IGame
{
public:
  GameId id() const override
  {
    return GameId::FlappyBird;
  }

  void enter(uint8_t difficulty, uint32_t nowMs) override
  {
    _difficulty = clampValue<uint8_t>(difficulty, 0, 2);
    _finished = false;
    _completed = false;
    _score = 0;
    _startMs = nowMs;
    _lastUpdateMs = nowMs;
    _birdY = hw::tftHeight * 0.5f;
    _birdVel = 0.0f;
    _pipeSpeed = (_difficulty == 0) ? 56.0f : (_difficulty == 1) ? 76.0f
                                                                 : 92.0f;
    _gapHeight = (_difficulty == 0) ? 50 : (_difficulty == 1) ? 38
                                                              : 30;
    _gravity = (_difficulty == 0) ? 216.0f : (_difficulty == 1) ? 252.0f
                                                                 : 286.0f;
    _flapVelocity = (_difficulty == 0) ? -84.0f : (_difficulty == 1) ? -92.0f
                                                                      : -96.0f;

    for (uint8_t i = 0; i < kPipeCount; ++i)
    {
      _pipes[i].x = hw::tftWidth + i * 62.0f;
      _pipes[i].gapY = 28 + random(0, 40);
      _pipes[i].scored = false;
    }
  }

  void update(uint32_t nowMs, const InputSnapshot &input, const SensorSnapshot &) override
  {
    if (_finished)
    {
      return;
    }

    float dt = (nowMs - _lastUpdateMs) / 1000.0f;
    dt = clampValue(dt, 0.0f, 0.05f);
    _lastUpdateMs = nowMs;

    if (input.btn1DownEdge || input.btn1Pressed)
    {
      _birdVel = _flapVelocity;
    }

    _birdVel += _gravity * dt;
    _birdY += _birdVel * dt;

    if (_birdY < 14.0f || _birdY > hw::tftHeight - 4.0f)
    {
      finish(nowMs, false);
      return;
    }

    float farthestX = 0.0f;
    for (uint8_t i = 0; i < kPipeCount; ++i)
    {
      farthestX = max(farthestX, _pipes[i].x);
      _pipes[i].x -= _pipeSpeed * dt;

      if (!_pipes[i].scored && (_pipes[i].x + kPipeW) < kBirdX)
      {
        _pipes[i].scored = true;
        ++_score;
      }

      if ((_pipes[i].x + kPipeW) < 0.0f)
      {
        _pipes[i].x = farthestX + 62.0f;
        _pipes[i].gapY = 28 + random(0, 40);
        _pipes[i].scored = false;
        farthestX = _pipes[i].x;
      }

      if (collisionWithPipe(_pipes[i]))
      {
        finish(nowMs, false);
        return;
      }
    }
  }

  void renderTft(Adafruit_ST7735 &display) override
  {
    display.fillScreen(color565(20, 30, 45));
    display.fillRect(0, 0, hw::tftWidth, 14, COLOR_PANEL);
    display.setCursor(4, 3);
    display.setTextColor(COLOR_TEXT);
    display.print("Flappy Bird");
    display.setCursor(hw::tftWidth - 44, 3);
    display.print(_score);
    display.setCursor(4, kFooterY);
    display.setTextColor(COLOR_DIM);
    display.print(kPongDifficultyNames[_difficulty]);

    for (uint8_t i = 0; i < kPipeCount; ++i)
    {
      const int x = static_cast<int>(_pipes[i].x);
      const int topH = _pipes[i].gapY;
      const int bottomY = _pipes[i].gapY + _gapHeight;
      display.fillRect(x, 14, kPipeW, topH - 14, COLOR_GOOD);
      display.fillRect(x, bottomY, kPipeW, hw::tftHeight - bottomY, COLOR_GOOD);
    }

    display.fillCircle(kBirdX, static_cast<int>(_birdY), 4, COLOR_WARN);
    display.fillTriangle(kBirdX - 2, static_cast<int>(_birdY) - 1, kBirdX - 2, static_cast<int>(_birdY) + 2, kBirdX + 4, static_cast<int>(_birdY), ST77XX_WHITE);
  }

  void renderOledOverlay(Adafruit_SSD1306 &display, const SensorSnapshot &sensors) override
  {
    drawGameSensorOverlay(display, "FLAPPY", _score, "Score:", sensors);
  }

  bool isFinished() const override
  {
    return _finished;
  }

  GameResult result() const override
  {
    GameResult result;
    result.gameId = id();
    result.difficulty = _difficulty;
    result.score = _score * 100;
    result.durationMs = _durationMs;
    result.completed = _completed;
    return result;
  }

private:
  struct Pipe
  {
    float x = 0.0f;
    int gapY = 0;
    bool scored = false;
  };

  bool collisionWithPipe(const Pipe &pipe) const
  {
    const bool inX = (kBirdX + 4) >= pipe.x && (kBirdX - 4) <= (pipe.x + kPipeW);
    const bool inGap = (_birdY - 4) >= pipe.gapY && (_birdY + 4) <= (pipe.gapY + _gapHeight);
    return inX && !inGap;
  }

  void finish(uint32_t nowMs, bool completed)
  {
    _finished = true;
    _completed = completed;
    _durationMs = nowMs - _startMs;
  }

  static constexpr uint8_t kPipeCount = 3;
  static constexpr int kPipeW = 20;
  static constexpr int kBirdX = 38;

  Pipe _pipes[kPipeCount];
  bool _finished = false;
  bool _completed = false;
  uint8_t _difficulty = 0;
  uint32_t _startMs = 0;
  uint32_t _lastUpdateMs = 0;
  uint32_t _durationMs = 0;
  uint32_t _score = 0;
  float _pipeSpeed = 78.0f;
  float _gravity = 260.0f;
  float _flapVelocity = -92.0f;
  float _birdY = 0.0f;
  float _birdVel = 0.0f;
  int _gapHeight = 34;
};

class RacingGame : public IGame
{
public:
  GameId id() const override
  {
    return GameId::Racing;
  }

  void enter(uint8_t difficulty, uint32_t nowMs) override
  {
    _difficulty = clampValue<uint8_t>(difficulty, 0, 2);
    _finished = false;
    _completed = false;
    _score = 0;
    _startMs = nowMs;
    _lastUpdateMs = nowMs;
    _spawnTimer = 0.0f;
    _roadDashOffset = 0.0f;
    _playerLane = 1;
    _targetLane = 1;
    _playerCenterX = static_cast<float>(laneCenter(_playerLane));
    _roadSpeed = (_difficulty == 0) ? 64.0f : (_difficulty == 1) ? 82.0f
                                                                 : 102.0f;
    _laneShiftSpeed = (_difficulty == 0) ? 128.0f : (_difficulty == 1) ? 148.0f
                                                                        : 168.0f;
    _spawnInterval = (_difficulty == 0) ? 1.26f : (_difficulty == 1) ? 0.98f
                                                                      : 0.80f;
    _targetScore = (_difficulty == 0) ? 10 : (_difficulty == 1) ? 16
                                                                : 22;
    _lastSpawnLane = 1;
    for (uint8_t i = 0; i < kObstacleCount; ++i)
    {
      _obstacles[i] = {};
    }
  }

  void update(uint32_t nowMs, const InputSnapshot &input, const SensorSnapshot &) override
  {
    if (_finished)
    {
      return;
    }

    float dt = (nowMs - _lastUpdateMs) / 1000.0f;
    dt = clampValue(dt, 0.0f, 0.05f);
    _lastUpdateMs = nowMs;

    if (input.btn1DownEdge && _playerLane > 0)
    {
      --_playerLane;
      _targetLane = _playerLane;
    }
    if (input.btn2DownEdge && _playerLane < 2)
    {
      ++_playerLane;
      _targetLane = _playerLane;
    }

    const float targetX = static_cast<float>(laneCenter(_targetLane));
    const float deltaX = targetX - _playerCenterX;
    const float maxShift = _laneShiftSpeed * dt;
    if (fabsf(deltaX) <= maxShift)
    {
      _playerCenterX = targetX;
    }
    else
    {
      _playerCenterX += (deltaX > 0.0f) ? maxShift : -maxShift;
    }

    _roadDashOffset += _roadSpeed * dt;
    while (_roadDashOffset >= 18.0f)
    {
      _roadDashOffset -= 18.0f;
    }

    _spawnTimer += dt;
    if (_spawnTimer >= _spawnInterval)
    {
      _spawnTimer -= _spawnInterval;
      spawnObstacle();
    }

    const float playerLeft = _playerCenterX - (kPlayerW * 0.5f) + 2.0f;
    const float playerTop = kPlayerY + 2.0f;
    const float playerW = kPlayerW - 4.0f;
    const float playerH = kPlayerH - 4.0f;

    for (uint8_t i = 0; i < kObstacleCount; ++i)
    {
      if (!_obstacles[i].active)
      {
        continue;
      }

      _obstacles[i].y += _roadSpeed * dt;
      const float obstacleLeft = laneCenter(_obstacles[i].lane) - (_obstacles[i].width * 0.5f) + 1.0f;
      const float obstacleTop = _obstacles[i].y + 1.0f;
      const float obstacleW = _obstacles[i].width - 2.0f;
      const float obstacleH = _obstacles[i].height - 2.0f;
      if (rectsOverlap(playerLeft, playerTop, playerW, playerH, obstacleLeft, obstacleTop, obstacleW, obstacleH))
      {
        finish(nowMs, false);
        return;
      }

      if (_obstacles[i].y > hw::tftHeight + 8)
      {
        _obstacles[i].active = false;
        ++_score;
        if (_score >= _targetScore)
        {
          finish(nowMs, true);
          return;
        }
      }
    }
  }

  void renderTft(Adafruit_ST7735 &display) override
  {
    display.fillScreen(color565(15, 22, 18));
    display.fillRect(0, 0, hw::tftWidth, 14, COLOR_PANEL);
    display.setTextColor(COLOR_TEXT);
    display.setCursor(4, 3);
    display.print("Racing");
    display.setCursor(hw::tftWidth - 44, 3);
    display.print(_score);

    display.fillRect(kRoadX - 4, 14, kRoadW + 8, hw::tftHeight - 14, color565(105, 92, 60));
    display.fillRect(kRoadX, 14, kRoadW, hw::tftHeight - 14, color565(42, 42, 48));
    display.drawFastVLine(kRoadX, 14, hw::tftHeight - 14, color565(255, 210, 70));
    display.drawFastVLine(kRoadX + kRoadW - 1, 14, hw::tftHeight - 14, color565(255, 210, 70));

    for (uint8_t divider = 1; divider < 3; ++divider)
    {
      const int dividerX = laneDividerX(divider);
      for (int y = 16 - static_cast<int>(_roadDashOffset); y < hw::tftHeight; y += 18)
      {
        display.fillRoundRect(dividerX - 1, y, 3, 10, 1, color565(228, 228, 186));
      }
    }

    for (uint8_t i = 0; i < kObstacleCount; ++i)
    {
      if (!_obstacles[i].active)
      {
        continue;
      }
      const int x = laneCenter(_obstacles[i].lane) - (_obstacles[i].width / 2);
      drawVehicle(display, x, static_cast<int>(_obstacles[i].y), _obstacles[i].width, _obstacles[i].height,
                  _obstacles[i].kind, _obstacles[i].bodyColor, _obstacles[i].detailColor);
    }

    const int playerX = static_cast<int>(_playerCenterX) - (kPlayerW / 2);
    drawVehicle(display, playerX, kPlayerY, kPlayerW, kPlayerH, VehicleKind::Player,
                color565(38, 132, 255), ST77XX_WHITE);

    display.setTextColor(COLOR_DIM);
    display.setCursor(4, kFooterY);
    display.print(kPongDifficultyNames[_difficulty]);
    display.setCursor(82, kFooterY);
    display.print("B1 L  B2 R");
  }

  void renderOledOverlay(Adafruit_SSD1306 &display, const SensorSnapshot &sensors) override
  {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print("RACING");
    display.setCursor(0, 12);
    display.print("Score:");
    display.print(_score);
    display.setCursor(70, 12);
    display.print("Ln:");
    display.print(_targetLane + 1);
    display.setCursor(0, 24);
    display.print("T:");
    if (sensors.dhtOk)
    {
      display.print(static_cast<int>(sensors.temperatureC));
      display.print(" H:");
      display.print(static_cast<int>(sensors.humidityPct));
      display.print("%");
    }
    else
    {
      display.print("-- H:--");
    }
    display.setCursor(0, 36);
    display.print("L:");
    display.print(sensors.lightPct);
    display.print("%");
    display.display();
  }

  bool isFinished() const override
  {
    return _finished;
  }

  GameResult result() const override
  {
    GameResult result;
    result.gameId = id();
    result.difficulty = _difficulty;
    result.score = _score * 100 + (_completed ? 500 : 0);
    result.durationMs = _durationMs;
    result.completed = _completed;
    return result;
  }

private:
  enum class VehicleKind : uint8_t
  {
    Compact,
    Taxi,
    Van,
    Pickup,
    Truck,
    Bus,
    Player
  };

  struct Obstacle
  {
    bool active = false;
    uint8_t lane = 0;
    float y = -40.0f;
    uint8_t width = 16;
    uint8_t height = 20;
    VehicleKind kind = VehicleKind::Compact;
    uint16_t bodyColor = COLOR_DANGER;
    uint16_t detailColor = COLOR_TEXT;
  };

  static bool rectsOverlap(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh)
  {
    return ax < (bx + bw) && (ax + aw) > bx && ay < (by + bh) && (ay + ah) > by;
  }

  static int laneCenter(uint8_t lane)
  {
    return kRoadX + static_cast<int>(lane) * kLaneWidth + (kLaneWidth / 2);
  }

  static int laneDividerX(uint8_t divider)
  {
    return kRoadX + static_cast<int>(divider) * kLaneWidth;
  }

  bool laneHasRoom(uint8_t lane, uint8_t proposedHeight) const
  {
    const float laneGap = (_difficulty == 0) ? 30.0f : (_difficulty == 1) ? 22.0f
                                                                           : 16.0f;
    for (uint8_t i = 0; i < kObstacleCount; ++i)
    {
      if (!_obstacles[i].active || _obstacles[i].lane != lane)
      {
        continue;
      }
      if (_obstacles[i].y < (static_cast<float>(proposedHeight) + laneGap))
      {
        return false;
      }
    }
    return true;
  }

  void configureObstacle(Obstacle &obstacle)
  {
    static const uint16_t kBodyPalette[] = {
        color565(214, 63, 54),
        color565(238, 181, 32),
        color565(34, 167, 142),
        color565(56, 126, 248),
        color565(236, 119, 52),
        color565(202, 205, 214)};
    static const uint16_t kRoofPalette[] = {
        color565(238, 240, 245),
        color565(34, 34, 42),
        color565(160, 220, 244),
        color565(255, 248, 210),
        color565(182, 198, 214),
        color565(90, 98, 118)};

    const uint8_t styleIndex = random(0, 6);
    obstacle.kind = static_cast<VehicleKind>(styleIndex);
    obstacle.bodyColor = kBodyPalette[random(0, static_cast<int>(sizeof(kBodyPalette) / sizeof(kBodyPalette[0])))];
    obstacle.detailColor = kRoofPalette[random(0, static_cast<int>(sizeof(kRoofPalette) / sizeof(kRoofPalette[0])))];

    switch (obstacle.kind)
    {
    case VehicleKind::Compact:
      obstacle.width = 16;
      obstacle.height = 20;
      break;
    case VehicleKind::Taxi:
      obstacle.width = 16;
      obstacle.height = 22;
      obstacle.bodyColor = color565(245, 196, 38);
      obstacle.detailColor = color565(32, 36, 40);
      break;
    case VehicleKind::Van:
      obstacle.width = 18;
      obstacle.height = 24;
      break;
    case VehicleKind::Pickup:
      obstacle.width = 18;
      obstacle.height = 26;
      break;
    case VehicleKind::Truck:
      obstacle.width = 18;
      obstacle.height = 30;
      obstacle.detailColor = color565(236, 236, 242);
      break;
    case VehicleKind::Bus:
      obstacle.width = 18;
      obstacle.height = 34;
      obstacle.bodyColor = color565(224, 97, 44);
      obstacle.detailColor = color565(176, 223, 244);
      break;
    case VehicleKind::Player:
      obstacle.width = 18;
      obstacle.height = 24;
      break;
    }
  }

  void spawnObstacle()
  {
    int freeIndex = -1;
    for (uint8_t i = 0; i < kObstacleCount; ++i)
    {
      if (!_obstacles[i].active)
      {
        freeIndex = i;
        break;
      }
    }

    if (freeIndex < 0)
    {
      return;
    }

    Obstacle candidate;
    configureObstacle(candidate);

    bool foundLane = false;
    uint8_t lane = 0;
    for (uint8_t attempt = 0; attempt < 8; ++attempt)
    {
      lane = random(0, 3);
      if (lane == _lastSpawnLane && attempt < 4)
      {
        continue;
      }
      if (laneHasRoom(lane, candidate.height))
      {
        foundLane = true;
        break;
      }
    }

    if (!foundLane)
    {
      return;
    }

    _lastSpawnLane = lane;
    candidate.active = true;
    candidate.lane = lane;
    candidate.y = -static_cast<float>(candidate.height) - random(6, 16);
    _obstacles[freeIndex] = candidate;
  }

  static void drawVehicle(Adafruit_ST7735 &display, int x, int y, int w, int h, VehicleKind kind, uint16_t bodyColor, uint16_t detailColor)
  {
    const uint16_t shadowColor = color565(10, 10, 14);
    const uint16_t wheelColor = color565(16, 16, 20);
    const uint16_t glassColor = color565(170, 220, 248);
    const uint16_t tailColor = color565(255, 110, 80);

    display.fillRoundRect(x + 1, y + 1, w, h, 3, shadowColor);
    display.fillRoundRect(x - 1, y + 4, 2, max(6, h - 8), 1, wheelColor);
    display.fillRoundRect(x + w - 1, y + 4, 2, max(6, h - 8), 1, wheelColor);

    switch (kind)
    {
    case VehicleKind::Compact:
      display.fillRoundRect(x, y, w, h, 3, bodyColor);
      display.fillRoundRect(x + 3, y + 4, w - 6, h - 8, 2, detailColor);
      display.fillRect(x + 3, y + 3, w - 6, 4, glassColor);
      display.fillRect(x + 4, y + h - 6, w - 8, 2, tailColor);
      break;
    case VehicleKind::Taxi:
      display.fillRoundRect(x, y, w, h, 3, bodyColor);
      display.fillRect(x + 3, y + 3, w - 6, h - 6, glassColor);
      for (int stripeY = y + 8; stripeY < (y + h - 5); stripeY += 4)
      {
        display.drawFastHLine(x + 3, stripeY, w - 6, detailColor);
      }
      display.fillRect(x + (w / 2) - 2, y + 1, 4, 2, ST77XX_WHITE);
      break;
    case VehicleKind::Van:
      display.fillRoundRect(x, y, w, h, 3, bodyColor);
      display.fillRoundRect(x + 2, y + 3, w - 4, 8, 2, glassColor);
      display.fillRect(x + 3, y + 13, w - 6, h - 16, detailColor);
      display.drawFastVLine(x + (w / 2), y + 13, h - 16, color565(230, 230, 236));
      break;
    case VehicleKind::Pickup:
      display.fillRoundRect(x, y, w, h, 3, bodyColor);
      display.fillRoundRect(x + 3, y + 3, w - 6, 8, 2, glassColor);
      display.fillRect(x + 2, y + 12, w - 4, h - 14, detailColor);
      display.drawFastHLine(x + 2, y + 13, w - 4, color565(245, 245, 250));
      break;
    case VehicleKind::Truck:
      display.fillRoundRect(x, y, w, h, 3, bodyColor);
      display.fillRoundRect(x + 2, y + 3, w - 4, 7, 2, glassColor);
      display.fillRect(x + 2, y + 12, w - 4, h - 14, detailColor);
      display.drawFastHLine(x + 3, y + 16, w - 6, color565(200, 205, 214));
      display.drawFastHLine(x + 3, y + 21, w - 6, color565(200, 205, 214));
      break;
    case VehicleKind::Bus:
      display.fillRoundRect(x, y, w, h, 3, bodyColor);
      display.fillRect(x + 2, y + 3, w - 4, h - 6, detailColor);
      for (int windowY = y + 5; windowY < (y + h - 6); windowY += 5)
      {
        display.fillRect(x + 3, windowY, w - 6, 2, glassColor);
      }
      display.fillRect(x + 4, y + h - 5, w - 8, 2, tailColor);
      break;
    case VehicleKind::Player:
      display.fillRoundRect(x, y, w, h, 4, bodyColor);
      display.fillRoundRect(x + 3, y + 3, w - 6, h - 6, 3, color565(214, 236, 255));
      display.fillRect(x + (w / 2) - 1, y + 2, 2, h - 4, detailColor);
      display.fillRect(x + 4, y + h - 5, w - 8, 2, tailColor);
      display.fillRect(x + 4, y + 3, w - 8, 3, glassColor);
      break;
    }
  }

  void finish(uint32_t nowMs, bool completed)
  {
    _finished = true;
    _completed = completed;
    _durationMs = nowMs - _startMs;
  }

  static constexpr uint8_t kObstacleCount = 5;
  static constexpr int kRoadX = 18;
  static constexpr int kRoadW = 124;
  static constexpr int kLaneWidth = kRoadW / 3;
  static constexpr int kPlayerW = 18;
  static constexpr int kPlayerH = 24;
  static constexpr int kPlayerY = 96;

  Obstacle _obstacles[kObstacleCount];
  bool _finished = false;
  bool _completed = false;
  uint8_t _difficulty = 0;
  uint8_t _playerLane = 1;
  uint8_t _targetLane = 1;
  uint8_t _lastSpawnLane = 1;
  uint32_t _startMs = 0;
  uint32_t _lastUpdateMs = 0;
  uint32_t _durationMs = 0;
  uint32_t _score = 0;
  uint8_t _targetScore = 12;
  float _roadSpeed = 92.0f;
  float _laneShiftSpeed = 148.0f;
  float _roadDashOffset = 0.0f;
  float _playerCenterX = 0.0f;
  float _spawnTimer = 0.0f;
  float _spawnInterval = 0.92f;
};

class QuickDrawGame : public IGame
{
public:
  GameId id() const override
  {
    return GameId::QuickDraw;
  }

  void enter(uint8_t difficulty, uint32_t nowMs) override
  {
    _difficulty = clampValue<uint8_t>(difficulty, 0, 2);
    _finished = false;
    _completed = false;
    _winner = 0;
    _falseStartBy = 0;
    _reactionMs = 0;
    _startMs = nowMs;
    _goShownMs = 0;
    _timeoutMs = (_difficulty == 0) ? 1500 : (_difficulty == 1) ? 1100
                                                                : 850;
    const uint32_t minDelay = (_difficulty == 0) ? 1300 : (_difficulty == 1) ? 1700
                                                                              : 2100;
    const uint32_t maxDelay = (_difficulty == 0) ? 2500 : (_difficulty == 1) ? 3200
                                                                              : 3800;
    _goAtMs = nowMs + random(minDelay, maxDelay + 1);
    _durationMs = 0;
  }

  void update(uint32_t nowMs, const InputSnapshot &input, const SensorSnapshot &) override
  {
    if (_finished)
    {
      return;
    }

    if (_goShownMs == 0)
    {
      if (input.btn1DownEdge && input.btn2DownEdge)
      {
        _falseStartBy = 3;
        finish(nowMs, false, 0);
        return;
      }
      if (input.btn1DownEdge)
      {
        _falseStartBy = 1;
        finish(nowMs, true, 2);
        return;
      }
      if (input.btn2DownEdge)
      {
        _falseStartBy = 2;
        finish(nowMs, true, 1);
        return;
      }
      if (nowMs >= _goAtMs)
      {
        _goShownMs = nowMs;
      }
      return;
    }

    if (input.btn1DownEdge && input.btn2DownEdge)
    {
      _reactionMs = nowMs - _goShownMs;
      finish(nowMs, false, 0);
      return;
    }
    if (input.btn1DownEdge)
    {
      _reactionMs = nowMs - _goShownMs;
      finish(nowMs, true, 1);
      return;
    }
    if (input.btn2DownEdge)
    {
      _reactionMs = nowMs - _goShownMs;
      finish(nowMs, true, 2);
      return;
    }
    if ((nowMs - _goShownMs) > _timeoutMs)
    {
      _reactionMs = _timeoutMs;
      finish(nowMs, false, 0);
    }
  }

  void renderTft(Adafruit_ST7735 &display) override
  {
    display.fillScreen(color565(20, 12, 16));
    display.fillRect(0, 0, hw::tftWidth, 14, COLOR_PANEL);
    display.setTextColor(COLOR_TEXT);
    display.setCursor(4, 3);
    display.print("Quick Draw");
    display.setCursor(112, 3);
    display.print(kPongDifficultyNames[_difficulty]);

    display.drawRoundRect(12, 22, hw::tftWidth - 24, 70, 6, COLOR_DIM);
    if (_goShownMs == 0)
    {
      drawCenteredText(display, 36, "WAIT", COLOR_WARN, 2);
      drawCenteredText(display, 60, "No early shot", COLOR_TEXT, 1);
      drawCenteredText(display, 78, "B1 vs B2", COLOR_DIM, 1);
    }
    else
    {
      display.fillRoundRect(20, 28, hw::tftWidth - 40, 54, 6, COLOR_GOOD);
      drawCenteredText(display, 54, "DRAW!", color565(16, 34, 18), 3);
      drawCenteredText(display, 82, "First press wins", COLOR_TEXT, 1);
    }

    display.setTextColor(COLOR_DIM);
    display.setCursor(12, kFooterY);
    display.print("B1 vs B2");
  }

  void renderOledOverlay(Adafruit_SSD1306 &display, const SensorSnapshot &sensors) override
  {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print("QUICK DRAW");
    display.setCursor(0, 12);
    if (_goShownMs == 0)
    {
      display.print("Wait...");
    }
    else
    {
      display.print("DRAW NOW");
    }
    display.setCursor(70, 12);
    display.print("D:");
    display.print(kPongDifficultyNames[_difficulty]);
    display.setCursor(0, 24);
    display.print("T:");
    if (sensors.dhtOk)
    {
      display.print(static_cast<int>(sensors.temperatureC));
      display.print(" H:");
      display.print(static_cast<int>(sensors.humidityPct));
      display.print("%");
    }
    else
    {
      display.print("-- H:--");
    }
    display.setCursor(0, 36);
    display.print("L:");
    display.print(sensors.lightPct);
    display.print("%");
    display.display();
  }

  bool isFinished() const override
  {
    return _finished;
  }

  GameResult result() const override
  {
    GameResult result;
    result.gameId = id();
    result.difficulty = _difficulty;
    result.durationMs = _reactionMs;
    result.completed = _completed;
    result.winner = _winner;
    if (_winner == 0)
    {
      result.score = 0;
      return result;
    }

    const uint32_t difficultyBonus = (_difficulty == 0) ? 0 : (_difficulty == 1) ? 180
                                                                                  : 320;
    if (_falseStartBy != 0)
    {
      result.score = 700 + difficultyBonus;
    }
    else
    {
      const uint32_t speedBonus = (_reactionMs >= 1200) ? 0 : (1200 - _reactionMs) * 2;
      result.score = 900 + difficultyBonus + speedBonus;
    }
    return result;
  }

private:
  void finish(uint32_t nowMs, bool completed, uint8_t winner)
  {
    _finished = true;
    _completed = completed;
    _winner = winner;
    _durationMs = nowMs - _startMs;
  }

  bool _finished = false;
  bool _completed = false;
  uint8_t _difficulty = 0;
  uint8_t _winner = 0;
  uint8_t _falseStartBy = 0;
  uint32_t _startMs = 0;
  uint32_t _goAtMs = 0;
  uint32_t _goShownMs = 0;
  uint32_t _timeoutMs = 1200;
  uint32_t _reactionMs = 0;
  uint32_t _durationMs = 0;
};

class PoleClimbGame : public IGame
{
public:
  GameId id() const override
  {
    return GameId::PoleClimb;
  }

  void enter(uint8_t difficulty, uint32_t nowMs) override
  {
    _difficulty = clampValue<uint8_t>(difficulty, 0, 2);
    _finished = false;
    _completed = false;
    _winner = 0;
    _startMs = nowMs;
    _lastUpdateMs = nowMs;
    _durationMs = 0;
    _goal = (_difficulty == 0) ? 16.0f : (_difficulty == 1) ? 22.0f
                                                            : 28.0f;
    _slipPerSec = (_difficulty == 0) ? 1.6f : (_difficulty == 1) ? 2.3f
                                                                 : 3.0f;
    _timeLimitMs = (_difficulty == 0) ? 18000 : (_difficulty == 1) ? 16000
                                                                   : 14000;
    _progress[0] = 0.0f;
    _progress[1] = 0.0f;
  }

  void update(uint32_t nowMs, const InputSnapshot &input, const SensorSnapshot &) override
  {
    if (_finished)
    {
      return;
    }

    float dt = (nowMs - _lastUpdateMs) / 1000.0f;
    dt = clampValue(dt, 0.0f, 0.06f);
    _lastUpdateMs = nowMs;

    if (input.btn1DownEdge)
    {
      _progress[0] += 1.0f;
    }
    if (input.btn2DownEdge)
    {
      _progress[1] += 1.0f;
    }

    _progress[0] = max(0.0f, _progress[0] - _slipPerSec * dt);
    _progress[1] = max(0.0f, _progress[1] - _slipPerSec * dt);

    if (_progress[0] >= _goal)
    {
      finish(nowMs, true, 1);
      return;
    }
    if (_progress[1] >= _goal)
    {
      finish(nowMs, true, 2);
      return;
    }

    if ((nowMs - _startMs) >= _timeLimitMs)
    {
      const uint8_t winner = (_progress[0] > _progress[1]) ? 1 : (_progress[1] > _progress[0]) ? 2
                                                                                                  : 0;
      finish(nowMs, winner != 0, winner);
    }
  }

  void renderTft(Adafruit_ST7735 &display) override
  {
    display.fillScreen(color565(15, 18, 34));
    display.fillRect(0, 0, hw::tftWidth, 14, COLOR_PANEL);
    display.setTextColor(COLOR_TEXT);
    display.setCursor(4, 3);
    display.print("Pole Climb");
    display.setCursor(112, 3);
    display.print(kPongDifficultyNames[_difficulty]);

    const int poleTop = 22;
    const int poleH = 82;
    const int leftPoleX = 46;
    const int rightPoleX = 112;
    display.drawFastVLine(leftPoleX, poleTop, poleH, color565(210, 210, 214));
    display.drawFastVLine(rightPoleX, poleTop, poleH, color565(210, 210, 214));
    display.fillRect(30, poleTop + poleH, 98, 4, color565(92, 58, 28));

    drawClimber(display, leftPoleX, poleTop, poleH, _progress[0] / _goal, color565(82, 168, 255));
    drawClimber(display, rightPoleX, poleTop, poleH, _progress[1] / _goal, color565(255, 136, 84));

    display.setTextColor(COLOR_TEXT);
    display.setCursor(18, 108);
    display.print("B1:");
    display.print(static_cast<int>(_progress[0]));
    display.setCursor(92, 108);
    display.print("B2:");
    display.print(static_cast<int>(_progress[1]));

    display.setTextColor(COLOR_DIM);
    display.setCursor(8, kFooterY);
    display.print("Mash B1 / B2");
  }

  void renderOledOverlay(Adafruit_SSD1306 &display, const SensorSnapshot &sensors) override
  {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print("POLE CLIMB");
    display.setCursor(0, 12);
    display.print("B1:");
    display.print(static_cast<int>(_progress[0]));
    display.setCursor(64, 12);
    display.print("B2:");
    display.print(static_cast<int>(_progress[1]));
    display.setCursor(0, 24);
    display.print("Goal:");
    display.print(static_cast<int>(_goal));
    display.setCursor(0, 36);
    display.print("L:");
    display.print(sensors.lightPct);
    display.print("%");
    display.display();
  }

  bool isFinished() const override
  {
    return _finished;
  }

  GameResult result() const override
  {
    GameResult result;
    result.gameId = id();
    result.difficulty = _difficulty;
    result.durationMs = _durationMs;
    result.completed = _completed;
    result.winner = _winner;
    const float lead = max(_progress[0], _progress[1]);
    result.score = static_cast<uint32_t>(lead * 100.0f) + (_winner != 0 ? 900 : 0) + _difficulty * 150;
    return result;
  }

private:
  static void drawClimber(Adafruit_ST7735 &display, int poleX, int poleTop, int poleH, float ratio, uint16_t color)
  {
    ratio = clampValue(ratio, 0.0f, 1.0f);
    const int y = poleTop + poleH - 8 - static_cast<int>(ratio * (poleH - 12));
    display.fillCircle(poleX, y, 4, color);
    display.drawLine(poleX, y + 4, poleX, y + 10, color);
    display.drawLine(poleX, y + 6, poleX - 4, y + 2, color);
    display.drawLine(poleX, y + 6, poleX + 4, y + 2, color);
    display.drawLine(poleX, y + 10, poleX - 4, y + 14, color);
    display.drawLine(poleX, y + 10, poleX + 4, y + 14, color);
  }

  void finish(uint32_t nowMs, bool completed, uint8_t winner)
  {
    _finished = true;
    _completed = completed;
    _winner = winner;
    _durationMs = nowMs - _startMs;
  }

  bool _finished = false;
  bool _completed = false;
  uint8_t _difficulty = 0;
  uint8_t _winner = 0;
  uint32_t _startMs = 0;
  uint32_t _lastUpdateMs = 0;
  uint32_t _durationMs = 0;
  uint32_t _timeLimitMs = 16000;
  float _progress[2] = {};
  float _goal = 22.0f;
  float _slipPerSec = 2.0f;
};

class BalloonBattleGame : public IGame
{
public:
  GameId id() const override
  {
    return GameId::BalloonBattle;
  }

  void enter(uint8_t difficulty, uint32_t nowMs) override
  {
    _difficulty = clampValue<uint8_t>(difficulty, 0, 2);
    _finished = false;
    _completed = false;
    _winner = 0;
    _startMs = nowMs;
    _lastUpdateMs = nowMs;
    _durationMs = 0;
    _balloonSize = 12.0f;
    _pumpAmount = (_difficulty == 0) ? 4.2f : (_difficulty == 1) ? 5.0f
                                                                 : 5.8f;
    _burstAt = random((_difficulty == 0) ? 58 : (_difficulty == 1) ? 49
                                                                    : 41,
                      (_difficulty == 0) ? 72 : (_difficulty == 1) ? 58
                                                                    : 48);
    _currentPlayer = 1;
    _turnCount = 0;
    _turnPumpCount = 0;
    _pumpAnimUntilMs = 0;
  }

  void update(uint32_t nowMs, const InputSnapshot &input, const SensorSnapshot &) override
  {
    if (_finished)
    {
      return;
    }

    float dt = (nowMs - _lastUpdateMs) / 1000.0f;
    dt = clampValue(dt, 0.0f, 0.06f);
    _lastUpdateMs = nowMs;

    const bool p1Pump = (_currentPlayer == 1) && input.btn1DownEdge;
    const bool p2Pump = (_currentPlayer == 2) && input.btn2DownEdge;
    if (p1Pump || p2Pump)
    {
      const float wobble = static_cast<float>(random(-8, 9)) * 0.12f;
      _balloonSize += _pumpAmount + wobble;
      _pumpAnimUntilMs = nowMs + 180;
      ++_turnCount;
      ++_turnPumpCount;

      if (_balloonSize >= _burstAt)
      {
        finish(nowMs, true, (_currentPlayer == 1) ? 2 : 1);
        return;
      }

      if (_turnPumpCount >= kPumpsPerTurn)
      {
        switchTurn();
      }
    }
  }

  void renderTft(Adafruit_ST7735 &display) override
  {
    display.fillScreen(color565(12, 22, 28));
    display.fillRect(0, 0, hw::tftWidth, 14, COLOR_PANEL);
    display.setTextColor(COLOR_TEXT);
    display.setCursor(4, 3);
    display.print("Balloon Battle");
    display.setCursor(112, 3);
    display.print(kPongDifficultyNames[_difficulty]);

    drawBalloon(display, 80, 54, _balloonSize, color565(255, 126, 166), _balloonSize / _burstAt);
    display.drawLine(80, 74, 80, 92, color565(236, 236, 242));
    drawPump(display, 80, 102, _pumpAnimUntilMs > millis(), _currentPlayer);

    display.setTextColor(COLOR_TEXT);
    display.setCursor(12, 90);
    display.print("Turn: B");
    display.print(_currentPlayer);
    display.setCursor(82, 90);
    display.print("This:");
    display.print(_turnPumpCount);
    display.print("/");
    display.print(kPumpsPerTurn);
    display.setCursor(16, 108);
    display.print("Size:");
    display.print(static_cast<int>(_balloonSize));
    display.setCursor(92, 108);
    display.print("All:");
    display.print(_turnCount);

    display.setTextColor(COLOR_DIM);
    display.setCursor(8, kFooterY);
    display.print("3 pumps then swap");
  }

  void renderOledOverlay(Adafruit_SSD1306 &display, const SensorSnapshot &sensors) override
  {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print("BALLOON");
    display.setCursor(0, 12);
    display.print("Turn:B");
    display.print(_currentPlayer);
    display.setCursor(54, 12);
    display.print("This:");
    display.print(_turnPumpCount);
    display.print("/");
    display.print(kPumpsPerTurn);
    display.setCursor(0, 24);
    display.print("Size:");
    display.print(static_cast<int>(_balloonSize));
    display.setCursor(54, 24);
    display.print("Risk:");
    display.print(clampValue(static_cast<int>((_balloonSize / _burstAt) * 100.0f), 0, 99));
    display.print("%");
    display.setCursor(0, 36);
    display.print("All:");
    display.print(_turnCount);
    display.setCursor(54, 36);
    display.print("L:");
    display.print(sensors.lightPct);
    display.print("%");
    display.display();
  }

  bool isFinished() const override
  {
    return _finished;
  }

  GameResult result() const override
  {
    GameResult result;
    result.gameId = id();
    result.difficulty = _difficulty;
    result.durationMs = _durationMs;
    result.completed = _completed;
    result.winner = _winner;
    result.score = static_cast<uint32_t>(_turnCount * 180.0f) + (_winner != 0 ? 900 : 0) + _difficulty * 140;
    return result;
  }

private:
  static void drawBalloon(Adafruit_ST7735 &display, int cx, int cy, float size, uint16_t color, float risk)
  {
    const int radius = clampValue(static_cast<int>(size * 0.5f), 8, 28);
    const uint16_t shine = (risk > 0.8f) ? COLOR_WARN : ST77XX_WHITE;
    display.fillCircle(cx, cy, radius, color);
    display.fillTriangle(cx - 4, cy + radius - 1, cx + 4, cy + radius - 1, cx, cy + radius + 6, color);
    display.fillCircle(cx - radius / 3, cy - radius / 3, max(2, radius / 5), shine);
  }

  static void drawPump(Adafruit_ST7735 &display, int cx, int baseY, bool pressed, uint8_t activePlayer)
  {
    const uint16_t pumpBody = color565(62, 86, 104);
    const uint16_t hoseColor = color565(226, 198, 92);
    const uint16_t handleColor = (activePlayer == 1) ? color565(82, 168, 255) : color565(255, 132, 90);
    const int handleY = pressed ? (baseY - 18) : (baseY - 26);

    display.fillRoundRect(cx - 12, baseY - 10, 24, 12, 3, pumpBody);
    display.drawRoundRect(cx - 12, baseY - 10, 24, 12, 3, COLOR_TEXT);
    display.drawFastVLine(cx, baseY - 30, 20, COLOR_TEXT);
    display.fillRoundRect(cx - 14, handleY, 28, 4, 2, handleColor);
    display.drawLine(cx + 12, baseY - 8, cx + 26, baseY - 20, hoseColor);
    display.drawLine(cx + 26, baseY - 20, cx + 12, baseY - 32, hoseColor);
  }

  void switchTurn()
  {
    _currentPlayer = (_currentPlayer == 1) ? 2 : 1;
    _turnPumpCount = 0;
  }

  void finish(uint32_t nowMs, bool completed, uint8_t winner)
  {
    _finished = true;
    _completed = completed;
    _winner = winner;
    _durationMs = nowMs - _startMs;
  }

  bool _finished = false;
  bool _completed = false;
  static constexpr uint8_t kPumpsPerTurn = 3;
  uint8_t _difficulty = 0;
  uint8_t _winner = 0;
  uint8_t _currentPlayer = 1;
  uint32_t _startMs = 0;
  uint32_t _lastUpdateMs = 0;
  uint32_t _durationMs = 0;
  uint32_t _pumpAnimUntilMs = 0;
  uint32_t _turnCount = 0;
  uint8_t _turnPumpCount = 0;
  float _balloonSize = 12.0f;
  float _burstAt = 58.0f;
  float _pumpAmount = 4.8f;
};

class DuckHuntGame : public IGame
{
public:
  GameId id() const override
  {
    return GameId::DuckHunt;
  }

  void enter(uint8_t difficulty, uint32_t nowMs) override
  {
    _difficulty = clampValue<uint8_t>(difficulty, 0, 2);
    _finished = false;
    _completed = false;
    _startMs = nowMs;
    _lastUpdateMs = nowMs;
    _durationMs = 0;
    _aimLane = 1;
    _shots = 0;
    _hits = 0;
    _misses = 0;
    _duckSpeed = (_difficulty == 0) ? 34.0f : (_difficulty == 1) ? 48.0f
                                                                 : 62.0f;
    _spawnGapMs = (_difficulty == 0) ? 1100 : (_difficulty == 1) ? 850
                                                                  : 650;
    _hitWindow = (_difficulty == 0) ? 16.0f : (_difficulty == 1) ? 12.0f
                                                                 : 9.0f;
    _targetHits = (_difficulty == 0) ? 8 : (_difficulty == 1) ? 10
                                                               : 12;
    _lastSpawnMs = nowMs;
    for (uint8_t i = 0; i < kDuckCount; ++i)
    {
      _ducks[i] = {};
    }
  }

  void update(uint32_t nowMs, const InputSnapshot &input, const SensorSnapshot &) override
  {
    if (_finished)
    {
      return;
    }

    float dt = (nowMs - _lastUpdateMs) / 1000.0f;
    dt = clampValue(dt, 0.0f, 0.06f);
    _lastUpdateMs = nowMs;

    if (input.btn1DownEdge && _aimLane > 0)
    {
      --_aimLane;
    }
    if (input.btn2DownEdge && _aimLane < 2)
    {
      ++_aimLane;
    }

    if ((nowMs - _lastSpawnMs) >= _spawnGapMs)
    {
      _lastSpawnMs = nowMs;
      spawnDuck();
    }

    for (uint8_t i = 0; i < kDuckCount; ++i)
    {
      if (!_ducks[i].active)
      {
        continue;
      }
      _ducks[i].x += _ducks[i].vx * dt;
      if (_ducks[i].x < -18.0f || _ducks[i].x > (hw::tftWidth + 18.0f))
      {
        _ducks[i].active = false;
        ++_misses;
        if (_misses >= 5)
        {
          finish(nowMs, false);
          return;
        }
      }
    }

    const bool shootNow = (input.btn1DownEdge && input.btn2Down) ||
                          (input.btn2DownEdge && input.btn1Down) ||
                          (input.btn1DownEdge && input.btn2DownEdge);
    if (shootNow)
    {
      ++_shots;
      bool hit = false;
      for (uint8_t i = 0; i < kDuckCount; ++i)
      {
        if (!_ducks[i].active || _ducks[i].lane != _aimLane)
        {
          continue;
        }
        if (fabsf(_ducks[i].x - kSightX) <= _hitWindow)
        {
          _ducks[i].active = false;
          ++_hits;
          hit = true;
          break;
        }
      }
      if (!hit)
      {
        ++_misses;
      }

      if (_hits >= _targetHits)
      {
        finish(nowMs, true);
        return;
      }
      if (_misses >= 5)
      {
        finish(nowMs, false);
      }
    }
  }

  void renderTft(Adafruit_ST7735 &display) override
  {
    display.fillScreen(color565(78, 170, 220));
    display.fillRect(0, 0, hw::tftWidth, 14, COLOR_PANEL);
    display.setTextColor(COLOR_TEXT);
    display.setCursor(4, 3);
    display.print("Duck Hunt");
    display.setCursor(112, 3);
    display.print(_hits);

    for (uint8_t lane = 0; lane < 3; ++lane)
    {
      const int y = laneY(lane);
      display.drawFastHLine(8, y + 8, hw::tftWidth - 16, color565(206, 236, 248));
    }
    display.fillRect(0, 98, hw::tftWidth, 30, color565(56, 138, 82));

    for (uint8_t i = 0; i < kDuckCount; ++i)
    {
      if (_ducks[i].active)
      {
        drawDuck(display, static_cast<int>(_ducks[i].x), laneY(_ducks[i].lane), _ducks[i].vx > 0.0f);
      }
    }

    const int sightY = laneY(_aimLane) + 4;
    display.drawFastVLine(kSightX, sightY - 8, 16, COLOR_DANGER);
    display.drawFastHLine(kSightX - 8, sightY, 16, COLOR_DANGER);
    display.drawCircle(kSightX, sightY, 10, COLOR_TEXT);

    display.setTextColor(COLOR_TEXT);
    display.setCursor(8, 108);
    display.print("H:");
    display.print(_hits);
    display.setCursor(50, 108);
    display.print("M:");
    display.print(_misses);
    display.setCursor(92, 108);
    display.print("Ln:");
    display.print(_aimLane + 1);

    display.setTextColor(COLOR_DIM);
    display.setCursor(8, kFooterY);
    display.print("B1/B2 aim  both fire");
  }

  void renderOledOverlay(Adafruit_SSD1306 &display, const SensorSnapshot &sensors) override
  {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print("DUCK HUNT");
    display.setCursor(0, 12);
    display.print("Hit:");
    display.print(_hits);
    display.setCursor(64, 12);
    display.print("Miss:");
    display.print(_misses);
    display.setCursor(0, 24);
    display.print("Lane:");
    display.print(_aimLane + 1);
    display.setCursor(64, 24);
    display.print("D:");
    display.print(kPongDifficultyNames[_difficulty]);
    display.setCursor(0, 36);
    display.print("L:");
    display.print(sensors.lightPct);
    display.print("%");
    display.display();
  }

  bool isFinished() const override
  {
    return _finished;
  }

  GameResult result() const override
  {
    GameResult result;
    result.gameId = id();
    result.difficulty = _difficulty;
    result.durationMs = _durationMs;
    result.completed = _completed;
    result.score = _hits * 150 + (_completed ? 600 : 0) + _difficulty * 120;
    return result;
  }

private:
  struct Duck
  {
    bool active = false;
    uint8_t lane = 0;
    float x = 0.0f;
    float vx = 0.0f;
  };

  static constexpr uint8_t kDuckCount = 4;
  static constexpr float kSightX = 80.0f;

  static int laneY(uint8_t lane)
  {
    return 26 + static_cast<int>(lane) * 22;
  }

  void spawnDuck()
  {
    for (uint8_t i = 0; i < kDuckCount; ++i)
    {
      if (_ducks[i].active)
      {
        continue;
      }

      _ducks[i].active = true;
      _ducks[i].lane = random(0, 3);
      const bool fromLeft = random(0, 2) == 0;
      _ducks[i].x = fromLeft ? -12.0f : (hw::tftWidth + 12.0f);
      _ducks[i].vx = fromLeft ? _duckSpeed : -_duckSpeed;
      return;
    }
  }

  static void drawDuck(Adafruit_ST7735 &display, int x, int y, bool facingRight)
  {
    const uint16_t body = color565(255, 214, 78);
    const uint16_t beak = color565(248, 124, 46);
    const uint16_t wing = color565(228, 170, 32);
    const int dir = facingRight ? 1 : -1;
    display.fillCircle(x, y, 5, body);
    display.fillCircle(x - dir * 4, y - 4, 3, body);
    display.fillCircle(x + dir * 5, y, 2, beak);
    display.fillTriangle(x - 2, y + 1, x - 7, y + 6, x + 1, y + 5, wing);
  }

  void finish(uint32_t nowMs, bool completed)
  {
    _finished = true;
    _completed = completed;
    _durationMs = nowMs - _startMs;
  }

  Duck _ducks[kDuckCount];
  bool _finished = false;
  bool _completed = false;
  uint8_t _difficulty = 0;
  uint8_t _aimLane = 1;
  uint32_t _startMs = 0;
  uint32_t _lastUpdateMs = 0;
  uint32_t _durationMs = 0;
  uint32_t _lastSpawnMs = 0;
  uint32_t _spawnGapMs = 900;
  uint32_t _shots = 0;
  uint32_t _hits = 0;
  uint32_t _misses = 0;
  uint8_t _targetHits = 10;
  float _duckSpeed = 46.0f;
  float _hitWindow = 12.0f;
};

InputService inputService;
StorageService storageService;
SensorService sensorService;
RgbService rgbService;
BreakoutGame breakoutGame;
PongGame pongGame;
FlappyBirdGame flappyBirdGame;
PoleClimbGame poleClimbGame;
BalloonBattleGame balloonBattleGame;
DuckHuntGame duckHuntGame;
RacingGame racingGame;
QuickDrawGame quickDrawGame;

AppState appState = AppState::Boot;
uint32_t bootStartedMs = 0;
uint32_t lastRenderMs = 0;
uint32_t passwordErrorUntilMs = 0;
uint32_t selfTestGreenUntilMs = 0;
uint32_t gamesToastUntilMs = 0;
bool gamesShowComingSoon = false;

bool oledReady = false;
bool pcaReady = false;
bool tftReady = false;
ToastState uiToast;

PasswordInputState passwordState;
SelfTestReport selfTestReport;
InputSnapshot latestInputSnapshot;
bool selfTestBtn1Seen = false;
bool selfTestBtn2Seen = false;
bool selfTestInitialized = false;
bool selfTestPassLatched = false;
uint32_t selfTestStartedMs = 0;

uint8_t mainMenuIndex = 0;
uint8_t gamesMenuIndex = 0;
uint8_t selectedLaunchDifficulty[static_cast<uint8_t>(GameId::Count)] = {};
uint8_t selectedLeaderboardDifficulty[static_cast<uint8_t>(GameId::Count)] = {};
bool gamesDifficultyMode = false;
uint8_t leaderboardIndex = 0;

IGame *activeGame = nullptr;
GameResult activeGameResult;
int8_t activeGameRank = -1;
uint8_t resultActionIndex = 0;

bool addressResponds(uint8_t address);
const char *buttonPressLevelLabel(ButtonWiringType wiring);
const char *pinLevelLabel(bool rawHigh);
float adcToVoltage(int raw, float reference = 3.3f);
const char *rangeStatusLabel(float value, float low, float high);
uint16_t rangeStatusColor(float value, float low, float high);
const GameDescriptor &descriptorFor(GameId gameId);
IGame *gameForId(GameId gameId);
void resetPasswordState();
void changeState(AppState newState, uint32_t nowMs);
void showToast(const char *message, uint16_t color, uint32_t durationMs = kToastMs);
void updateRgb(uint32_t nowMs);
void runSelfTestStart(uint32_t nowMs);
bool selfTestPassed();
void startGame(GameId gameId, uint8_t difficulty, uint32_t nowMs);
void exitToMainMenu();
void finalizeGameResult();
void handlePasswordInput(const InputSnapshot &input);
void handleMainMenu(const InputSnapshot &input);
void handleSensorMonitor(const InputSnapshot &input);
void handleSelfTest(const InputSnapshot &input, uint32_t nowMs);
void handleGamesMenu(const InputSnapshot &input, uint32_t nowMs);
void handleLeaderboard(const InputSnapshot &input);
void handleGameResult(const InputSnapshot &input, uint32_t nowMs);
void drawSensorSummaryOled(const SensorSnapshot &sensors, const char *headline);
void drawGameSensorOverlay(Adafruit_SSD1306 &display, const char *headline, uint32_t primaryValue, const char *primaryLabel, const SensorSnapshot &sensors);
void renderBoot(uint32_t nowMs);
void renderLockScreen();
void renderMainMenu();
void renderSensorMonitor();
void renderSelfTest();
void renderGamesMenu();
void renderLeaderboards();
void renderGameResult();
void renderGameFrame();
void renderToastOverlay(uint32_t nowMs);
void renderFrame(uint32_t nowMs);

bool addressResponds(uint8_t address)
{
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

const char *buttonPressLevelLabel(ButtonWiringType wiring)
{
  switch (wiring)
  {
  case ButtonWiringType::AutoByIdle:
    return "AUTO";
  case ButtonWiringType::PullDownActiveHigh:
    return "HIGH";
  case ButtonWiringType::PullUpActiveLow:
  default:
    return "LOW";
  }
}

const char *pinLevelLabel(bool rawHigh)
{
  return rawHigh ? "HIGH" : "LOW";
}

float adcToVoltage(int raw, float reference)
{
  return (static_cast<float>(clampValue(raw, 0, 4095)) / 4095.0f) * reference;
}

const char *rangeStatusLabel(float value, float low, float high)
{
  if (isnan(value))
  {
    return "FAIL";
  }
  if (value < low)
  {
    return "LOW";
  }
  if (value > high)
  {
    return "HIGH";
  }
  return "OK";
}

uint16_t rangeStatusColor(float value, float low, float high)
{
  if (isnan(value))
  {
    return COLOR_DANGER;
  }
  if (value < low || value > high)
  {
    return COLOR_WARN;
  }
  return COLOR_GOOD;
}

const GameDescriptor &descriptorFor(GameId gameId)
{
  return kGameDescriptors[static_cast<uint8_t>(gameId)];
}

IGame *gameForId(GameId gameId)
{
  switch (gameId)
  {
  case GameId::Breakout:
    return &breakoutGame;
  case GameId::Pong:
    return &pongGame;
  case GameId::FlappyBird:
    return &flappyBirdGame;
  case GameId::BalloonBattle:
    return &balloonBattleGame;
  case GameId::PoleClimb:
    return &poleClimbGame;
  case GameId::Racing:
    return &racingGame;
  case GameId::DuckHunt:
    return &duckHuntGame;
  case GameId::QuickDraw:
    return &quickDrawGame;
  default:
    return nullptr;
  }
}

void resetPasswordState()
{
  passwordState = {};
}

void changeState(AppState newState, uint32_t nowMs)
{
  appState = newState;
  inputService.suppressUntilRelease(nowMs);
}

void drawCenteredText(Adafruit_GFX &display, int16_t centerY, const char *text, uint16_t color, uint8_t textSize)
{
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t w = 0;
  uint16_t h = 0;
  display.setTextSize(textSize);
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display.setTextColor(color);
  display.setCursor((display.width() - static_cast<int16_t>(w)) / 2, centerY - (static_cast<int16_t>(h) / 2));
  display.print(text);
}

void showToast(const char *message, uint16_t color, uint32_t durationMs)
{
  strncpy(uiToast.message, message, sizeof(uiToast.message) - 1);
  uiToast.message[sizeof(uiToast.message) - 1] = '\0';
  uiToast.color = color;
  uiToast.untilMs = millis() + durationMs;
  uiToast.active = true;
}

void updateRgb(uint32_t nowMs)
{
  if (!pcaReady)
  {
    return;
  }

  if (appState == AppState::GameRunning)
  {
    rgbService.setColor(0, 0, 0);
    return;
  }

  if (selfTestGreenUntilMs > nowMs)
  {
    rgbService.setColor(0, 3500, 0);
    return;
  }

  if (passwordErrorUntilMs > nowMs)
  {
    const bool on = ((nowMs / 180) % 2) == 0;
    rgbService.setColor(on ? 4095 : 0, 0, 0);
    return;
  }

  if (appState == AppState::Boot)
  {
    const uint16_t level = static_cast<uint16_t>((sinf(nowMs / 180.0f) * 0.5f + 0.5f) * 3000.0f) + 400;
    rgbService.setColor(0, 0, level);
    return;
  }

  if (appState == AppState::LockScreen)
  {
    rgbService.setColor(2600, 1800, 0);
    return;
  }

  if (appState == AppState::SelfTest)
  {
    if (kBypassPasswordForButtonTest)
    {
      if (selfTestReport.buttonsOk)
      {
        rgbService.setColor(0, 2800, 0);
      }
      else
      {
        rgbService.setColor(0, 0, 1800);
      }
      return;
    }
    const bool failure = !selfTestReport.oledOk || !selfTestReport.pcaOk || !selfTestReport.dhtOk;
    if (failure)
    {
      const bool on = ((nowMs / 220) % 2) == 0;
      rgbService.setColor(on ? 4095 : 0, 0, 0);
      return;
    }
  }

  rgbService.setColor(0, 0, 0);
}

void runSelfTestStart(uint32_t nowMs)
{
  selfTestInitialized = true;
  selfTestStartedMs = nowMs;
  selfTestBtn1Seen = false;
  selfTestBtn2Seen = false;
  selfTestReport = {};
  selfTestReport.tftOk = tftReady;
  selfTestReport.oledOk = addressResponds(Hardware.oledAddr);
  selfTestReport.pcaOk = addressResponds(Hardware.pcaAddr);

  uint8_t extremeCount = 0;
  for (uint8_t i = 0; i < 5; ++i)
  {
    const int sample = analogRead(Hardware.ldrPin);
    if (sample == 0 || sample == 4095)
    {
      ++extremeCount;
    }
    delay(5);
  }
  selfTestReport.ldrState = (extremeCount == 5) ? LdrState::Warning : LdrState::Ok;
}

bool selfTestPassed()
{
  return selfTestReport.tftOk && selfTestReport.oledOk && selfTestReport.pcaOk && selfTestReport.dhtOk && selfTestReport.buttonsOk;
}

void startGame(GameId gameId, uint8_t difficulty, uint32_t nowMs)
{
  activeGame = gameForId(gameId);
  if (activeGame == nullptr)
  {
    return;
  }

  activeGameRank = -1;
  activeGame->enter(difficulty, nowMs);
  resultActionIndex = 0;
  changeState(AppState::GameRunning, nowMs);
}

void exitToMainMenu()
{
  changeState(AppState::MainMenu, millis());
  activeGame = nullptr;
  gamesDifficultyMode = false;
}

void finalizeGameResult()
{
  if (activeGame == nullptr)
  {
    return;
  }

  activeGameResult = activeGame->result();
  activeGameRank = storageService.submitResult(activeGameResult);
  activeGame = nullptr;
  resultActionIndex = 0;
  changeState(AppState::GameResult, millis());
}

void handlePasswordInput(const InputSnapshot &input)
{
  if (passwordErrorUntilMs != 0)
  {
    if (millis() > passwordErrorUntilMs)
    {
      passwordErrorUntilMs = 0;
      resetPasswordState();
    }
    return;
  }

  if (input.btn1Pressed)
  {
    passwordState.currentValue = (passwordState.currentValue + 1) % 10;
  }

  if (input.btn1Long)
  {
    if (passwordState.cursor > 0)
    {
      passwordState.cursor--;
      passwordState.filledCount = passwordState.cursor;
      passwordState.currentValue = (passwordState.digits[passwordState.cursor] >= 0) ? passwordState.digits[passwordState.cursor] : 0;
      passwordState.digits[passwordState.cursor] = -1;
    }
    return;
  }

  if (input.btn2Pressed)
  {
    passwordState.digits[passwordState.cursor] = passwordState.currentValue;
    passwordState.filledCount = min<uint8_t>(passwordState.cursor + 1, kPasswordLength);
    if (passwordState.cursor == (kPasswordLength - 1))
    {
      passwordState.isComplete = true;
      char entered[kPasswordLength + 1];
      for (uint8_t i = 0; i < kPasswordLength; ++i)
      {
        entered[i] = static_cast<char>('0' + passwordState.digits[i]);
      }
      entered[kPasswordLength] = '\0';

      if (storageService.password().equals(entered))
      {
        changeState(AppState::MainMenu, millis());
        resetPasswordState();
        showToast("Unlocked", COLOR_GOOD, 1200);
      }
      else
      {
        passwordErrorUntilMs = millis() + kErrorScreenMs;
      }
    }
    else
    {
      passwordState.cursor++;
      passwordState.currentValue = 0;
    }
  }
}

void handleMainMenu(const InputSnapshot &input)
{
  if (input.btn1DownEdge)
  {
    mainMenuIndex = (mainMenuIndex + 1) % 4;
  }
  if (input.btn2DownEdge)
  {
    switch (mainMenuIndex)
    {
    case 0:
      changeState(AppState::SensorMonitor, millis());
      break;
    case 1:
      changeState(AppState::GamesMenu, millis());
      break;
    case 2:
      changeState(AppState::Leaderboard, millis());
      break;
    case 3:
      changeState(AppState::SelfTest, millis());
      selfTestInitialized = false;
      selfTestPassLatched = false;
      break;
    }
  }
}

void handleSensorMonitor(const InputSnapshot &input)
{
  if (input.btn1DownEdge)
  {
    storageService.saveCalibration(sensorService.snapshot().lightRaw, storageService.brightRaw());
    sensorService.setCalibration(storageService.darkRaw(), storageService.brightRaw());
    char message[28];
    snprintf(message, sizeof(message), "Dark saved: %d", storageService.darkRaw());
    showToast(message, COLOR_WARN);
  }
  if (input.btn2DownEdge)
  {
    storageService.saveCalibration(storageService.darkRaw(), sensorService.snapshot().lightRaw);
    sensorService.setCalibration(storageService.darkRaw(), storageService.brightRaw());
    char message[28];
    snprintf(message, sizeof(message), "Bright saved: %d", storageService.brightRaw());
    showToast(message, COLOR_ACCENT);
  }
  if (input.btn1Long)
  {
    changeState(AppState::MainMenu, millis());
  }
}

void handleSelfTest(const InputSnapshot &input, uint32_t nowMs)
{
  if (!selfTestInitialized)
  {
    runSelfTestStart(nowMs);
  }

  if ((nowMs - selfTestStartedMs) > 200)
  {
    const bool btn1SeenBefore = selfTestBtn1Seen;
    const bool btn2SeenBefore = selfTestBtn2Seen;
    if (input.btn1DownEdge || input.btn1Pressed || input.btn1Long)
    {
      selfTestBtn1Seen = true;
    }
    if (input.btn2DownEdge || input.btn2Pressed || input.btn2Long)
    {
      selfTestBtn2Seen = true;
    }
  }

  selfTestReport.buttonsOk = selfTestBtn1Seen && selfTestBtn2Seen;
  selfTestReport.dhtOk = sensorService.snapshot().dhtOk && ((nowMs - selfTestStartedMs) <= 2500 || sensorService.snapshot().dhtOk);

  if (kBypassPasswordForButtonTest)
  {
    if (input.btn1Long)
    {
      selfTestInitialized = false;
      selfTestPassLatched = false;
      showToast("Button retest", COLOR_ACCENT);
      return;
    }

    if (input.btn2Long)
    {
      if (selfTestReport.buttonsOk)
      {
        showToast("Open games", COLOR_GOOD);
        changeState(AppState::GamesMenu, millis());
      }
      else
      {
        showToast("Need both buttons", COLOR_WARN);
      }
      return;
    }
  }

  if (selfTestPassed() && !selfTestPassLatched)
  {
    selfTestGreenUntilMs = nowMs + kGreenPulseMs;
    selfTestPassLatched = true;
    showToast("Self-test pass", COLOR_GOOD);
  }

  if (input.btn1Long)
  {
    changeState(AppState::MainMenu, millis());
  }
  if (input.btn2Long)
  {
    selfTestInitialized = false;
    selfTestPassLatched = false;
  }
}

void handleGamesMenu(const InputSnapshot &input, uint32_t nowMs)
{
  const GameDescriptor &current = kGameDescriptors[gamesMenuIndex];

  if (gamesDifficultyMode)
  {
    if (input.btn1DownEdge)
    {
      selectedLaunchDifficulty[gamesMenuIndex] = (selectedLaunchDifficulty[gamesMenuIndex] + 1) % current.difficultyCount;
    }
    if (input.btn1Long)
    {
      gamesDifficultyMode = false;
    }
    if (input.btn2DownEdge)
    {
      gamesDifficultyMode = false;
      startGame(current.id, selectedLaunchDifficulty[gamesMenuIndex], nowMs);
    }
    return;
  }

  if (input.btn1DownEdge)
  {
    gamesMenuIndex = (gamesMenuIndex + 1) % static_cast<uint8_t>(GameId::Count);
  }
  if (input.btn1Long)
  {
    changeState(AppState::MainMenu, millis());
  }
  if (input.btn2DownEdge)
  {
    const GameDescriptor &selected = kGameDescriptors[gamesMenuIndex];
    if (!selected.implemented)
    {
      gamesShowComingSoon = true;
      gamesToastUntilMs = nowMs + kToastMs;
      showToast("Coming Soon", COLOR_WARN);
      return;
    }

    if (selected.difficultyCount > 1)
    {
      gamesDifficultyMode = true;
      return;
    }
    startGame(selected.id, 0, nowMs);
  }
}

void handleLeaderboard(const InputSnapshot &input)
{
  const GameDescriptor &selected = kGameDescriptors[leaderboardIndex];
  if (input.btn1DownEdge)
  {
    leaderboardIndex = (leaderboardIndex + 1) % static_cast<uint8_t>(GameId::Count);
  }
  if (input.btn1Long)
  {
    changeState(AppState::MainMenu, millis());
  }
  if (input.btn2DownEdge && selected.difficultyCount > 1)
  {
    selectedLeaderboardDifficulty[leaderboardIndex] = (selectedLeaderboardDifficulty[leaderboardIndex] + 1) % selected.difficultyCount;
  }
}

void handleGameResult(const InputSnapshot &input, uint32_t nowMs)
{
  if (input.btn1DownEdge)
  {
    resultActionIndex = (resultActionIndex == 0) ? 1 : 0;
  }
  if (input.btn1Long)
  {
    changeState(AppState::MainMenu, millis());
  }
  if (input.btn2DownEdge)
  {
    if (resultActionIndex == 0)
    {
      startGame(activeGameResult.gameId, activeGameResult.difficulty, nowMs);
    }
    else
    {
      changeState(AppState::MainMenu, millis());
    }
  }
}

void drawSensorSummaryOled(const SensorSnapshot &sensors, const char *headline)
{
  if (!oledReady)
  {
    return;
  }

  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.print(headline);
  oled.setCursor(0, 14);
  oled.print("T:");
  if (sensors.dhtOk)
  {
    oled.print(static_cast<int>(sensors.temperatureC));
    oled.print("C H:");
    oled.print(static_cast<int>(sensors.humidityPct));
    oled.print("%");
  }
  else
  {
    oled.print("--");
  }
  oled.setCursor(0, 28);
  oled.print("Light:");
  oled.print(sensors.lightPct);
  oled.print("%");
  oled.setCursor(0, 42);
  oled.print("Raw:");
  oled.print(sensors.lightRaw);
  oled.display();
}

void drawGameSensorOverlay(Adafruit_SSD1306 &display, const char *headline, uint32_t primaryValue, const char *primaryLabel, const SensorSnapshot &sensors)
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(headline);
  display.setCursor(0, 12);
  display.print(primaryLabel);
  display.print(primaryValue);
  display.setCursor(0, 24);
  display.print("T:");
  if (sensors.dhtOk)
  {
    display.print(static_cast<int>(sensors.temperatureC));
    display.print("C H:");
    display.print(static_cast<int>(sensors.humidityPct));
    display.print("%");
  }
  else
  {
    display.print("-- H:--");
  }
  display.setCursor(0, 36);
  display.print("L:");
  display.print(sensors.lightPct);
  display.print("%");
  display.display();
}

void renderBoot(uint32_t nowMs)
{
  tft.fillScreen(COLOR_BG);
  const int barWidth = static_cast<int>((static_cast<float>(hw::tftWidth - 24) * min<uint32_t>(nowMs - bootStartedMs, kBootDurationMs)) / kBootDurationMs);
  drawCenteredText(tft, 34, "REC HANDHELD", COLOR_TEXT, 2);
  drawCenteredText(tft, 62, "ESP32 BOOT", COLOR_DIM, 1);
  tft.drawRoundRect(12, 84, hw::tftWidth - 24, 12, 5, COLOR_DIM);
  tft.fillRoundRect(14, 86, max(0, barWidth), 8, 4, COLOR_ACCENT);
  tft.fillCircle(26, 32, 8, COLOR_WARN);
  tft.fillCircle(hw::tftWidth - 26, 32, 8, COLOR_GOOD);

  drawSensorSummaryOled(sensorService.snapshot(), "BOOT");
}

void renderLockScreen()
{
  tft.fillScreen(COLOR_BG);
  drawCenteredText(tft, 14, "LOCK SCREEN", COLOR_WARN, 1);

  if (passwordErrorUntilMs > millis())
  {
    drawCenteredText(tft, 34, "ERROR", COLOR_DANGER, 2);
    drawCenteredText(tft, 60, "Wrong password", COLOR_TEXT, 1);
    drawCenteredText(tft, 86, "Try again", COLOR_DIM, 1);
  }
  else
  {
    drawCenteredText(tft, 30, "Enter 4-digit code", COLOR_TEXT, 1);
    for (uint8_t i = 0; i < kPasswordLength; ++i)
    {
      const int x = 18 + i * 34;
      const bool active = i == passwordState.cursor;
      tft.drawRoundRect(x, 48, 24, 28, 4, active ? COLOR_ACCENT : COLOR_DIM);
      if (i < passwordState.filledCount)
      {
        tft.setTextColor(COLOR_TEXT);
        tft.setTextSize(2);
        tft.setCursor(x + 7, 56);
        tft.print(passwordState.digits[i]);
      }
      else if (active)
      {
        tft.setTextColor(COLOR_WARN);
        tft.setTextSize(2);
        tft.setCursor(x + 7, 56);
        tft.print(passwordState.currentValue);
      }
      else
      {
        tft.drawFastHLine(x + 7, 66, 10, COLOR_DIM);
      }
    }
    tft.setTextSize(1);
    tft.setTextColor(COLOR_DIM);
    tft.setCursor(12, 98);
    tft.print("B1:+1  B2:OK  HOLD B1:DEL");
  }

  drawSensorSummaryOled(sensorService.snapshot(), "LOCK");
}

void renderMainMenu()
{
  tft.fillScreen(COLOR_BG);
  drawCenteredText(tft, 10, "MAIN MENU", COLOR_TEXT, 1);
  for (uint8_t i = 0; i < 4; ++i)
  {
    const int y = 26 + i * 22;
    const bool active = i == mainMenuIndex;
    tft.fillRoundRect(14, y, hw::tftWidth - 28, 16, 4, active ? COLOR_PANEL : COLOR_BG);
    tft.drawRoundRect(14, y, hw::tftWidth - 28, 16, 4, active ? COLOR_ACCENT : COLOR_DIM);
    tft.setTextColor(active ? COLOR_TEXT : COLOR_DIM);
    tft.setCursor(24, y + 4);
    tft.print(kMainMenuItems[i]);
  }
  tft.setTextColor(COLOR_DIM);
  tft.setCursor(22, kFooterY);
  tft.print("B1 next  B2 OK");

  drawSensorSummaryOled(sensorService.snapshot(), "MENU");
}

void renderSensorMonitor()
{
  const SensorSnapshot &sensors = sensorService.snapshot();
  tft.fillScreen(COLOR_BG);
  drawCenteredText(tft, 10, "SENSOR MONITOR", COLOR_TEXT, 1);

  tft.fillRoundRect(10, 22, 140, 28, 4, COLOR_PANEL);
  tft.setCursor(16, 30);
  tft.setTextColor(COLOR_TEXT);
  tft.print("Temp: ");
  if (sensors.dhtOk)
  {
    tft.print(static_cast<int>(sensors.temperatureC));
    tft.print(" C");
  }
  else
  {
    tft.print("--");
  }

  tft.fillRoundRect(10, 54, 140, 28, 4, COLOR_PANEL);
  tft.setCursor(16, 62);
  tft.print("Humidity: ");
  if (sensors.dhtOk)
  {
    tft.print(static_cast<int>(sensors.humidityPct));
    tft.print(" %");
  }
  else
  {
    tft.print("--");
  }

  tft.fillRoundRect(10, 86, 140, 28, 4, COLOR_PANEL);
  tft.setCursor(16, 94);
  tft.print("Light: ");
  tft.print(sensors.lightPct);
  tft.print("%  Raw:");
  tft.print(sensors.lightRaw);

  tft.setTextColor(COLOR_DIM);
  tft.setCursor(10, 104);
  tft.print("D:");
  tft.print(storageService.darkRaw());
  tft.print(" B:");
  tft.print(storageService.brightRaw());

  tft.setTextColor(COLOR_DIM);
  tft.setCursor(14, kFooterY);
  tft.print("B1 dark  B2 bright");

  drawSensorSummaryOled(sensors, "SENSORS");
}

void renderSelfTest()
{
  if (kBypassPasswordForButtonTest)
  {
    tft.fillScreen(COLOR_BG);
    drawCenteredText(tft, 10, "BUTTON TEST", COLOR_TEXT, 1);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT);

    tft.setCursor(8, 28);
    tft.print("M1:");
    tft.print(buttonPressLevelLabel(kBtn1Wiring));
    tft.setCursor(88, 28);
    tft.print("M2:");
    tft.print(buttonPressLevelLabel(kBtn2Wiring));

    tft.setCursor(8, 44);
    tft.print("P1:");
    tft.print(pinLevelLabel(latestInputSnapshot.btn1RawHigh));
    tft.setCursor(88, 44);
    tft.print("P2:");
    tft.print(pinLevelLabel(latestInputSnapshot.btn2RawHigh));

    tft.setCursor(8, 60);
    tft.print("S1:");
    tft.print(latestInputSnapshot.btn1Down ? "DOWN" : "UP  ");
    tft.setCursor(88, 60);
    tft.print("S2:");
    tft.print(latestInputSnapshot.btn2Down ? "DOWN" : "UP");

    tft.setCursor(8, 76);
    tft.print("K1:");
    tft.print(selfTestBtn1Seen ? "OK" : "..");
    tft.setCursor(88, 76);
    tft.print("K2:");
    tft.print(selfTestBtn2Seen ? "OK" : "..");

    drawCenteredText(tft, 98, selfTestReport.buttonsOk ? "BUTTONS READY" : "PRESS BOTH", selfTestReport.buttonsOk ? COLOR_GOOD : COLOR_WARN, 1);

    tft.setTextColor(selfTestReport.buttonsOk ? COLOR_GOOD : COLOR_WARN);
    tft.setTextColor(COLOR_DIM);
    drawCenteredText(tft, 116, selfTestReport.buttonsOk ? "H1 RETEST  H2 GAME" : "CHECK M/P/S/K", COLOR_DIM, 1);

    if (oledReady)
    {
      oled.clearDisplay();
      oled.setTextSize(1);
      oled.setTextColor(SSD1306_WHITE);
      oled.setCursor(0, 0);
      oled.print("BUTTON TEST");
      oled.setCursor(0, 14);
      oled.print("M1:");
      oled.print(buttonPressLevelLabel(kBtn1Wiring));
      oled.print(" M2:");
      oled.print(buttonPressLevelLabel(kBtn2Wiring));
      oled.setCursor(0, 28);
      oled.print("P1:");
      oled.print(pinLevelLabel(latestInputSnapshot.btn1RawHigh)[0]);
      oled.print(" P2:");
      oled.print(pinLevelLabel(latestInputSnapshot.btn2RawHigh)[0]);
      oled.setCursor(0, 42);
      oled.print("S1:");
      oled.print(latestInputSnapshot.btn1Down ? "D" : "U");
      oled.print(" S2:");
      oled.print(latestInputSnapshot.btn2Down ? "D" : "U");
      oled.setCursor(0, 54);
      oled.print(selfTestReport.buttonsOk ? "H2 GAME" : "PRESS BOTH");
      oled.display();
    }
    return;
  }

  const SensorSnapshot &sensors = sensorService.snapshot();
  const float lightVoltage = adcToVoltage(sensors.lightRaw);
  const char *tempStatus = sensors.dhtOk ? rangeStatusLabel(sensors.temperatureC, 15.0f, 35.0f) : "FAIL";
  const char *humidityStatus = sensors.dhtOk ? rangeStatusLabel(sensors.humidityPct, 30.0f, 85.0f) : "FAIL";
  const char *lightStatus = (sensors.lightPct < 20) ? "LOW" : ((sensors.lightPct > 95) ? "HIGH" : "OK");
  const uint16_t tempColor = sensors.dhtOk ? rangeStatusColor(sensors.temperatureC, 15.0f, 35.0f) : COLOR_DANGER;
  const uint16_t humidityColor = sensors.dhtOk ? rangeStatusColor(sensors.humidityPct, 30.0f, 85.0f) : COLOR_DANGER;
  const uint16_t lightColor = (sensors.lightPct < 20 || sensors.lightPct > 95) ? COLOR_WARN : COLOR_GOOD;

  tft.fillScreen(COLOR_BG);
  drawCenteredText(tft, 10, "SELF-TEST", COLOR_TEXT, 1);
  tft.setTextSize(1);

  tft.setCursor(8, 24);
  tft.setTextColor(COLOR_TEXT);
  tft.print("TFT:");
  tft.setTextColor(selfTestReport.tftOk ? COLOR_GOOD : COLOR_DANGER);
  tft.print(selfTestReport.tftOk ? "OK" : "FAIL");
  tft.setCursor(64, 24);
  tft.setTextColor(COLOR_TEXT);
  tft.print("OLED:");
  tft.setTextColor(selfTestReport.oledOk ? COLOR_GOOD : COLOR_DANGER);
  tft.print(selfTestReport.oledOk ? "OK" : "FAIL");

  tft.setCursor(8, 36);
  tft.setTextColor(COLOR_TEXT);
  tft.print("PCA:");
  tft.setTextColor(selfTestReport.pcaOk ? COLOR_GOOD : COLOR_DANGER);
  tft.print(selfTestReport.pcaOk ? "OK" : "FAIL");
  tft.setCursor(64, 36);
  tft.setTextColor(COLOR_TEXT);
  tft.print("LDR:");
  tft.setTextColor(selfTestReport.ldrState == LdrState::Warning ? COLOR_WARN : COLOR_GOOD);
  tft.print(selfTestReport.ldrState == LdrState::Warning ? "WARN" : "OK");

  tft.setCursor(8, 50);
  tft.setTextColor(COLOR_TEXT);
  tft.print("T:");
  if (sensors.dhtOk)
  {
    tft.print(static_cast<int>(sensors.temperatureC));
    tft.print("C ");
  }
  else
  {
    tft.print("-- ");
  }
  tft.setTextColor(tempColor);
  tft.print(tempStatus);

  tft.setCursor(8, 62);
  tft.setTextColor(COLOR_TEXT);
  tft.print("H:");
  if (sensors.dhtOk)
  {
    tft.print(static_cast<int>(sensors.humidityPct));
    tft.print("% ");
  }
  else
  {
    tft.print("-- ");
  }
  tft.setTextColor(humidityColor);
  tft.print(humidityStatus);

  tft.setCursor(8, 74);
  tft.setTextColor(COLOR_TEXT);
  tft.print("L:");
  tft.print(sensors.lightPct);
  tft.print("% ");
  tft.setTextColor(lightColor);
  tft.print(lightStatus);

  tft.setCursor(8, 86);
  tft.setTextColor(COLOR_TEXT);
  tft.print("V:");
  tft.print(lightVoltage, 2);
  tft.print("V Raw:");
  tft.print(sensors.lightRaw);

  tft.setCursor(8, 98);
  tft.setTextColor(COLOR_TEXT);
  tft.print("B1 GPIO");
  tft.print(Hardware.btn1Pin);
  tft.print(":");
  tft.setTextColor(selfTestBtn1Seen ? COLOR_GOOD : COLOR_WARN);
  tft.print(selfTestBtn1Seen ? "OK" : "..");

  tft.setCursor(8, 108);
  tft.setTextColor(COLOR_TEXT);
  tft.print("B2 GPIO");
  tft.print(Hardware.btn2Pin);
  tft.print(":");
  tft.setTextColor(selfTestBtn2Seen ? COLOR_GOOD : COLOR_WARN);
  tft.print(selfTestBtn2Seen ? "OK" : "..");

  tft.setTextColor(COLOR_DIM);
  tft.setCursor(22, kFooterY);
  tft.print("H1 back  H2 rerun");

  if (oledReady)
  {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 0);
    oled.print("SELF-TEST");
    oled.setCursor(0, 14);
    oled.print("T:");
    if (sensors.dhtOk)
    {
      oled.print(static_cast<int>(sensors.temperatureC));
      oled.print(" ");
    }
    else
    {
      oled.print("-- ");
    }
    oled.print(tempStatus);
    oled.setCursor(64, 14);
    oled.print("H:");
    if (sensors.dhtOk)
    {
      oled.print(static_cast<int>(sensors.humidityPct));
      oled.print(" ");
    }
    else
    {
      oled.print("-- ");
    }
    oled.print(humidityStatus);
    oled.setCursor(0, 28);
    oled.print("L:");
    oled.print(sensors.lightPct);
    oled.print("% ");
    oled.print(lightStatus);
    oled.setCursor(64, 28);
    oled.print("V:");
    oled.print(lightVoltage, 1);
    oled.setCursor(0, 42);
    oled.print("B1:");
    oled.print(Hardware.btn1Pin);
    oled.print(selfTestBtn1Seen ? " OK" : " ..");
    oled.setCursor(64, 42);
    oled.print("B2:");
    oled.print(Hardware.btn2Pin);
    oled.print(selfTestBtn2Seen ? " OK" : " ..");
    oled.setCursor(0, 54);
    oled.print("DSP/I2C:");
    oled.print(selfTestReport.tftOk ? "T" : "x");
    oled.print(selfTestReport.oledOk ? "O" : "x");
    oled.print(selfTestReport.pcaOk ? "P" : "x");
    oled.display();
  }
}

void renderGamesMenu()
{
  const GameDescriptor &selected = kGameDescriptors[gamesMenuIndex];
  tft.fillScreen(COLOR_BG);
  drawCenteredText(tft, 10, "GAMES", COLOR_TEXT, 1);

  const int start = max(0, static_cast<int>(gamesMenuIndex) - 2);
  const int end = min(static_cast<int>(GameId::Count), start + 4);
  int row = 0;
  for (int i = start; i < end; ++i)
  {
    const int y = 22 + row * 18;
    const bool active = i == gamesMenuIndex;
    tft.fillRoundRect(8, y, hw::tftWidth - 16, 14, 4, active ? COLOR_PANEL : COLOR_BG);
    tft.drawRoundRect(8, y, hw::tftWidth - 16, 14, 4, active ? COLOR_ACCENT : COLOR_DIM);
    tft.setCursor(14, y + 3);
    tft.setTextColor(active ? COLOR_TEXT : COLOR_DIM);
    tft.print(kGameDescriptors[i].title);
    if (!kGameDescriptors[i].implemented)
    {
      tft.setCursor(112, y + 3);
      tft.print("Soon");
    }
    ++row;
  }

  tft.fillRect(8, 102, hw::tftWidth - 16, 24, COLOR_BG);
  tft.setTextColor(COLOR_TEXT);
  tft.setCursor(12, 106);
  tft.print(selected.title);
  if (gamesDifficultyMode)
  {
    tft.setTextColor(COLOR_WARN);
    tft.setCursor(12, kFooterY);
    tft.print("Diff:");
    tft.print(selected.difficultyNames[selectedLaunchDifficulty[gamesMenuIndex]]);
  }
  else if (selected.difficultyCount > 1)
  {
    tft.setTextColor(COLOR_DIM);
    tft.setCursor(12, 106);
    tft.print("Diff:");
    tft.print(selected.difficultyNames[selectedLaunchDifficulty[gamesMenuIndex]]);
    tft.setCursor(12, kFooterY);
    tft.print("B1 diff  B2 OK");
  }
  else if (!selected.implemented && gamesShowComingSoon && gamesToastUntilMs > millis())
  {
    tft.setTextColor(COLOR_WARN);
    tft.setCursor(12, kFooterY);
    tft.print("Coming Soon");
  }
  else
  {
    tft.setTextColor(COLOR_DIM);
    tft.setCursor(12, kFooterY);
    tft.print("B1 next  B2 OK");
  }

  drawSensorSummaryOled(sensorService.snapshot(), "GAMES");
}

void renderLeaderboards()
{
  const GameDescriptor &selected = kGameDescriptors[leaderboardIndex];
  const uint8_t difficulty = selectedLeaderboardDifficulty[leaderboardIndex];

  tft.fillScreen(COLOR_BG);
  drawCenteredText(tft, 10, "SCORES", COLOR_TEXT, 1);
  tft.setCursor(8, 24);
  tft.setTextColor(COLOR_TEXT);
  tft.print(selected.title);
  tft.setCursor(8, 36);
  tft.setTextColor(COLOR_DIM);
  tft.print("Diff: ");
  tft.print(selected.difficultyNames[difficulty]);

  for (uint8_t i = 0; i < kTopScores; ++i)
  {
    const LeaderboardEntry entry = storageService.loadEntry(selected.id, difficulty, i);
    const int y = 50 + i * 20;
    tft.fillRoundRect(8, y, hw::tftWidth - 16, 16, 4, COLOR_PANEL);
    tft.setCursor(14, y + 4);
    tft.setTextColor(COLOR_TEXT);
    tft.print(i + 1);
    tft.print(".");
    if (entry.score == 0 && entry.durationMs == 0)
    {
      tft.print(" --");
    }
    else
    {
      tft.print(" ");
      tft.print(entry.score);
      tft.setCursor(78, y + 4);
      tft.print(entry.durationMs / 1000);
      tft.print("s");
    }
  }

  tft.setTextColor(COLOR_DIM);
  tft.setCursor(14, kFooterY);
  tft.print("B1 next  B2 diff");

  if (oledReady)
  {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 0);
    oled.print("LEADERBOARD");
    oled.setCursor(0, 14);
    oled.print(selected.title);
    oled.setCursor(0, 26);
    oled.print("Diff:");
    oled.print(selected.difficultyNames[difficulty]);
    for (uint8_t i = 0; i < kTopScores; ++i)
    {
      const LeaderboardEntry entry = storageService.loadEntry(selected.id, difficulty, i);
      oled.setCursor(0, 38 + i * 8);
      oled.print(i + 1);
      oled.print(":");
      oled.print(entry.score);
    }
    oled.display();
  }
}

void renderGameResult()
{
  const GameDescriptor &descriptor = descriptorFor(activeGameResult.gameId);
  tft.fillScreen(COLOR_BG);
  drawCenteredText(tft, 14, activeGameResult.completed ? "RESULT: WIN" : "RESULT: END", activeGameResult.completed ? COLOR_GOOD : COLOR_WARN, 1);
  tft.setCursor(18, 36);
  tft.setTextColor(COLOR_TEXT);
  tft.print(descriptor.title);
  tft.setCursor(18, 50);
  tft.print("Score: ");
  tft.print(activeGameResult.score);
  tft.setCursor(18, 64);
  tft.print("Time : ");
  if (activeGameResult.gameId == GameId::QuickDraw)
  {
    tft.print(activeGameResult.durationMs);
    tft.print("ms");
  }
  else
  {
    tft.print(activeGameResult.durationMs / 1000);
    tft.print("s");
  }
  tft.setCursor(18, 78);
  if (activeGameResult.winner != 0)
  {
    tft.print("Winr : B");
    tft.print(activeGameResult.winner);
    if (activeGameRank >= 0)
    {
      tft.print("  #");
      tft.print(activeGameRank + 1);
    }
  }
  else if (activeGameRank >= 0)
  {
    tft.print("Rank : #");
    tft.print(activeGameRank + 1);
  }
  else
  {
    tft.print("Rank : No Top 3");
  }

  const uint16_t retryColor = (resultActionIndex == 0) ? COLOR_ACCENT : COLOR_DIM;
  const uint16_t menuColor = (resultActionIndex == 1) ? COLOR_ACCENT : COLOR_DIM;
  tft.drawRoundRect(18, 90, 52, 20, 4, retryColor);
  tft.drawRoundRect(88, 90, 52, 20, 4, menuColor);
  tft.setTextColor(COLOR_TEXT);
  tft.setCursor(32, 97);
  tft.print("Retry");
  tft.setCursor(105, 97);
  tft.print("Menu");

  tft.setTextColor(COLOR_DIM);
  tft.setCursor(14, kFooterY);
  tft.print("B1 switch  B2 OK");

  drawSensorSummaryOled(sensorService.snapshot(), "RESULT");
}

void renderGameFrame()
{
  if (activeGame == nullptr)
  {
    return;
  }

  activeGame->renderTft(tft);
  if (oledReady)
  {
    activeGame->renderOledOverlay(oled, sensorService.snapshot());
  }
}

void renderToastOverlay(uint32_t nowMs)
{
  if (!uiToast.active)
  {
    return;
  }

  if (uiToast.untilMs <= nowMs)
  {
    uiToast.active = false;
    return;
  }

  tft.fillRoundRect(12, hw::tftHeight - 24, hw::tftWidth - 24, 16, 4, COLOR_PANEL);
  tft.drawRoundRect(12, hw::tftHeight - 24, hw::tftWidth - 24, 16, 4, uiToast.color);
  tft.setTextColor(uiToast.color);
  tft.setTextSize(1);
  tft.setCursor(18, hw::tftHeight - 20);
  tft.print(uiToast.message);
}

void renderFrame(uint32_t nowMs)
{
  if (!tftReady)
  {
    return;
  }

  switch (appState)
  {
  case AppState::Boot:
    renderBoot(nowMs);
    break;
  case AppState::LockScreen:
    renderLockScreen();
    break;
  case AppState::MainMenu:
    renderMainMenu();
    break;
  case AppState::SensorMonitor:
    renderSensorMonitor();
    break;
  case AppState::SelfTest:
    renderSelfTest();
    break;
  case AppState::GamesMenu:
    renderGamesMenu();
    break;
  case AppState::Leaderboard:
    renderLeaderboards();
    break;
  case AppState::GameRunning:
    renderGameFrame();
    break;
  case AppState::GameResult:
    renderGameResult();
    break;
  }

  renderToastOverlay(nowMs);
}

void setup()
{
  Serial.begin(115200);
  randomSeed(micros());

  pinMode(Hardware.tftBl, OUTPUT);
  digitalWrite(Hardware.tftBl, Hardware.backlightActiveHigh ? HIGH : LOW);

  inputService.begin();
  storageService.begin();
  storageService.load();
  sensorService.begin(storageService.darkRaw(), storageService.brightRaw());

  Wire.begin(Hardware.i2cSda, Hardware.i2cScl);
  oledReady = oled.begin(SSD1306_SWITCHCAPVCC, Hardware.oledAddr);

  pcaReady = pwm.begin();
  if (pcaReady)
  {
    pwm.setPWMFreq(1000);
  }
  rgbService.begin(pcaReady);

  SPI.begin(Hardware.tftSck, -1, Hardware.tftMosi, Hardware.tftCs);
  tft.initR(kTftInitMode);
  tft.setRotation(kTftRotation);
  tft.invertDisplay(false);
  tft.fillScreen(COLOR_BG);
  tft.setTextWrap(false);
  tftReady = true;

  bootStartedMs = millis();
  resetPasswordState();
}

void loop()
{
  const uint32_t nowMs = millis();
  const InputSnapshot input = inputService.update(nowMs);
  latestInputSnapshot = input;
  sensorService.update(nowMs);

  if (appState != AppState::Boot && appState != AppState::LockScreen && appState != AppState::MainMenu && input.panicCombo)
  {
    exitToMainMenu();
    showToast("Returned to menu", COLOR_WARN);
  }

  switch (appState)
  {
  case AppState::Boot:
    if ((nowMs - bootStartedMs) >= kBootDurationMs)
    {
      if (kBypassPasswordForButtonTest)
      {
        selfTestInitialized = false;
        selfTestPassLatched = false;
        changeState(AppState::SelfTest, nowMs);
      }
      else if (kBypassPasswordLock)
      {
        changeState(AppState::MainMenu, nowMs);
      }
      else
      {
        changeState(AppState::LockScreen, nowMs);
      }
    }
    break;
  case AppState::LockScreen:
    handlePasswordInput(input);
    break;
  case AppState::MainMenu:
    handleMainMenu(input);
    break;
  case AppState::SensorMonitor:
    handleSensorMonitor(input);
    break;
  case AppState::SelfTest:
    handleSelfTest(input, nowMs);
    break;
  case AppState::GamesMenu:
    handleGamesMenu(input, nowMs);
    break;
  case AppState::Leaderboard:
    handleLeaderboard(input);
    break;
  case AppState::GameRunning:
    if (activeGame != nullptr)
    {
      activeGame->update(nowMs, input, sensorService.snapshot());
      if (activeGame->isFinished())
      {
        finalizeGameResult();
      }
    }
    break;
  case AppState::GameResult:
    handleGameResult(input, nowMs);
    break;
  }

  if (gamesToastUntilMs <= nowMs)
  {
    gamesShowComingSoon = false;
  }
  updateRgb(nowMs);

  if ((nowMs - lastRenderMs) >= kRenderIntervalMs)
  {
    lastRenderMs = nowMs;
    renderFrame(nowMs);
  }
}
