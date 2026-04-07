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
    {GameId::Breakout, "Breakout", true, 1, kBaseDifficultyNames},
    {GameId::Pong, "Pong", true, 3, kPongDifficultyNames},
    {GameId::FlappyBird, "Flappy Bird", true, 1, kBaseDifficultyNames},
    {GameId::ShieldSword, "Shield & Sword", false, 1, kBaseDifficultyNames},
    {GameId::BalloonBattle, "Balloon Battle", false, 1, kBaseDifficultyNames},
    {GameId::GridBattle, "Grid Battle", false, 1, kBaseDifficultyNames},
    {GameId::PoleClimb, "Pole Climb", false, 1, kBaseDifficultyNames},
    {GameId::Racing, "Racing", false, 1, kBaseDifficultyNames},
    {GameId::DuckHunt, "Duck Hunt", false, 1, kBaseDifficultyNames},
    {GameId::QuickDraw, "Quick Draw", false, 1, kBaseDifficultyNames},
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

class BreakoutGame : public IGame
{
public:
  GameId id() const override
  {
    return GameId::Breakout;
  }

  void enter(uint8_t difficulty, uint32_t nowMs) override
  {
    (void)difficulty;
    _difficulty = 0;
    _startMs = nowMs;
    _lastUpdateMs = nowMs;
    _finished = false;
    _completed = false;
    _score = 0;
    _powerUntilMs = 0;
    _paddleWidth = 28.0f;
    _paddleX = (hw::tftWidth - _paddleWidth) * 0.5f;
    _ballX = hw::tftWidth * 0.5f;
    _ballY = hw::tftHeight - 24.0f;
    _ballVx = 78.0f;
    _ballVy = -98.0f;

    for (uint8_t r = 0; r < kRows; ++r)
    {
      for (uint8_t c = 0; c < kCols; ++c)
      {
        _bricks[r][c] = true;
        _special[r][c] = false;
      }
    }

    randomSeed(micros());
    for (uint8_t i = 0; i < 3; ++i)
    {
      const uint8_t pick = random(0, kRows * kCols);
      _special[pick / kCols][pick % kCols] = true;
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

    const float paddleSpeed = 160.0f;
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
      _paddleWidth = 28.0f;
      _powerUntilMs = 0;
      _paddleX = clampValue(_paddleX, 2.0f, hw::tftWidth - _paddleWidth - 2.0f);
    }

    _ballX += _ballVx * dt;
    _ballY += _ballVy * dt;

    if (_ballX <= kRadius || _ballX >= (hw::tftWidth - kRadius))
    {
      _ballX = clampValue(_ballX, kRadius, static_cast<float>(hw::tftWidth - kRadius));
      _ballVx = -_ballVx;
    }
    if (_ballY <= kRadius + 14)
    {
      _ballY = kRadius + 14;
      _ballVy = fabsf(_ballVy);
    }

    float nx = 0.0f;
    float ny = 0.0f;
    if (circleRectCollision(_ballX, _ballY, kRadius, _paddleX, hw::tftHeight - 12.0f, _paddleWidth, 4.0f, nx, ny) && _ballVy > 0.0f)
    {
      reflect(nx, ny);
      const float hitFactor = ((_ballX - _paddleX) / _paddleWidth) - 0.5f;
      _ballVx += hitFactor * 90.0f;
      _ballVy = -fabsf(_ballVy);
      normalizeBallSpeed(125.0f);
    }

    bool brickHit = false;
    for (uint8_t r = 0; r < kRows && !brickHit; ++r)
    {
      for (uint8_t c = 0; c < kCols && !brickHit; ++c)
      {
        if (!_bricks[r][c])
        {
          continue;
        }

        const float brickX = 8.0f + c * (kBrickW + 2.0f);
        const float brickY = 20.0f + r * (kBrickH + 3.0f);
        if (circleRectCollision(_ballX, _ballY, kRadius, brickX, brickY, kBrickW, kBrickH, nx, ny))
        {
          _bricks[r][c] = false;
          brickHit = true;
          reflect(nx, ny);
          _score += _special[r][c] ? 25 : 10;
          if (_special[r][c])
          {
            _paddleWidth = 42.0f;
            _powerUntilMs = nowMs + 8000;
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
    for (uint8_t r = 0; r < kRows && !anyBrickLeft; ++r)
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

    for (uint8_t r = 0; r < kRows; ++r)
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
    display.fillRoundRect(static_cast<int>(_paddleX), hw::tftHeight - 12, static_cast<int>(_paddleWidth), 4, 2, ST77XX_WHITE);
    display.fillCircle(static_cast<int>(_ballX), static_cast<int>(_ballY), static_cast<int>(kRadius), COLOR_GOOD);
    if (_powerUntilMs != 0)
    {
      display.setCursor(4, kFooterY);
      display.setTextColor(COLOR_WARN);
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

    for (uint8_t i = 0; i < 18; ++i)
    {
      x += vx;
      y += vy;
      if (x <= kRadius || x >= (hw::tftWidth - kRadius))
      {
        vx = -vx;
      }
      if (y <= kRadius + 14)
      {
        vy = -vy;
      }
      if ((i % 2) == 0)
      {
        display.drawPixel(static_cast<int>(x), static_cast<int>(y), COLOR_DIM);
      }
    }
  }

  static constexpr uint8_t kRows = 4;
  static constexpr uint8_t kCols = 6;
  static constexpr float kBrickW = 22.0f;
  static constexpr float kBrickH = 10.0f;
  static constexpr float kRadius = 3.0f;

  bool _bricks[kRows][kCols] = {};
  bool _special[kRows][kCols] = {};
  bool _finished = false;
  bool _completed = false;
  uint8_t _difficulty = 0;
  uint32_t _startMs = 0;
  uint32_t _lastUpdateMs = 0;
  uint32_t _durationMs = 0;
  uint32_t _powerUntilMs = 0;
  uint32_t _score = 0;
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

    const float playerSpeed = 110.0f;
    if (input.btn1Down && !input.btn2Down)
    {
      _playerY -= playerSpeed * dt;
    }
    if (input.btn2Down && !input.btn1Down)
    {
      _playerY += playerSpeed * dt;
    }
    _playerY = clampValue(_playerY, 16.0f, static_cast<float>(hw::tftHeight - kPaddleH - 4));

    const float aiSpeed = (_difficulty == 0) ? 65.0f : (_difficulty == 1) ? 90.0f
                                                                            : 115.0f;
    const float aiTarget = _ballY - (kPaddleH * 0.5f);
    if (_aiY < aiTarget)
    {
      _aiY += aiSpeed * dt;
    }
    else if (_aiY > aiTarget)
    {
      _aiY -= aiSpeed * dt;
    }
    _aiY = clampValue(_aiY, 16.0f, static_cast<float>(hw::tftHeight - kPaddleH - 4));

    _ballX += _ballVx * dt;
    _ballY += _ballVy * dt;

    if (_ballY <= kRadius + 14 || _ballY >= (hw::tftHeight - kRadius - 1))
    {
      _ballVy = -_ballVy;
      _ballY = clampValue(_ballY, kRadius + 14, static_cast<float>(hw::tftHeight - kRadius - 1));
    }

    float nx = 0.0f;
    float ny = 0.0f;
    if (circleRectCollision(_ballX, _ballY, kRadius, 8.0f, _playerY, 4.0f, kPaddleH, nx, ny) && _ballVx < 0.0f)
    {
      reflect(nx, ny);
      _ballVx = fabsf(_ballVx) + 8.0f;
      _ballVy += ((_ballY - _playerY) / kPaddleH - 0.5f) * 40.0f;
    }

    if (circleRectCollision(_ballX, _ballY, kRadius, hw::tftWidth - 12.0f, _aiY, 4.0f, kPaddleH, nx, ny) && _ballVx > 0.0f)
    {
      reflect(nx, ny);
      _ballVx = -fabsf(_ballVx) - 8.0f;
      _ballVy += ((_ballY - _aiY) / kPaddleH - 0.5f) * 30.0f;
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

    display.fillRoundRect(8, static_cast<int>(_playerY), 4, kPaddleH, 2, COLOR_ACCENT);
    display.fillRoundRect(hw::tftWidth - 12, static_cast<int>(_aiY), 4, kPaddleH, 2, COLOR_WARN);
    display.fillCircle(static_cast<int>(_ballX), static_cast<int>(_ballY), static_cast<int>(kRadius), ST77XX_WHITE);

    display.setTextColor(COLOR_DIM);
    display.setCursor(54, kFooterY);
    display.print(kPongDifficultyNames[_difficulty]);
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
    const float speed = (_difficulty == 0) ? 72.0f : (_difficulty == 1) ? 88.0f
                                                                         : 104.0f;
    _ballVx = speed * direction;
    _ballVy = random(-25, 26);
  }

  void finish(uint32_t nowMs, bool completed)
  {
    _finished = true;
    _completed = completed;
    _durationMs = nowMs - _startMs;
  }

  static constexpr float kRadius = 3.0f;
  static constexpr int kPaddleH = 24;

  bool _finished = false;
  bool _completed = false;
  uint8_t _difficulty = 0;
  uint32_t _startMs = 0;
  uint32_t _lastUpdateMs = 0;
  uint32_t _durationMs = 0;
  uint8_t _playerScore = 0;
  uint8_t _aiScore = 0;
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
    (void)difficulty;
    _difficulty = 0;
    _finished = false;
    _completed = false;
    _score = 0;
    _startMs = nowMs;
    _lastUpdateMs = nowMs;
    _birdY = hw::tftHeight * 0.5f;
    _birdVel = 0.0f;

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

    if (input.btn1Pressed)
    {
      _birdVel = -92.0f;
    }

    _birdVel += 260.0f * dt;
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
      _pipes[i].x -= 75.0f * dt;

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

    for (uint8_t i = 0; i < kPipeCount; ++i)
    {
      const int x = static_cast<int>(_pipes[i].x);
      const int topH = _pipes[i].gapY;
      const int bottomY = _pipes[i].gapY + kGapH;
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
    const bool inGap = (_birdY - 4) >= pipe.gapY && (_birdY + 4) <= (pipe.gapY + kGapH);
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
  static constexpr int kGapH = 34;
  static constexpr int kBirdX = 38;

  Pipe _pipes[kPipeCount];
  bool _finished = false;
  bool _completed = false;
  uint8_t _difficulty = 0;
  uint32_t _startMs = 0;
  uint32_t _lastUpdateMs = 0;
  uint32_t _durationMs = 0;
  uint32_t _score = 0;
  float _birdY = 0.0f;
  float _birdVel = 0.0f;
};

InputService inputService;
StorageService storageService;
SensorService sensorService;
RgbService rgbService;
BreakoutGame breakoutGame;
PongGame pongGame;
FlappyBirdGame flappyBirdGame;

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
void drawCenteredText(Adafruit_GFX &display, int16_t centerY, const char *text, uint16_t color, uint8_t textSize = 1);
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
  tft.print(activeGameResult.durationMs / 1000);
  tft.print("s");
  tft.setCursor(18, 78);
  if (activeGameRank >= 0)
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
