#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_PWMServoDriver.h>
#include <DHT.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
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
constexpr char kWifiSsid[] = "dlinkae26";
constexpr char kWifiPassword[] = "27696041";
constexpr char kWifiBackupSsid[] = "BWUn-cnVieWduYWioYw";
constexpr char kWifiBackupPassword[] = "0960596972";

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
  GomokuLearn,
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
  AngryBirds,
  ChoiceOfLife,
  Gomoku,
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

struct LifeSaveData
{
  uint8_t valid = 0;
  uint8_t nodeIndex = 0;
  uint8_t age = 0;
  int16_t money = 0;
  int16_t honor = 0;
  uint8_t gender = 0;
};

struct LifeHistoryData
{
  uint8_t age = 0;
  int16_t money = 0;
  int16_t honor = 0;
  char cause[20] = {};
};

struct GomokuLearningData
{
  uint16_t totalGames = 0;
  uint16_t studyGames = 0;
  uint16_t defenseSkill = 0;
  uint16_t readingSkill = 0;
  uint16_t bestMoveSkill = 0;
};

struct AiLearningStatusData
{
  uint16_t selfPlayMatches = 0;
  uint16_t selfPlayWins = 0;
  uint16_t onlineSyncBursts = 0;
  uint16_t ruleSyncBursts = 0;
  uint8_t taskCursor = 0;
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
  char detail[24] = {};
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
const char *const kConnectFourModeNames[] = {"2P"};
const char *const kPongDifficultyNames[] = {"Easy", "Normal", "Hard"};
const char *const kGomokuDifficultyNames[] = {"2P", "AI", "Hard AI", "AI Learn"};
const GameDescriptor kGameDescriptors[] = {
    {GameId::Breakout, "Breakout", true, 3, kPongDifficultyNames},
    {GameId::Pong, "Pong", true, 3, kPongDifficultyNames},
    {GameId::FlappyBird, "Flappy Bird", true, 3, kPongDifficultyNames},
    {GameId::ShieldSword, "Stickman", true, 3, kPongDifficultyNames},
    {GameId::BalloonBattle, "Balloon Battle", true, 3, kPongDifficultyNames},
    {GameId::GridBattle, "Cold Trivia", true, 3, kPongDifficultyNames},
    {GameId::PoleClimb, "Pole Climb", true, 3, kPongDifficultyNames},
    {GameId::Racing, "Racing", true, 3, kPongDifficultyNames},
    {GameId::DuckHunt, "Duck Hunt", true, 3, kPongDifficultyNames},
    {GameId::QuickDraw, "Quick Draw", true, 3, kPongDifficultyNames},
    {GameId::AngryBirds, "Angry Birds", true, 3, kPongDifficultyNames},
    {GameId::ChoiceOfLife, "Connect Four", true, 1, kConnectFourModeNames},
    {GameId::Gomoku, "Gomoku", true, 4, kGomokuDifficultyNames},
};

const char *const kMainMenuItems[] = {
    "Sensors",
    "Games",
    "AI Learn",
    "Scores",
    "Self Test",
};

const GameId kVisibleGameIds[] = {
    GameId::Breakout,
    GameId::Pong,
    GameId::FlappyBird,
    GameId::ShieldSword,
    GameId::BalloonBattle,
    GameId::GridBattle,
    GameId::PoleClimb,
    GameId::Racing,
    GameId::DuckHunt,
    GameId::QuickDraw,
    GameId::AngryBirds,
    GameId::ChoiceOfLife,
    GameId::Gomoku,
};

const char *gomokuModeFocus(uint8_t mode);
uint8_t gomokuModeLevel(uint8_t mode);
uint8_t gomokuLearningLevel(const GomokuLearningData &data, uint8_t mode);

const HardwareProfile Hardware = {
    hw::tftSck, hw::tftMosi, hw::tftCs, hw::tftDc, hw::tftRst, hw::tftBl,
    hw::i2cSda, hw::i2cScl, hw::dhtPin, hw::btn1Pin, hw::btn2Pin,
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
    if (prefs.getBytesLength("gomoku_lr") == sizeof(GomokuLearningData))
    {
      prefs.getBytes("gomoku_lr", &_gomokuLearning, sizeof(GomokuLearningData));
    }
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
    if (prefs.getBytesLength("gomoku_lr") != sizeof(GomokuLearningData))
    {
      prefs.putBytes("gomoku_lr", &_gomokuLearning, sizeof(GomokuLearningData));
    }
    if (prefs.getBytesLength("ai_lr") == sizeof(AiLearningStatusData))
    {
      prefs.getBytes("ai_lr", &_aiLearningStatus, sizeof(AiLearningStatusData));
    }
    else
    {
      prefs.putBytes("ai_lr", &_aiLearningStatus, sizeof(AiLearningStatusData));
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

  const GomokuLearningData &gomokuLearning() const
  {
    return _gomokuLearning;
  }

  const AiLearningStatusData &aiLearningStatus() const
  {
    return _aiLearningStatus;
  }

  void recordGomokuSession(uint8_t mode, bool completed)
  {
    ++_gomokuLearning.totalGames;
    switch (mode)
    {
    case 1:
      _gomokuLearning.defenseSkill = min<uint16_t>(999, _gomokuLearning.defenseSkill + (completed ? 10 : 6));
      _gomokuLearning.readingSkill = min<uint16_t>(999, _gomokuLearning.readingSkill + 2);
      break;
    case 2:
      _gomokuLearning.defenseSkill = min<uint16_t>(999, _gomokuLearning.defenseSkill + (completed ? 8 : 5));
      _gomokuLearning.readingSkill = min<uint16_t>(999, _gomokuLearning.readingSkill + (completed ? 12 : 8));
      _gomokuLearning.bestMoveSkill = min<uint16_t>(999, _gomokuLearning.bestMoveSkill + 2);
      break;
    case 3:
      ++_gomokuLearning.studyGames;
      _gomokuLearning.defenseSkill = min<uint16_t>(999, _gomokuLearning.defenseSkill + 3);
      _gomokuLearning.readingSkill = min<uint16_t>(999, _gomokuLearning.readingSkill + 5);
      _gomokuLearning.bestMoveSkill = min<uint16_t>(999, _gomokuLearning.bestMoveSkill + (completed ? 12 : 9));
      break;
    default:
      break;
    }
    prefs.putBytes("gomoku_lr", &_gomokuLearning, sizeof(GomokuLearningData));
  }

  void recordBackgroundGomokuStudy(bool online)
  {
    _gomokuLearning.totalGames = min<uint16_t>(9999, _gomokuLearning.totalGames + 1);
    _gomokuLearning.studyGames = min<uint16_t>(9999, _gomokuLearning.studyGames + 1);
    _gomokuLearning.defenseSkill = min<uint16_t>(999, _gomokuLearning.defenseSkill + (online ? 2 : 1));
    _gomokuLearning.readingSkill = min<uint16_t>(999, _gomokuLearning.readingSkill + (online ? 3 : 2));
    _gomokuLearning.bestMoveSkill = min<uint16_t>(999, _gomokuLearning.bestMoveSkill + (online ? 4 : 2));
    prefs.putBytes("gomoku_lr", &_gomokuLearning, sizeof(GomokuLearningData));
  }

  void saveAiLearningStatus(const AiLearningStatusData &status)
  {
    _aiLearningStatus = status;
    prefs.putBytes("ai_lr", &_aiLearningStatus, sizeof(AiLearningStatusData));
  }

  bool loadLifeSave(LifeSaveData &save) const
  {
    save = {};
    if (prefs.getBytesLength("life_save") != sizeof(LifeSaveData))
    {
      return false;
    }
    prefs.getBytes("life_save", &save, sizeof(LifeSaveData));
    return save.valid == 1;
  }

  void saveLifeSave(const LifeSaveData &save)
  {
    prefs.putBytes("life_save", &save, sizeof(LifeSaveData));
  }

  void clearLifeSave()
  {
    prefs.remove("life_save");
  }

  void saveLifeHistory(const LifeHistoryData &history)
  {
    prefs.putBytes("life_hist", &history, sizeof(LifeHistoryData));
  }

  bool loadLifeHistory(LifeHistoryData &history) const
  {
    history = {};
    if (prefs.getBytesLength("life_hist") != sizeof(LifeHistoryData))
    {
      return false;
    }
    prefs.getBytes("life_hist", &history, sizeof(LifeHistoryData));
    return true;
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
  GomokuLearningData _gomokuLearning;
  AiLearningStatusData _aiLearningStatus;
};

class SensorService
{
public:
  void begin(int darkRaw, int brightRaw)
  {
    (void)darkRaw;
    (void)brightRaw;
    dht.begin();
    _snapshot.lightRaw = 0;
    _snapshot.lightPct = 0;
  }

  void update(uint32_t now)
  {
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

  SensorSnapshot _snapshot;
  uint32_t _lastDhtSampleMs = 0;
  uint32_t _lastDhtValidMs = 0;
};

class WifiService
{
public:
  void begin(const char *ssid, const char *password, const char *backupSsid = nullptr, const char *backupPassword = nullptr)
  {
    _ssid[0] = ssid;
    _password[0] = password;
    _ssid[1] = backupSsid;
    _password[1] = backupPassword;
    _networkCount = 0;
    for (uint8_t i = 0; i < 2; ++i)
    {
      if (_ssid[i] != nullptr && _ssid[i][0] != '\0')
      {
        ++_networkCount;
      }
    }
    _configured = _networkCount > 0;
    if (!_configured)
    {
      return;
    }

    WiFi.mode(WIFI_OFF);
  }

  void setDesired(bool desired, uint32_t nowMs)
  {
    if (!_configured)
    {
      return;
    }

    if (_desired == desired)
    {
      return;
    }

    _desired = desired;
    _connectionEvent = false;
    if (_desired)
    {
      WiFi.mode(WIFI_STA);
      WiFi.setSleep(false);
      _attemptCount = 0;
      _activeProfile = 0;
      attemptConnect(nowMs);
      return;
    }

    shutdownRadio();
  }

  void update(uint32_t nowMs)
  {
    if (!_configured)
    {
      return;
    }

    if (!_desired)
    {
      _connected = false;
      _lastStatus = WL_IDLE_STATUS;
      return;
    }

    const wl_status_t status = WiFi.status();
    _connected = (status == WL_CONNECTED);
    if (_connected)
    {
      if (!_wasConnected)
      {
        _connectionEvent = true;
      }
      if (_lastIp != WiFi.localIP())
      {
        _lastIp = WiFi.localIP();
      }
      _lastStatus = status;
      _wasConnected = true;
      return;
    }

    _lastStatus = status;
    _wasConnected = false;
    if ((nowMs - _lastAttemptMs) >= 10000)
    {
      attemptConnect(nowMs);
    }
  }

  bool connected() const
  {
    return _connected;
  }

  bool consumeConnectionEvent()
  {
    const bool value = _connectionEvent;
    _connectionEvent = false;
    return value;
  }

  const char *statusLabel() const
  {
    if (!_configured)
    {
      return "Off";
    }
    if (!_desired)
    {
      return "Standby";
    }
    if (_connected)
    {
      return (_networkCount > 1 && _activeProfile == 1) ? "Bkup" : "Link";
    }
    switch (_lastStatus)
    {
    case WL_IDLE_STATUS:
      return "Scan";
    case WL_CONNECT_FAILED:
      return "Fail";
    case WL_NO_SSID_AVAIL:
      return "NoAP";
    case WL_DISCONNECTED:
      return "Retry";
    default:
      return "Join";
    }
  }

  const char *ssid() const
  {
    if (!_configured)
    {
      return "";
    }
    return _ssid[_activeProfile];
  }

  String ipString() const
  {
    if (!_connected)
    {
      return "--";
    }
    return _lastIp.toString();
  }

  int rssi() const
  {
    return _connected ? WiFi.RSSI() : 0;
  }

private:
  void attemptConnect(uint32_t nowMs)
  {
    if (_networkCount == 0)
    {
      return;
    }

    if (_attemptCount == 0)
    {
      _activeProfile = 0;
    }
    else if (_networkCount > 1)
    {
      _activeProfile = (_activeProfile + 1) % _networkCount;
    }
    WiFi.disconnect(true, true);
    delay(30);
    WiFi.begin(_ssid[_activeProfile], _password[_activeProfile]);
    _lastAttemptMs = nowMs;
    ++_attemptCount;
  }

  void shutdownRadio()
  {
    WiFi.disconnect(true, true);
    delay(20);
    WiFi.mode(WIFI_OFF);
    _connected = false;
    _lastIp = IPAddress();
    _lastStatus = WL_IDLE_STATUS;
  }

  const char *_ssid[2] = {nullptr, nullptr};
  const char *_password[2] = {nullptr, nullptr};
  bool _configured = false;
  bool _desired = false;
  bool _connected = false;
  bool _wasConnected = false;
  bool _connectionEvent = false;
  uint8_t _networkCount = 0;
  uint8_t _activeProfile = 0;
  uint32_t _attemptCount = 0;
  wl_status_t _lastStatus = WL_IDLE_STATUS;
  uint32_t _lastAttemptMs = 0;
  IPAddress _lastIp;
};

class TriviaNewsService
{
public:
  void update(uint32_t nowMs, bool wifiConnected)
  {
    if (!wifiConnected)
    {
      return;
    }

    if (_headlineCount == kFeedCount && (nowMs - _lastSuccessMs) < kRefreshMs)
    {
      return;
    }

    if (_lastAttemptMs != 0 && (nowMs - _lastAttemptMs) < kRetryMs)
    {
      return;
    }

    refresh(nowMs);
  }

  bool ready() const
  {
    return _headlineCount == kFeedCount;
  }

  const char *statusLabel() const
  {
    if (ready())
    {
      return "LIVE";
    }
    if (_lastAttemptMs == 0)
    {
      return "WAIT";
    }
    return _lastFetchOk ? "STALE" : "SYNC";
  }

  bool fillQuestion(uint8_t seed, char *prompt, size_t promptSize, char options[4][24], uint8_t &answer, char *source, size_t sourceSize) const
  {
    if (!ready())
    {
      return false;
    }

    const uint8_t correct = seed % kFeedCount;
    snprintf(prompt, promptSize, "%s", _headlines[correct].title);
    for (uint8_t i = 0; i < kFeedCount; ++i)
    {
      copyText(options[i], 24, _headlines[i].label);
    }
    answer = correct;
    copyText(source, sourceSize, "BBC LIVE");
    return true;
  }

private:
  struct Headline
  {
    char label[24] = {};
    char title[120] = {};
  };

  struct FeedSource
  {
    const char *label;
    const char *url;
  };

  static constexpr uint8_t kFeedCount = 4;
  static constexpr uint32_t kRetryMs = 15000;
  static constexpr uint32_t kRefreshMs = 30UL * 60UL * 1000UL;

  static const FeedSource *feeds()
  {
    static const FeedSource kFeeds[kFeedCount] = {
        {"World", "http://news.bbc.co.uk/rss/newsonline_world_edition/front_page/rss.xml"},
        {"Business", "http://news.bbc.co.uk/rss/newsonline_uk_edition/business/rss.xml"},
        {"Health", "http://news.bbc.co.uk/rss/newsonline_uk_edition/health/rss.xml"},
        {"Showbiz", "http://news.bbc.co.uk/rss/newsonline_uk_edition/entertainment/rss091.xml"}};
    return kFeeds;
  }

  static void copyText(char *dst, size_t size, const char *text)
  {
    if (size == 0)
    {
      return;
    }
    strncpy(dst, text, size - 1);
    dst[size - 1] = '\0';
  }

  static bool extractFirstItemTitle(const String &xml, char *dst, size_t size)
  {
    const int itemStart = xml.indexOf("<item");
    if (itemStart < 0)
    {
      return false;
    }

    const int titleStart = xml.indexOf("<title>", itemStart);
    const int titleEnd = (titleStart >= 0) ? xml.indexOf("</title>", titleStart) : -1;
    if (titleStart < 0 || titleEnd < 0)
    {
      return false;
    }

    String title = xml.substring(titleStart + 7, titleEnd);
    title.replace("&#39;", "'");
    title.replace("&amp;", "&");
    title.replace("&quot;", "\"");
    title.replace("&lt;", "<");
    title.replace("&gt;", ">");
    title.replace("  ", " ");
    title.trim();
    if (title.endsWith(" - BBC News"))
    {
      title.remove(title.length() - 11);
    }
    title.toCharArray(dst, size);
    return title.length() > 0;
  }

  bool fetchHeadline(const FeedSource &feed, Headline &out) const
  {
    HTTPClient http;
    http.setTimeout(3500);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.useHTTP10(true);
    if (!http.begin(feed.url))
    {
      return false;
    }

    const int code = http.GET();
    if (code != HTTP_CODE_OK)
    {
      http.end();
      return false;
    }

    const String payload = http.getString();
    http.end();
    if (!extractFirstItemTitle(payload, out.title, sizeof(out.title)))
    {
      return false;
    }

    copyText(out.label, sizeof(out.label), feed.label);
    return true;
  }

  void refresh(uint32_t nowMs)
  {
    _lastAttemptMs = nowMs;
    _headlineCount = 0;
    _lastFetchOk = false;
    const FeedSource *const kFeeds = feeds();
    for (uint8_t i = 0; i < kFeedCount; ++i)
    {
      Headline item;
      if (!fetchHeadline(kFeeds[i], item))
      {
        continue;
      }
      _headlines[_headlineCount++] = item;
    }

    _lastFetchOk = _headlineCount == kFeedCount;
    if (_lastFetchOk)
    {
      _lastSuccessMs = nowMs;
    }
  }

  Headline _headlines[kFeedCount];
  uint8_t _headlineCount = 0;
  bool _lastFetchOk = false;
  uint32_t _lastAttemptMs = 0;
  uint32_t _lastSuccessMs = 0;
};

class AiLearningService
{
public:
  void begin(uint32_t nowMs, const AiLearningStatusData &saved = {})
  {
    _lastAdvanceMs = nowMs;
    _lastBackgroundSelfPlayMs = nowMs;
    _lastPersistMs = nowMs;
    _taskCursor = saved.taskCursor % taskCount();
    _parallelJobs = 2;
    _selfPlayMatches = saved.selfPlayMatches;
    _selfPlayWins = min<uint16_t>(_selfPlayMatches, saved.selfPlayWins);
    _onlineSyncBursts = saved.onlineSyncBursts;
    _ruleSyncBursts = saved.ruleSyncBursts;
    _wifiConnected = false;
    _lastWifiSeenMs = 0;
    _dirty = false;
  }

  void update(uint32_t nowMs, bool wifiConnected, const GomokuLearningData &learning)
  {
    _wifiConnected = wifiConnected;
    if (_lastAdvanceMs == 0)
    {
      begin(nowMs);
    }

    if (wifiConnected)
    {
      _lastWifiSeenMs = nowMs;
    }

    const uint32_t interval = wifiConnected ? 1300 : 2200;
    if ((nowMs - _lastAdvanceMs) < interval)
    {
      return;
    }

    _lastAdvanceMs = nowMs;
    _taskCursor = (_taskCursor + 1) % taskCount();
    _parallelJobs = wifiConnected ? 5 : 2;

    if (wifiConnected)
    {
      _onlineSyncBursts = min<uint16_t>(999, _onlineSyncBursts + 1);
      if (((_onlineSyncBursts + learning.totalGames + learning.studyGames) % 4) == 0)
      {
        _ruleSyncBursts = min<uint16_t>(999, _ruleSyncBursts + 1);
      }
      _dirty = true;
    }
  }

  void recordSelfPlay(bool completed)
  {
    _selfPlayMatches = min<uint16_t>(9999, _selfPlayMatches + 1);
    if (completed)
    {
      _selfPlayWins = min<uint16_t>(9999, _selfPlayWins + 1);
    }
    _taskCursor = (_taskCursor + 2) % taskCount();
    _dirty = true;
  }

  bool backgroundSelfPlayDue(uint32_t nowMs, bool activeMatch) const
  {
    const uint32_t interval = _wifiConnected ? 32000UL : 46000UL;
    const uint32_t adjusted = activeMatch ? (interval + 18000UL) : interval;
    return (nowMs - _lastBackgroundSelfPlayMs) >= adjusted;
  }

  void recordBackgroundSelfPlay(uint32_t nowMs, bool online)
  {
    _lastBackgroundSelfPlayMs = nowMs;
    _selfPlayMatches = min<uint16_t>(9999, _selfPlayMatches + 1);
    _selfPlayWins = min<uint16_t>(9999, _selfPlayWins + 1);
    if (online)
    {
      _onlineSyncBursts = min<uint16_t>(999, _onlineSyncBursts + 1);
    }
    if (((_selfPlayMatches + _ruleSyncBursts) % 3) == 0)
    {
      _ruleSyncBursts = min<uint16_t>(999, _ruleSyncBursts + 1);
    }
    _taskCursor = (_taskCursor + 1) % taskCount();
    _dirty = true;
  }

  AiLearningStatusData snapshot() const
  {
    AiLearningStatusData data;
    data.selfPlayMatches = _selfPlayMatches;
    data.selfPlayWins = _selfPlayWins;
    data.onlineSyncBursts = _onlineSyncBursts;
    data.ruleSyncBursts = _ruleSyncBursts;
    data.taskCursor = _taskCursor;
    return data;
  }

  bool shouldPersist(uint32_t nowMs) const
  {
    return _dirty && (nowMs - _lastPersistMs) >= 16000;
  }

  void notePersisted(uint32_t nowMs)
  {
    _lastPersistMs = nowMs;
    _dirty = false;
  }

  uint8_t estimatedAbility(const GomokuLearningData &learning) const
  {
    const uint16_t core = static_cast<uint16_t>((learning.defenseSkill + learning.readingSkill + learning.bestMoveSkill) / 3);
    const uint16_t selfPlayBoost = min<uint16_t>(180, static_cast<uint16_t>(_selfPlayMatches * 3 + _selfPlayWins * 2));
    const uint16_t networkBoost = min<uint16_t>(140, static_cast<uint16_t>(_onlineSyncBursts + _ruleSyncBursts * 6));
    return clampValue<uint8_t>(static_cast<uint8_t>(min<uint16_t>(99, (core + selfPlayBoost + networkBoost) / 10)), 0, 99);
  }

  const char *abilityTier(uint8_t ability) const
  {
    if (ability >= 90)
    {
      return "God";
    }
    if (ability >= 60)
    {
      return "Pro";
    }
    return "Normal";
  }

  const char *abilityMeaning(uint8_t ability) const
  {
    if (ability >= 90)
    {
      return "World #1";
    }
    if (ability >= 60)
    {
      return "Top amateur";
    }
    return "Beat regulars";
  }

  uint8_t parallelJobs() const
  {
    return _parallelJobs;
  }

  uint16_t selfPlayMatches() const
  {
    return _selfPlayMatches;
  }

  uint16_t selfPlayWins() const
  {
    return _selfPlayWins;
  }

  const char *ruleSyncLabel() const
  {
    if (!_wifiConnected)
    {
      return "CACHE";
    }
    if (_ruleSyncBursts == 0)
    {
      return "WATCH";
    }
    if (_lastWifiSeenMs != 0 && (millis() - _lastWifiSeenMs) < 20000)
    {
      return "LIVE";
    }
    return "SYNC";
  }

  const char *networkLabel() const
  {
    return _wifiConnected ? "ONLINE" : "OFFLINE";
  }

  void taskLine(uint8_t index, char *topic, size_t topicSize, char *detail, size_t detailSize) const
  {
    const Task &task = tasks()[(_taskCursor + index) % taskCount()];
    copyText(topic, topicSize, task.topic);

    if (task.needsWifi && !_wifiConnected)
    {
      snprintf(detail, detailSize, "wait net");
      return;
    }

    const uint16_t progress = static_cast<uint16_t>(((millis() / max<uint16_t>(1, task.cycleMs / 10)) + index * 17 + _selfPlayMatches) % 100);
    const uint8_t phase = static_cast<uint8_t>(((millis() / max<uint16_t>(1, task.cycleMs / 3)) + index + (_onlineSyncBursts % 3)) % 3);
    const char *phaseLabel = (phase == 0) ? "read" : (phase == 1) ? "merge"
                                                                   : "index";
    snprintf(detail, detailSize, "%s %u%%", phaseLabel, progress);
  }

  void summaryLine(char *line, size_t size, const GomokuLearningData &learning) const
  {
    const uint8_t ability = estimatedAbility(learning);
    snprintf(line, size, "%s %u%% jobs:%u", abilityTier(ability), ability, parallelJobs());
  }

  void digestLine(uint8_t index, char *line, size_t size, const GomokuLearningData &learning) const
  {
    switch (index % 3)
    {
    case 0:
      snprintf(line, size, "Selfplay %u-%u", selfPlayWins(), selfPlayMatches());
      break;
    case 1:
      snprintf(line, size, "Rules %s  Net %s", ruleSyncLabel(), networkLabel());
      break;
    default:
      snprintf(line, size, "%s",
               abilityMeaning(estimatedAbility(learning)));
      break;
    }
  }

private:
  struct Task
  {
    const char *topic;
    bool needsWifi;
    uint16_t cycleMs;
  };

  static constexpr uint8_t taskCount()
  {
    return 6;
  }

  static const Task *tasks()
  {
    static const Task kTasks[taskCount()] = {
        {"F1 Start", true, 5200},
        {"WRC Rot", true, 6400},
        {"GT Def", false, 5600},
        {"Pace Lab", true, 7000},
        {"Rule Sync", true, 7600},
        {"Selfplay", false, 4800}};
    return kTasks;
  }

  static void copyText(char *dst, size_t size, const char *text)
  {
    if (size == 0)
    {
      return;
    }
    strncpy(dst, text, size - 1);
    dst[size - 1] = '\0';
  }

  uint32_t _lastAdvanceMs = 0;
  uint32_t _lastBackgroundSelfPlayMs = 0;
  uint32_t _lastWifiSeenMs = 0;
  uint32_t _lastPersistMs = 0;
  uint16_t _selfPlayMatches = 0;
  uint16_t _selfPlayWins = 0;
  uint16_t _onlineSyncBursts = 0;
  uint16_t _ruleSyncBursts = 0;
  uint8_t _taskCursor = 0;
  uint8_t _parallelJobs = 2;
  bool _wifiConnected = false;
  bool _dirty = false;
};

class DuckImageService
{
public:
  void begin()
  {
    _paletteCount = 0;
    _nextSource = 0;
    _lastAttemptMs = 0;
    _lastSuccessMs = 0;
    _lastFetchOk = false;
  }

  void update(uint32_t nowMs, bool wifiConnected)
  {
    if (!wifiConnected)
    {
      return;
    }
    if (_paletteCount >= kRemoteSourceCount)
    {
      return;
    }
    if (_lastAttemptMs != 0 && (nowMs - _lastAttemptMs) < kRetryMs)
    {
      return;
    }
    fetchNext(nowMs);
  }

  const char *statusLabel() const
  {
    if (_paletteCount >= kRemoteSourceCount)
    {
      return "WEB";
    }
    if (_paletteCount > 0)
    {
      return "MIX";
    }
    if (_lastAttemptMs == 0)
    {
      return "PACK";
    }
    return _lastFetchOk ? "MIX" : "SYNC";
  }

  uint8_t variantCount() const
  {
    return kFallbackStyleCount + _paletteCount;
  }

  void styleForSeed(uint8_t seed, uint16_t &body, uint16_t &wing, uint16_t &beak, uint16_t &outline, char *tag, size_t tagSize) const
  {
    const uint8_t total = max<uint8_t>(1, variantCount());
    const uint8_t pick = seed % total;
    DuckStyle style;
    if (pick < _paletteCount)
    {
      style = _remoteStyles[pick];
    }
    else
    {
      style = fallbackStyles()[(pick - _paletteCount) % kFallbackStyleCount];
    }

    body = style.body;
    wing = style.wing;
    beak = style.beak;
    outline = style.outline;
    copyText(tag, tagSize, style.tag);
  }

private:
  struct DuckStyle
  {
    uint16_t body = color565(255, 214, 78);
    uint16_t wing = color565(228, 170, 32);
    uint16_t beak = color565(248, 124, 46);
    uint16_t outline = color565(36, 38, 44);
    char tag[6] = "LOC";

    DuckStyle() {}

    DuckStyle(uint16_t bodyColor, uint16_t wingColor, uint16_t beakColor, uint16_t outlineColor, const char *label)
        : body(bodyColor), wing(wingColor), beak(beakColor), outline(outlineColor)
    {
      strncpy(tag, label, sizeof(tag) - 1);
      tag[sizeof(tag) - 1] = '\0';
    }
  };

  struct RemoteSource
  {
    const char *tag;
    const char *url;
  };

  static constexpr uint8_t kRemoteSourceCount = 3;
  static constexpr uint8_t kFallbackStyleCount = 3;
  static constexpr uint32_t kRetryMs = 18000;

  static const DuckStyle *fallbackStyles()
  {
    static const DuckStyle kFallback[kFallbackStyleCount] = {
        {color565(255, 214, 78), color565(228, 170, 32), color565(248, 124, 46), color565(38, 40, 46), "LOC1"},
        {color565(116, 196, 90), color565(64, 124, 58), color565(242, 184, 44), color565(32, 40, 34), "LOC2"},
        {color565(230, 236, 244), color565(142, 164, 188), color565(255, 162, 54), color565(40, 46, 58), "LOC3"}};
    return kFallback;
  }

  static const RemoteSource *remoteSources()
  {
    static const RemoteSource kSources[kRemoteSourceCount] = {
        {"TWE", "https://raw.githubusercontent.com/twitter/twemoji/gh-pages/svg/1f986.svg"},
        {"OMJ", "https://raw.githubusercontent.com/hfg-gmuend/openmoji/master/color/svg/1F986.svg"},
        {"NTO", "https://raw.githubusercontent.com/googlefonts/noto-emoji/main/svg/emoji_u1f986.svg"}};
    return kSources;
  }

  static void copyText(char *dst, size_t size, const char *text)
  {
    if (size == 0)
    {
      return;
    }
    strncpy(dst, text, size - 1);
    dst[size - 1] = '\0';
  }

  static bool isHex(char c)
  {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
  }

  static uint8_t hexValue(char c)
  {
    if (c >= '0' && c <= '9')
    {
      return static_cast<uint8_t>(c - '0');
    }
    if (c >= 'a' && c <= 'f')
    {
      return static_cast<uint8_t>(10 + (c - 'a'));
    }
    return static_cast<uint8_t>(10 + (c - 'A'));
  }

  static bool parseHexColor(const String &svg, int pos, uint16_t &out)
  {
    if (pos < 0 || (pos + 6) >= svg.length())
    {
      return false;
    }
    for (int i = 1; i <= 6; ++i)
    {
      if (!isHex(svg[pos + i]))
      {
        return false;
      }
    }
    const uint8_t r = static_cast<uint8_t>(hexValue(svg[pos + 1]) * 16 + hexValue(svg[pos + 2]));
    const uint8_t g = static_cast<uint8_t>(hexValue(svg[pos + 3]) * 16 + hexValue(svg[pos + 4]));
    const uint8_t b = static_cast<uint8_t>(hexValue(svg[pos + 5]) * 16 + hexValue(svg[pos + 6]));
    out = color565(r, g, b);
    return true;
  }

  static bool extractPalette(const String &svg, DuckStyle &style)
  {
    uint16_t colors[4] = {};
    uint8_t count = 0;
    for (int i = 0; i < svg.length() - 6 && count < 4; ++i)
    {
      if (svg[i] != '#')
      {
        continue;
      }
      uint16_t parsed = 0;
      if (!parseHexColor(svg, i, parsed))
      {
        continue;
      }

      bool unique = true;
      for (uint8_t j = 0; j < count; ++j)
      {
        if (colors[j] == parsed)
        {
          unique = false;
          break;
        }
      }
      if (!unique)
      {
        continue;
      }

      colors[count++] = parsed;
    }

    if (count == 0)
    {
      return false;
    }

    style.body = colors[0];
    style.wing = colors[(count > 1) ? 1 : 0];
    style.beak = colors[(count > 2) ? 2 : ((count > 1) ? 1 : 0)];
    style.outline = colors[(count > 3) ? 3 : 0];
    return true;
  }

  bool fetchRemoteStyle(const RemoteSource &source, DuckStyle &style) const
  {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(4500);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(client, source.url))
    {
      return false;
    }

    const int code = http.GET();
    if (code != HTTP_CODE_OK)
    {
      http.end();
      return false;
    }

    const String payload = http.getString();
    http.end();
    if (!extractPalette(payload, style))
    {
      return false;
    }

    copyText(style.tag, sizeof(style.tag), source.tag);
    return true;
  }

  void fetchNext(uint32_t nowMs)
  {
    _lastAttemptMs = nowMs;
    _lastFetchOk = false;

    const RemoteSource &source = remoteSources()[_nextSource % kRemoteSourceCount];
    DuckStyle style;
    if (fetchRemoteStyle(source, style))
    {
      _remoteStyles[_paletteCount++] = style;
      _lastFetchOk = true;
      _lastSuccessMs = nowMs;
    }
    ++_nextSource;
  }

  DuckStyle _remoteStyles[kRemoteSourceCount];
  uint8_t _paletteCount = 0;
  uint8_t _nextSource = 0;
  bool _lastFetchOk = false;
  uint32_t _lastAttemptMs = 0;
  uint32_t _lastSuccessMs = 0;
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

extern StorageService storageService;
extern WifiService wifiService;
extern TriviaNewsService triviaNewsService;
extern AiLearningService aiLearningService;
extern DuckImageService duckImageService;

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
    _baseBallSpeed = (_difficulty == 0) ? 86.0f : (_difficulty == 1) ? 118.0f
                                                                      : 138.0f;
    _ballSpeed = _baseBallSpeed;
    _basePaddleWidth = (_difficulty == 0) ? 42.0f : (_difficulty == 1) ? 30.0f
                                                                        : 24.0f;
    _bowlStrength = (_difficulty == 0) ? 0.95f : (_difficulty == 1) ? 0.72f
                                                                     : 0.48f;
    _baseRows = (_difficulty == 0) ? 3 : (_difficulty == 1) ? 4
                                                             : 5;
    _activeRows = _baseRows;
    _baseSpecialTarget = (_difficulty == 0) ? 2 : (_difficulty == 1) ? 3
                                                                      : 4;
    _specialTarget = _baseSpecialTarget;
    _paddleWidth = _basePaddleWidth;
    _paddleX = (hw::tftWidth - _paddleWidth) * 0.5f;
    _wave = 1;
    _wavesCleared = 0;
    seedWave(nowMs, _wave);
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

    if (_serveUntilMs != 0)
    {
      resetBallOnPaddle();
      if (nowMs >= _serveUntilMs)
      {
        _serveUntilMs = 0;
        launchServe();
      }
      return;
    }

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
      ++_wavesCleared;
      _score += 100 + _wave * 35;
      seedWave(nowMs, static_cast<uint16_t>(_wave + 1));
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
    display.setCursor(74, 3);
    display.print("W");
    display.print(_wave);
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
    if (_serveUntilMs != 0)
    {
      display.setTextColor(COLOR_WARN);
      display.setCursor(92, kFooterY);
      display.print("RELOAD");
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
    _completed = completed || (_wavesCleared > 0);
    _durationMs = nowMs - _startMs;
  }

  void resetBallOnPaddle()
  {
    _ballX = _paddleX + _paddleWidth * 0.5f;
    _ballY = kPaddleY - _ballRadius - 1.0f;
    _ballVx = 0.0f;
    _ballVy = 0.0f;
  }

  void launchServe()
  {
    const float serveBias = ((_wave % 2) == 0) ? -0.56f : 0.56f;
    _ballVx = _ballSpeed * serveBias;
    const float vySquared = max(36.0f, (_ballSpeed * _ballSpeed) - (_ballVx * _ballVx));
    _ballVy = -sqrtf(vySquared);
  }

  void seedWave(uint32_t nowMs, uint16_t wave)
  {
    _wave = max<uint16_t>(1, wave);
    _ballRadius = kBaseRadius;
    _powerUntilMs = 0;
    _ballSpeed = _baseBallSpeed + min<uint16_t>(8, _wave - 1) * 5.5f;
    _activeRows = min<uint8_t>(kMaxRows, static_cast<uint8_t>(_baseRows + min<uint16_t>(2, (_wave - 1) / 2)));
    _specialTarget = min<uint8_t>(_activeRows * kCols, static_cast<uint8_t>(_baseSpecialTarget + min<uint16_t>(3, (_wave - 1) / 2)));

    for (uint8_t r = 0; r < kMaxRows; ++r)
    {
      for (uint8_t c = 0; c < kCols; ++c)
      {
        _bricks[r][c] = r < _activeRows;
        _special[r][c] = false;
      }
    }

    randomSeed(micros() + _wave * 37UL);
    uint8_t placed = 0;
    uint8_t attempts = 0;
    while (placed < _specialTarget && attempts < 64)
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

    _serveUntilMs = nowMs + 1700;
    resetBallOnPaddle();
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
  uint8_t _baseRows = 4;
  uint8_t _activeRows = 4;
  uint8_t _baseSpecialTarget = 3;
  uint8_t _specialTarget = 3;
  uint16_t _wave = 1;
  uint16_t _wavesCleared = 0;
  uint32_t _startMs = 0;
  uint32_t _lastUpdateMs = 0;
  uint32_t _durationMs = 0;
  uint32_t _powerUntilMs = 0;
  uint32_t _serveUntilMs = 0;
  uint32_t _score = 0;
  float _baseBallSpeed = 126.0f;
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
      applyRallyReturn(nowMs, true, hitOffset);
    }

    if (circleRectCollision(_ballX, _ballY, kRadius, hw::tftWidth - 12.0f, _aiY, 4.0f, _paddleH, nx, ny) && _ballVx > 0.0f)
    {
      reflect(nx, ny);
      ++_rallyCount;
      applyRallyReturn(nowMs, false, ((_ballY - _aiY) / _paddleH) - 0.5f);
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
    display.print("Wi:");
    display.print(wifiService.statusLabel());
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
  float baseBallSpeed() const
  {
    return (_difficulty == 0) ? 56.0f : (_difficulty == 1) ? 78.0f
                                                           : 96.0f;
  }

  float rallyStepSpeed() const
  {
    return (_difficulty == 0) ? 9.0f : (_difficulty == 1) ? 12.0f
                                                          : 15.0f;
  }

  float smashBonus() const
  {
    return (_difficulty == 0) ? 26.0f : (_difficulty == 1) ? 34.0f
                                                           : 42.0f;
  }

  void applyRallyReturn(uint32_t nowMs, bool toRight, float hitOffset)
  {
    const float base = baseBallSpeed();
    const float cycleStep = rallyStepSpeed();
    float targetSpeed = base + min<uint8_t>(_rallyCount, _smashThreshold - 1) * cycleStep;
    bool smash = false;
    if (_rallyCount >= _smashThreshold)
    {
      targetSpeed = base + (_smashThreshold - 1) * cycleStep + smashBonus();
      _smashFlashUntilMs = nowMs + 700;
      _rallyCount = 0;
      smash = true;
    }

    hitOffset = clampValue(hitOffset, -0.5f, 0.5f);
    _ballVx = (toRight ? 1.0f : -1.0f) * targetSpeed * (smash ? 1.0f : 0.92f);
    _ballVy = hitOffset * (smash ? 132.0f : 88.0f);
    normalizeBallSpeed(targetSpeed);
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

  void resetBall(float direction)
  {
    _ballX = hw::tftWidth * 0.5f;
    _ballY = hw::tftHeight * 0.5f;
    const float speed = baseBallSpeed();
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
    _stage = Stage::SelectCar;
    _finished = false;
    _completed = false;
    _score = 0;
    _startMs = nowMs;
    _lastUpdateMs = nowMs;
    _durationMs = 0;
    _countdownStartMs = 0;
    _spawnTimer = 0.0f;
    _roadDashOffset = 0.0f;
    _launchMeter = 0.0f;
    _launchState = LaunchState::None;
    _launchEffectUntilMs = 0;
    _overtakeBoostUntilMs = 0;
    _overtakeStreak = 0;
    _raceStartMs = 0;
    _raceShieldUntilMs = 0;
    _playerLane = 1;
    _targetLane = 1;
    _selectedCar %= kCarCount;
    _selectedTrack %= kTrackCount;
    _playerCenterX = static_cast<float>(laneCenter(_playerLane));
    _roadSpeed = 72.0f;
    _laneShiftSpeed = 140.0f;
    _spawnInterval = 1.0f;
    _targetScore = 0;
    _playerRaceSpeed = 100.0f;
    _totalDistance = 2400.0f;
    _playerDistance = 0.0f;
    _progressPct = 0;
    _playerRank = 1;
    _finishRank = 8;
    _lastSpawnLane = 1;
    _playerOffsetNorm = 0.0f;
    _playerSlipVisual = 0.0f;
    _crashEffectUntilMs = 0;
    _crashX = hw::tftWidth / 2;
    _crashY = kPlayerY;
    _trackProgress = 0.0f;
    _curveCurrent = 0.0f;
    _curveTarget = 0.0f;
    for (uint8_t i = 0; i < kObstacleCount; ++i)
    {
      _obstacles[i] = {};
    }
    for (uint8_t i = 0; i < kAiCount; ++i)
    {
      const float startGrid = -260.0f + i * 34.0f;
      _aiDistance[i] = startGrid;
      _aiPhase[i] = random(0, 628) / 100.0f;
      _aiRaceSpeed[i] = 0.0f;
      _aiCarIndex[i] = (i + 1) % kCarCount;
      _aiOffsetNorm[i] = offsetNormForLane(i % 3);
      _aiTargetOffsetNorm[i] = _aiOffsetNorm[i];
      _aiIntent[i] = AiIntent::Cruise;
      _aiDecisionAtMs[i] = nowMs + 120 + random(0, 260);
      _aiWasAhead[i] = false;
    }
  }

  void update(uint32_t nowMs, const InputSnapshot &input, const SensorSnapshot &) override
  {
    if (_finished)
    {
      return;
    }

    if (_crashEffectUntilMs != 0)
    {
      if (nowMs < _crashEffectUntilMs)
      {
        _lastUpdateMs = nowMs;
        return;
      }
      _crashEffectUntilMs = 0;
      finish(nowMs, false);
      return;
    }

    if (_stage == Stage::SelectCar)
    {
      if (input.btn1DownEdge)
      {
        _selectedCar = (_selectedCar + 1) % kCarCount;
      }
      if (input.btn2DownEdge)
      {
        _stage = Stage::SelectTrack;
      }
      return;
    }

    if (_stage == Stage::SelectTrack)
    {
      if (input.btn1DownEdge)
      {
        _selectedTrack = (_selectedTrack + 1) % kTrackCount;
      }
      if (input.btn1Long)
      {
        _stage = Stage::SelectCar;
        return;
      }
      if (input.btn2DownEdge)
      {
        beginRace(nowMs);
      }
      return;
    }

    if (_stage == Stage::Countdown)
    {
      float dt = (nowMs - _lastUpdateMs) / 1000.0f;
      dt = clampValue(dt, 0.0f, 0.05f);
      _lastUpdateMs = nowMs;
      updateLaunchMeter(input, dt);
      if ((nowMs - _countdownStartMs) >= 2800)
      {
        applyLaunchResult(nowMs);
        _stage = Stage::Race;
        _raceStartMs = nowMs;
        _raceShieldUntilMs = nowMs + 900;
        _lastUpdateMs = nowMs;
      }
      return;
    }

    float dt = (nowMs - _lastUpdateMs) / 1000.0f;
    dt = clampValue(dt, 0.0f, 0.05f);
    _lastUpdateMs = nowMs;

    _trackProgress = clampValue(_playerDistance / max(1.0f, _totalDistance), 0.0f, 1.0f);
    _curveTarget = sampleCurveAtProgress(_trackProgress);
    _curveCurrent += (_curveTarget - _curveCurrent) * clampValue(dt * 5.8f, 0.0f, 1.0f);

    float steer = 0.0f;
    if (input.btn1Down && !input.btn2Down)
    {
      steer -= 1.0f;
    }
    if (input.btn2Down && !input.btn1Down)
    {
      steer += 1.0f;
    }

    const CarProfile &car = selectedCarProfile();
    const float steerRate = ((_difficulty == 0) ? 0.86f : (_difficulty == 1) ? 0.98f
                                                                              : 1.08f) *
                            car.shiftBias;
    _playerOffsetNorm += steer * steerRate * dt;
    _playerOffsetNorm -= _curveCurrent * (0.46f / max(0.82f, car.gripBias)) * dt;
    _playerOffsetNorm = clampValue(_playerOffsetNorm, -0.88f, 0.88f);
    _playerCenterX = worldXForOffsetNorm(_playerOffsetNorm);
    syncPlayerLaneFromOffset();

    const float idealLine = clampValue(-_curveCurrent * 0.52f, -0.66f, 0.66f);
    const float driftTarget = clampValue((_playerOffsetNorm - idealLine) * 1.58f - steer * 0.28f + _curveCurrent * 0.70f, -1.0f, 1.0f);
    _playerSlipVisual += (driftTarget - _playerSlipVisual) * clampValue(dt * 4.2f, 0.0f, 1.0f);
    const float linePenalty = 1.0f - clampValue(fabsf(_playerOffsetNorm - idealLine) * 0.12f, 0.0f, 0.13f);
    const float shoulderPenalty = (fabsf(_playerOffsetNorm) > 0.82f) ? 0.72f : (fabsf(_playerOffsetNorm) > 0.70f) ? 0.88f
                                                                                                                   : 1.0f;
    const float driftPenalty = 1.0f - clampValue((fabsf(_playerSlipVisual) - 0.16f) * 0.06f, 0.0f, 0.10f);
    float raceMul = linePenalty * shoulderPenalty * driftPenalty;
    if (_launchEffectUntilMs > nowMs)
    {
      switch (_launchState)
      {
      case LaunchState::Perfect:
        raceMul *= 1.18f;
        break;
      case LaunchState::Good:
        raceMul *= 1.08f;
        break;
      case LaunchState::Slow:
        raceMul *= 0.92f;
        break;
      case LaunchState::Spin:
        raceMul *= 0.78f;
        break;
      default:
        break;
      }
    }
    if (_overtakeBoostUntilMs > nowMs)
    {
      raceMul *= 1.06f + min(0.04f, _overtakeStreak * 0.01f);
    }
    if (_difficulty == 0 && _playerRank > 4)
    {
      raceMul *= 1.05f;
    }
    else if (_difficulty == 2 && _playerRank <= 2)
    {
      raceMul *= 0.98f;
    }
    _playerDistance += _playerRaceSpeed * raceMul * dt;

    _roadDashOffset += _roadSpeed * dt;
    while (_roadDashOffset >= 24.0f)
    {
      _roadDashOffset -= 24.0f;
    }

    const bool raceArmed = nowMs >= _raceShieldUntilMs;

    for (uint8_t i = 0; i < kAiCount; ++i)
    {
      const bool wasAhead = _aiDistance[i] > _playerDistance;
      updateAiRival(i, nowMs, dt);
      const bool nowAhead = _aiDistance[i] > _playerDistance;
      if (raceArmed && wasAhead && !nowAhead)
      {
        ++_score;
        _overtakeStreak = min<uint8_t>(12, _overtakeStreak + 1);
        _overtakeBoostUntilMs = nowMs + 1100;
      }
      _aiWasAhead[i] = nowAhead;
    }
    updateRaceRank();
    _progressPct = clampValue(static_cast<int>((_playerDistance / _totalDistance) * 100.0f + 0.5f), 0, 100);

    _spawnTimer += dt;
    if (_spawnTimer >= _spawnInterval)
    {
      _spawnTimer -= _spawnInterval;
      spawnObstacle();
    }

    const ScreenRect playerRect = playerHitRectFromRect(projectedPlayerRect());

    for (uint8_t i = 0; i < kObstacleCount; ++i)
    {
      if (!_obstacles[i].active)
      {
        continue;
      }

      updateTrafficBehavior(_obstacles[i], dt);
      _obstacles[i].y += _roadSpeed * dt;
      const ScreenRect obstacleRect = rivalHitRectFromRect(projectedObstacleRect(_obstacles[i]));
      if (raceArmed && obstacleRect.valid &&
          rectsOverlap(playerRect.x, playerRect.y, playerRect.w, playerRect.h, obstacleRect.x, obstacleRect.y, obstacleRect.w, obstacleRect.h))
      {
        triggerCrash(nowMs, obstacleRect.x + obstacleRect.w / 2, obstacleRect.y + obstacleRect.h / 2);
        return;
      }

      if (_obstacles[i].y > hw::tftHeight + 8)
      {
        _obstacles[i].active = false;
      }
    }

    if (_overtakeBoostUntilMs <= nowMs)
    {
      _overtakeStreak = 0;
    }

    for (uint8_t i = 0; i < kAiCount; ++i)
    {
      const ScreenRect aiRect = rivalHitRectFromRect(projectedAiRect(i));
      if (!aiRect.valid || aiRect.y < (kHorizonY + 20))
      {
        continue;
      }
      if (raceArmed &&
          rectsOverlap(playerRect.x, playerRect.y, playerRect.w, playerRect.h, aiRect.x, aiRect.y, aiRect.w, aiRect.h))
      {
        triggerCrash(nowMs, aiRect.x + aiRect.w / 2, aiRect.y + aiRect.h / 2);
        return;
      }
    }

    if (raceArmed && _playerDistance >= _totalDistance)
    {
      _finishRank = _playerRank;
      finish(nowMs, _playerRank <= 3);
      return;
    }
  }

  void renderTft(Adafruit_ST7735 &display) override
  {
    if (_stage == Stage::SelectCar)
    {
      renderCarSelect(display);
      return;
    }
    if (_stage == Stage::SelectTrack)
    {
      renderTrackSelect(display);
      return;
    }

    renderCityBackdrop(display);
    renderPerspectiveRoad(display);
    if (_stage == Stage::Countdown)
    {
      renderStartGridOverlay(display);
    }

    uint8_t order[kAiCount] = {0, 1, 2, 3, 4, 5, 6};
    for (uint8_t i = 0; i < kAiCount; ++i)
    {
      for (uint8_t j = i + 1; j < kAiCount; ++j)
      {
        const float deltaA = _aiDistance[order[i]] - _playerDistance;
        const float deltaB = _aiDistance[order[j]] - _playerDistance;
        if (deltaA < deltaB)
        {
          const uint8_t tmp = order[i];
          order[i] = order[j];
          order[j] = tmp;
        }
      }
    }
    for (uint8_t i = 0; i < kAiCount; ++i)
    {
      renderAiRival(display, order[i]);
    }

    for (uint8_t i = 0; i < kObstacleCount; ++i)
    {
      if (!_obstacles[i].active)
      {
        continue;
      }
      renderTrafficVehicle(display, _obstacles[i]);
    }

    renderPlayerVehicle(display);
    renderOverlapGhosts(display);
    if (_crashEffectUntilMs > millis())
    {
      renderCrashEffect(display);
    }
    renderMiniMap(display);

    if (_stage == Stage::Countdown)
    {
      renderCountdown(display);
    }

    drawRaceChip(display, 4, 4, "#", _playerRank, COLOR_ACCENT);
    drawRaceChip(display, 116, 4, "", _progressPct, COLOR_WARN, "%");
    if (_stage != Stage::Countdown)
    {
      drawRaceChip(display, 4, 108, "P", _score, COLOR_TEXT);
      if (_overtakeBoostUntilMs > millis())
      {
        drawRaceChip(display, 116, 108, "R", _overtakeStreak, COLOR_GOOD);
      }
      else if (_launchEffectUntilMs > millis())
      {
        drawRaceChip(display, 116, 108, "L", (_launchState == LaunchState::Perfect) ? 3 : (_launchState == LaunchState::Good) ? 2
                                                                                                                  : (_launchState == LaunchState::Slow) ? 1
                                                                                                                                                      : 0,
                    (_launchState == LaunchState::Spin) ? COLOR_DANGER : COLOR_ACCENT);
      }
      else
      {
        drawRaceChip(display, 116, 108, "D", static_cast<uint32_t>(fabsf(_playerSlipVisual) * 99.0f), COLOR_WARN);
      }
    }
  }

  void renderOledOverlay(Adafruit_SSD1306 &display, const SensorSnapshot &sensors) override
  {
    (void)sensors;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    if (_stage == Stage::SelectCar)
    {
      display.setCursor(0, 0);
      display.print("RACE CAR");
      display.setCursor(0, 12);
      display.print(_selectedCar + 1);
      display.print("/30 ");
      display.print(selectedCarProfile().name);
      display.setCursor(0, 24);
      display.print("SPD:");
      display.print(static_cast<int>(selectedCarProfile().speedBias * 100));
      display.setCursor(64, 24);
      display.print("GRP:");
      display.print(static_cast<int>(selectedCarProfile().gripBias * 100));
      display.setCursor(0, 36);
      display.print("SFT:");
      display.print(static_cast<int>(selectedCarProfile().shiftBias * 100));
      display.setCursor(0, 52);
      display.print("B1 next B2 ok");
      display.display();
      return;
    }

    if (_stage == Stage::SelectTrack)
    {
      display.setCursor(0, 0);
      display.print("RACE MAP");
      display.setCursor(0, 12);
      display.print(_selectedTrack + 1);
      display.print("/10 ");
      display.print(selectedTrackProfile().name);
      display.setCursor(0, 24);
      display.print("KM:");
      display.print(previewDistanceKm(selectedTrackProfile()), 1);
      display.setCursor(64, 24);
      display.print("AI:7");
      display.setCursor(0, 36);
      display.print(themeLabel(selectedTrackProfile().theme));
      display.setCursor(0, 52);
      display.print("B1 next B2 race");
      display.display();
      return;
    }

    if (_stage == Stage::Countdown)
    {
      display.setCursor(0, 0);
      display.print("START GRID");
      display.setCursor(0, 12);
      display.print(selectedCarProfile().name);
      display.print(" ");
      display.print(selectedTrackProfile().name);
      display.setCursor(0, 24);
      display.print("Heat:");
      display.print(static_cast<int>(_launchMeter));
      display.setCursor(70, 24);
      display.print("Aim:");
      display.print(static_cast<int>(launchGreenMin()));
      display.print("-");
      display.print(static_cast<int>(launchGreenMax()));
      display.setCursor(0, 40);
      display.print("Hold B1+B2");
      display.setCursor(0, 56);
      display.print("Stat:");
      display.print(launchLabel());
      display.display();
      return;
    }

    display.setCursor(0, 0);
    display.print("RACING");
    display.setCursor(0, 12);
    display.print(selectedCarProfile().name);
    display.print(" ");
    display.print(selectedTrackProfile().name);
    display.setCursor(0, 24);
    display.print("Rank:");
    display.print(_playerRank);
    display.print("/8");
    display.setCursor(72, 24);
    display.print("P:");
    display.print(_progressPct);
    display.print("%");
    display.setCursor(0, 36);
    display.print("Sect:");
    display.print(currentSectorLabel());
    display.setCursor(0, 48);
    display.print("Slip:");
    display.print(static_cast<int>(fabsf(_playerSlipVisual) * 99.0f));
    display.setCursor(48, 48);
    display.print("AI:");
    display.print(aiPressure());
    display.setCursor(72, 48);
    display.print("R:");
    display.print(_overtakeStreak);
    display.setCursor(0, 56);
    display.print("L:");
    display.print(launchLabel());
    display.setCursor(48, 56);
    display.print(kPongDifficultyNames[_difficulty]);
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
    result.score = (9 - _finishRank) * 260 + _score * 80 + (_completed ? 800 : 0);
    result.durationMs = _durationMs;
    result.completed = _completed;
    snprintf(result.detail, sizeof(result.detail), "#%u %s %s", _finishRank, selectedCarProfile().name, selectedTrackProfile().name);
    return result;
  }

private:
  enum class Stage : uint8_t
  {
    SelectCar,
    SelectTrack,
    Countdown,
    Race
  };

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

  enum class SceneTheme : uint8_t
  {
    DowntownDay,
    NeonNight,
    HarborSunset,
    TunnelRun,
    Ruins,
    MetroRain,
    SnowPort,
    PinePass,
    CanyonHeat,
    AlpineFog
  };

  enum class AiIntent : uint8_t
  {
    Cruise,
    Overtake,
    Defend,
    Recover
  };

  enum class LaunchState : uint8_t
  {
    None,
    Perfect,
    Good,
    Slow,
    Spin
  };

  struct TrackSegment
  {
    constexpr TrackSegment() = default;
    constexpr TrackSegment(uint8_t segmentLength, float segmentCurve, uint8_t roadsideStyle)
        : length(segmentLength), curve(segmentCurve), roadside(roadsideStyle)
    {
    }

    uint8_t length = 16;
    float curve = 0.0f;
    uint8_t roadside = 0;
  };

  struct CarProfile
  {
    const char *name;
    VehicleKind kind;
    uint16_t bodyColor;
    uint16_t detailColor;
    float speedBias;
    float shiftBias;
    float gripBias;
  };

  struct TrackProfile
  {
    const char *name;
    SceneTheme theme;
    uint16_t shoulderColor;
    uint16_t roadColor;
    uint16_t lineColor;
    uint16_t skyTop;
    uint16_t skyBottom;
    uint16_t glowColor;
    float distanceScale;
    float trafficScale;
    float aiScale;
    float curveIntensity;
    float roadWidthScale;
    uint8_t segmentSet;
    uint8_t trafficStyle;
  };

  struct Obstacle
  {
    bool active = false;
    uint8_t lane = 0;
    uint8_t targetLane = 0;
    float y = -40.0f;
    float x = 0.0f;
    float targetX = 0.0f;
    uint8_t width = 16;
    uint8_t height = 20;
    VehicleKind kind = VehicleKind::Compact;
    uint16_t bodyColor = COLOR_DANGER;
    uint16_t detailColor = COLOR_TEXT;
    uint8_t behavior = 0;
    bool laneChanging = false;
  };

  struct ScreenRect
  {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    bool valid = false;
  };

  static bool rectsOverlap(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh)
  {
    return ax < (bx + bw) && (ax + aw) > bx && ay < (by + bh) && (ay + ah) > by;
  }

  static ScreenRect insetRect(const ScreenRect &rect, int insetX, int insetY)
  {
    ScreenRect out = rect;
    out.x += insetX;
    out.y += insetY;
    out.w = max(1, rect.w - insetX * 2);
    out.h = max(1, rect.h - insetY * 2);
    out.valid = rect.valid;
    return out;
  }

  static ScreenRect playerHitRectFromRect(const ScreenRect &rect)
  {
    ScreenRect out = rect;
    if (!rect.valid)
    {
      return out;
    }
    out.x += 2;
    out.w = max(7, rect.w - 4);
    out.y += max(1, rect.h / 5);
    out.h = max(8, (rect.h * 3) / 5);
    return out;
  }

  static ScreenRect rivalHitRectFromRect(const ScreenRect &rect)
  {
    ScreenRect out = rect;
    if (!rect.valid)
    {
      return out;
    }
    out.x += 2;
    out.w = max(7, rect.w - 4);
    out.y += rect.h / 3;
    out.h = max(6, (rect.h * 3) / 5);
    return out;
  }

  static int laneCenter(uint8_t lane)
  {
    return kRoadX + static_cast<int>(lane) * kLaneWidth + (kLaneWidth / 2);
  }

  static int laneDividerX(uint8_t divider)
  {
    return kRoadX + static_cast<int>(divider) * kLaneWidth;
  }

  static const CarProfile *carTable()
  {
    static const CarProfile table[kCarCount] = {
        {"AE86", VehicleKind::Compact, color565(236, 240, 244), color565(34, 36, 40), 0.94f, 1.10f, 1.10f},
        {"RX7", VehicleKind::Compact, color565(243, 110, 36), color565(230, 240, 252), 1.08f, 1.02f, 0.98f},
        {"R32", VehicleKind::Van, color565(74, 116, 214), color565(216, 230, 248), 1.10f, 0.94f, 1.00f},
        {"Supra", VehicleKind::Pickup, color565(255, 158, 28), color565(33, 38, 46), 1.12f, 0.92f, 0.95f},
        {"S13", VehicleKind::Compact, color565(52, 200, 152), color565(232, 248, 246), 1.00f, 1.08f, 1.08f},
        {"TypeR", VehicleKind::Compact, color565(220, 54, 76), color565(245, 245, 245), 1.03f, 1.06f, 1.06f},
        {"Evo3", VehicleKind::Van, color565(248, 244, 246), color565(195, 26, 46), 1.06f, 1.01f, 1.03f},
        {"GT4", VehicleKind::Pickup, color565(31, 138, 230), color565(242, 245, 246), 1.01f, 0.99f, 1.10f},
        {"Miata", VehicleKind::Compact, color565(204, 40, 80), color565(242, 218, 224), 0.97f, 1.09f, 1.08f},
        {"MR2", VehicleKind::Compact, color565(240, 222, 60), color565(38, 40, 44), 1.05f, 1.02f, 1.01f},
        {"NSX", VehicleKind::Compact, color565(226, 54, 42), color565(236, 239, 245), 1.10f, 1.00f, 1.00f},
        {"Z32", VehicleKind::Taxi, color565(122, 88, 214), color565(232, 236, 250), 1.07f, 0.99f, 0.99f},
        {"Sil80", VehicleKind::Compact, color565(214, 242, 252), color565(44, 62, 88), 0.99f, 1.07f, 1.05f},
        {"Cosmo", VehicleKind::Van, color565(66, 72, 84), color565(206, 220, 248), 1.04f, 0.97f, 1.02f},
        {"WRX", VehicleKind::Pickup, color565(46, 116, 238), color565(248, 248, 250), 1.06f, 0.96f, 1.12f},
        {"GTO", VehicleKind::Truck, color565(238, 64, 52), color565(214, 218, 228), 1.09f, 0.90f, 0.96f},
        {"S660", VehicleKind::Compact, color565(252, 244, 88), color565(34, 38, 44), 0.92f, 1.12f, 1.10f},
        {"Copen", VehicleKind::Compact, color565(42, 188, 172), color565(232, 248, 248), 0.91f, 1.11f, 1.08f},
        {"Lotus", VehicleKind::Compact, color565(24, 168, 84), color565(244, 246, 236), 1.05f, 1.04f, 1.04f},
        {"Cobra", VehicleKind::Pickup, color565(22, 84, 214), color565(246, 246, 246), 1.11f, 0.93f, 0.95f},
        {"GT40", VehicleKind::Compact, color565(36, 104, 206), color565(248, 244, 228), 1.12f, 0.95f, 0.98f},
        {"Viper", VehicleKind::Truck, color565(40, 74, 224), color565(242, 242, 246), 1.13f, 0.89f, 0.92f},
        {"Lambo", VehicleKind::Compact, color565(236, 214, 24), color565(26, 30, 36), 1.14f, 0.92f, 0.94f},
        {"F40", VehicleKind::Compact, color565(214, 22, 32), color565(242, 242, 242), 1.13f, 0.94f, 0.94f},
        {"Diablo", VehicleKind::Pickup, color565(244, 114, 30), color565(42, 42, 42), 1.15f, 0.88f, 0.90f},
        {"Strato", VehicleKind::Taxi, color565(246, 246, 246), color565(34, 94, 216), 1.02f, 1.06f, 1.07f},
        {"M1", VehicleKind::Van, color565(244, 246, 252), color565(56, 84, 214), 1.08f, 0.97f, 1.01f},
        {"M3", VehicleKind::Van, color565(58, 96, 222), color565(245, 245, 245), 1.06f, 0.98f, 1.03f},
        {"Nine11", VehicleKind::Compact, color565(214, 214, 214), color565(36, 36, 42), 1.09f, 0.97f, 1.00f},
        {"R8", VehicleKind::Compact, color565(190, 196, 204), color565(48, 48, 52), 1.10f, 0.96f, 0.99f}};
    return table;
  }

  static const TrackProfile *trackTable()
  {
    static const TrackProfile table[kTrackCount] = {
        {"Downtown", SceneTheme::DowntownDay, color565(72, 82, 86), color565(42, 44, 54), color565(244, 238, 188), color565(48, 104, 176), color565(230, 176, 118), color565(255, 186, 96), 1.00f, 0.84f, 1.00f, 1.00f, 1.00f, 0, 0},
        {"NeonNite", SceneTheme::NeonNight, color565(84, 26, 102), color565(34, 36, 46), color565(66, 244, 230), color565(18, 22, 66), color565(110, 42, 126), color565(255, 92, 162), 1.08f, 0.88f, 1.03f, 1.14f, 0.98f, 1, 1},
        {"Harbor", SceneTheme::HarborSunset, color565(126, 112, 88), color565(48, 50, 56), color565(230, 230, 210), color565(255, 150, 72), color565(96, 126, 178), color565(255, 212, 122), 1.04f, 0.82f, 1.00f, 0.94f, 1.00f, 2, 2},
        {"TunnelRun", SceneTheme::TunnelRun, color565(54, 56, 66), color565(34, 34, 40), color565(255, 222, 112), color565(14, 18, 30), color565(44, 52, 72), color565(96, 196, 255), 0.96f, 0.86f, 1.03f, 0.90f, 0.94f, 3, 3},
        {"Ruins", SceneTheme::Ruins, color565(124, 98, 78), color565(52, 48, 44), color565(232, 210, 160), color565(168, 118, 78), color565(244, 196, 118), color565(255, 218, 152), 1.12f, 0.85f, 1.05f, 1.20f, 1.02f, 4, 4},
        {"MetroRain", SceneTheme::MetroRain, color565(76, 84, 98), color565(30, 34, 44), color565(220, 226, 236), color565(34, 58, 92), color565(84, 98, 122), color565(86, 192, 255), 0.98f, 0.90f, 1.02f, 1.08f, 0.98f, 5, 5},
        {"SnowPort", SceneTheme::SnowPort, color565(220, 228, 238), color565(74, 88, 108), color565(254, 248, 218), color565(142, 178, 214), color565(238, 242, 246), color565(255, 246, 208), 1.06f, 0.78f, 0.97f, 0.92f, 1.04f, 6, 6},
        {"PinePass", SceneTheme::PinePass, color565(68, 118, 84), color565(44, 50, 46), color565(236, 238, 212), color565(52, 90, 114), color565(172, 200, 184), color565(240, 224, 168), 1.02f, 0.82f, 0.99f, 1.10f, 1.00f, 7, 7},
        {"Canyon", SceneTheme::CanyonHeat, color565(176, 118, 72), color565(58, 50, 48), color565(255, 236, 160), color565(246, 138, 62), color565(255, 208, 124), color565(255, 188, 98), 1.10f, 0.84f, 1.04f, 1.22f, 1.01f, 8, 8},
        {"AlpineFog", SceneTheme::AlpineFog, color565(188, 196, 204), color565(56, 60, 70), color565(246, 246, 250), color565(120, 132, 150), color565(214, 220, 228), color565(236, 236, 228), 1.14f, 0.76f, 0.98f, 1.06f, 1.03f, 9, 9}};
    return table;
  }

  const CarProfile &selectedCarProfile() const
  {
    return carTable()[_selectedCar % kCarCount];
  }

  const TrackProfile &selectedTrackProfile() const
  {
    return trackTable()[_selectedTrack % kTrackCount];
  }

  static uint8_t expand5(uint8_t value)
  {
    return static_cast<uint8_t>((value << 3) | (value >> 2));
  }

  static uint8_t expand6(uint8_t value)
  {
    return static_cast<uint8_t>((value << 2) | (value >> 4));
  }

  static uint16_t blend565(uint16_t a, uint16_t b, float t)
  {
    t = clampValue(t, 0.0f, 1.0f);
    const uint8_t ar = expand5((a >> 11) & 0x1F);
    const uint8_t ag = expand6((a >> 5) & 0x3F);
    const uint8_t ab = expand5(a & 0x1F);
    const uint8_t br = expand5((b >> 11) & 0x1F);
    const uint8_t bg = expand6((b >> 5) & 0x3F);
    const uint8_t bb = expand5(b & 0x1F);
    const uint8_t r = static_cast<uint8_t>(ar + (br - ar) * t);
    const uint8_t g = static_cast<uint8_t>(ag + (bg - ag) * t);
    const uint8_t b8 = static_cast<uint8_t>(ab + (bb - ab) * t);
    return color565(r, g, b8);
  }

  static float smoothCurveBlend(float t)
  {
    t = clampValue(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
  }

  static const TrackSegment *segmentTable(uint8_t set, uint8_t &count)
  {
    static const TrackSegment s0[] = {{12, 0.00f, 0}, {10, 0.22f, 0}, {10, 0.34f, 0}, {8, -0.18f, 5}, {10, -0.30f, 0}, {10, 0.20f, 0}, {12, 0.00f, 0}};
    static const TrackSegment s1[] = {{10, 0.00f, 1}, {10, -0.26f, 1}, {8, 0.34f, 1}, {8, -0.38f, 5}, {10, 0.18f, 1}, {10, -0.24f, 1}, {12, 0.00f, 1}};
    static const TrackSegment s2[] = {{10, 0.00f, 2}, {10, 0.18f, 2}, {8, -0.24f, 2}, {8, 0.34f, 2}, {8, -0.18f, 0}, {10, 0.26f, 2}, {12, 0.00f, 2}};
    static const TrackSegment s3[] = {{10, 0.00f, 3}, {10, 0.16f, 3}, {8, -0.20f, 3}, {8, 0.18f, 3}, {8, -0.24f, 3}, {10, 0.14f, 3}, {10, 0.00f, 3}};
    static const TrackSegment s4[] = {{10, 0.00f, 4}, {8, 0.28f, 4}, {8, 0.42f, 4}, {8, -0.24f, 8}, {8, -0.34f, 4}, {8, 0.30f, 4}, {12, 0.00f, 4}};
    static const TrackSegment s5[] = {{10, 0.00f, 5}, {8, -0.20f, 5}, {8, 0.30f, 5}, {8, -0.30f, 5}, {8, 0.34f, 5}, {8, -0.18f, 5}, {12, 0.00f, 0}};
    static const TrackSegment s6[] = {{12, 0.00f, 6}, {8, 0.18f, 6}, {8, -0.22f, 6}, {8, 0.16f, 2}, {8, -0.20f, 6}, {10, 0.14f, 6}, {12, 0.00f, 6}};
    static const TrackSegment s7[] = {{10, 0.00f, 7}, {10, -0.28f, 7}, {8, -0.40f, 7}, {8, 0.22f, 7}, {8, 0.30f, 9}, {10, -0.16f, 7}, {12, 0.00f, 9}};
    static const TrackSegment s8[] = {{10, 0.00f, 8}, {8, 0.26f, 8}, {8, 0.38f, 8}, {8, -0.32f, 8}, {8, -0.18f, 4}, {8, 0.24f, 8}, {12, 0.00f, 8}};
    static const TrackSegment s9[] = {{10, 0.00f, 9}, {8, -0.24f, 9}, {8, 0.30f, 9}, {8, -0.34f, 9}, {8, 0.18f, 7}, {8, -0.20f, 9}, {12, 0.00f, 9}};

    switch (set % 10)
    {
    case 0:
      count = sizeof(s0) / sizeof(s0[0]);
      return s0;
    case 1:
      count = sizeof(s1) / sizeof(s1[0]);
      return s1;
    case 2:
      count = sizeof(s2) / sizeof(s2[0]);
      return s2;
    case 3:
      count = sizeof(s3) / sizeof(s3[0]);
      return s3;
    case 4:
      count = sizeof(s4) / sizeof(s4[0]);
      return s4;
    case 5:
      count = sizeof(s5) / sizeof(s5[0]);
      return s5;
    case 6:
      count = sizeof(s6) / sizeof(s6[0]);
      return s6;
    case 7:
      count = sizeof(s7) / sizeof(s7[0]);
      return s7;
    case 8:
      count = sizeof(s8) / sizeof(s8[0]);
      return s8;
    default:
      count = sizeof(s9) / sizeof(s9[0]);
      return s9;
    }
  }

  float normalizedTrackProgress(float progress) const
  {
    if (progress < 0.0f)
    {
      progress = 0.0f;
    }
    while (progress > 1.0f)
    {
      progress -= 1.0f;
    }
    return progress;
  }

  float sampleCurveAtProgress(float progress) const
  {
    progress = normalizedTrackProgress(progress);
    uint8_t count = 0;
    const TrackSegment *segments = segmentTable(selectedTrackProfile().segmentSet, count);
    int total = 0;
    for (uint8_t i = 0; i < count; ++i)
    {
      total += segments[i].length;
    }
    if (total <= 0)
    {
      return 0.0f;
    }

    const float cursorTarget = progress * total;
    int cursor = 0;
    for (uint8_t i = 0; i < count; ++i)
    {
      const int next = cursor + segments[i].length;
      if (cursorTarget <= next || i == count - 1)
      {
        const float local = clampValue((cursorTarget - cursor) / max(1.0f, static_cast<float>(segments[i].length)), 0.0f, 1.0f);
        const float previousCurve = (i == 0) ? segments[i].curve : segments[i - 1].curve;
        return previousCurve + (segments[i].curve - previousCurve) * smoothCurveBlend(local);
      }
      cursor = next;
    }
    return 0.0f;
  }

  uint8_t roadsideAtProgress(float progress) const
  {
    progress = normalizedTrackProgress(progress);
    uint8_t count = 0;
    const TrackSegment *segments = segmentTable(selectedTrackProfile().segmentSet, count);
    int total = 0;
    for (uint8_t i = 0; i < count; ++i)
    {
      total += segments[i].length;
    }
    if (total <= 0)
    {
      return 0;
    }

    const float cursorTarget = progress * total;
    int cursor = 0;
    for (uint8_t i = 0; i < count; ++i)
    {
      cursor += segments[i].length;
      if (cursorTarget <= cursor || i == count - 1)
      {
        return segments[i].roadside;
      }
    }
    return segments[count - 1].roadside;
  }

  static const char *roadsideLabel(uint8_t roadside)
  {
    switch (roadside)
    {
    case 0:
      return "City";
    case 1:
      return "Neon";
    case 2:
      return "Pier";
    case 3:
      return "Tunnel";
    case 4:
      return "Ruins";
    case 5:
      return "Metro";
    case 6:
      return "Snow";
    case 7:
      return "Pine";
    case 8:
      return "Canyon";
    default:
      return "Alps";
    }
  }

  static const char *themeLabel(SceneTheme theme)
  {
    switch (theme)
    {
    case SceneTheme::DowntownDay:
      return "Downtown";
    case SceneTheme::NeonNight:
      return "Neon Night";
    case SceneTheme::HarborSunset:
      return "Harbor";
    case SceneTheme::TunnelRun:
      return "Tunnel";
    case SceneTheme::Ruins:
      return "Ruins";
    case SceneTheme::MetroRain:
      return "Metro Rain";
    case SceneTheme::SnowPort:
      return "Snow Port";
    case SceneTheme::PinePass:
      return "Pine";
    case SceneTheme::CanyonHeat:
      return "Canyon";
    default:
      return "Alpine";
    }
  }

  const char *curveStateLabel() const
  {
    if (_curveCurrent > 0.12f)
    {
      return "Right";
    }
    if (_curveCurrent < -0.12f)
    {
      return "Left";
    }
    return "Straight";
  }

  const char *launchLabel() const
  {
    switch (_launchState)
    {
    case LaunchState::Perfect:
      return "Perfect";
    case LaunchState::Good:
      return "Good";
    case LaunchState::Slow:
      return "Slow";
    case LaunchState::Spin:
      return "Spin";
    default:
      return "Warm";
    }
  }

  float launchGreenMin() const
  {
    return (_difficulty == 0) ? 38.0f : (_difficulty == 1) ? 44.0f
                                                           : 50.0f;
  }

  float launchGreenMax() const
  {
    return (_difficulty == 0) ? 70.0f : (_difficulty == 1) ? 74.0f
                                                           : 78.0f;
  }

  void updateLaunchMeter(const InputSnapshot &input, float dt)
  {
    const float fillRate = (_difficulty == 0) ? 92.0f : (_difficulty == 1) ? 104.0f
                                                                            : 118.0f;
    const float drainRate = (_difficulty == 0) ? 68.0f : (_difficulty == 1) ? 74.0f
                                                                             : 80.0f;
    if (input.btn1Down && input.btn2Down)
    {
      _launchMeter += fillRate * dt;
    }
    else
    {
      _launchMeter -= drainRate * dt;
    }
    _launchMeter = clampValue(_launchMeter, 0.0f, 100.0f);
  }

  void applyLaunchResult(uint32_t nowMs)
  {
    const float low = launchGreenMin();
    const float high = launchGreenMax();
    if (_launchMeter >= low && _launchMeter <= high)
    {
      _launchState = LaunchState::Perfect;
      _launchEffectUntilMs = nowMs + 2200;
    }
    else if (_launchMeter >= max(20.0f, low - 12.0f) && _launchMeter < low)
    {
      _launchState = LaunchState::Good;
      _launchEffectUntilMs = nowMs + 1400;
    }
    else if (_launchMeter > (high + 14.0f))
    {
      _launchState = LaunchState::Spin;
      _launchEffectUntilMs = nowMs + 1500;
    }
    else
    {
      _launchState = LaunchState::Slow;
      _launchEffectUntilMs = nowMs + 900;
    }
  }

  const char *currentSectorLabel() const
  {
    const uint8_t roadside = roadsideAtProgress(_trackProgress);
    if (fabsf(_curveCurrent) > 0.18f)
    {
      return (_curveCurrent > 0.0f) ? "Right Sweep" : "Left Sweep";
    }
    switch (roadside)
    {
    case 0:
      return "Start/Finish";
    case 1:
      return "Night Sector";
    case 2:
      return "Dock Chicane";
    case 3:
      return "Tunnel Esses";
    case 4:
      return "Stone Apex";
    case 5:
      return "Wet S";
    case 6:
      return "Snow Kink";
    case 7:
      return "Forest Curve";
    case 8:
      return "Desert Bend";
    default:
      return "Fog Sweep";
    }
  }

  float previewBaseDistance() const
  {
    return (_difficulty == 0) ? 1700.0f : (_difficulty == 1) ? 2500.0f : 3300.0f;
  }

  float previewDistanceKm(const TrackProfile &track) const
  {
    return (previewBaseDistance() * track.distanceScale) / 1000.0f;
  }

  void beginRace(uint32_t nowMs)
  {
    const CarProfile &car = selectedCarProfile();
    const TrackProfile &track = selectedTrackProfile();
    _stage = Stage::Countdown;
    _startMs = nowMs;
    _countdownStartMs = nowMs;
    _lastUpdateMs = nowMs;
    _durationMs = 0;
    _spawnTimer = 0.0f;
    _roadDashOffset = 0.0f;
    _score = 0;
    _launchMeter = 0.0f;
    _launchState = LaunchState::None;
    _launchEffectUntilMs = 0;
    _overtakeBoostUntilMs = 0;
    _overtakeStreak = 0;
    _raceStartMs = 0;
    _raceShieldUntilMs = 0;
    _playerLane = 1;
    _targetLane = 1;
    _playerOffsetNorm = 0.0f;
    _playerCenterX = worldXForOffsetNorm(_playerOffsetNorm);
    _playerDistance = 0.0f;
    _trackProgress = 0.0f;
    _curveCurrent = 0.0f;
    _curveTarget = 0.0f;
    _playerSlipVisual = 0.0f;
    _crashEffectUntilMs = 0;
    _crashX = hw::tftWidth / 2;
    _crashY = kPlayerY;
    _progressPct = 0;
    _playerRank = 1;
    _finishRank = 8;
    _lastSpawnLane = 1;
    _roadSpeed = ((_difficulty == 0) ? 60.0f : (_difficulty == 1) ? 76.0f : 92.0f) * (0.96f + track.trafficScale * 0.10f);
    _laneShiftSpeed = ((_difficulty == 0) ? 122.0f : (_difficulty == 1) ? 138.0f : 154.0f) * car.shiftBias;
    _spawnInterval = ((_difficulty == 0) ? 1.92f : (_difficulty == 1) ? 1.58f : 1.28f) / max(0.72f, track.trafficScale);
    _playerRaceSpeed = ((_difficulty == 0) ? 92.0f : (_difficulty == 1) ? 106.0f : 120.0f) * car.speedBias;
    _totalDistance = previewBaseDistance() * track.distanceScale;
    for (uint8_t i = 0; i < kObstacleCount; ++i)
    {
      _obstacles[i] = {};
    }
    for (uint8_t i = 0; i < kAiCount; ++i)
    {
      _aiDistance[i] = -260.0f + i * 34.0f;
      _aiPhase[i] = random(0, 628) / 100.0f;
      _aiCarIndex[i] = (_selectedCar + 3 + i * 4) % kCarCount;
      const float tier = (i < 3) ? (0.80f + i * 0.04f) : (i < 6) ? (0.92f + (i - 3) * 0.03f) : 1.07f;
      _aiRaceSpeed[i] = _playerRaceSpeed * tier * carTable()[_aiCarIndex[i]].speedBias * track.aiScale;
      _aiOffsetNorm[i] = offsetNormForLane(i % 3);
      _aiTargetOffsetNorm[i] = _aiOffsetNorm[i];
      _aiIntent[i] = AiIntent::Cruise;
      _aiDecisionAtMs[i] = nowMs + 180 + random(0, 320);
      _aiWasAhead[i] = _aiDistance[i] > _playerDistance;
    }
  }

  void updateRaceRank()
  {
    _playerRank = 1;
    for (uint8_t i = 0; i < kAiCount; ++i)
    {
      if (_aiDistance[i] > _playerDistance)
      {
        ++_playerRank;
      }
    }
  }

  static float offsetNormForLane(uint8_t lane)
  {
    switch (lane)
    {
    case 0:
      return -0.58f;
    case 2:
      return 0.58f;
    default:
      return 0.0f;
    }
  }

  float worldXForOffsetNorm(float offsetNorm) const
  {
    return kRoadX + kRoadW * (0.5f + offsetNorm * 0.42f);
  }

  float offsetNormForWorldX(float worldX) const
  {
    const float ratio = clampValue((worldX - kRoadX) / static_cast<float>(kRoadW), 0.0f, 1.0f);
    return clampValue((ratio - 0.5f) / 0.42f, -1.0f, 1.0f);
  }

  void syncPlayerLaneFromOffset()
  {
    if (_playerOffsetNorm < -0.22f)
    {
      _playerLane = 0;
    }
    else if (_playerOffsetNorm > 0.22f)
    {
      _playerLane = 2;
    }
    else
    {
      _playerLane = 1;
    }
    _targetLane = _playerLane;
  }

  float nearestTrafficPenalty(float offsetNorm, uint8_t ignoreIndex = 255) const
  {
    float penalty = 0.0f;
    for (uint8_t i = 0; i < kObstacleCount; ++i)
    {
      if (!_obstacles[i].active || i == ignoreIndex)
      {
        continue;
      }
      const float trafficOffset = offsetNormForWorldX(_obstacles[i].x);
      const float depth = 1.0f - clampValue((_obstacles[i].y - (kHorizonY + 6)) / static_cast<float>(hw::tftHeight - kHorizonY), 0.0f, 1.0f);
      if (fabsf(trafficOffset - offsetNorm) < 0.20f && _obstacles[i].y > (kHorizonY + 4) && _obstacles[i].y < (kPlayerY + 4))
      {
        penalty += (0.22f + depth * 0.50f);
      }
    }
    return penalty;
  }

  float chooseAiOffset(uint8_t index, float preferred) const
  {
    static const float candidates[] = {-0.72f, -0.38f, 0.0f, 0.38f, 0.72f};
    float bestOffset = clampValue(preferred, -0.78f, 0.78f);
    float bestScore = -1000.0f;

    for (uint8_t c = 0; c < sizeof(candidates) / sizeof(candidates[0]); ++c)
    {
      const float candidate = candidates[c];
      float score = 100.0f - fabsf(candidate - preferred) * 46.0f;
      score -= nearestTrafficPenalty(candidate) * 90.0f;

      for (uint8_t other = 0; other < kAiCount; ++other)
      {
        if (other == index)
        {
          continue;
        }
        if (fabsf(_aiDistance[other] - _aiDistance[index]) < 92.0f && fabsf(_aiTargetOffsetNorm[other] - candidate) < 0.18f)
        {
          score -= 30.0f;
        }
      }

      if (fabsf(candidate) > 0.74f)
      {
        score -= 18.0f;
      }

      if (score > bestScore)
      {
        bestScore = score;
        bestOffset = candidate;
      }
    }

    return bestOffset;
  }

  void updateAiRival(uint8_t index, uint32_t nowMs, float dt)
  {
    _aiPhase[index] += dt * (0.90f + index * 0.08f);
    const float difficultyAggro = (_difficulty == 0) ? 0.88f : (_difficulty == 1) ? 1.00f
                                                                                   : 1.12f;
    const float skill = ((index < 3) ? 0.74f : (index < 6) ? 0.96f
                                                            : 1.18f) *
                        difficultyAggro;
    const float distanceGapToPlayer = _playerDistance - _aiDistance[index];
    const float myProgress = clampValue(_aiDistance[index] / max(1.0f, _totalDistance), 0.0f, 1.0f);
    const float idealLine = clampValue(-sampleCurveAtProgress(myProgress + 0.04f) * 0.38f, -0.60f, 0.60f);

    if (nowMs >= _aiDecisionAtMs[index])
    {
      float desired = idealLine;
      AiIntent intent = AiIntent::Cruise;

      bool blockedByTraffic = nearestTrafficPenalty(_aiOffsetNorm[index]) > 0.18f;
      bool blockedByAi = false;
      for (uint8_t other = 0; other < kAiCount; ++other)
      {
        if (other == index)
        {
          continue;
        }
        if ((_aiDistance[other] > _aiDistance[index]) && (_aiDistance[other] - _aiDistance[index]) < 88.0f &&
            fabsf(_aiOffsetNorm[other] - _aiOffsetNorm[index]) < 0.18f)
        {
          blockedByAi = true;
          break;
        }
      }

      if ((blockedByTraffic || blockedByAi) && skill >= 0.74f)
      {
        desired = chooseAiOffset(index, _aiOffsetNorm[index] + ((_aiOffsetNorm[index] <= 0.0f) ? 0.36f : -0.36f));
        intent = AiIntent::Overtake;
      }
      else if (distanceGapToPlayer > 12.0f && distanceGapToPlayer < 140.0f)
      {
        desired = chooseAiOffset(index, _playerOffsetNorm + ((index & 1U) ? 0.28f : -0.28f));
        intent = AiIntent::Overtake;
      }
      else if (distanceGapToPlayer < -8.0f && distanceGapToPlayer > -118.0f && skill > 1.0f)
      {
        desired = chooseAiOffset(index, _playerOffsetNorm);
        intent = AiIntent::Defend;
      }
      else
      {
        desired = chooseAiOffset(index, idealLine);
        intent = AiIntent::Recover;
      }

      _aiTargetOffsetNorm[index] = clampValue(desired, -0.80f, 0.80f);
      _aiIntent[index] = intent;
      _aiDecisionAtMs[index] = nowMs + static_cast<uint32_t>(340 + (1.25f - skill) * 210.0f + random(0, 180));
    }

    const float shiftRate = (0.42f + skill * 0.34f) * dt;
    const float delta = _aiTargetOffsetNorm[index] - _aiOffsetNorm[index];
    _aiOffsetNorm[index] += clampValue(delta, -shiftRate, shiftRate);

    float speedMul = 1.0f + 0.035f * sinf(_aiPhase[index]);
    if (_aiIntent[index] == AiIntent::Overtake)
    {
      speedMul += 0.06f + 0.03f * skill;
    }
    else if (_aiIntent[index] == AiIntent::Defend)
    {
      speedMul += 0.03f + 0.02f * skill;
    }
    else if (_aiIntent[index] == AiIntent::Recover)
    {
      speedMul -= 0.03f;
    }

    if (fabsf(_aiOffsetNorm[index]) > 0.84f)
    {
      speedMul *= 0.82f;
    }

    if (_difficulty == 0 && _playerRank > 4)
    {
      speedMul *= 0.95f;
    }
    else if (_difficulty == 2 && _playerRank <= 3)
    {
      speedMul *= 1.03f;
    }

    _aiDistance[index] += _aiRaceSpeed[index] * speedMul * dt;
  }

  uint8_t aiPressure() const
  {
    uint8_t pressure = 0;
    for (uint8_t i = 0; i < kAiCount; ++i)
    {
      const float delta = _aiDistance[i] - _playerDistance;
      if (delta > -40.0f && delta < 130.0f && fabsf(_aiOffsetNorm[i] - _playerOffsetNorm) < 0.28f)
      {
        ++pressure;
      }
    }
    return pressure;
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
    candidate.targetLane = lane;
    candidate.x = static_cast<float>(laneCenter(lane));
    candidate.targetX = candidate.x;
    candidate.y = -static_cast<float>(candidate.height) - random(6, 16);
    candidate.behavior = random(0, 3);
    candidate.laneChanging = false;
    _obstacles[freeIndex] = candidate;
  }

  bool canShiftTrafficTo(uint8_t lane, const Obstacle &source) const
  {
    if (lane > 2)
    {
      return false;
    }
    for (uint8_t i = 0; i < kObstacleCount; ++i)
    {
      const Obstacle &other = _obstacles[i];
      if (!other.active || &other == &source)
      {
        continue;
      }
      if (other.targetLane != lane && other.lane != lane)
      {
        continue;
      }
      if (fabsf(other.y - source.y) < 22.0f)
      {
        return false;
      }
    }
    return true;
  }

  void tryLaneShift(Obstacle &obstacle, uint8_t lane)
  {
    if (lane == obstacle.lane || lane > 2 || !canShiftTrafficTo(lane, obstacle))
    {
      return;
    }
    obstacle.targetLane = lane;
    obstacle.targetX = static_cast<float>(laneCenter(lane));
    obstacle.laneChanging = true;
  }

  void updateTrafficBehavior(Obstacle &obstacle, float dt)
  {
    if (!obstacle.active)
    {
      return;
    }

    if (!obstacle.laneChanging && obstacle.y > 24.0f && obstacle.y < 68.0f)
    {
      if (obstacle.behavior == 0)
      {
        if (obstacle.lane != _targetLane && abs(static_cast<int>(obstacle.lane) - static_cast<int>(_targetLane)) == 1)
        {
          tryLaneShift(obstacle, _targetLane);
        }
      }
      else if (obstacle.behavior == 1)
      {
        if (obstacle.lane == _targetLane)
        {
          const uint8_t preferred = (_targetLane == 0) ? 1 : (_targetLane == 2) ? 1
                                                                                 : ((millis() / 320U) & 1U) ? 0 : 2;
          tryLaneShift(obstacle, preferred);
        }
      }
      else if (obstacle.behavior == 2)
      {
        const uint8_t preferred = (obstacle.lane == 1) ? static_cast<uint8_t>(((millis() / 180U) & 1U) ? 0 : 2) : 1;
        tryLaneShift(obstacle, preferred);
      }
    }

    if (obstacle.laneChanging)
    {
      const float delta = obstacle.targetX - obstacle.x;
      const float maxShift = ((_difficulty == 0) ? 38.0f : (_difficulty == 1) ? 52.0f : 66.0f) * dt;
      if (fabsf(delta) <= maxShift)
      {
        obstacle.x = obstacle.targetX;
        obstacle.lane = obstacle.targetLane;
        obstacle.laneChanging = false;
      }
      else
      {
        obstacle.x += (delta > 0.0f) ? maxShift : -maxShift;
      }
    }
  }

  int roadLeftAtY(int y) const
  {
    return roadCenterAtY(y) - roadHalfWidthAtY(y);
  }

  int roadRightAtY(int y) const
  {
    return roadCenterAtY(y) + roadHalfWidthAtY(y);
  }

  int roadCenterAtY(int y) const
  {
    const float t = clampValue((y - kHorizonY) / static_cast<float>(hw::tftHeight - kHorizonY), 0.0f, 1.0f);
    const float nearCurve = sampleCurveAtProgress(_trackProgress + t * 0.10f);
    const float midCurve = sampleCurveAtProgress(_trackProgress + t * 0.24f);
    const float farCurve = sampleCurveAtProgress(_trackProgress + t * 0.38f);
    const float horizonCurve = sampleCurveAtProgress(_trackProgress + t * 0.52f);
    const float curveBlend = nearCurve * 0.10f + midCurve * 0.24f + farCurve * 0.42f + horizonCurve * 0.24f;
    const float perspective = powf(t, 1.58f);
    const float shift = curveBlend * selectedTrackProfile().curveIntensity * (34.0f + perspective * 156.0f);
    return (hw::tftWidth / 2) + static_cast<int>(shift);
  }

  int roadHalfWidthAtY(int y) const
  {
    const float t = clampValue((y - kHorizonY) / static_cast<float>(hw::tftHeight - kHorizonY), 0.0f, 1.0f);
    return static_cast<int>((15.0f + powf(t, 1.18f) * 62.0f) * selectedTrackProfile().roadWidthScale);
  }

  int projectRoadX(float worldX, int y) const
  {
    const float ratio = clampValue((worldX - kRoadX) / static_cast<float>(kRoadW), 0.0f, 1.0f);
    const int left = roadLeftAtY(y);
    const int right = roadRightAtY(y);
    return left + static_cast<int>((right - left) * ratio);
  }

  void renderCityBackdrop(Adafruit_ST7735 &display) const
  {
    const TrackProfile &track = selectedTrackProfile();
    for (int y = 0; y < kHorizonY; ++y)
    {
      const float t = y / static_cast<float>(max(1, kHorizonY - 1));
      display.drawFastHLine(0, y, hw::tftWidth, blend565(track.skyTop, track.skyBottom, t));
    }

    switch (track.theme)
    {
    case SceneTheme::DowntownDay:
    case SceneTheme::NeonNight:
    case SceneTheme::MetroRain:
      for (uint8_t i = 0; i < 8; ++i)
      {
        const int x = 4 + i * 19 + ((_selectedTrack * 3 + i * 7) % 5);
        const int w = 12 + ((i * 5 + _selectedTrack) % 10);
        const int h = 10 + ((i * 7 + _selectedTrack * 3) % 18);
        const int top = kHorizonY - h;
        const uint16_t body = blend565(color565(18, 22, 28), track.roadColor, 0.30f);
        display.fillRect(x, top, w, h, body);
        for (int wx = x + 2; wx < (x + w - 1); wx += 4)
        {
          for (int wy = top + 2; wy < (top + h - 2); wy += 5)
          {
            display.drawPixel(wx, wy, ((wx + wy + i) & 1) ? track.glowColor : COLOR_WARN);
          }
        }
      }
      if (track.theme == SceneTheme::MetroRain)
      {
        for (int x = 8; x < hw::tftWidth; x += 18)
        {
          display.drawLine(x, 2, x - 3, 12, color565(180, 210, 255));
        }
      }
      break;
    case SceneTheme::HarborSunset:
      display.fillRect(8, 12, 16, 6, color565(62, 68, 78));
      display.drawLine(24, 18, 34, 8, color565(82, 84, 92));
      display.drawLine(34, 8, 46, 8, color565(82, 84, 92));
      display.drawLine(46, 8, 36, 18, color565(82, 84, 92));
      display.fillRect(104, 13, 20, 5, color565(52, 60, 72));
      display.drawFastHLine(0, 15, hw::tftWidth, blend565(track.glowColor, track.skyBottom, 0.55f));
      break;
    case SceneTheme::TunnelRun:
      display.fillTriangle(44, kHorizonY, 80, 4, 116, kHorizonY, color565(18, 20, 26));
      display.drawFastHLine(0, kHorizonY, hw::tftWidth, color565(54, 60, 74));
      break;
    case SceneTheme::Ruins:
    case SceneTheme::CanyonHeat:
      display.fillTriangle(0, kHorizonY, 18, 6, 44, kHorizonY, color565(94, 68, 46));
      display.fillTriangle(30, kHorizonY, 58, 4, 86, kHorizonY, color565(122, 84, 52));
      display.fillTriangle(90, kHorizonY, 118, 7, 154, kHorizonY, color565(146, 96, 58));
      break;
    default:
      display.fillTriangle(0, kHorizonY, 24, 6, 54, kHorizonY, color565(86, 100, 110));
      display.fillTriangle(36, kHorizonY, 76, 5, 116, kHorizonY, color565(108, 122, 134));
      display.fillTriangle(102, kHorizonY, 132, 8, 159, kHorizonY, color565(82, 96, 106));
      break;
    }

    display.drawFastHLine(0, kHorizonY, hw::tftWidth, blend565(track.glowColor, track.roadColor, 0.42f));
    display.drawFastHLine(0, kHorizonY + 1, hw::tftWidth, blend565(track.roadColor, COLOR_BG, 0.38f));
  }

  void renderPerspectiveRoad(Adafruit_ST7735 &display) const
  {
    const TrackProfile &track = selectedTrackProfile();
    for (int y = kHorizonY + 1; y < hw::tftHeight; ++y)
    {
      const int left = roadLeftAtY(y);
      const int right = roadRightAtY(y);
      const float t = clampValue((y - kHorizonY) / static_cast<float>(hw::tftHeight - kHorizonY), 0.0f, 1.0f);
      const int curbW = max(2, static_cast<int>(2 + t * 5.0f));
      const int outerLeft = max(0, left - curbW);
      const int outerRight = min<int>(hw::tftWidth, right + curbW);
      const bool curbStripe = ((((y + _selectedTrack * 5) / 4) & 1) == 0);
      const uint16_t shoulder = blend565(track.shoulderColor, COLOR_BG, 0.14f + t * 0.16f);
      const uint16_t road = blend565(track.roadColor, color565(8, 10, 12), 0.10f + t * 0.12f);
      const uint16_t curbA = curbStripe ? COLOR_TEXT : COLOR_DANGER;
      const uint16_t curbB = curbStripe ? COLOR_DANGER : COLOR_TEXT;
      display.drawFastHLine(0, y, max(0, outerLeft), shoulder);
      display.drawFastHLine(outerLeft, y, max(0, left - outerLeft), curbA);
      display.drawFastHLine(left, y, max(0, right - left), road);
      display.drawFastHLine(right, y, max(0, outerRight - right), curbB);
      display.drawFastHLine(outerRight, y, max(0, hw::tftWidth - outerRight), shoulder);

      if ((((y * 3) + static_cast<int>(_roadDashOffset * 2.0f)) / 7) % 2 == 0)
      {
        const float apexOffset = clampValue(-sampleCurveAtProgress(_trackProgress + t * 0.18f) * 0.34f, -0.44f, 0.44f);
        const int apexX = projectRoadX(worldXForOffsetNorm(apexOffset), y);
        display.fillRoundRect(apexX - 1, y, 2, max(1, static_cast<int>(1 + t * 3.0f)), 1, blend565(track.glowColor, track.lineColor, 0.32f));
      }
    }

    for (int y = kHorizonY + 10; y < hw::tftHeight; y += 14)
    {
      const int left = roadLeftAtY(y);
      const int right = roadRightAtY(y);
      const int roadside = roadsideAtProgress(_trackProgress + clampValue((y - kHorizonY) / 200.0f, 0.0f, 0.55f));
      const int propH = 5 + ((y - kHorizonY) / 8);
      const int propW = max(3, propH / 3);
      const int lx = left - 6 - propW;
      const int rx = right + 5;
      const uint16_t propBody = blend565(track.glowColor, color565(40, 48, 54), 0.55f);
      const uint16_t propAlt = blend565(track.lineColor, COLOR_TEXT, 0.35f);

      switch (roadside)
      {
      case 0:
      case 1:
      case 5:
        display.drawLine(lx + propW, y, lx + propW, y - propH, color565(84, 90, 102));
        display.drawLine(rx, y, rx, y - propH, color565(84, 90, 102));
        display.fillRect(lx, y - propH - 2, propW + 2, 3, (roadside == 1) ? track.glowColor : COLOR_WARN);
        display.fillRect(rx - 1, y - propH - 2, propW + 2, 3, (roadside == 5) ? COLOR_ACCENT : COLOR_WARN);
        break;
      case 2:
        display.drawRect(lx, y - propH, propW + 2, propH, color565(92, 94, 100));
        display.drawRect(rx, y - propH, propW + 2, propH, color565(92, 94, 100));
        break;
      case 3:
        display.drawFastHLine(0, y, left, color565(28, 28, 34));
        display.drawFastHLine(right, y, hw::tftWidth - right, color565(28, 28, 34));
        break;
      case 4:
      case 8:
        display.fillRect(lx, y - propH, propW + 3, propH, propBody);
        display.fillRect(rx, y - propH, propW + 3, propH, propBody);
        break;
      default:
        display.fillTriangle(lx, y, lx + propW, y - propH, lx + propW + 2, y, propAlt);
        display.fillTriangle(rx, y, rx + propW, y - propH, rx + propW + 2, y, propAlt);
        break;
      }
    }
  }

  void renderStartGridOverlay(Adafruit_ST7735 &display) const
  {
    const uint16_t slotColor = blend565(selectedTrackProfile().lineColor, COLOR_TEXT, 0.40f);
    const int playerSlotY = 106;
    const int playerSlotX = projectRoadX(worldXForOffsetNorm(0.0f), playerSlotY);
    display.drawRoundRect(playerSlotX - 12, playerSlotY - 4, 24, 8, 2, slotColor);

    static const float kRivalOffset[4] = {-0.22f, 0.22f, -0.18f, 0.18f};
    static const int kRivalY[4] = {86, 74, 63, 54};
    for (uint8_t i = 0; i < 4; ++i)
    {
      const int centerX = projectRoadX(worldXForOffsetNorm(kRivalOffset[i]), kRivalY[i]);
      const int slotW = 15 - i;
      const int slotH = 6;
      display.drawRoundRect(centerX - slotW / 2, kRivalY[i] - slotH / 2, slotW, slotH, 2, slotColor);

      const CarProfile &rival = carTable()[(_selectedCar + 5 + i * 3) % kCarCount];
      const int carW = max(9, 12 - static_cast<int>(i));
      const int carH = max(11, 14 - static_cast<int>(i));
      drawVehicle(display, centerX - carW / 2, kRivalY[i] - carH / 2, carW, carH, rival.kind, rival.bodyColor, rival.detailColor);
    }
  }

  int projectRivalY(float deltaAhead) const
  {
    const float visibleRange = 360.0f;
    const float t = 1.0f - clampValue(deltaAhead / visibleRange, 0.0f, 1.0f);
    return kHorizonY + 8 + static_cast<int>(powf(t, 1.75f) * (kPlayerY - kHorizonY - 10));
  }

  ScreenRect projectedPlayerRect() const
  {
    ScreenRect rect;
    const int carY = 94;
    rect.x = projectRoadX(_playerCenterX, carY) - (kPlayerW / 2);
    rect.y = carY;
    rect.w = kPlayerW;
    rect.h = kPlayerH;
    rect.valid = true;
    return rect;
  }

  ScreenRect projectedObstacleRect(const Obstacle &obstacle) const
  {
    ScreenRect rect;
    if (!obstacle.active)
    {
      return rect;
    }

    const int centerY = static_cast<int>(obstacle.y + obstacle.height * 0.5f);
    const float depth = clampValue((centerY - kHorizonY) / static_cast<float>(hw::tftHeight - kHorizonY), 0.0f, 1.0f);
    const int drawW = max(10, static_cast<int>(obstacle.width * (0.40f + depth * 0.70f)));
    const int drawH = max(12, static_cast<int>(obstacle.height * (0.45f + depth * 0.72f)));
    rect.x = projectRoadX(obstacle.x, centerY) - (drawW / 2);
    rect.y = max(kHorizonY + 2, centerY - drawH / 2);
    rect.w = drawW;
    rect.h = drawH;
    rect.valid = (rect.y < hw::tftHeight) && ((rect.y + rect.h) >= (kHorizonY + 2));
    return rect;
  }

  ScreenRect projectedAiRect(uint8_t index) const
  {
    ScreenRect rect;
    const float delta = _aiDistance[index] - _playerDistance;
    if (delta < 18.0f || delta > 360.0f)
    {
      return rect;
    }

    const int centerY = projectRivalY(delta);
    const float depth = clampValue((centerY - kHorizonY) / static_cast<float>(hw::tftHeight - kHorizonY), 0.0f, 1.0f);
    const int drawW = max(9, static_cast<int>(10 + depth * 10.0f));
    const int drawH = max(11, static_cast<int>(12 + depth * 13.0f));
    rect.x = projectRoadX(worldXForOffsetNorm(_aiOffsetNorm[index]), centerY) - (drawW / 2);
    rect.y = centerY - drawH / 2;
    rect.w = drawW;
    rect.h = drawH;
    rect.valid = (rect.y < hw::tftHeight) && ((rect.y + rect.h) >= (kHorizonY + 2));
    return rect;
  }

  float overlapGhostRatio(const ScreenRect &rect) const
  {
    if (!rect.valid)
    {
      return 0.0f;
    }
    const ScreenRect playerRect = projectedPlayerRect();
    if (!rectsOverlap(playerRect.x, playerRect.y, playerRect.w, playerRect.h, rect.x, rect.y, rect.w, rect.h))
    {
      return 0.0f;
    }
    const int top = max(playerRect.y, rect.y);
    const int bottom = min(playerRect.y + playerRect.h, rect.y + rect.h);
    const int overlap = max(0, bottom - top);
    return clampValue(overlap / static_cast<float>(max(1, min(playerRect.h, rect.h))), 0.0f, 1.0f);
  }

  void renderAiRival(Adafruit_ST7735 &display, uint8_t index) const
  {
    const ScreenRect rect = projectedAiRect(index);
    if (!rect.valid)
    {
      return;
    }

    const CarProfile &car = carTable()[_aiCarIndex[index] % kCarCount];
    drawVehicle(display, rect.x, rect.y, rect.w, rect.h, car.kind, car.bodyColor, car.detailColor);

    if (_aiIntent[index] == AiIntent::Overtake)
    {
      display.drawFastHLine(rect.x + 1, rect.y + rect.h - 2, rect.w - 2, COLOR_ACCENT);
    }
    else if (_aiIntent[index] == AiIntent::Defend)
    {
      display.drawFastHLine(rect.x + 2, rect.y + 1, rect.w - 4, COLOR_DANGER);
    }
  }

  void renderTrafficVehicle(Adafruit_ST7735 &display, const Obstacle &obstacle) const
  {
    const ScreenRect rect = projectedObstacleRect(obstacle);
    if (!rect.valid)
    {
      return;
    }

    drawVehicle(display, rect.x, rect.y, rect.w, rect.h, obstacle.kind, obstacle.bodyColor, obstacle.detailColor);
    if (obstacle.laneChanging)
    {
      display.drawFastHLine(rect.x + 1, rect.y + rect.h - 2, rect.w - 2, color565(200, 232, 255));
    }
  }

  void renderOverlapGhosts(Adafruit_ST7735 &display) const
  {
    for (uint8_t i = 0; i < kObstacleCount; ++i)
    {
      if (!_obstacles[i].active)
      {
        continue;
      }
      const ScreenRect rect = projectedObstacleRect(_obstacles[i]);
      const float fade = overlapGhostRatio(rect);
      if (fade <= 0.0f)
      {
        continue;
      }
      const uint16_t body = blend565(_obstacles[i].bodyColor, color565(224, 228, 236), 0.35f + fade * 0.45f);
      const uint16_t detail = blend565(_obstacles[i].detailColor, color565(238, 242, 246), 0.45f + fade * 0.45f);
      drawVehicle(display, rect.x, rect.y, rect.w, rect.h, _obstacles[i].kind, body, detail);
    }

    for (uint8_t i = 0; i < kAiCount; ++i)
    {
      const ScreenRect rect = projectedAiRect(i);
      const float fade = overlapGhostRatio(rect);
      if (fade <= 0.0f)
      {
        continue;
      }
      const CarProfile &car = carTable()[_aiCarIndex[i] % kCarCount];
      const uint16_t body = blend565(car.bodyColor, color565(224, 228, 236), 0.35f + fade * 0.45f);
      const uint16_t detail = blend565(car.detailColor, color565(238, 242, 246), 0.45f + fade * 0.45f);
      drawVehicle(display, rect.x, rect.y, rect.w, rect.h, car.kind, body, detail);
    }
  }

  void renderPlayerVehicle(Adafruit_ST7735 &display) const
  {
    const ScreenRect rect = projectedPlayerRect();
    const int trailShift = clampValue(static_cast<int>(_playerSlipVisual * 6.0f), -6, 6);
    if (trailShift != 0)
    {
      const uint16_t skidColor = (fabsf(_playerSlipVisual) > 0.56f) ? COLOR_WARN : color565(170, 176, 186);
      display.drawLine(rect.x + 4, rect.y + rect.h - 2, rect.x + 4 - trailShift, rect.y + rect.h + 5, skidColor);
      display.drawLine(rect.x + rect.w - 4, rect.y + rect.h - 2, rect.x + rect.w - 4 - trailShift, rect.y + rect.h + 5, skidColor);
    }
    drawVehicle(display, rect.x, rect.y, rect.w, rect.h, selectedCarProfile().kind, selectedCarProfile().bodyColor, selectedCarProfile().detailColor);
    drawHeroLivery(display, rect.x, rect.y, rect.w, rect.h);
    display.fillTriangle(rect.x + 2, rect.y + rect.h - 4, rect.x + rect.w - 3, rect.y + rect.h - 4, rect.x + rect.w / 2, rect.y + rect.h + 7, color565(36, 36, 44));
    if (fabsf(_playerOffsetNorm) > 0.72f || fabsf(_playerSlipVisual) > 0.58f)
    {
      display.drawFastHLine(rect.x + 2, rect.y + rect.h + 3, rect.w - 4, COLOR_WARN);
    }
  }

  void drawRaceChip(Adafruit_ST7735 &display, int x, int y, const char *label, uint32_t value, uint16_t accent, const char *suffix = "") const
  {
    display.fillRoundRect(x, y, 40, 12, 4, color565(18, 22, 28));
    display.drawRoundRect(x, y, 40, 12, 4, accent);
    display.setTextColor(COLOR_TEXT);
    display.setCursor(x + 4, y + 3);
    display.print(label);
    display.print(value);
    display.print(suffix);
  }

  static void drawVehicle(Adafruit_ST7735 &display, int x, int y, int w, int h, VehicleKind kind, uint16_t bodyColor, uint16_t detailColor)
  {
    const uint16_t shadowColor = color565(10, 10, 14);
    const uint16_t wheelColor = color565(16, 16, 20);
    const uint16_t glassColor = color565(170, 220, 248);
    const uint16_t tailColor = color565(255, 110, 80);
    const uint16_t outline = color565(28, 30, 36);
    const uint16_t highlight = color565(244, 246, 250);

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

    display.drawRoundRect(x, y, w, h, 3, outline);
    if (w > 10 && h > 12)
    {
      display.drawFastHLine(x + 3, y + 2, w - 6, highlight);
      display.drawFastVLine(x + 2, y + 4, max(2, h - 8), highlight);
      display.drawFastHLine(x + 3, y + h - 3, w - 6, outline);
    }
  }

  void drawHeroLivery(Adafruit_ST7735 &display, int x, int y, int w, int h) const
  {
    const uint8_t style = _selectedCar % 6;
    const uint16_t stripe = selectedCarProfile().detailColor;
    const uint16_t hood = color565(238, 242, 248);
    const uint16_t dark = color565(26, 30, 36);

    switch (style)
    {
    case 0:
      display.drawFastVLine(x + 4, y + 3, h - 6, dark);
      display.drawFastVLine(x + w - 5, y + 3, h - 6, dark);
      display.fillRect(x + 5, y + h - 4, w - 10, 2, hood);
      break;
    case 1:
      display.fillRect(x + 4, y + 5, w - 8, 2, stripe);
      display.fillRect(x + 4, y + h - 7, w - 8, 2, stripe);
      display.fillRect(x + 6, y + h - 3, w - 12, 2, dark);
      break;
    case 2:
      display.fillRect(x + (w / 2) - 1, y + 4, 2, h - 8, hood);
      display.fillRect(x + 4, y + 8, 3, h - 14, stripe);
      display.fillRect(x + w - 7, y + 8, 3, h - 14, stripe);
      break;
    case 3:
      display.fillRect(x + 3, y + 4, w - 6, 2, hood);
      display.fillRect(x + 3, y + 8, w - 6, 2, stripe);
      display.fillRect(x + 3, y + 12, w - 6, 2, hood);
      break;
    case 4:
      display.fillRect(x + 4, y + 4, w - 8, 3, dark);
      display.fillRect(x + 6, y + 8, w - 12, 2, hood);
      display.fillRect(x + 6, y + h - 5, w - 12, 2, stripe);
      break;
    default:
      display.fillTriangle(x + 2, y + h - 4, x + w - 3, y + h - 4, x + (w / 2), y + h - 8, hood);
      display.fillRect(x + 4, y + 5, 3, h - 10, stripe);
      display.fillRect(x + w - 7, y + 5, 3, h - 10, stripe);
      break;
    }
  }

  void drawGarageSideCar(Adafruit_ST7735 &display, int x, int y, int w, int h, VehicleKind kind, uint16_t bodyColor, uint16_t detailColor) const
  {
    const uint16_t tire = color565(20, 22, 28);
    const uint16_t rim = color565(214, 220, 232);
    const uint16_t glass = color565(176, 224, 248);
    const uint16_t shadow = color565(14, 16, 22);
    const uint16_t lamp = color565(255, 198, 96);

    display.fillRoundRect(x + 10, y + h - 8, w - 20, 5, 3, shadow);
    display.fillCircle(x + 18, y + h - 6, 6, tire);
    display.fillCircle(x + w - 18, y + h - 6, 6, tire);
    display.fillCircle(x + 18, y + h - 6, 3, rim);
    display.fillCircle(x + w - 18, y + h - 6, 3, rim);

    switch (kind)
    {
    case VehicleKind::Compact:
    case VehicleKind::Player:
      display.fillRoundRect(x + 8, y + 18, w - 16, 16, 5, bodyColor);
      display.fillRoundRect(x + 24, y + 10, w - 40, 14, 4, bodyColor);
      display.fillRoundRect(x + 28, y + 12, w - 48, 9, 3, glass);
      break;
    case VehicleKind::Taxi:
      display.fillRoundRect(x + 8, y + 18, w - 16, 16, 5, bodyColor);
      display.fillRoundRect(x + 26, y + 10, w - 44, 14, 4, bodyColor);
      display.fillRoundRect(x + 30, y + 12, w - 52, 9, 3, glass);
      display.fillRect(x + w / 2 - 4, y + 7, 8, 4, detailColor);
      break;
    case VehicleKind::Van:
      display.fillRoundRect(x + 6, y + 16, w - 12, 18, 4, bodyColor);
      display.fillRoundRect(x + 20, y + 8, w - 30, 16, 4, bodyColor);
      display.fillRoundRect(x + 24, y + 10, w - 40, 10, 3, glass);
      break;
    case VehicleKind::Pickup:
      display.fillRoundRect(x + 10, y + 18, w - 18, 16, 4, bodyColor);
      display.fillRoundRect(x + 16, y + 10, w - 40, 15, 4, bodyColor);
      display.fillRoundRect(x + 20, y + 12, w - 50, 9, 3, glass);
      display.fillRect(x + w - 28, y + 14, 10, 8, detailColor);
      break;
    case VehicleKind::Truck:
      display.fillRoundRect(x + 8, y + 16, w - 16, 18, 4, detailColor);
      display.fillRoundRect(x + 10, y + 10, 22, 16, 4, bodyColor);
      display.fillRoundRect(x + 14, y + 12, 14, 9, 3, glass);
      break;
    case VehicleKind::Bus:
      display.fillRoundRect(x + 6, y + 14, w - 12, 20, 4, bodyColor);
      display.fillRoundRect(x + 12, y + 10, w - 20, 12, 3, detailColor);
      for (int wx = x + 16; wx < (x + w - 18); wx += 12)
      {
        display.fillRoundRect(wx, y + 12, 8, 6, 2, glass);
      }
      break;
    }

    display.fillRect(x + 10, y + 28, 4, 3, lamp);
    display.fillRect(x + w - 14, y + 28, 4, 3, COLOR_DANGER);

    const uint8_t style = _selectedCar % 5;
    if (style == 0)
    {
      display.fillRect(x + 20, y + 26, w - 40, 2, detailColor);
    }
    else if (style == 1)
    {
      display.fillRect(x + 28, y + 12, 3, 18, detailColor);
      display.fillRect(x + w - 31, y + 12, 3, 18, detailColor);
    }
    else if (style == 2)
    {
      display.fillRect(x + 18, y + 20, w - 36, 3, detailColor);
      display.fillRect(x + 22, y + 24, w - 44, 2, COLOR_TEXT);
    }
    else if (style == 3)
    {
      display.fillTriangle(x + w - 22, y + 18, x + w - 6, y + 22, x + w - 22, y + 26, detailColor);
    }
    else
    {
      display.fillRect(x + 22, y + 14, w - 44, 2, COLOR_TEXT);
      display.fillRect(x + 22, y + 30, w - 44, 2, detailColor);
    }
  }

  void renderStatsBar(Adafruit_ST7735 &display, int x, int y, const char *label, float value, uint16_t color) const
  {
    display.setTextColor(COLOR_DIM);
    display.setCursor(x, y);
    display.print(label);
    display.drawRoundRect(x + 30, y, 58, 7, 3, COLOR_DIM);
    const int fillW = clampValue(static_cast<int>((value - 0.88f) * 130.0f), 6, 54);
    display.fillRoundRect(x + 32, y + 2, fillW, 3, 2, color);
  }

  void renderCarSelect(Adafruit_ST7735 &display) const
  {
    const CarProfile &car = selectedCarProfile();
    const float rating = (car.speedBias + car.shiftBias + car.gripBias) / 3.0f;
    const char *tier = (rating >= 1.10f) ? "S" : (rating >= 1.04f) ? "A" : (rating >= 0.98f) ? "B"
                                                                                              : "C";
    display.fillScreen(COLOR_BG);
    display.fillRoundRect(8, 6, 144, 16, 5, COLOR_PANEL);
    display.setTextColor(COLOR_TEXT);
    display.setCursor(14, 11);
    display.print("GARAGE");
    display.setTextColor(COLOR_WARN);
    display.setCursor(118, 11);
    display.print("T");
    display.print(tier);
    display.setTextColor(COLOR_DIM);
    display.setCursor(12, 28);
    display.print("CAR ");
    display.print(_selectedCar + 1);
    display.print("/30");
    display.setTextColor(COLOR_TEXT);
    display.setCursor(78, 28);
    display.print(car.name);
    display.drawRoundRect(10, 34, 140, 52, 7, COLOR_ACCENT);
    display.fillRoundRect(12, 36, 136, 48, 7, color565(24, 30, 42));
    display.fillRect(18, 72, 124, 3, color565(70, 82, 102));
    drawGarageSideCar(display, 22, 36, 116, 40, car.kind, car.bodyColor, car.detailColor);
    display.setTextColor(COLOR_DIM);
    display.setCursor(18, 84);
    display.print("7 AI / 10 MAP");
    renderStatsBar(display, 18, 90, "SPD", car.speedBias, COLOR_DANGER);
    renderStatsBar(display, 18, 98, "SFT", car.shiftBias, COLOR_ACCENT);
    renderStatsBar(display, 18, 106, "GRP", car.gripBias, COLOR_GOOD);
    display.setTextColor(COLOR_DIM);
    display.setCursor(10, kFooterY);
    display.print("B1 next  B2 map");
  }

  void renderTrackSelect(Adafruit_ST7735 &display) const
  {
    const TrackProfile &track = selectedTrackProfile();
    const CarProfile &car = selectedCarProfile();
    display.fillScreen(COLOR_BG);
    display.fillRoundRect(8, 6, 144, 16, 5, COLOR_PANEL);
    display.setTextColor(COLOR_TEXT);
    display.setCursor(14, 11);
    display.print("TRACK LIST");
    display.setTextColor(COLOR_DIM);
    display.setCursor(12, 28);
    display.print("MAP ");
    display.print(_selectedTrack + 1);
    display.print("/10");
    display.setTextColor(COLOR_TEXT);
    display.setCursor(74, 28);
    display.print(track.name);
    display.drawRoundRect(10, 34, 140, 50, 7, COLOR_ACCENT);
    for (int y = 36; y < 82; ++y)
    {
      const float t = (y - 36) / 46.0f;
      display.drawFastHLine(12, y, 136, blend565(track.skyTop, track.skyBottom, t));
    }
    for (int x = 18; x < 72; x += 18)
    {
      display.fillRect(x, 52 - (x / 12), 12, 16 + (x / 12), blend565(track.roadColor, COLOR_BG, 0.48f));
    }
    display.fillTriangle(52, 82, 78, 46, 108, 82, track.shoulderColor);
    display.fillTriangle(58, 82, 80, 46, 114, 82, track.roadColor);
    for (int y = 52; y < 82; ++y)
    {
      const bool curbStripe = (((y / 4) & 1) == 0);
      display.drawPixel(58 + ((82 - y) / 2), y, curbStripe ? COLOR_TEXT : COLOR_DANGER);
      display.drawPixel(113 - ((82 - y) / 3), y, curbStripe ? COLOR_DANGER : COLOR_TEXT);
    }
    for (int y = 58; y < 80; y += 6)
    {
      display.fillRoundRect(83, y, 2, 3, 1, blend565(track.glowColor, track.lineColor, 0.35f));
    }
    drawGarageSideCar(display, 96, 54, 42, 20, car.kind, car.bodyColor, car.detailColor);
    display.setTextColor(COLOR_TEXT);
    display.setCursor(16, 88);
    display.print("Len:");
    display.print(previewDistanceKm(track), 1);
    display.print("km");
    display.setCursor(92, 88);
    display.print("AI:7");
    display.setCursor(16, 98);
    display.print(themeLabel(track.theme));
    display.setCursor(16, 108);
    display.print("Flow:");
    display.print(static_cast<int>(track.trafficScale * 100));
    display.setCursor(90, 108);
    display.print("Turn:");
    display.print(static_cast<int>(track.curveIntensity * 100));
    display.setTextColor(COLOR_DIM);
    display.setCursor(8, kFooterY);
    display.print("B1 next  B2 race");
  }

  void renderCountdown(Adafruit_ST7735 &display) const
  {
    const uint32_t elapsed = millis() - _countdownStartMs;
    const uint8_t lightCount = (elapsed >= 2450) ? 0 : min<uint8_t>(5, static_cast<uint8_t>(elapsed / 430U) + 1U);
    display.fillRoundRect(24, 18, 112, 22, 7, color565(18, 20, 28));
    display.drawRoundRect(24, 18, 112, 22, 7, COLOR_DIM);
    for (uint8_t i = 0; i < 5; ++i)
    {
      const int cx = 42 + i * 19;
      const uint16_t lamp = (i < lightCount) ? COLOR_DANGER : color565(48, 18, 22);
      display.fillCircle(cx, 29, 6, lamp);
      display.drawCircle(cx, 29, 6, COLOR_DIM);
    }
    if (elapsed > 2450)
    {
      drawCenteredText(display, 54, "GO", COLOR_GOOD, 2);
    }
    else
    {
      drawCenteredText(display, 54, "F1 START", COLOR_WARN, 1);
    }

    const int barX = 28;
    const int barY = 86;
    const int barW = 104;
    const int barH = 10;
    const int greenX = barX + static_cast<int>(barW * (launchGreenMin() / 100.0f));
    const int greenW = static_cast<int>(barW * ((launchGreenMax() - launchGreenMin()) / 100.0f));
    const int fillW = clampValue(static_cast<int>(barW * (_launchMeter / 100.0f)), 0, barW);
    display.drawRoundRect(barX, barY, barW, barH, 4, COLOR_DIM);
    display.fillRoundRect(greenX, barY + 2, max(6, greenW), barH - 4, 3, color565(40, 120, 52));
    if (fillW > 0)
    {
      display.fillRoundRect(barX + 1, barY + 1, max(1, fillW - 2), barH - 2, 3, (_launchMeter > launchGreenMax() + 14.0f) ? COLOR_DANGER : COLOR_ACCENT);
    }
    display.setTextColor(COLOR_DIM);
    display.setCursor(28, 100);
    display.print("B1+B2 clutch bite");
    display.setTextColor(COLOR_TEXT);
    display.setCursor(40, 112);
    display.print(launchLabel());
  }

  void renderMiniMap(Adafruit_ST7735 &display) const
  {
    const int mapX = 147;
    const int mapY = 20;
    const int mapH = 82;
    display.drawRoundRect(mapX, mapY, 10, mapH, 2, COLOR_DIM);
    for (uint8_t i = 0; i < kAiCount; ++i)
    {
      const int y = mapY + mapH - 4 - static_cast<int>(clampValue(_aiDistance[i] / _totalDistance, 0.0f, 1.0f) * (mapH - 8));
      const uint16_t color = (i < 3) ? color565(242, 174, 64) : (i < 6) ? color565(186, 204, 222)
                                                                         : COLOR_DANGER;
      display.drawPixel(mapX + 2 + (i % 2) * 3, y, color);
    }
    const int py = mapY + mapH - 4 - static_cast<int>(clampValue(_playerDistance / _totalDistance, 0.0f, 1.0f) * (mapH - 8));
    display.fillCircle(mapX + 7, py, 2, COLOR_ACCENT);
  }

  void triggerCrash(uint32_t nowMs, int x, int y)
  {
    _crashX = x;
    _crashY = y;
    _crashEffectUntilMs = nowMs + 420;
  }

  void renderCrashEffect(Adafruit_ST7735 &display) const
  {
    const uint32_t remaining = _crashEffectUntilMs - millis();
    const int radius = 6 + static_cast<int>((420 - min<uint32_t>(420, remaining)) / 28);
    display.fillCircle(_crashX, _crashY, radius, COLOR_WARN);
    display.fillCircle(_crashX, _crashY, max(2, radius / 2), color565(255, 232, 164));
    for (uint8_t i = 0; i < 6; ++i)
    {
      const float angle = radians(60.0f * i + (millis() % 60));
      const int ox = static_cast<int>(cosf(angle) * (radius + 5));
      const int oy = static_cast<int>(sinf(angle) * (radius + 4));
      display.drawLine(_crashX, _crashY, _crashX + ox, _crashY + oy, COLOR_DANGER);
    }
  }

  void finish(uint32_t nowMs, bool completed)
  {
    _finished = true;
    _completed = completed;
    _durationMs = nowMs - _startMs;
  }

  static constexpr uint8_t kObstacleCount = 4;
  static constexpr uint8_t kAiCount = 7;
  static constexpr uint8_t kCarCount = 30;
  static constexpr uint8_t kTrackCount = 10;
  static constexpr int kHorizonY = 18;
  static constexpr int kRoadX = 18;
  static constexpr int kRoadW = 124;
  static constexpr int kLaneWidth = kRoadW / 3;
  static constexpr int kPlayerW = 18;
  static constexpr int kPlayerH = 24;
  static constexpr int kPlayerY = 96;

  Obstacle _obstacles[kObstacleCount];
  Stage _stage = Stage::SelectCar;
  bool _finished = false;
  bool _completed = false;
  uint8_t _difficulty = 0;
  uint8_t _selectedCar = 0;
  uint8_t _selectedTrack = 0;
  uint8_t _playerLane = 1;
  uint8_t _targetLane = 1;
  uint8_t _lastSpawnLane = 1;
  uint8_t _overtakeStreak = 0;
  uint32_t _startMs = 0;
  uint32_t _lastUpdateMs = 0;
  uint32_t _durationMs = 0;
  uint32_t _countdownStartMs = 0;
  uint32_t _launchEffectUntilMs = 0;
  uint32_t _overtakeBoostUntilMs = 0;
  uint32_t _raceStartMs = 0;
  uint32_t _raceShieldUntilMs = 0;
  uint32_t _crashEffectUntilMs = 0;
  uint32_t _score = 0;
  uint8_t _targetScore = 12;
  uint8_t _playerRank = 1;
  uint8_t _finishRank = 8;
  uint8_t _progressPct = 0;
  uint8_t _aiCarIndex[kAiCount] = {};
  uint32_t _aiDecisionAtMs[kAiCount] = {};
  LaunchState _launchState = LaunchState::None;
  float _roadSpeed = 92.0f;
  float _laneShiftSpeed = 148.0f;
  float _roadDashOffset = 0.0f;
  float _launchMeter = 0.0f;
  float _playerCenterX = 0.0f;
  float _playerOffsetNorm = 0.0f;
  float _playerSlipVisual = 0.0f;
  int _crashX = hw::tftWidth / 2;
  int _crashY = kPlayerY;
  float _playerRaceSpeed = 96.0f;
  float _playerDistance = 0.0f;
  float _trackProgress = 0.0f;
  float _curveCurrent = 0.0f;
  float _curveTarget = 0.0f;
  float _totalDistance = 2400.0f;
  float _spawnTimer = 0.0f;
  float _spawnInterval = 0.92f;
  float _aiDistance[kAiCount] = {};
  float _aiOffsetNorm[kAiCount] = {};
  float _aiTargetOffsetNorm[kAiCount] = {};
  float _aiRaceSpeed[kAiCount] = {};
  float _aiPhase[kAiCount] = {};
  AiIntent _aiIntent[kAiCount] = {};
  bool _aiWasAhead[kAiCount] = {};
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
    display.print("Wi:");
    display.print(wifiService.statusLabel());
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
    _goal = (_difficulty == 0) ? 12.0f : (_difficulty == 1) ? 16.0f
                                                            : 20.0f;
    _slipPerSec = (_difficulty == 0) ? 0.95f : (_difficulty == 1) ? 1.35f
                                                                  : 1.85f;
    _timeLimitMs = (_difficulty == 0) ? 22000 : (_difficulty == 1) ? 20000
                                                                   : 18000;
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
      const float comeback = (_progress[0] + 2.5f < _progress[1]) ? 0.30f : 0.0f;
      _progress[0] += 1.15f + comeback;
    }
    if (input.btn2DownEdge)
    {
      const float comeback = (_progress[1] + 2.5f < _progress[0]) ? 0.30f : 0.0f;
      _progress[1] += 1.15f + comeback;
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
    display.print("Fast taps, comeback boost");
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
    display.setCursor(64, 24);
    display.print("Time:");
    display.print((_timeLimitMs - min<uint32_t>(_timeLimitMs, millis() - _startMs)) / 1000);
    display.setCursor(0, 36);
    display.print("Slip:");
    display.print(_slipPerSec, 1);
    display.setCursor(64, 36);
    display.print("Wi:");
    display.print(wifiService.statusLabel());
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
    _currentPlayer = 0;
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

    bool b1Pressed = input.btn1DownEdge;
    bool b2Pressed = input.btn2DownEdge;
    bool activePump = false;

    if (_currentPlayer == 0)
    {
      if (b1Pressed)
      {
        _currentPlayer = 1;
        _turnPumpCount = 0;
        activePump = true;
      }
      else if (b2Pressed)
      {
        _currentPlayer = 2;
        _turnPumpCount = 0;
        activePump = true;
      }
    }
    else
    {
      const bool otherPressed = (_currentPlayer == 1) ? b2Pressed : b1Pressed;
      if (otherPressed && _turnPumpCount > 0)
      {
        switchTurn();
        activePump = true;
      }
      else if ((_currentPlayer == 1 && b1Pressed) || (_currentPlayer == 2 && b2Pressed))
      {
        activePump = true;
      }
    }

    if (activePump)
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
    display.print("Turn:");
    display.print((_currentPlayer == 0) ? "--" : (_currentPlayer == 1) ? "B1" : "B2");
    display.setCursor(82, 90);
    display.print("Run:");
    display.print(_turnPumpCount);
    display.setCursor(16, 108);
    display.print("Size:");
    display.print(static_cast<int>(_balloonSize));
    display.setCursor(92, 108);
    display.print("All:");
    display.print(_turnCount);

    display.setTextColor(COLOR_DIM);
    display.setCursor(8, kFooterY);
    display.print("Other press swaps");
  }

  void renderOledOverlay(Adafruit_SSD1306 &display, const SensorSnapshot &sensors) override
  {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print("BALLOON");
    display.setCursor(0, 12);
    display.print("Turn:");
    display.print((_currentPlayer == 0) ? "--" : (_currentPlayer == 1) ? "B1" : "B2");
    display.setCursor(54, 12);
    display.print("Run:");
    display.print(_turnPumpCount);
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
    display.print("Wi:");
    display.print(wifiService.statusLabel());
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
  uint8_t _difficulty = 0;
  uint8_t _winner = 0;
  uint8_t _currentPlayer = 0;
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
        drawDuck(display, static_cast<int>(_ducks[i].x), laneY(_ducks[i].lane), _ducks[i].vx > 0.0f, _ducks[i].styleSeed);
      }
    }

    const int sightY = laneY(_aimLane) + 4;
    display.drawFastVLine(kSightX, sightY - 8, 16, COLOR_DANGER);
    display.drawFastHLine(kSightX - 8, sightY, 16, COLOR_DANGER);
    display.drawCircle(kSightX, sightY, 10, COLOR_TEXT);
    drawShotgun(display, sightY);

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
    display.setCursor(124, 108);
    display.print("12G");

    display.setTextColor(COLOR_DIM);
    display.setCursor(8, kFooterY);
    display.print("B1/B2 aim  both blast");
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
    display.print("Wi:");
    display.print(wifiService.statusLabel());
    display.setCursor(64, 36);
    display.print("Art:");
    display.print(duckImageService.statusLabel());
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
    uint8_t styleSeed = 0;
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
      _ducks[i].styleSeed = random(0, 250);
      const bool fromLeft = random(0, 2) == 0;
      _ducks[i].x = fromLeft ? -12.0f : (hw::tftWidth + 12.0f);
      _ducks[i].vx = fromLeft ? _duckSpeed : -_duckSpeed;
      return;
    }
  }

  static void drawDuck(Adafruit_ST7735 &display, int x, int y, bool facingRight, uint8_t styleSeed)
  {
    uint16_t head = color565(34, 118, 78);
    uint16_t chest = color565(128, 76, 42);
    uint16_t body = color565(176, 182, 188);
    uint16_t wing = color565(72, 96, 122);
    uint16_t speculum = color565(44, 128, 196);
    uint16_t beak = color565(246, 206, 74);
    uint16_t outline = color565(28, 34, 36);
    char tag[6] = {};
    duckImageService.styleForSeed(styleSeed, wing, speculum, beak, outline, tag, sizeof(tag));
    const int dir = facingRight ? 1 : -1;
    display.fillRoundRect(x - 7, y - 2, 14, 8, 3, body);
    display.drawRoundRect(x - 7, y - 2, 14, 8, 3, outline);
    display.fillCircle(x - dir * 5, y - 2, 3, head);
    display.drawCircle(x - dir * 5, y - 2, 3, outline);
    display.drawLine(x - dir * 2, y - 1, x + dir, y - 1, ST77XX_WHITE);
    display.fillCircle(x - 1, y + 2, 3, chest);
    display.fillTriangle(x - 1, y + 1, x - 7, y + 4, x + 1, y + 5, wing);
    display.fillTriangle(x - 2, y + 1, x - 4, y + 4, x + 1, y + 3, speculum);
    display.drawLine(x - 4, y + 2, x - 7, y + 4, outline);
    display.fillTriangle(x + dir * 5, y - 3, x + dir * 11, y - 2, x + dir * 5, y, beak);
    display.drawPixel(x - dir * 4, y - 4, ST77XX_WHITE);
    display.drawPixel(x - dir * 3, y - 4, outline);
    display.drawLine(x - 1, y + 5, x - 2, y + 8, outline);
    display.drawLine(x + 2, y + 5, x + 1, y + 8, outline);
  }

  static void drawShotgun(Adafruit_ST7735 &display, int sightY)
  {
    const uint16_t wood = color565(116, 76, 42);
    const uint16_t metal = color565(170, 182, 196);
    const int stockY = clampValue(sightY + 24, 104, 120);
    display.fillRect(8, stockY, 18, 4, wood);
    display.fillTriangle(6, stockY + 2, 14, stockY - 4, 20, stockY + 2, wood);
    display.fillRect(26, stockY + 1, 10, 2, metal);
    display.drawLine(36, stockY + 2, 58, sightY + 2, metal);
    display.drawLine(37, stockY + 4, 59, sightY + 4, metal);
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

class StickmanCombatGame : public IGame
{
public:
  GameId id() const override
  {
    return GameId::ShieldSword;
  }

  void enter(uint8_t difficulty, uint32_t nowMs) override
  {
    _difficulty = clampValue<uint8_t>(difficulty, 0, 2);
    _finished = false;
    _completed = false;
    _weaponSelect = true;
    _weapon = WeaponType::Katana;
    _score = 0;
    _lives = 3;
    _startMs = nowMs;
    _lastUpdateMs = nowMs;
    _lastSpawnMs = nowMs;
    _durationMs = 0;
    _spawnGapMs = (_difficulty == 0) ? 1300 : (_difficulty == 1) ? 980
                                                                  : 760;
    _enemySpeed = (_difficulty == 0) ? 24.0f : (_difficulty == 1) ? 32.0f
                                                                  : 40.0f;
    _targetScore = (_difficulty == 0) ? 12 : (_difficulty == 1) ? 18
                                                                : 24;
    _attackFlashUntilMs[0] = 0;
    _attackFlashUntilMs[1] = 0;
    _cooldownUntilMs = 0;
    _playerPose = Pose::Idle;
    _playerPoseUntilMs = 0;
    _playerFacingLeft = false;
    _attackCycle = 0;
    _comboCount = 0;
    _comboFlashUntilMs = 0;
    for (uint8_t i = 0; i < kEnemyCount; ++i)
    {
      _enemies[i] = {};
    }
  }

  void update(uint32_t nowMs, const InputSnapshot &input, const SensorSnapshot &) override
  {
    if (_finished)
    {
      return;
    }

    if (_weaponSelect)
    {
      if (input.btn1DownEdge)
      {
        _weapon = static_cast<WeaponType>((static_cast<uint8_t>(_weapon) + 1) % 3);
      }
      if (input.btn2DownEdge)
      {
        _weaponSelect = false;
        _lastSpawnMs = nowMs;
        _cooldownUntilMs = nowMs;
      }
      return;
    }

    const float dt = clampValue((nowMs - _lastUpdateMs) / 1000.0f, 0.0f, 0.05f);
    _lastUpdateMs = nowMs;

    if (input.btn1DownEdge && nowMs >= _cooldownUntilMs)
    {
      _attackFlashUntilMs[0] = nowMs + attackFlashMs();
      _cooldownUntilMs = nowMs + weaponCooldownMs();
      triggerPlayerAttack(nowMs, true);
      attackSide(true);
    }
    if (input.btn2DownEdge && nowMs >= _cooldownUntilMs)
    {
      _attackFlashUntilMs[1] = nowMs + attackFlashMs();
      _cooldownUntilMs = nowMs + weaponCooldownMs();
      triggerPlayerAttack(nowMs, false);
      attackSide(false);
    }

    if ((nowMs - _lastSpawnMs) >= _spawnGapMs)
    {
      _lastSpawnMs = nowMs;
      spawnEnemy();
    }

    for (uint8_t i = 0; i < kEnemyCount; ++i)
    {
      if (!_enemies[i].active)
      {
        continue;
      }
      const float speedMul = (_enemies[i].type == EnemyType::Ninja) ? 1.26f : (_enemies[i].type == EnemyType::Brute) ? 0.78f
                                                                                                                         : 1.0f;
      _enemies[i].x += _enemies[i].fromLeft ? _enemies[i].speed * speedMul * dt : -_enemies[i].speed * speedMul * dt;
      const float hitRange = (_enemies[i].type == EnemyType::Lancer) ? 14.0f : 8.0f;
      if (fabsf(_enemies[i].x - kPlayerX) <= hitRange)
      {
        _enemies[i].active = false;
        if (_lives > 0)
        {
          _lives = (_enemies[i].type == EnemyType::Brute && _lives > 1) ? (_lives - 2) : (_lives - 1);
        }
        _comboCount = 0;
        if (_lives == 0)
        {
          finish(nowMs, false);
          return;
        }
      }
    }

    if (_score >= _targetScore)
    {
      finish(nowMs, true);
    }
  }

  void renderTft(Adafruit_ST7735 &display) override
  {
    if (_weaponSelect)
    {
      renderWeaponSelect(display);
      return;
    }

    for (int y = 0; y < 100; ++y)
    {
      const float t = y / 99.0f;
      display.drawFastHLine(0, y, hw::tftWidth, color565(static_cast<uint8_t>(18 + t * 24), static_cast<uint8_t>(16 + t * 20), static_cast<uint8_t>(28 + t * 40)));
    }
    display.fillRect(0, 0, hw::tftWidth, 14, color565(18, 12, 22));
    display.setTextColor(COLOR_TEXT);
    display.setCursor(4, 3);
    display.print("Stickman");
    display.setCursor(100, 3);
    display.print(_score);
    display.print("/");
    display.print(_targetScore);
    display.setCursor(4, 15);
    display.setTextColor(COLOR_DIM);
    display.print(weaponName(_weapon));

    display.drawFastHLine(14, 28, 28, color565(255, 88, 128));
    display.drawFastHLine(118, 22, 26, color565(86, 198, 255));
    for (int x = 12; x < hw::tftWidth; x += 24)
    {
      display.fillRect(x, 52, 10, 34, color565(28, 22, 30));
      display.drawFastVLine(x + 5, 52, 34, color565(76, 68, 84));
    }
    display.fillRect(0, 86, hw::tftWidth, 14, color565(34, 30, 38));
    display.fillRect(0, 100, hw::tftWidth, 28, color565(52, 48, 58));
    drawStickman(display, kPlayerX, 86, COLOR_TEXT, _weapon, currentPlayerPose(millis()), _playerFacingLeft);
    if (_attackFlashUntilMs[0] > millis())
    {
      drawAttack(display, true);
    }
    if (_attackFlashUntilMs[1] > millis())
    {
      drawAttack(display, false);
    }

    for (uint8_t i = 0; i < kEnemyCount; ++i)
    {
      if (_enemies[i].active)
      {
        drawEnemy(display, _enemies[i]);
      }
    }

    if (_comboFlashUntilMs > millis() && _comboCount > 1)
    {
      display.setTextColor(COLOR_WARN);
      display.setCursor(54, 18);
      display.print("COMBO x");
      display.print(_comboCount);
    }

    display.setCursor(10, 108);
    display.setTextColor(COLOR_TEXT);
    display.print("Lives:");
    display.print(_lives);
    display.setCursor(88, 108);
    display.print(kPongDifficultyNames[_difficulty]);

    display.setTextColor(COLOR_DIM);
    display.setCursor(8, kFooterY);
    display.print("B1/B2 strike side");
  }

  void renderOledOverlay(Adafruit_SSD1306 &display, const SensorSnapshot &sensors) override
  {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print("STICKMAN");
    display.setCursor(0, 12);
    if (_weaponSelect)
    {
      display.print("Pick:");
      display.print(weaponName(_weapon));
    }
    else
    {
      display.print("Wpn:");
      display.print(weaponName(_weapon));
      display.setCursor(74, 12);
      display.print("HP:");
      display.print(_lives);
    }
    display.setCursor(0, 24);
    display.print("Score:");
    display.print(_score);
    display.setCursor(70, 24);
    display.print("Wi:");
    display.print(wifiService.statusLabel());
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
    result.score = _score * 140 + (_completed ? 700 : 0);
    result.durationMs = _durationMs;
    result.completed = _completed;
    snprintf(result.detail, sizeof(result.detail), "%s %uHP", weaponShort(_weapon), _lives);
    return result;
  }

private:
  enum class WeaponType : uint8_t
  {
    Spear,
    Katana,
    Fist
  };

  enum class EnemyType : uint8_t
  {
    Runner,
    Brute,
    Ninja,
    Guard,
    Lancer
  };

  enum class Pose : uint8_t
  {
    Idle,
    Guard,
    Sprint,
    PunchHigh,
    PunchLow,
    Uppercut,
    Kick,
    SlashHigh,
    SlashLow,
    DashSlash,
    Thrust
  };

  struct Enemy
  {
    bool active = false;
    bool fromLeft = true;
    float x = 0.0f;
    float speed = 30.0f;
    EnemyType type = EnemyType::Runner;
    uint8_t hp = 1;
    uint8_t motionSeed = 0;
  };

  static constexpr uint8_t kEnemyCount = 6;
  static constexpr int kPlayerX = 80;

  static const char *weaponName(WeaponType weapon)
  {
    switch (weapon)
    {
    case WeaponType::Spear:
      return "Spear";
    case WeaponType::Katana:
      return "Katana";
    default:
      return "Fist";
    }
  }

  static const char *weaponShort(WeaponType weapon)
  {
    switch (weapon)
    {
    case WeaponType::Spear:
      return "SPR";
    case WeaponType::Katana:
      return "KAT";
    default:
      return "FST";
    }
  }

  static Pose previewPoseForWeapon(WeaponType weapon)
  {
    switch (weapon)
    {
    case WeaponType::Spear:
      return Pose::Thrust;
    case WeaponType::Katana:
      return Pose::SlashHigh;
    default:
      return Pose::Kick;
    }
  }

  uint16_t attackFlashMs() const
  {
    switch (_weapon)
    {
    case WeaponType::Spear:
      return 170;
    case WeaponType::Katana:
      return 150;
    default:
      return 120;
    }
  }

  uint16_t weaponCooldownMs() const
  {
    switch (_weapon)
    {
    case WeaponType::Spear:
      return 330;
    case WeaponType::Katana:
      return 200;
    default:
      return 120;
    }
  }

  uint8_t weaponDamage() const
  {
    return (_weapon == WeaponType::Fist) ? 1 : (_weapon == WeaponType::Katana ? 2 : 2);
  }

  float weaponRange() const
  {
    switch (_weapon)
    {
    case WeaponType::Spear:
      return 56.0f;
    case WeaponType::Katana:
      return 36.0f;
    default:
      return 20.0f;
    }
  }

  uint8_t weaponPierce() const
  {
    return (_weapon == WeaponType::Spear) ? 2 : 1;
  }

  Pose currentPlayerPose(uint32_t nowMs) const
  {
    if (_playerPoseUntilMs > nowMs)
    {
      return _playerPose;
    }
    if (_cooldownUntilMs > nowMs)
    {
      return Pose::Guard;
    }
    return Pose::Idle;
  }

  void triggerPlayerAttack(uint32_t nowMs, bool leftSide)
  {
    _playerFacingLeft = leftSide;
    ++_attackCycle;
    switch (_weapon)
    {
    case WeaponType::Spear:
      _playerPose = (_attackCycle % 3U == 0U) ? Pose::DashSlash : ((_attackCycle & 1U) == 0U) ? Pose::Thrust
                                                                                                : Pose::SlashLow;
      break;
    case WeaponType::Katana:
      _playerPose = (_attackCycle % 3U == 0U) ? Pose::DashSlash : ((_attackCycle & 1U) == 0U) ? Pose::SlashHigh
                                                                                                : Pose::SlashLow;
      break;
    case WeaponType::Fist:
    default:
      _playerPose = (_attackCycle % 3U == 0U) ? Pose::Uppercut : ((_attackCycle & 1U) == 0U) ? Pose::PunchHigh
                                                                                                 : Pose::Kick;
      break;
    }
    _playerPoseUntilMs = nowMs + attackFlashMs() + 70;
  }

  Pose enemyPose(const Enemy &enemy) const
  {
    const bool inRange = fabsf(enemy.x - kPlayerX) <= ((enemy.type == EnemyType::Lancer) ? 24.0f : 18.0f);
    const bool altFrame = (((millis() / 140U) + enemy.motionSeed) & 1U) != 0U;
    switch (enemy.type)
    {
    case EnemyType::Runner:
      return inRange ? (altFrame ? Pose::PunchHigh : Pose::Kick) : Pose::Sprint;
    case EnemyType::Brute:
      return inRange ? (altFrame ? Pose::PunchLow : Pose::Kick) : Pose::Guard;
    case EnemyType::Ninja:
      return inRange ? (altFrame ? Pose::SlashHigh : Pose::SlashLow) : Pose::Sprint;
    case EnemyType::Guard:
      return inRange ? Pose::SlashLow : Pose::Guard;
    case EnemyType::Lancer:
    default:
      return inRange ? Pose::Thrust : (altFrame ? Pose::Guard : Pose::Sprint);
    }
  }

  static void drawWeaponArt(Adafruit_ST7735 &display, WeaponType weapon, int handX, int handY, int dir, Pose pose, uint16_t color)
  {
    const uint16_t steel = color565(220, 226, 240);
    const uint16_t gold = color565(246, 214, 88);
    switch (weapon)
    {
    case WeaponType::Spear:
      if (pose == Pose::Thrust)
      {
        display.drawLine(handX, handY, handX + dir * 22, handY - 2, steel);
        display.drawLine(handX + dir * 22, handY - 2, handX + dir * 28, handY - 4, gold);
      }
      else if (pose == Pose::DashSlash)
      {
        display.drawLine(handX, handY, handX + dir * 18, handY - 11, steel);
        display.drawLine(handX + dir * 18, handY - 11, handX + dir * 28, handY - 14, gold);
      }
      else if (pose == Pose::SlashLow)
      {
        display.drawLine(handX, handY, handX + dir * 16, handY + 7, steel);
        display.drawLine(handX + dir * 16, handY + 7, handX + dir * 20, handY + 9, gold);
      }
      else
      {
        display.drawLine(handX, handY, handX + dir * 16, handY - 8, steel);
        display.drawLine(handX + dir * 16, handY - 8, handX + dir * 20, handY - 10, gold);
      }
      break;
    case WeaponType::Katana:
      if (pose == Pose::SlashLow)
      {
        display.drawLine(handX, handY, handX + dir * 12, handY + 6, steel);
        display.drawLine(handX + dir * 12, handY + 6, handX + dir * 14, handY + 4, steel);
      }
      else if (pose == Pose::DashSlash)
      {
        display.drawLine(handX, handY, handX + dir * 18, handY - 1, steel);
        display.drawLine(handX + dir * 18, handY - 1, handX + dir * 22, handY - 4, steel);
      }
      else if (pose == Pose::SlashHigh)
      {
        display.drawLine(handX, handY, handX + dir * 10, handY - 10, steel);
        display.drawLine(handX + dir * 10, handY - 10, handX + dir * 14, handY - 8, steel);
      }
      else
      {
        display.drawLine(handX, handY, handX + dir * 10, handY - 7, steel);
        display.drawLine(handX + dir * 10, handY - 7, handX + dir * 12, handY - 5, steel);
      }
      break;
    case WeaponType::Fist:
    default:
      display.fillCircle(handX + dir, handY, (pose == Pose::PunchHigh || pose == Pose::PunchLow || pose == Pose::Uppercut) ? 2 : 1, color);
      break;
    }
  }

  static void drawStickman(Adafruit_ST7735 &display, int x, int y, uint16_t color, WeaponType weapon, Pose pose, bool facingLeft)
  {
    const int dir = facingLeft ? -1 : 1;
    int headX = x;
    int headY = y - 10;
    int neckX = x;
    int neckY = y - 6;
    int hipX = x;
    int hipY = y + 6;
    int backHandX = x - dir * 5;
    int backHandY = y + 3;
    int frontHandX = x + dir * 6;
    int frontHandY = y + 2;
    int backFootX = x - dir * 5;
    int backFootY = y + 12;
    int frontFootX = x + dir * 5;
    int frontFootY = y + 12;

    switch (pose)
    {
    case Pose::Guard:
      backHandX = x - dir * 4;
      backHandY = y + 1;
      frontHandX = x + dir * 4;
      frontHandY = y - 4;
      backFootX = x - dir * 6;
      backFootY = y + 12;
      frontFootX = x + dir * 3;
      frontFootY = y + 11;
      break;
    case Pose::Sprint:
      headX = x + dir;
      neckX = x + dir;
      hipX = x - dir;
      backHandX = x - dir * 8;
      backHandY = y - 1;
      frontHandX = x + dir * 7;
      frontHandY = y + 5;
      backFootX = x - dir * 8;
      backFootY = y + 12;
      frontFootX = x + dir * 8;
      frontFootY = y + 9;
      break;
    case Pose::PunchHigh:
      frontHandX = x + dir * 11;
      frontHandY = y - 4;
      backHandX = x - dir * 5;
      backHandY = y + 1;
      backFootX = x - dir * 7;
      backFootY = y + 13;
      frontFootX = x + dir * 5;
      frontFootY = y + 11;
      break;
    case Pose::PunchLow:
      frontHandX = x + dir * 11;
      frontHandY = y + 2;
      backHandX = x - dir * 4;
      backHandY = y - 2;
      backFootX = x - dir * 6;
      backFootY = y + 13;
      frontFootX = x + dir * 6;
      frontFootY = y + 11;
      break;
    case Pose::Uppercut:
      headY = y - 11;
      neckX = x + dir;
      backHandX = x - dir * 5;
      backHandY = y + 1;
      frontHandX = x + dir * 8;
      frontHandY = y - 10;
      backFootX = x - dir * 6;
      backFootY = y + 13;
      frontFootX = x + dir * 4;
      frontFootY = y + 10;
      break;
    case Pose::Kick:
      hipX = x - dir;
      backHandX = x - dir * 4;
      backHandY = y - 3;
      frontHandX = x + dir * 5;
      frontHandY = y - 2;
      backFootX = x - dir * 4;
      backFootY = y + 13;
      frontFootX = x + dir * 11;
      frontFootY = y + 5;
      break;
    case Pose::SlashHigh:
      frontHandX = x + dir * 7;
      frontHandY = y - 7;
      backHandX = x - dir * 5;
      backHandY = y + 2;
      backFootX = x - dir * 6;
      backFootY = y + 13;
      frontFootX = x + dir * 6;
      frontFootY = y + 11;
      break;
    case Pose::SlashLow:
      neckX = x + dir;
      frontHandX = x + dir * 9;
      frontHandY = y + 1;
      backHandX = x - dir * 5;
      backHandY = y - 3;
      backFootX = x - dir * 4;
      backFootY = y + 13;
      frontFootX = x + dir * 8;
      frontFootY = y + 10;
      break;
    case Pose::DashSlash:
      headX = x + dir * 2;
      neckX = x + dir * 2;
      hipX = x + dir;
      backHandX = x - dir * 2;
      backHandY = y - 2;
      frontHandX = x + dir * 11;
      frontHandY = y - 3;
      backFootX = x - dir * 9;
      backFootY = y + 12;
      frontFootX = x + dir * 9;
      frontFootY = y + 9;
      break;
    case Pose::Thrust:
      headX = x + dir;
      neckX = x + dir;
      hipX = x - dir;
      backHandX = x - dir * 3;
      backHandY = y;
      frontHandX = x + dir * 7;
      frontHandY = y - 2;
      backFootX = x - dir * 6;
      backFootY = y + 13;
      frontFootX = x + dir * 8;
      frontFootY = y + 11;
      break;
    case Pose::Idle:
    default:
      break;
    }

    display.drawCircle(headX, headY, 4, color);
    display.drawLine(neckX, neckY, hipX, hipY, color);
    display.drawLine(neckX, y - 1, backHandX, backHandY, color);
    display.drawLine(neckX, y - 1, frontHandX, frontHandY, color);
    display.drawLine(hipX, hipY, backFootX, backFootY, color);
    display.drawLine(hipX, hipY, frontFootX, frontFootY, color);
    drawWeaponArt(display, weapon, frontHandX, frontHandY, dir, pose, color);
  }

  void renderWeaponSelect(Adafruit_ST7735 &display)
  {
    display.fillScreen(color565(18, 18, 24));
    drawCenteredText(display, 12, "SELECT WEAPON", COLOR_TEXT, 1);
    for (uint8_t i = 0; i < 3; ++i)
    {
      const int x = 10 + i * 50;
      const bool active = i == static_cast<uint8_t>(_weapon);
      display.drawRoundRect(x, 28, 40, 54, 4, active ? COLOR_ACCENT : COLOR_DIM);
      display.setCursor(x + 6, 34);
      display.setTextColor(active ? COLOR_TEXT : COLOR_DIM);
      display.print((i == 0) ? "Spear" : (i == 1) ? "Sword" : "Fist");
      drawStickman(display, x + 20, 64, active ? COLOR_TEXT : COLOR_DIM,
                   static_cast<WeaponType>(i), previewPoseForWeapon(static_cast<WeaponType>(i)), false);
    }
    display.setTextColor(COLOR_DIM);
    display.setCursor(12, 94);
    display.print((_weapon == WeaponType::Spear) ? "Long reach, slow" : (_weapon == WeaponType::Katana) ? "Sharp mid combo" : "Short, very fast");
    display.setCursor(10, kFooterY);
    display.print("B1 cycle  B2 start");
  }

  void drawAttack(Adafruit_ST7735 &display, bool leftSide) const
  {
    const int dir = leftSide ? -1 : 1;
    const uint16_t color = leftSide ? COLOR_ACCENT : COLOR_WARN;
    const Pose pose = currentPlayerPose(millis());
    if (pose == Pose::Uppercut)
    {
      display.drawLine(kPlayerX + dir * 2, 88, kPlayerX + dir * 12, 72, color);
      display.fillCircle(kPlayerX + dir * 12, 72, 3, color);
    }
    else if (pose == Pose::Kick)
    {
      display.drawLine(kPlayerX + dir * 1, 90, kPlayerX + dir * 16, 82, color);
      display.drawLine(kPlayerX + dir * 16, 82, kPlayerX + dir * 22, 84, color);
    }
    else if (pose == Pose::PunchHigh || pose == Pose::PunchLow)
    {
      display.fillCircle(kPlayerX + dir * 16, (pose == Pose::PunchHigh) ? 80 : 84, 3, color);
      display.drawLine(kPlayerX + dir * 4, 86, kPlayerX + dir * 16, (pose == Pose::PunchHigh) ? 80 : 84, color);
    }
    else if (_weapon == WeaponType::Spear)
    {
      if (pose == Pose::DashSlash)
      {
        display.drawLine(kPlayerX + dir * 2, 82, kPlayerX + dir * 38, 66, color);
        display.drawLine(kPlayerX + dir * 38, 66, kPlayerX + dir * 50, 64, color565(246, 214, 88));
      }
      else if (pose == Pose::SlashLow)
      {
        display.drawLine(kPlayerX + dir * 4, 88, kPlayerX + dir * 32, 94, color);
        display.drawLine(kPlayerX + dir * 32, 94, kPlayerX + dir * 40, 98, color565(246, 214, 88));
      }
      else
      {
        display.drawLine(kPlayerX + dir * 2, 84, kPlayerX + dir * 42, 74, color);
        display.drawLine(kPlayerX + dir * 42, 74, kPlayerX + dir * 52, 72, color565(246, 214, 88));
      }
    }
    else if (_weapon == WeaponType::Katana)
    {
      if (pose == Pose::DashSlash)
      {
        display.drawLine(kPlayerX + dir * 2, 84, kPlayerX + dir * 30, 84, color);
        display.drawLine(kPlayerX + dir * 2, 80, kPlayerX + dir * 26, 76, color);
      }
      else if (pose == Pose::SlashLow)
      {
        display.drawLine(kPlayerX + dir * 6, 90, kPlayerX + dir * 28, 96, color);
        display.drawLine(kPlayerX + dir * 4, 84, kPlayerX + dir * 24, 90, color);
      }
      else
      {
        display.drawLine(kPlayerX + dir * 4, 84, kPlayerX + dir * 26, 70, color);
        display.drawLine(kPlayerX + dir * 4, 90, kPlayerX + dir * 28, 82, color);
      }
    }
    else
    {
      display.fillCircle(kPlayerX + dir * 14, 82, 3, color);
      display.drawLine(kPlayerX + dir * 4, 86, kPlayerX + dir * 14, 82, color);
    }
  }

  void drawEnemy(Adafruit_ST7735 &display, const Enemy &enemy) const
  {
    const bool facingLeft = !enemy.fromLeft;
    uint16_t color = color565(255, 136, 90);
    WeaponType weapon = WeaponType::Fist;
    switch (enemy.type)
    {
    case EnemyType::Runner:
      color = color565(255, 136, 90);
      weapon = WeaponType::Fist;
      break;
    case EnemyType::Brute:
      color = color565(184, 92, 56);
      weapon = WeaponType::Fist;
      break;
    case EnemyType::Ninja:
      color = color565(148, 90, 220);
      weapon = WeaponType::Katana;
      break;
    case EnemyType::Guard:
      color = color565(82, 168, 255);
      weapon = WeaponType::Katana;
      break;
    case EnemyType::Lancer:
      color = color565(246, 198, 86);
      weapon = WeaponType::Spear;
      break;
    }
    drawStickman(display, static_cast<int>(enemy.x), 86, color, weapon, enemyPose(enemy), facingLeft);
    if (enemy.type == EnemyType::Guard)
    {
      const int dir = enemy.fromLeft ? 1 : -1;
      display.drawRoundRect(static_cast<int>(enemy.x) + dir * 6, 78, 6, 12, 2, color565(180, 196, 220));
    }
  }

  void attackSide(bool leftSide)
  {
    uint8_t hits = 0;
    for (uint8_t i = 0; i < kEnemyCount; ++i)
    {
      if (!_enemies[i].active || _enemies[i].fromLeft != leftSide)
      {
        continue;
      }
      const float dist = fabsf(_enemies[i].x - kPlayerX);
      if (dist <= weaponRange())
      {
        if (_weapon == WeaponType::Fist && dist > 22.0f)
        {
          continue;
        }
        if (_enemies[i].hp > weaponDamage())
        {
          _enemies[i].hp -= weaponDamage();
        }
        else
        {
          _enemies[i].active = false;
          ++_score;
        }
        ++hits;
        _comboCount = min<uint8_t>(9, _comboCount + 1);
        _comboFlashUntilMs = millis() + 900;
        if (hits >= weaponPierce())
        {
          break;
        }
      }
    }
    if (hits == 0)
    {
      _comboCount = 0;
    }
  }

  void spawnEnemy()
  {
    for (uint8_t i = 0; i < kEnemyCount; ++i)
    {
      if (_enemies[i].active)
      {
        continue;
      }
      const uint8_t typeRoll = random(0, 5);
      _enemies[i].type = static_cast<EnemyType>(typeRoll);
      _enemies[i].active = true;
      _enemies[i].fromLeft = random(0, 2) == 0;
      _enemies[i].x = _enemies[i].fromLeft ? 10.0f : 150.0f;
      _enemies[i].speed = _enemySpeed + random(-4, 7);
      _enemies[i].hp = (_enemies[i].type == EnemyType::Brute || _enemies[i].type == EnemyType::Guard) ? 3 : (_enemies[i].type == EnemyType::Lancer ? 2 : 1);
      _enemies[i].motionSeed = random(0, 4);
      return;
    }
  }

  void finish(uint32_t nowMs, bool completed)
  {
    _finished = true;
    _completed = completed;
    _durationMs = nowMs - _startMs;
  }

  Enemy _enemies[kEnemyCount];
  bool _finished = false;
  bool _completed = false;
  bool _weaponSelect = true;
  uint8_t _difficulty = 0;
  uint8_t _lives = 3;
  uint8_t _targetScore = 12;
  WeaponType _weapon = WeaponType::Katana;
  uint32_t _startMs = 0;
  uint32_t _lastUpdateMs = 0;
  uint32_t _lastSpawnMs = 0;
  uint32_t _durationMs = 0;
  uint32_t _spawnGapMs = 1000;
  uint32_t _attackFlashUntilMs[2] = {};
  uint32_t _cooldownUntilMs = 0;
  uint32_t _playerPoseUntilMs = 0;
  uint32_t _score = 0;
  uint32_t _comboFlashUntilMs = 0;
  uint8_t _attackCycle = 0;
  uint8_t _comboCount = 0;
  bool _playerFacingLeft = false;
  float _enemySpeed = 30.0f;
  Pose _playerPose = Pose::Idle;
};
class TriviaQuizGame : public IGame
{
public:
  GameId id() const override
  {
    return GameId::GridBattle;
  }

  void enter(uint8_t difficulty, uint32_t nowMs) override
  {
    _difficulty = clampValue<uint8_t>(difficulty, 0, 2);
    _finished = false;
    _completed = false;
    _questionIndex = 0;
    _selectedOption = 0;
    _correct = 0;
    _asked = 10;
    _timePerQuestionMs = (_difficulty == 0) ? 12000 : (_difficulty == 1) ? 9000
                                                                         : 7000;
    _startMs = nowMs;
    _questionStartMs = nowMs;
    _durationMs = 0;
    _liveQuestionsUsed = 0;
    buildQuestionOrder();
    loadCurrentQuestion();
  }

  void update(uint32_t nowMs, const InputSnapshot &input, const SensorSnapshot &) override
  {
    if (_finished)
    {
      return;
    }

    triviaNewsService.update(nowMs, wifiService.connected());
    if (!_current.live && _questionIndex == 0 && _liveQuestionsUsed == 0 && wifiService.connected() && triviaNewsService.ready() && (nowMs - _questionStartMs) < 1200)
    {
      loadCurrentQuestion();
    }

    if ((nowMs - _questionStartMs) >= _timePerQuestionMs)
    {
      advanceQuestion(nowMs, false);
      return;
    }

    if (input.btn1DownEdge)
    {
      _selectedOption = (_selectedOption + 1) % 4;
    }
    if (input.btn2DownEdge)
    {
      advanceQuestion(nowMs, _selectedOption == _current.answer);
    }
  }

  void renderTft(Adafruit_ST7735 &display) override
  {
    display.fillScreen(color565(18, 22, 34));
    display.fillRect(0, 0, hw::tftWidth, 14, COLOR_PANEL);
    display.setTextColor(COLOR_TEXT);
    display.setCursor(4, 3);
    display.print(_current.live ? "Live Facts" : "Cold Trivia");
    display.setCursor(112, 3);
    display.print(_correct);
    display.print("/");
    display.print(_asked);

    const uint32_t elapsed = min<uint32_t>(_timePerQuestionMs, millis() - _questionStartMs);
    const int timerWidth = static_cast<int>(((hw::tftWidth - 20) * (_timePerQuestionMs - elapsed)) / max<uint32_t>(1, _timePerQuestionMs));
    display.drawRoundRect(10, 20, hw::tftWidth - 20, 12, 3, COLOR_DIM);
    display.fillRoundRect(12, 22, max(0, timerWidth - 2), 8, 2, _current.live ? COLOR_WARN : COLOR_ACCENT);

    char promptLines[3][22] = {};
    wrapToLines(_current.prompt, promptLines, 3, 21);
    display.setTextColor(COLOR_TEXT);
    display.setCursor(10, 38);
    display.print(promptLines[0]);
    display.setCursor(10, 48);
    display.print(promptLines[1]);
    display.setCursor(10, 58);
    display.print(promptLines[2]);

    for (uint8_t i = 0; i < 4; ++i)
    {
      const int y = 72 + i * 11;
      const bool active = i == _selectedOption;
      display.fillRoundRect(8, y, 144, 9, 3, active ? COLOR_PANEL : COLOR_BG);
      display.drawRoundRect(8, y, 144, 9, 3, active ? (_current.live ? COLOR_WARN : COLOR_ACCENT) : COLOR_DIM);
      display.setCursor(12, y + 1);
      display.setTextColor(active ? COLOR_TEXT : COLOR_DIM);
      char optionLine[22] = {};
      formatOptionLine(optionLine, sizeof(optionLine), i, _selectedOption == i, _current.options[i]);
      display.print(optionLine);
    }

    display.setTextColor(COLOR_WARN);
    display.setCursor(10, 110);
    display.print("T:");
    display.print(static_cast<int>((_timePerQuestionMs - elapsed) / 1000));
    display.setCursor(52, 110);
    display.print("Net:");
    display.print(_current.live ? _current.source : triviaNewsService.statusLabel());
    display.setCursor(122, 110);
    display.print(kPongDifficultyNames[_difficulty][0]);

    display.setTextColor(COLOR_DIM);
    display.setCursor(18, kFooterY);
    display.print("B1 pick  B2 lock");
  }

  void renderOledOverlay(Adafruit_SSD1306 &display, const SensorSnapshot &) override
  {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print(_current.live ? "LIVE FACTS" : "COLD TRIVIA");
    display.setCursor(0, 8);
    display.print("Q");
    display.print(_questionIndex + 1);
    display.print("/");
    display.print(_asked);
    display.print(" OK:");
    display.print(_correct);
    display.setCursor(72, 8);
    display.print("Sel:");
    display.print(_selectedOption + 1);
    display.setCursor(0, 16);
    display.print("Src:");
    display.print(_current.live ? _current.source : "BANK");
    display.setCursor(0, 24);
    display.print("Left:");
    display.print(static_cast<int>((_timePerQuestionMs - min<uint32_t>(_timePerQuestionMs, millis() - _questionStartMs)) / 1000));
    display.print("s");
    display.setCursor(64, 24);
    display.print("Diff:");
    display.print(kPongDifficultyNames[_difficulty][0]);
    display.setCursor(0, 40);
    display.print("B1 pick");
    display.setCursor(0, 52);
    display.print("B2 lock");
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
    result.score = _correct * 180 + (_completed ? 600 : 0);
    result.durationMs = _durationMs;
    result.completed = _completed;
    snprintf(result.detail, sizeof(result.detail), "%u/%u correct", _correct, _asked);
    return result;
  }

private:
  struct QuestionView
  {
    char prompt[120] = {};
    char options[4][24] = {};
    uint8_t answer = 0;
    bool live = false;
    char source[12] = {};
  };

  struct FixedTriviaFact
  {
    const char *prompt;
    const char *correct;
    const char *wrong[3];
  };

  static constexpr uint16_t kBankSize = 500;
  static constexpr uint8_t kQuestionCount = 10;

  static const FixedTriviaFact *factTable()
  {
    static const FixedTriviaFact kFacts[] = {
        {"Capital of Canada?", "Ottawa", {"Toronto", "Vancouver", "Montreal"}},
        {"Biggest ocean?", "Pacific", {"Atlantic", "Arctic", "Indian"}},
        {"Sahara is a?", "Desert", {"River", "Lake", "Forest"}},
        {"Mount Fuji in?", "Japan", {"China", "Korea", "Nepal"}},
        {"Nile flows in?", "Africa", {"Europe", "Asia", "Oceania"}},
        {"Brazil speaks?", "Portuguese", {"Spanish", "French", "English"}},
        {"Tallest mountain?", "Everest", {"K2", "Fuji", "Kilimanjaro"}},
        {"Earth has how many poles?", "2", {"1", "3", "4"}},
        {"Map north is often?", "Top", {"Left", "Bottom", "Center"}},
        {"Greenland is on?", "North Atlantic", {"Indian", "South Pacific", "Black Sea"}},
        {"Water formula?", "H2O", {"CO2", "NaCl", "O3"}},
        {"Red planet?", "Mars", {"Venus", "Mercury", "Saturn"}},
        {"Humans breathe?", "Oxygen", {"Helium", "Steam", "Nitrogen only"}},
        {"Sun is a?", "Star", {"Planet", "Moon", "Comet"}},
        {"Plants need for food?", "Light", {"Plastic", "Glass", "Iron"}},
        {"Electric unit?", "Volt", {"Meter", "Gram", "Liter"}},
        {"Sound travels slower in?", "Air", {"Steel", "Water", "Glass"}},
        {"DNA shape is?", "Double helix", {"Triangle", "Cube", "Ring only"}},
        {"Body cools by?", "Sweating", {"Rusting", "Freezing", "Echo"}},
        {"Rainbow needs?", "Light and water", {"Dust and fire", "Snow and mud", "Metal and wind"}},
        {"Bee makes?", "Honey", {"Milk", "Silk", "Glass"}},
        {"Fridge keeps food?", "Cold", {"Dry", "Heavy", "Noisy"}},
        {"Seat belt helps in?", "Crash", {"Homework", "Rain", "Sunrise"}},
        {"Soap helps remove?", "Oil", {"Gravity", "Sound", "Shadow"}},
        {"Rice grows in?", "Fields", {"Caves", "Icebergs", "Volcanoes"}},
        {"Flashlight uses?", "Battery", {"Brick", "Ink", "Flour"}},
        {"Thermometer measures?", "Temperature", {"Speed", "Distance", "Weight"}},
        {"Traffic red means?", "Stop", {"Run", "Dance", "Sleep"}},
        {"Recycling cuts?", "Waste", {"Gravity", "Clouds", "Sunset"}},
        {"Window rain means air is?", "Humid", {"Dry", "Frozen", "Empty"}},
        {"Honey rarely?", "Spoils", {"Melts", "Shines", "Flows"}},
        {"Octopus has?", "3 hearts", {"1 heart", "2 hearts", "5 hearts"}},
        {"Bananas are berries?", "Yes", {"No", "Only green", "Only wild"}},
        {"Sharks older than?", "Trees", {"Cats", "Cars", "Birds"}},
        {"Wombat poop shape?", "Cube", {"Star", "Round only", "Triangle"}},
        {"Hot water may freeze faster?", "Sometimes", {"Never", "Always", "Only salt water"}},
        {"Blue whale heart?", "Car sized", {"Phone sized", "Coin sized", "Rice sized"}},
        {"Koala prints look like?", "Human prints", {"Dog prints", "Fish scales", "Leaf veins"}},
        {"Venus day vs year?", "Day is longer", {"Year is longer", "Same length", "No day"}},
        {"Ants can farm?", "Yes", {"No", "Only in labs", "Only in winter"}}};
    return kFacts;
  }

  static constexpr size_t factCount()
  {
    return 40;
  }

  static void drawWrapped(Adafruit_ST7735 &display, int x, int y, int maxWidth, const char *text, uint16_t color)
  {
    display.setTextColor(color);
    display.setCursor(x, y);
    int lineX = x;
    char word[18] = {};
    uint8_t len = 0;
    for (const char *ptr = text;; ++ptr)
    {
      const char ch = *ptr;
      if (ch != ' ' && ch != '\0')
      {
        if (len < (sizeof(word) - 1))
        {
          word[len++] = ch;
        }
      }
      if (ch == ' ' || ch == '\0')
      {
        word[len] = '\0';
        if (len > 0)
        {
          const int wordWidth = len * 6;
          if ((lineX + wordWidth) > (x + maxWidth))
          {
            y += 8;
            lineX = x;
            display.setCursor(x, y);
          }
          display.print(word);
          display.print(" ");
          lineX += wordWidth + 6;
        }
        len = 0;
      }
      if (ch == '\0')
      {
        break;
      }
    }
  }

  void advanceQuestion(uint32_t nowMs, bool correct)
  {
    if (correct)
    {
      ++_correct;
    }
    ++_questionIndex;
    _selectedOption = 0;
    _questionStartMs = nowMs;
    if (_questionIndex >= _asked)
    {
      _finished = true;
      _completed = _correct >= (_asked / 2);
      _durationMs = nowMs - _startMs;
    }
    else
    {
      loadCurrentQuestion();
    }
  }

  void buildQuestionOrder()
  {
    for (uint8_t i = 0; i < kQuestionCount; ++i)
    {
      bool unique = false;
      while (!unique)
      {
        unique = true;
        const uint16_t candidate = random(0, kBankSize);
        for (uint8_t j = 0; j < i; ++j)
        {
          if (_questionOrder[j] == candidate)
          {
            unique = false;
            break;
          }
        }
        if (unique)
        {
          _questionOrder[i] = candidate;
        }
      }
    }
  }

  static void setOption(char *dst, size_t size, const char *text)
  {
    strncpy(dst, text, size - 1);
    dst[size - 1] = '\0';
  }

  static void wrapToLines(const char *text, char lines[][22], uint8_t lineCount, uint8_t maxChars)
  {
    uint8_t line = 0;
    uint8_t pos = 0;
    char word[22] = {};
    uint8_t wordLen = 0;
    for (const char *ptr = text;; ++ptr)
    {
      const char ch = *ptr;
      if (ch != ' ' && ch != '\0')
      {
        if (wordLen < (sizeof(word) - 1))
        {
          word[wordLen++] = ch;
        }
      }
      if (ch == ' ' || ch == '\0')
      {
        word[wordLen] = '\0';
        if (wordLen > 0 && line < lineCount)
        {
          const uint8_t needed = wordLen + ((pos == 0) ? 0 : 1);
          if ((pos + needed) > maxChars)
          {
            ++line;
            pos = 0;
          }
          if (line >= lineCount)
          {
            break;
          }
          if (pos != 0)
          {
            lines[line][pos++] = ' ';
          }
          for (uint8_t i = 0; i < wordLen && pos < maxChars; ++i)
          {
            lines[line][pos++] = word[i];
          }
          lines[line][pos] = '\0';
        }
        wordLen = 0;
      }
      if (ch == '\0')
      {
        break;
      }
    }
  }

  static void formatOptionLine(char *dst, size_t size, uint8_t index, bool active, const char *text)
  {
    char clipped[17] = {};
    strncpy(clipped, text, sizeof(clipped) - 1);
    clipped[sizeof(clipped) - 1] = '\0';
    snprintf(dst, size, "%c%u.%s", active ? '>' : ' ', index + 1, clipped);
  }

  static void setNumberOption(char *dst, size_t size, int value)
  {
    snprintf(dst, size, "%d", value);
  }

  void placeNumericOptions(int correctValue, int spreadA, int spreadB)
  {
    _current.answer = random(0, 4);
    int values[4] = {
        correctValue,
        correctValue + spreadA,
        correctValue - spreadB,
        correctValue + spreadA + spreadB};
    if (values[1] == correctValue)
    {
      values[1] += 3;
    }
    if (values[2] == correctValue)
    {
      values[2] -= 2;
    }
    if (values[3] == correctValue || values[3] == values[1] || values[3] == values[2])
    {
      values[3] += 5;
    }

    uint8_t cursor = 0;
    for (uint8_t i = 0; i < 4; ++i)
    {
      if (i == _current.answer)
      {
        setNumberOption(_current.options[i], sizeof(_current.options[i]), correctValue);
      }
      else
      {
        while (cursor < 4 && values[cursor] == correctValue)
        {
          ++cursor;
        }
        setNumberOption(_current.options[i], sizeof(_current.options[i]), values[cursor++]);
      }
    }
  }

  void placeFactOptions(const FixedTriviaFact &fact)
  {
    _current.answer = random(0, 4);
    uint8_t wrongIndex = 0;
    for (uint8_t i = 0; i < 4; ++i)
    {
      if (i == _current.answer)
      {
        setOption(_current.options[i], sizeof(_current.options[i]), fact.correct);
      }
      else
      {
        setOption(_current.options[i], sizeof(_current.options[i]), fact.wrong[wrongIndex++]);
      }
    }
  }

  void loadCurrentQuestion()
  {
    _current = {};
    if (wifiService.connected() && triviaNewsService.ready() && _liveQuestionsUsed < 3 && ((_questionIndex % 3) == 0))
    {
      if (triviaNewsService.fillQuestion(_questionIndex + _liveQuestionsUsed, _current.prompt, sizeof(_current.prompt), _current.options, _current.answer, _current.source, sizeof(_current.source)))
      {
        _current.live = true;
        ++_liveQuestionsUsed;
        return;
      }
    }

    const uint16_t bankId = _questionOrder[_questionIndex];
    const FixedTriviaFact &fact = factTable()[bankId % factCount()];
    setOption(_current.prompt, sizeof(_current.prompt), fact.prompt);
    placeFactOptions(fact);
  }

  bool _finished = false;
  bool _completed = false;
  uint8_t _difficulty = 0;
  uint8_t _questionIndex = 0;
  uint8_t _selectedOption = 0;
  uint8_t _correct = 0;
  uint8_t _asked = 10;
  uint32_t _startMs = 0;
  uint32_t _questionStartMs = 0;
  uint32_t _durationMs = 0;
  uint32_t _timePerQuestionMs = 9000;
  uint8_t _liveQuestionsUsed = 0;
  uint16_t _questionOrder[kQuestionCount] = {};
  QuestionView _current;
};

class AngryBirdsGame : public IGame
{
public:
  GameId id() const override
  {
    return GameId::AngryBirds;
  }

  void enter(uint8_t difficulty, uint32_t nowMs) override
  {
    _difficulty = clampValue<uint8_t>(difficulty, 0, 2);
    _finished = false;
    _completed = false;
    _angleDeg = 44;
    _startMs = nowMs;
    _lastUpdateMs = nowMs;
    _durationMs = 0;
    _destroyed = 0;
    _pigHits = 0;
    _levelIndex = 0;
    _levelPigCount = 0;
    _birdActive = false;
    _birdContacts = 0;
    _birdX = kLaunchX;
    _birdY = kLaunchY;
    _birdVx = 0.0f;
    _birdVy = 0.0f;
    for (uint8_t i = 0; i < kBlockCount; ++i)
    {
      _blocks[i] = {};
    }
    loadLevel(0, nowMs);
  }

  void update(uint32_t nowMs, const InputSnapshot &input, const SensorSnapshot &) override
  {
    if (_finished)
    {
      return;
    }

    const float dt = clampValue((nowMs - _lastUpdateMs) / 1000.0f, 0.0f, 0.05f);
    _lastUpdateMs = nowMs;

    const bool fireNow = (!_birdActive) &&
                         ((input.btn1DownEdge && input.btn2Down) ||
                          (input.btn2DownEdge && input.btn1Down) ||
                          (input.btn1DownEdge && input.btn2DownEdge));

    if (!_birdActive)
    {
      if (input.btn1DownEdge && !input.btn2Down)
      {
        _angleDeg = min<int>(_angleDeg + 3, 72);
      }
      if (input.btn2DownEdge && !input.btn1Down)
      {
        _angleDeg = max<int>(_angleDeg - 3, 14);
      }
      if (fireNow && _shotsLeft > 0 && nowMs >= _levelBannerUntilMs)
      {
        launchBird();
      }
    }

    if (_birdActive)
    {
      updateBird(dt);
    }
    updateBlocks(dt);

    if (remainingPigs() == 0)
    {
      if ((_levelIndex + 1) >= kLevelCount)
      {
        finish(nowMs, true);
      }
      else
      {
        loadLevel(static_cast<uint8_t>(_levelIndex + 1), nowMs);
      }
      return;
    }

    if (!_birdActive && _shotsLeft == 0 && !anyDynamicBlocks())
    {
      finish(nowMs, false);
    }
  }

  void renderTft(Adafruit_ST7735 &display) override
  {
    display.fillScreen(color565(132, 196, 245));
    display.fillRect(0, 0, hw::tftWidth, 14, COLOR_PANEL);
    display.setTextColor(COLOR_TEXT);
    display.setCursor(4, 3);
    display.print("Angry Birds");
    display.setCursor(102, 3);
    display.print("L");
    display.print(_levelIndex + 1);
    display.print("/");
    display.print(kLevelCount);

    display.fillRect(0, 98, hw::tftWidth, 30, color565(86, 170, 82));
    display.fillCircle(kLaunchX - 4, kLaunchY + 4, 9, color565(124, 82, 34));
    display.drawLine(kLaunchX - 4, kLaunchY + 4, kLaunchX + static_cast<int>(cosf(radians(_angleDeg)) * 18.0f),
                     kLaunchY - static_cast<int>(sinf(radians(_angleDeg)) * 18.0f), COLOR_DANGER);
    display.drawLine(kLaunchX - 10, kLaunchY + 10, kLaunchX - 4, kLaunchY + 4, color565(92, 56, 24));
    display.drawLine(kLaunchX + 2, kLaunchY + 10, kLaunchX - 4, kLaunchY + 4, color565(92, 56, 24));

    for (uint8_t i = 0; i < kBlockCount; ++i)
    {
      if (_blocks[i].active)
      {
        if (_blocks[i].kind == 1)
        {
          drawPig(display, static_cast<int>(_blocks[i].x + _blocks[i].w * 0.5f), static_cast<int>(_blocks[i].y + _blocks[i].h * 0.5f));
        }
        else
        {
          drawStructureBlock(display, _blocks[i]);
        }
      }
    }

    if (!_birdActive)
    {
      drawPrediction(display);
      drawBird(display, static_cast<int>(kLaunchX), static_cast<int>(kLaunchY), false);
    }
    else
    {
      drawBird(display, static_cast<int>(_birdX), static_cast<int>(_birdY), true);
    }

    display.setTextColor(COLOR_TEXT);
    display.setCursor(8, 108);
    display.print("A:");
    display.print(_angleDeg);
    display.setCursor(48, 108);
    display.print("Pig:");
    display.print(remainingPigs());
    display.setCursor(98, 108);
    display.print("B:");
    display.print(_shotsLeft);
    display.setCursor(134, 108);
    display.print("K");
    display.print(_pigHits);

    display.setTextColor(COLOR_DIM);
    display.setCursor(8, kFooterY);
    display.print("B1 up B2 dn both sling");

    if (_levelBannerUntilMs > millis())
    {
      display.fillRoundRect(36, 34, 88, 20, 4, COLOR_PANEL);
      display.drawRoundRect(36, 34, 88, 20, 4, COLOR_WARN);
      display.setTextColor(COLOR_WARN);
      display.setCursor(48, 40);
      display.print("LEVEL ");
      display.print(_levelIndex + 1);
    }
  }

  void renderOledOverlay(Adafruit_SSD1306 &display, const SensorSnapshot &sensors) override
  {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print("ANGRY BIRDS");
    display.setCursor(0, 12);
    display.print("Lv:");
    display.print(_levelIndex + 1);
    display.print("/");
    display.print(kLevelCount);
    display.setCursor(64, 12);
    display.print("Shot:");
    display.print(_shotsLeft);
    display.setCursor(0, 24);
    display.print("Pig:");
    display.print(remainingPigs());
    display.setCursor(64, 24);
    display.print("Blk:");
    display.print(remainingBlocks());
    display.setCursor(0, 36);
    display.print("Hit:");
    display.print(_pigHits);
    display.setCursor(64, 36);
    display.print("Wi:");
    display.print(wifiService.statusLabel());
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
    result.score = _pigHits * 260 + _destroyed * 70 + _levelIndex * 140 + (_completed ? 1200 : 0);
    result.durationMs = _durationMs;
    result.completed = _completed;
    snprintf(result.detail, sizeof(result.detail), "L%u pigs:%u", _levelIndex + 1, _pigHits);
    return result;
  }

private:
  struct Block
  {
    bool active = false;
    bool dynamic = false;
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    uint8_t w = 12;
    uint8_t h = 12;
    uint8_t kind = 0;
    uint8_t hp = 18;
  };

  static constexpr uint8_t kBlockCount = 18;
  static constexpr uint8_t kLevelCount = 6;
  static constexpr float kLaunchX = 10.0f;
  static constexpr float kLaunchY = 94.0f;
  static constexpr float kGroundY = 98.0f;
  static constexpr float kBirdRadius = 5.0f;

  void launchBird()
  {
    _birdActive = true;
    _birdContacts = 0;
    _birdX = kLaunchX;
    _birdY = kLaunchY;
    _birdVx = cosf(radians(_angleDeg)) * launchSpeed();
    _birdVy = -sinf(radians(_angleDeg)) * launchSpeed();
    if (_shotsLeft > 0)
    {
      --_shotsLeft;
    }
  }

  uint8_t remainingBlocks() const
  {
    uint8_t count = 0;
    for (uint8_t i = 0; i < kBlockCount; ++i)
    {
      if (_blocks[i].active)
      {
        ++count;
      }
    }
    return count;
  }

  uint8_t remainingPigs() const
  {
    uint8_t pigs = 0;
    for (uint8_t i = 0; i < kBlockCount; ++i)
    {
      if (_blocks[i].active && _blocks[i].kind == 1)
      {
        ++pigs;
      }
    }
    return pigs;
  }

  bool anyDynamicBlocks() const
  {
    for (uint8_t i = 0; i < kBlockCount; ++i)
    {
      if (_blocks[i].active && (_blocks[i].dynamic || fabsf(_blocks[i].vx) > 6.0f || fabsf(_blocks[i].vy) > 8.0f))
      {
        return true;
      }
    }
    return false;
  }

  static void drawPig(Adafruit_ST7735 &display, int cx, int cy)
  {
    const uint16_t body = color565(118, 214, 92);
    const uint16_t shade = color565(68, 162, 56);
    const uint16_t snout = color565(164, 234, 140);
    const uint16_t dark = color565(34, 66, 28);
    display.fillCircle(cx, cy, 7, body);
    display.fillCircle(cx - 3, cy - 7, 2, body);
    display.fillCircle(cx + 3, cy - 7, 2, body);
    display.fillCircle(cx - 2, cy - 2, 1, dark);
    display.fillCircle(cx + 2, cy - 2, 1, dark);
    display.fillCircle(cx, cy + 2, 3, snout);
    display.drawPixel(cx - 1, cy + 2, shade);
    display.drawPixel(cx + 1, cy + 2, shade);
    display.drawCircle(cx, cy, 7, dark);
  }

  static void drawStructureBlock(Adafruit_ST7735 &display, const Block &block)
  {
    uint16_t fill = color565(218, 186, 122);
    uint16_t shade = color565(110, 80, 58);
    if (block.kind == 2)
    {
      fill = color565(198, 118, 72);
      shade = color565(130, 78, 48);
    }
    else if (block.kind == 3)
    {
      fill = color565(128, 138, 148);
      shade = color565(82, 90, 96);
    }
    display.fillRect(static_cast<int>(block.x), static_cast<int>(block.y), block.w, block.h, fill);
    display.drawRect(static_cast<int>(block.x), static_cast<int>(block.y), block.w, block.h, color565(52, 44, 40));
    if (block.w > block.h)
    {
      display.drawFastHLine(static_cast<int>(block.x) + 2, static_cast<int>(block.y) + (block.h / 2), block.w - 4, shade);
    }
    else
    {
      display.drawFastVLine(static_cast<int>(block.x) + (block.w / 2), static_cast<int>(block.y) + 2, block.h - 4, shade);
    }
  }

  static void drawBird(Adafruit_ST7735 &display, int cx, int cy, bool flying)
  {
    const uint16_t body = color565(214, 42, 44);
    const uint16_t belly = color565(248, 236, 210);
    const uint16_t beak = color565(252, 190, 48);
    const uint16_t dark = color565(34, 30, 34);
    display.fillCircle(cx, cy, 5, body);
    display.fillCircle(cx + 1, cy + 2, 3, belly);
    display.fillTriangle(cx - 2, cy - 4, cx + 3, cy - 6, cx + 1, cy - 1, dark);
    display.fillTriangle(cx + 4, cy, cx + 9, cy - 1, cx + 5, cy + 2, beak);
    if (flying)
    {
      display.drawLine(cx - 5, cy, cx - 9, cy - 4, dark);
      display.drawLine(cx - 4, cy + 1, cx - 8, cy + 4, dark);
    }
    else
    {
      display.drawLine(cx - 5, cy + 1, cx - 9, cy + 3, dark);
    }
    display.fillCircle(cx + 1, cy - 1, 1, ST77XX_WHITE);
    display.drawPixel(cx + 2, cy - 1, dark);
  }

  void drawPrediction(Adafruit_ST7735 &display)
  {
    float px = kLaunchX;
    float py = kLaunchY;
    float vx = cosf(radians(_angleDeg)) * launchSpeed();
    float vy = -sinf(radians(_angleDeg)) * launchSpeed();
    for (uint8_t i = 0; i < 20; ++i)
    {
      px += vx * 0.08f;
      py += vy * 0.08f;
      vy += gravity() * 0.08f;
      if (py > 102.0f || px > hw::tftWidth)
      {
        break;
      }
      display.drawPixel(static_cast<int>(px), static_cast<int>(py), COLOR_WARN);
    }
  }

  float launchSpeed() const
  {
    return (_difficulty == 0) ? 150.0f : (_difficulty == 1) ? 158.0f
                                                            : 166.0f;
  }

  static float gravity()
  {
    return 162.0f;
  }

  static float overlapAmount(float a0, float a1, float b0, float b1)
  {
    return max(0.0f, min(a1, b1) - max(a0, b0));
  }

  bool hasSupport(uint8_t index) const
  {
    const Block &block = _blocks[index];
    if (!block.active)
    {
      return false;
    }
    if ((block.y + block.h) >= (kGroundY - 0.5f))
    {
      return true;
    }
    for (uint8_t other = 0; other < kBlockCount; ++other)
    {
      if (other == index || !_blocks[other].active)
      {
        continue;
      }
      const float overlapX = overlapAmount(block.x, block.x + block.w, _blocks[other].x, _blocks[other].x + _blocks[other].w);
      if (overlapX < min<float>(4.0f, min(block.w, _blocks[other].w) * 0.35f))
      {
        continue;
      }
      if (fabsf((block.y + block.h) - _blocks[other].y) <= 2.5f && block.y < _blocks[other].y)
      {
        return true;
      }
    }
    return false;
  }

  void updateBird(float dt)
  {
    _birdX += _birdVx * dt;
    _birdY += _birdVy * dt;
    _birdVy += gravity() * dt;

    for (uint8_t i = 0; i < kBlockCount; ++i)
    {
      if (!_blocks[i].active)
      {
        continue;
      }
      float nx = 0.0f;
      float ny = 0.0f;
      if (circleRectCollision(_birdX, _birdY, kBirdRadius, _blocks[i].x, _blocks[i].y, _blocks[i].w, _blocks[i].h, nx, ny))
      {
        applyBirdImpact(i, nx, ny);
        break;
      }
    }

    const float birdSpeed = sqrtf(_birdVx * _birdVx + _birdVy * _birdVy);
    if (_birdX > hw::tftWidth + 12 || _birdY > hw::tftHeight + 12 || _birdY < 6 || birdSpeed < 34.0f || _birdContacts >= 4)
    {
      _birdActive = false;
    }
  }

  void updateBlocks(float dt)
  {
    for (uint8_t i = 0; i < kBlockCount; ++i)
    {
      if (_blocks[i].active && !_blocks[i].dynamic && !hasSupport(i))
      {
        _blocks[i].dynamic = true;
      }
    }

    for (uint8_t i = 0; i < kBlockCount; ++i)
    {
      Block &block = _blocks[i];
      if (!block.active || !block.dynamic)
      {
        continue;
      }

      const float prevY = block.y;
      block.vy += gravity() * dt;
      block.x += block.vx * dt;
      block.y += block.vy * dt;
      block.vx *= 0.988f;

      if (block.x < 84.0f)
      {
        block.x = 84.0f;
        block.vx *= -0.24f;
      }
      if ((block.x + block.w) > (hw::tftWidth - 2.0f))
      {
        block.x = hw::tftWidth - 2.0f - block.w;
        block.vx *= -0.24f;
      }

      bool landed = false;
      if ((block.y + block.h) >= kGroundY)
      {
        const float impact = fabsf(block.vy);
        block.y = kGroundY - block.h;
        block.vy = (impact > 30.0f) ? (-block.vy * 0.16f) : 0.0f;
        block.vx *= 0.82f;
        landed = true;
        applyImpactDamage(i, impact);
      }
      else
      {
        for (uint8_t other = 0; other < kBlockCount; ++other)
        {
          if (other == i || !_blocks[other].active)
          {
            continue;
          }

          const float overlapX = overlapAmount(block.x, block.x + block.w, _blocks[other].x, _blocks[other].x + _blocks[other].w);
          if (overlapX < min<float>(4.0f, min(block.w, _blocks[other].w) * 0.35f))
          {
            continue;
          }

          const bool crossingTop = (prevY + block.h) <= (_blocks[other].y + 2.0f) && (block.y + block.h) >= _blocks[other].y;
          if (!crossingTop)
          {
            continue;
          }

          if (_blocks[other].kind == 1 && block.kind != 1)
          {
            destroyBlock(other);
            continue;
          }

          const float impact = fabsf(block.vy);
          block.y = _blocks[other].y - block.h;
          block.vy = (impact > 26.0f) ? (-block.vy * 0.14f) : 0.0f;
          block.vx *= 0.86f;
          landed = true;
          applyImpactDamage(i, impact * 0.75f);
          break;
        }
      }

      if (block.kind != 1 && (fabsf(block.vx) + fabsf(block.vy)) > 42.0f)
      {
        crushNearbyPigs(i);
      }

      if (landed && fabsf(block.vx) < 8.0f && fabsf(block.vy) < 12.0f && hasSupport(i))
      {
        block.vx = 0.0f;
        block.vy = 0.0f;
        block.dynamic = false;
      }
    }
  }

  void applyBirdImpact(uint8_t index, float nx, float ny)
  {
    Block &block = _blocks[index];
    const float speed = sqrtf(_birdVx * _birdVx + _birdVy * _birdVy);
    const float mass = (block.kind == 3) ? 1.8f : (block.kind == 2) ? 1.25f : (block.kind == 1) ? 0.55f
                                                                                                 : 0.9f;

    block.dynamic = true;
    block.vx += _birdVx * 0.24f + nx * speed * 0.18f / mass;
    block.vy += _birdVy * 0.18f + ny * speed * 0.16f / mass;
    applyDamage(index, speed * ((block.kind == 3) ? 0.30f : 0.46f) + 8.0f);
    scatterNearby(block.x + block.w * 0.5f, block.y + block.h * 0.5f, speed * 0.28f, index);

    const float dot = (_birdVx * nx) + (_birdVy * ny);
    _birdVx -= 1.45f * dot * nx;
    _birdVy -= 1.45f * dot * ny;
    _birdVx *= 0.56f;
    _birdVy *= 0.56f;
    ++_birdContacts;
  }

  void applyImpactDamage(uint8_t index, float impact)
  {
    if (!_blocks[index].active || impact < 24.0f)
    {
      return;
    }
    applyDamage(index, impact * ((_blocks[index].kind == 3) ? 0.22f : 0.34f));
  }

  void applyDamage(uint8_t index, float amount)
  {
    if (!_blocks[index].active)
    {
      return;
    }

    if (_blocks[index].kind == 1)
    {
      if (amount >= 10.0f)
      {
        destroyBlock(index);
      }
      return;
    }

    const int nextHp = static_cast<int>(_blocks[index].hp) - static_cast<int>(amount);
    if (nextHp <= 0)
    {
      destroyBlock(index);
      return;
    }

    _blocks[index].hp = static_cast<uint8_t>(nextHp);
    if (amount >= 18.0f)
    {
      _blocks[index].dynamic = true;
    }
  }

  void destroyBlock(uint8_t index)
  {
    if (!_blocks[index].active)
    {
      return;
    }

    const float cx = _blocks[index].x + _blocks[index].w * 0.5f;
    const float cy = _blocks[index].y + _blocks[index].h * 0.5f;
    if (_blocks[index].kind == 1)
    {
      ++_pigHits;
    }
    ++_destroyed;
    _blocks[index].active = false;
    _blocks[index].dynamic = false;
    scatterNearby(cx, cy, 26.0f, index);
  }

  void scatterNearby(float cx, float cy, float force, int ignoreIndex)
  {
    const float radius = 32.0f;
    for (uint8_t i = 0; i < kBlockCount; ++i)
    {
      if (!_blocks[i].active || static_cast<int>(i) == ignoreIndex)
      {
        continue;
      }
      const float ox = (_blocks[i].x + _blocks[i].w * 0.5f) - cx;
      const float oy = (_blocks[i].y + _blocks[i].h * 0.5f) - cy;
      const float distSq = ox * ox + oy * oy;
      if (distSq > (radius * radius))
      {
        continue;
      }
      const float dist = max(2.0f, sqrtf(distSq));
      const float scale = (radius - dist) / radius;
      _blocks[i].dynamic = true;
      _blocks[i].vx += (ox / dist) * force * 0.24f * scale;
      _blocks[i].vy += (oy / dist) * force * 0.20f * scale - 8.0f * scale;
    }
  }

  void crushNearbyPigs(uint8_t index)
  {
    const Block &block = _blocks[index];
    for (uint8_t i = 0; i < kBlockCount; ++i)
    {
      if (i == index || !_blocks[i].active || _blocks[i].kind != 1)
      {
        continue;
      }
      if (overlapAmount(block.x, block.x + block.w, _blocks[i].x, _blocks[i].x + _blocks[i].w) > 4.0f &&
          overlapAmount(block.y, block.y + block.h, _blocks[i].y, _blocks[i].y + _blocks[i].h) > 3.0f)
      {
        destroyBlock(i);
      }
    }
  }

  void addBlock(uint8_t kind, float x, float y, uint8_t w, uint8_t h)
  {
    for (uint8_t i = 0; i < kBlockCount; ++i)
    {
      if (_blocks[i].active)
      {
        continue;
      }
      _blocks[i].active = true;
      _blocks[i].dynamic = false;
      _blocks[i].x = x;
      _blocks[i].y = y;
      _blocks[i].vx = 0.0f;
      _blocks[i].vy = 0.0f;
      _blocks[i].w = w;
      _blocks[i].h = h;
      _blocks[i].kind = kind;
      _blocks[i].hp = (kind == 3) ? 38 : (kind == 2) ? 28 : (kind == 1) ? 10
                                                                         : 22;
      if (kind == 1)
      {
        ++_levelPigCount;
      }
      return;
    }
  }

  void loadLevel(uint8_t level, uint32_t nowMs)
  {
    _levelIndex = clampValue<uint8_t>(level, 0, kLevelCount - 1);
    _levelPigCount = 0;
    _birdActive = false;
    _birdContacts = 0;
    _birdX = kLaunchX;
    _birdY = kLaunchY;
    _birdVx = 0.0f;
    _birdVy = 0.0f;
    _angleDeg = 44;
    _shotsLeft = (_difficulty == 0) ? 6 : (_difficulty == 1) ? 5
                                                             : 4;
    _levelBannerUntilMs = nowMs + 1200;
    for (uint8_t i = 0; i < kBlockCount; ++i)
    {
      _blocks[i] = {};
    }

    switch (_levelIndex)
    {
    case 0:
      addBlock(0, 114, 82, 12, 16);
      addBlock(0, 136, 82, 12, 16);
      addBlock(2, 110, 70, 42, 10);
      addBlock(1, 124, 56, 14, 14);
      break;
    case 1:
      addBlock(0, 110, 84, 12, 14);
      addBlock(0, 128, 84, 12, 14);
      addBlock(0, 146, 84, 12, 14);
      addBlock(2, 108, 72, 52, 8);
      addBlock(1, 118, 58, 14, 14);
      addBlock(1, 142, 58, 14, 14);
      break;
    case 2:
      addBlock(3, 108, 88, 18, 10);
      addBlock(3, 138, 88, 18, 10);
      addBlock(0, 112, 72, 12, 16);
      addBlock(0, 140, 72, 12, 16);
      addBlock(2, 110, 60, 44, 8);
      addBlock(1, 124, 46, 14, 14);
      break;
    case 3:
      addBlock(0, 104, 84, 12, 14);
      addBlock(0, 122, 84, 12, 14);
      addBlock(0, 140, 84, 12, 14);
      addBlock(2, 102, 72, 34, 8);
      addBlock(2, 138, 72, 22, 8);
      addBlock(1, 110, 58, 14, 14);
      addBlock(1, 142, 58, 14, 14);
      addBlock(1, 126, 44, 14, 14);
      break;
    case 4:
      addBlock(3, 98, 88, 20, 10);
      addBlock(3, 142, 88, 20, 10);
      addBlock(0, 102, 72, 12, 16);
      addBlock(0, 146, 72, 12, 16);
      addBlock(2, 104, 60, 52, 8);
      addBlock(0, 122, 76, 12, 12);
      addBlock(1, 106, 46, 14, 14);
      addBlock(1, 140, 46, 14, 14);
      addBlock(1, 124, 62, 14, 14);
      break;
    default:
      addBlock(3, 98, 88, 18, 10);
      addBlock(3, 120, 88, 18, 10);
      addBlock(3, 142, 88, 18, 10);
      addBlock(0, 100, 72, 12, 16);
      addBlock(0, 124, 72, 12, 16);
      addBlock(0, 148, 72, 12, 16);
      addBlock(2, 98, 60, 26, 8);
      addBlock(2, 122, 48, 26, 8);
      addBlock(2, 146, 60, 14, 8);
      addBlock(1, 104, 46, 14, 14);
      addBlock(1, 128, 34, 14, 14);
      addBlock(1, 148, 46, 14, 14);
      break;
    }
  }

  void finish(uint32_t nowMs, bool completed)
  {
    _finished = true;
    _completed = completed;
    _durationMs = nowMs - _startMs;
  }

  Block _blocks[kBlockCount];
  bool _finished = false;
  bool _completed = false;
  bool _birdActive = false;
  uint8_t _difficulty = 0;
  uint8_t _angleDeg = 42;
  uint8_t _shotsLeft = 3;
  uint8_t _birdContacts = 0;
  uint8_t _levelIndex = 0;
  uint8_t _levelPigCount = 0;
  uint8_t _destroyed = 0;
  uint8_t _pigHits = 0;
  uint32_t _startMs = 0;
  uint32_t _lastUpdateMs = 0;
  uint32_t _durationMs = 0;
  uint32_t _levelBannerUntilMs = 0;
  float _birdX = kLaunchX;
  float _birdY = kLaunchY;
  float _birdVx = 0.0f;
  float _birdVy = 0.0f;
};

class GomokuGame : public IGame
{
public:
  GameId id() const override
  {
    return GameId::Gomoku;
  }

  bool studyMode() const
  {
    return _difficulty == 3 && !_finished;
  }

  void enter(uint8_t difficulty, uint32_t nowMs) override
  {
    _difficulty = clampValue<uint8_t>(difficulty, 0, 3);
    _finished = false;
    _completed = false;
    _winner = 0;
    _cursorX = 4;
    _cursorY = 4;
    _currentPlayer = 1;
    _startMs = nowMs;
    _durationMs = 0;
    memset(_board, 0, sizeof(_board));
    refreshHint();
  }

  void update(uint32_t nowMs, const InputSnapshot &input, const SensorSnapshot &) override
  {
    if (_finished)
    {
      return;
    }

    const bool placeNow = (input.btn1DownEdge && input.btn2Down) ||
                          (input.btn2DownEdge && input.btn1Down) ||
                          (input.btn1DownEdge && input.btn2DownEdge);
    if (!placeNow)
    {
      if (input.btn1DownEdge)
      {
        _cursorX = (_cursorX + 1) % kBoardSize;
      }
      if (input.btn2DownEdge)
      {
        _cursorY = (_cursorY + 1) % kBoardSize;
      }
      return;
    }

    if (_board[_cursorY][_cursorX] != 0)
    {
      return;
    }

    placeStone(_cursorX, _cursorY, _currentPlayer);
    if (checkWin(_cursorX, _cursorY, _currentPlayer))
    {
      finish(nowMs, _currentPlayer == 1, _currentPlayer);
      return;
    }
    if (boardFull())
    {
      finish(nowMs, false, 0);
      return;
    }

    if (_difficulty == 0 || _difficulty == 3)
    {
      _currentPlayer = (_currentPlayer == 1) ? 2 : 1;
      refreshHint();
      return;
    }

    _currentPlayer = 2;
    aiMove(_difficulty == 2);
    if (checkWin(_lastAiX, _lastAiY, 2))
    {
      finish(nowMs, false, 2);
      return;
    }
    if (boardFull())
    {
      finish(nowMs, false, 0);
      return;
    }
    _currentPlayer = 1;
    refreshHint();
  }

  void renderTft(Adafruit_ST7735 &display) override
  {
    const uint16_t woodBase = color565(168, 126, 72);
    const uint16_t woodDark = color565(98, 62, 28);
    const uint16_t woodLine = color565(84, 52, 22);
    const uint16_t woodLight = color565(196, 156, 98);
    display.fillScreen(color565(82, 58, 30));
    display.fillRoundRect(4, 16, 152, 108, 6, woodBase);
    display.drawRoundRect(4, 16, 152, 108, 6, woodDark);
    display.fillRect(0, 0, hw::tftWidth, 14, color565(42, 28, 16));
    display.setTextColor(COLOR_TEXT);
    display.setCursor(4, 3);
    display.print("Gomoku");
    display.setCursor(86, 3);
    display.print(kGomokuDifficultyNames[_difficulty]);

    for (int grainY = 22; grainY < 118; grainY += 10)
    {
      display.drawFastHLine(10, grainY, 140, (grainY & 0x10) ? woodLight : woodDark);
    }

    for (uint8_t i = 0; i < kBoardSize; ++i)
    {
      display.drawFastHLine(kBoardX, kBoardY + i * kCell, (kBoardSize - 1) * kCell, woodLine);
      display.drawFastVLine(kBoardX + i * kCell, kBoardY, (kBoardSize - 1) * kCell, woodLine);
    }

    for (uint8_t sy = 2; sy <= 6; sy += 2)
    {
      for (uint8_t sx = 2; sx <= 6; sx += 2)
      {
        display.fillCircle(kBoardX + sx * kCell, kBoardY + sy * kCell, 1, woodDark);
      }
    }

    for (uint8_t y = 0; y < kBoardSize; ++y)
    {
      for (uint8_t x = 0; x < kBoardSize; ++x)
      {
        const int px = kBoardX + x * kCell;
        const int py = kBoardY + y * kCell;
        if (_board[y][x] == 1)
        {
          display.fillCircle(px + 1, py + 1, 4, color565(52, 34, 18));
          display.fillCircle(px, py, 4, ST77XX_BLACK);
        }
        else if (_board[y][x] == 2)
        {
          display.fillCircle(px + 1, py + 1, 4, color565(110, 96, 68));
          display.fillCircle(px, py, 4, ST77XX_WHITE);
          display.drawCircle(px, py, 4, woodDark);
        }
      }
    }

    if (_difficulty == 3 && _hintReady && _board[_hintY][_hintX] == 0)
    {
      const int hx = kBoardX + _hintX * kCell;
      const int hy = kBoardY + _hintY * kCell;
      display.drawCircle(hx, hy, 6, COLOR_GOOD);
      display.drawFastHLine(hx - 3, hy, 7, COLOR_GOOD);
      display.drawFastVLine(hx, hy - 3, 7, COLOR_GOOD);
    }

    if (!_finished)
    {
      const int cx = kBoardX + _cursorX * kCell;
      const int cy = kBoardY + _cursorY * kCell;
      const uint16_t cursorColor = (_currentPlayer == 1) ? COLOR_ACCENT : COLOR_WARN;
      display.drawCircle(cx, cy, 6, cursorColor);
      display.drawFastHLine(cx - 4, cy, 9, cursorColor);
      display.drawFastVLine(cx, cy - 4, 9, cursorColor);
    }

    display.setTextColor(COLOR_TEXT);
    display.setCursor(8, 108);
    display.print("Turn:");
    if (_difficulty == 0 || _difficulty == 3)
    {
      display.print(_currentPlayer == 1 ? "P1" : "P2");
    }
    else
    {
      display.print(_currentPlayer == 1 ? "You" : "AI");
    }
    display.setCursor(86, 108);
    if (_difficulty == 3 && _hintReady)
    {
      display.print("Tip ");
      display.print(_hintX + 1);
      display.print(",");
      display.print(_hintY + 1);
    }
    else
    {
      display.print("XY ");
      display.print(_cursorX + 1);
      display.print(",");
      display.print(_cursorY + 1);
    }

    display.setTextColor(COLOR_DIM);
    display.setCursor(8, kFooterY);
    if (_difficulty == 3)
    {
      display.print("H1 exit Tip:");
      display.print(_hintReady ? "on" : "--");
    }
    else
    {
      display.print("H1 exit both set");
    }
  }

  void renderOledOverlay(Adafruit_SSD1306 &display, const SensorSnapshot &sensors) override
  {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print("GOMOKU");
    display.setCursor(0, 12);
    display.print("Cur:");
    display.print(_cursorX + 1);
    display.print(",");
    display.print(_cursorY + 1);
    display.setCursor(64, 12);
    display.print("Mode:");
    display.print(kGomokuDifficultyNames[_difficulty]);
    display.setCursor(0, 24);
    display.print("Turn:");
    display.print(_currentPlayer);
    if (_difficulty == 3 && _hintReady)
    {
      display.setCursor(52, 24);
      display.print("Tip:");
      display.print(_hintX + 1);
      display.print(",");
      display.print(_hintY + 1);
    }
    display.setCursor(0, 36);
    if (_difficulty == 3)
    {
      display.print("Focus:");
      display.print(gomokuModeFocus(_difficulty));
      display.setCursor(0, 48);
      display.print("Games:");
      display.print(storageService.gomokuLearning().totalGames);
      display.setCursor(72, 48);
      display.print("Lv~");
      display.print(gomokuLearningLevel(storageService.gomokuLearning(), _difficulty));
    }
    else
    {
      display.print("Wi:");
      display.print(wifiService.statusLabel());
    }
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
    result.winner = (_difficulty == 0 || _difficulty == 3) ? _winner : 0;
    result.score = (_winner == 1 ? 1500 : 700) + _difficulty * 180;
    if (_difficulty == 0 || _difficulty == 3)
    {
      snprintf(result.detail, sizeof(result.detail), "Winner P%u", _winner);
    }
    else
    {
      strncpy(result.detail, (_winner == 1) ? "You win" : (_winner == 2) ? "AI wins" : "Draw", sizeof(result.detail) - 1);
    }
    return result;
  }

private:
  static constexpr uint8_t kBoardSize = 9;
  static constexpr int kBoardX = 28;
  static constexpr int kBoardY = 20;
  static constexpr int kCell = 11;

  void placeStone(uint8_t x, uint8_t y, uint8_t player)
  {
    _board[y][x] = player;
  }

  bool boardFull() const
  {
    for (uint8_t y = 0; y < kBoardSize; ++y)
    {
      for (uint8_t x = 0; x < kBoardSize; ++x)
      {
        if (_board[y][x] == 0)
        {
          return false;
        }
      }
    }
    return true;
  }

  bool checkWin(uint8_t x, uint8_t y, uint8_t player) const
  {
    static const int8_t dirs[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};
    for (uint8_t i = 0; i < 4; ++i)
    {
      int count = 1;
      count += countDir(x, y, dirs[i][0], dirs[i][1], player);
      count += countDir(x, y, -dirs[i][0], -dirs[i][1], player);
      if (count >= 5)
      {
        return true;
      }
    }
    return false;
  }

  int countDir(int x, int y, int dx, int dy, uint8_t player) const
  {
    int total = 0;
    x += dx;
    y += dy;
    while (x >= 0 && x < kBoardSize && y >= 0 && y < kBoardSize && _board[y][x] == player)
    {
      ++total;
      x += dx;
      y += dy;
    }
    return total;
  }

  bool inBounds(int x, int y) const
  {
    return x >= 0 && x < kBoardSize && y >= 0 && y < kBoardSize;
  }

  bool hasNeighbor(uint8_t x, uint8_t y, uint8_t radius = 2) const
  {
    for (int dy = -radius; dy <= radius; ++dy)
    {
      for (int dx = -radius; dx <= radius; ++dx)
      {
        if ((dx == 0 && dy == 0) || !inBounds(x + dx, y + dy))
        {
          continue;
        }
        if (_board[y + dy][x + dx] != 0)
        {
          return true;
        }
      }
    }
    return false;
  }

  int lineScore(int len, int openEnds, bool attackBias, bool hard) const
  {
    if (len >= 5)
    {
      return 200000;
    }
    if (len == 4 && openEnds == 2)
    {
      return attackBias ? 30000 : 26000;
    }
    if (len == 4 && openEnds == 1)
    {
      return attackBias ? 12000 : 18000;
    }
    if (len == 3 && openEnds == 2)
    {
      return attackBias ? (hard ? 7000 : 5200) : (hard ? 9200 : 6400);
    }
    if (len == 3 && openEnds == 1)
    {
      return attackBias ? 1800 : 2600;
    }
    if (len == 2 && openEnds == 2)
    {
      return attackBias ? 700 : 820;
    }
    if (len == 2 && openEnds == 1)
    {
      return 180;
    }
    return (len == 1 && openEnds == 2) ? 24 : 0;
  }

  int evaluatePattern(uint8_t x, uint8_t y, uint8_t player, bool hard) const
  {
    static const int8_t dirs[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};
    int score = 0;
    const uint8_t opponent = 3 - player;
    for (uint8_t i = 0; i < 4; ++i)
    {
      const int dx = dirs[i][0];
      const int dy = dirs[i][1];
      const int ownF = countDir(x, y, dx, dy, player);
      const int ownB = countDir(x, y, -dx, -dy, player);
      const int ownLen = 1 + ownF + ownB;
      const bool ownOpenF = inBounds(x + (ownF + 1) * dx, y + (ownF + 1) * dy) && _board[y + (ownF + 1) * dy][x + (ownF + 1) * dx] == 0;
      const bool ownOpenB = inBounds(x - (ownB + 1) * dx, y - (ownB + 1) * dy) && _board[y - (ownB + 1) * dy][x - (ownB + 1) * dx] == 0;
      score += lineScore(ownLen, static_cast<int>(ownOpenF) + static_cast<int>(ownOpenB), true, hard);

      const int oppF = countDir(x, y, dx, dy, opponent);
      const int oppB = countDir(x, y, -dx, -dy, opponent);
      const int oppLen = 1 + oppF + oppB;
      const bool oppOpenF = inBounds(x + (oppF + 1) * dx, y + (oppF + 1) * dy) && _board[y + (oppF + 1) * dy][x + (oppF + 1) * dx] == 0;
      const bool oppOpenB = inBounds(x - (oppB + 1) * dx, y - (oppB + 1) * dy) && _board[y - (oppB + 1) * dy][x - (oppB + 1) * dx] == 0;
      score += lineScore(oppLen, static_cast<int>(oppOpenF) + static_cast<int>(oppOpenB), false, hard);
    }
    const int cx = abs(static_cast<int>(x) - 4);
    const int cy = abs(static_cast<int>(y) - 4);
    score += hard ? (26 - (cx + cy) * 3) : (14 - (cx + cy) * 2);
    return score;
  }

  int strongestReply(uint8_t player, bool hard) const
  {
    int best = 0;
    for (uint8_t y = 0; y < kBoardSize; ++y)
    {
      for (uint8_t x = 0; x < kBoardSize; ++x)
      {
        if (_board[y][x] != 0 || !hasNeighbor(x, y))
        {
          continue;
        }
        best = max(best, evaluatePattern(x, y, player, hard));
      }
    }
    return best;
  }

  bool chooseBestMove(uint8_t player, bool hard, uint8_t &bestX, uint8_t &bestY, int &bestScore)
  {
    const uint8_t opponent = 3 - player;
    bool hasStone = false;
    for (uint8_t y = 0; y < kBoardSize && !hasStone; ++y)
    {
      for (uint8_t x = 0; x < kBoardSize; ++x)
      {
        if (_board[y][x] != 0)
        {
          hasStone = true;
          break;
        }
      }
    }

    if (!hasStone)
    {
      bestX = 4;
      bestY = 4;
      bestScore = 1000;
      return true;
    }

    bestScore = -999999;
    bestX = 4;
    bestY = 4;

    for (uint8_t y = 0; y < kBoardSize; ++y)
    {
      for (uint8_t x = 0; x < kBoardSize; ++x)
      {
        if (_board[y][x] != 0 || !hasNeighbor(x, y))
        {
          continue;
        }

        _board[y][x] = player;
        if (checkWin(x, y, player))
        {
          _board[y][x] = 0;
          bestX = x;
          bestY = y;
          bestScore = 300000;
          return true;
        }

        int score = evaluatePattern(x, y, player, hard);
        _board[y][x] = opponent;
        const bool blocksOpponentWin = checkWin(x, y, opponent);
        _board[y][x] = player;
        if (blocksOpponentWin)
        {
          score += hard ? 32000 : 25000;
        }

        if (hard)
        {
          score -= strongestReply(opponent, false) / 2;
        }
        else
        {
          score += random(0, 18);
        }
        _board[y][x] = 0;

        if (score > bestScore)
        {
          bestScore = score;
          bestX = x;
          bestY = y;
        }
      }
    }
    return bestScore > -999999;
  }

  void aiMove(bool hard)
  {
    int bestScore = 0;
    uint8_t bestX = 4;
    uint8_t bestY = 4;
    if (!chooseBestMove(2, hard, bestX, bestY, bestScore))
    {
      bestX = 4;
      bestY = 4;
    }
    placeStone(bestX, bestY, 2);
    _lastAiX = bestX;
    _lastAiY = bestY;
    _cursorX = bestX;
    _cursorY = bestY;
  }

  void refreshHint()
  {
    _hintReady = false;
    _hintScore = 0;
    if (_finished || _difficulty != 3)
    {
      return;
    }

    int bestScore = 0;
    uint8_t bestX = 4;
    uint8_t bestY = 4;
    if (chooseBestMove(_currentPlayer, true, bestX, bestY, bestScore))
    {
      _hintReady = true;
      _hintX = bestX;
      _hintY = bestY;
      _hintScore = bestScore;
    }
  }

  void finish(uint32_t nowMs, bool completed, uint8_t winner)
  {
    _finished = true;
    _completed = completed;
    _winner = winner;
    _durationMs = nowMs - _startMs;
  }

  uint8_t _board[kBoardSize][kBoardSize] = {};
  bool _finished = false;
  bool _completed = false;
  uint8_t _difficulty = 0;
  uint8_t _winner = 0;
  uint8_t _currentPlayer = 1;
  uint8_t _cursorX = 4;
  uint8_t _cursorY = 4;
  uint8_t _lastAiX = 4;
  uint8_t _lastAiY = 4;
  uint8_t _hintX = 4;
  uint8_t _hintY = 4;
  bool _hintReady = false;
  int _hintScore = 0;
  uint32_t _startMs = 0;
  uint32_t _durationMs = 0;
};

class ChoiceOfLifeGame : public IGame
{
public:
  GameId id() const override
  {
    return GameId::ChoiceOfLife;
  }

  void enter(uint8_t difficulty, uint32_t nowMs) override
  {
    (void)difficulty;
    _finished = false;
    _completed = false;
    _startMs = nowMs;
    _durationMs = 0;
    memset(_endingCause, 0, sizeof(_endingCause));

    LifeSaveData save;
    if (storageService.loadLifeSave(save))
    {
      _nodeIndex = save.nodeIndex;
      _age = save.age;
      _money = save.money;
      _honor = save.honor;
      _gender = save.gender;
    }
    else
    {
      _nodeIndex = 0;
      _age = 0;
      _money = 0;
      _honor = 0;
      _gender = 0;
      persist();
    }
  }

  void update(uint32_t nowMs, const InputSnapshot &input, const SensorSnapshot &) override
  {
    if (_finished)
    {
      return;
    }
    if (input.btn1DownEdge)
    {
      choose(nowMs, true);
    }
    if (input.btn2DownEdge)
    {
      choose(nowMs, false);
    }
  }

  void renderTft(Adafruit_ST7735 &display) override
  {
    display.fillScreen(color565(22, 20, 16));
    display.fillRect(0, 0, hw::tftWidth, 14, COLOR_PANEL);
    display.setTextColor(COLOR_TEXT);
    display.setCursor(4, 3);
    display.print("Choice of Life");

    display.setCursor(8, 20);
    display.print("Age:");
    display.print(_age);
    display.setCursor(58, 20);
    display.print("$:");
    display.print(_money);
    display.setCursor(108, 20);
    display.print("H:");
    display.print(_honor);

    display.drawRoundRect(8, 30, 144, 34, 4, COLOR_DIM);
    drawWrapped(display, 12, 36, 136, currentBody(), COLOR_TEXT);

    display.drawRoundRect(8, 74, 66, 30, 4, COLOR_ACCENT);
    display.drawRoundRect(86, 74, 66, 30, 4, COLOR_WARN);
    drawWrapped(display, 12, 82, 58, leftChoice(), COLOR_TEXT);
    drawWrapped(display, 90, 82, 58, rightChoice(), COLOR_TEXT);

    display.setTextColor(COLOR_DIM);
    display.setCursor(12, kFooterY);
    display.print("B1 left  B2 right");
  }

  void renderOledOverlay(Adafruit_SSD1306 &display, const SensorSnapshot &sensors) override
  {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print("CHOICE LIFE");
    display.setCursor(0, 12);
    display.print("Age:");
    display.print(_age);
    display.setCursor(52, 12);
    display.print("$:");
    display.print(_money);
    display.setCursor(92, 12);
    display.print("H:");
    display.print(_honor);
    display.setCursor(0, 24);
    display.print("Node:");
    display.print(_nodeIndex);
    display.setCursor(0, 36);
    display.print("Wi:");
    display.print(wifiService.statusLabel());
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
    result.difficulty = 0;
    result.durationMs = static_cast<uint32_t>(_age) * 1000UL;
    result.completed = _completed;
    result.score = max<int>(0, _money * 20 + _honor * 18 + _age * 5);
    strncpy(result.detail, _endingCause, sizeof(result.detail) - 1);
    return result;
  }

private:
  void choose(uint32_t nowMs, bool left)
  {
    switch (_nodeIndex)
    {
    case 0:
      _gender = left ? 1 : 2;
      _age = 8;
      _nodeIndex = 1;
      break;
    case 1:
      _age += 8;
      if (left)
      {
        _honor += 3;
      }
      else
      {
        _honor += 1;
        _money += 2;
      }
      _nodeIndex = 2;
      break;
    case 2:
      _age += 6;
      if (left)
      {
        _money += 8;
      }
      else
      {
        _honor += 4;
      }
      _nodeIndex = 3;
      break;
    case 3:
      _age += 12;
      if (left)
      {
        _money += 20;
        _honor -= 1;
      }
      else
      {
        _money += 12;
        _honor += 3;
      }
      _nodeIndex = 4;
      break;
    case 4:
      _age += 4;
      if (left)
      {
        finishLife(nowMs, false, "Street crash");
        return;
      }
      _honor += 3;
      _nodeIndex = 5;
      break;
    case 5:
      _age += 18;
      if (left)
      {
        _money -= 8;
        _honor += 10;
        finishLife(nowMs, true, "Loved elder");
      }
      else
      {
        _money += 18;
        _honor -= 4;
        finishLife(nowMs, true, "Rich legacy");
      }
      return;
    default:
      finishLife(nowMs, true, "Closed book");
      return;
    }
    persist();
  }

  void finishLife(uint32_t nowMs, bool completed, const char *cause)
  {
    _finished = true;
    _completed = completed;
    _durationMs = nowMs - _startMs;
    strncpy(_endingCause, cause, sizeof(_endingCause) - 1);
    LifeHistoryData history;
    history.age = _age;
    history.money = _money;
    history.honor = _honor;
    strncpy(history.cause, cause, sizeof(history.cause) - 1);
    storageService.saveLifeHistory(history);
    storageService.clearLifeSave();
  }

  void persist()
  {
    LifeSaveData save;
    save.valid = 1;
    save.nodeIndex = _nodeIndex;
    save.age = _age;
    save.money = _money;
    save.honor = _honor;
    save.gender = _gender;
    storageService.saveLifeSave(save);
  }

  const char *currentBody() const
  {
    switch (_nodeIndex)
    {
    case 0:
      return "Start a life. Pick your first identity.";
    case 1:
      return "Childhood calls. Study hard or enjoy friends?";
    case 2:
      return "Teen years. Work early or join every club?";
    case 3:
      return "Adult path. Start business or take stable job?";
    case 4:
      return "Night after work. Race home or stay safe?";
    case 5:
      return "Old age. Give back or grow your empire?";
    default:
      return "The story is done.";
    }
  }

  const char *leftChoice() const
  {
    switch (_nodeIndex)
    {
    case 0:
      return "Boy";
    case 1:
      return "Study";
    case 2:
      return "Part-time";
    case 3:
      return "Business";
    case 4:
      return "Speed home";
    case 5:
      return "Donate";
    default:
      return "End";
    }
  }

  const char *rightChoice() const
  {
    switch (_nodeIndex)
    {
    case 0:
      return "Girl";
    case 1:
      return "Friends";
    case 2:
      return "Clubs";
    case 3:
      return "Job";
    case 4:
      return "Stay home";
    case 5:
      return "Expand";
    default:
      return "End";
    }
  }

  static void drawWrapped(Adafruit_ST7735 &display, int x, int y, int maxWidth, const char *text, uint16_t color)
  {
    display.setTextColor(color);
    display.setCursor(x, y);
    int lineX = x;
    char word[18] = {};
    uint8_t len = 0;
    for (const char *ptr = text;; ++ptr)
    {
      const char ch = *ptr;
      if (ch != ' ' && ch != '\0')
      {
        if (len < (sizeof(word) - 1))
        {
          word[len++] = ch;
        }
      }
      if (ch == ' ' || ch == '\0')
      {
        word[len] = '\0';
        if (len > 0)
        {
          const int wordWidth = len * 6;
          if ((lineX + wordWidth) > (x + maxWidth))
          {
            y += 8;
            lineX = x;
            display.setCursor(x, y);
          }
          display.print(word);
          display.print(" ");
          lineX += wordWidth + 6;
        }
        len = 0;
      }
      if (ch == '\0')
      {
        break;
      }
    }
  }

  bool _finished = false;
  bool _completed = false;
  uint8_t _nodeIndex = 0;
  uint8_t _age = 0;
  int16_t _money = 0;
  int16_t _honor = 0;
  uint8_t _gender = 0;
  uint32_t _startMs = 0;
  uint32_t _durationMs = 0;
  char _endingCause[24] = {};
};

class ConnectFourGame : public IGame
{
public:
  GameId id() const override
  {
    return GameId::ChoiceOfLife;
  }

  void enter(uint8_t difficulty, uint32_t nowMs) override
  {
    _difficulty = 0;
    _finished = false;
    _completed = false;
    _winner = 0;
    _cursorCol = 3;
    _currentPiece = 1;
    _startMs = nowMs;
    _durationMs = 0;
    memset(_board, 0, sizeof(_board));
  }

  void update(uint32_t nowMs, const InputSnapshot &input, const SensorSnapshot &) override
  {
    if (_finished)
    {
      return;
    }

    if (input.btn1DownEdge)
    {
      _cursorCol = (_cursorCol + 1) % kCols;
    }
    if (input.btn2DownEdge)
    {
      if (dropPiece(_cursorCol, _currentPiece))
      {
        if (hasConnectFour(_currentPiece))
        {
          finish(nowMs, true, _currentPiece);
          return;
        }
        if (boardFull())
        {
          finish(nowMs, false, 0);
          return;
        }
        _currentPiece = (_currentPiece == 1) ? 2 : 1;
      }
    }
  }

  void renderTft(Adafruit_ST7735 &display) override
  {
    display.fillScreen(color565(12, 20, 52));
    display.fillRect(0, 0, hw::tftWidth, 14, COLOR_PANEL);
    display.setTextColor(COLOR_TEXT);
    display.setCursor(4, 3);
    display.print("Connect Four");
    display.setCursor(116, 3);
    display.print("2P");

    const int boardX = 17;
    const int boardY = 26;
    const int cellW = 18;
    const int cellH = 15;

    const int arrowX = boardX + _cursorCol * cellW + cellW / 2;
    const uint16_t turnColor = (_currentPiece == 1) ? COLOR_DANGER : COLOR_WARN;
    display.fillTriangle(arrowX, 18, arrowX - 4, 24, arrowX + 4, 24, turnColor);
    display.setTextColor(turnColor);
    display.setCursor(8, 16);
    display.print((_currentPiece == 1) ? "Red turn" : "Yellow turn");

    display.fillRoundRect(boardX, boardY, cellW * kCols, cellH * kRows, 5, color565(34, 72, 186));
    for (uint8_t row = 0; row < kRows; ++row)
    {
      for (uint8_t col = 0; col < kCols; ++col)
      {
        const int cx = boardX + col * cellW + cellW / 2;
        const int cy = boardY + row * cellH + cellH / 2;
        display.fillCircle(cx, cy, 5, color565(10, 22, 68));
        if (_board[row][col] == 1)
        {
          display.fillCircle(cx, cy, 4, COLOR_DANGER);
        }
        else if (_board[row][col] == 2)
        {
          display.fillCircle(cx, cy, 4, COLOR_WARN);
        }
      }
    }

    display.setTextColor(COLOR_TEXT);
    display.setCursor(8, 112);
    display.print("Red");
    display.fillCircle(34, 116, 3, COLOR_DANGER);
    display.setCursor(58, 112);
    display.print("Yel");
    display.fillCircle(76, 116, 3, COLOR_WARN);
    display.setCursor(92, 112);
    display.print((_currentPiece == 1) ? "R" : "Y");

    display.setTextColor(COLOR_DIM);
    display.setCursor(12, kFooterY);
    display.print("B1 move  B2 drop");
  }

  void renderOledOverlay(Adafruit_SSD1306 &display, const SensorSnapshot &) override
  {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print("CONNECT FOUR");
    display.setCursor(0, 12);
    display.print("Turn:");
    display.print((_currentPiece == 1) ? "Red" : "Yellow");
    display.setCursor(68, 12);
    display.print("Mode:");
    display.print("2P");
    display.setCursor(0, 24);
    display.print("Col:");
    display.print(_cursorCol + 1);
    display.setCursor(68, 24);
    display.print("Fill:");
    display.print(filledCells());
    display.setCursor(0, 40);
    display.print("Wi:");
    display.print(wifiService.statusLabel());
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
    result.score = (_winner != 0 ? 900 : 360);
    snprintf(result.detail, sizeof(result.detail), _winner == 1 ? "Red 4 in row" : _winner == 2 ? "Yellow 4 in row"
                                                                                                 : "Board full");
    return result;
  }

private:
  static constexpr uint8_t kCols = 7;
  static constexpr uint8_t kRows = 6;

  bool dropPiece(uint8_t col, uint8_t piece)
  {
    if (col >= kCols)
    {
      return false;
    }
    for (int row = kRows - 1; row >= 0; --row)
    {
      if (_board[row][col] == 0)
      {
        _board[row][col] = piece;
        return true;
      }
    }
    return false;
  }

  int nextOpenRow(uint8_t col) const
  {
    for (int row = kRows - 1; row >= 0; --row)
    {
      if (_board[row][col] == 0)
      {
        return row;
      }
    }
    return -1;
  }

  bool boardFull() const
  {
    for (uint8_t col = 0; col < kCols; ++col)
    {
      if (_board[0][col] == 0)
      {
        return false;
      }
    }
    return true;
  }

  uint8_t filledCells() const
  {
    uint8_t count = 0;
    for (uint8_t row = 0; row < kRows; ++row)
    {
      for (uint8_t col = 0; col < kCols; ++col)
      {
        if (_board[row][col] != 0)
        {
          ++count;
        }
      }
    }
    return count;
  }

  bool hasConnectFour(uint8_t piece) const
  {
    for (uint8_t row = 0; row < kRows; ++row)
    {
      for (uint8_t col = 0; col < kCols; ++col)
      {
        if (_board[row][col] != piece)
        {
          continue;
        }
        if (checkDir(row, col, 0, 1, piece) || checkDir(row, col, 1, 0, piece) ||
            checkDir(row, col, 1, 1, piece) || checkDir(row, col, 1, -1, piece))
        {
          return true;
        }
      }
    }
    return false;
  }

  bool checkDir(int row, int col, int dr, int dc, uint8_t piece) const
  {
    for (uint8_t step = 1; step < 4; ++step)
    {
      const int nr = row + dr * step;
      const int nc = col + dc * step;
      if (nr < 0 || nr >= kRows || nc < 0 || nc >= kCols || _board[nr][nc] != piece)
      {
        return false;
      }
    }
    return true;
  }

  void finish(uint32_t nowMs, bool completed, uint8_t winner)
  {
    _finished = true;
    _completed = completed;
    _winner = winner;
    _durationMs = nowMs - _startMs;
  }

  uint8_t _board[kRows][kCols] = {};
  bool _finished = false;
  bool _completed = false;
  uint8_t _difficulty = 0;
  uint8_t _winner = 0;
  uint8_t _cursorCol = 3;
  uint8_t _currentPiece = 1;
  uint32_t _startMs = 0;
  uint32_t _durationMs = 0;
};

InputService inputService;
StorageService storageService;
SensorService sensorService;
WifiService wifiService;
TriviaNewsService triviaNewsService;
AiLearningService aiLearningService;
DuckImageService duckImageService;
RgbService rgbService;
BreakoutGame breakoutGame;
PongGame pongGame;
FlappyBirdGame flappyBirdGame;
StickmanCombatGame stickmanCombatGame;
PoleClimbGame poleClimbGame;
BalloonBattleGame balloonBattleGame;
DuckHuntGame duckHuntGame;
TriviaQuizGame triviaQuizGame;
RacingGame racingGame;
QuickDrawGame quickDrawGame;
AngryBirdsGame angryBirdsGame;
ChoiceOfLifeGame choiceOfLifeGame;
ConnectFourGame connectFourGame;
GomokuGame gomokuGame;

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
uint8_t gomokuLearnPage = 0;

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
void handleGomokuLearnMenu(const InputSnapshot &input, uint32_t nowMs);
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
void renderGomokuLearnMenu();
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

constexpr uint8_t visibleGameCount()
{
  return sizeof(kVisibleGameIds) / sizeof(kVisibleGameIds[0]);
}

GameId visibleGameAt(uint8_t index)
{
  return kVisibleGameIds[index % visibleGameCount()];
}

const GameDescriptor &visibleGameDescriptor(uint8_t index)
{
  return descriptorFor(visibleGameAt(index));
}

const char *gomokuModeFocus(uint8_t mode)
{
  switch (mode)
  {
  case 0:
    return "Human duel";
  case 1:
    return "Block four";
  case 2:
    return "Read forks";
  case 3:
  default:
    return "Best move";
  }
}

uint8_t gomokuModeLevel(uint8_t mode)
{
  switch (mode)
  {
  case 0:
    return 0;
  case 1:
    return 36;
  case 2:
    return 74;
  case 3:
  default:
    return 82;
  }
}

uint8_t gomokuLearningLevel(const GomokuLearningData &data, uint8_t mode)
{
  uint16_t score = 0;
  switch (mode)
  {
  case 0:
    score = 0;
    break;
  case 1:
    score = data.defenseSkill;
    break;
  case 2:
    score = static_cast<uint16_t>((data.defenseSkill + data.readingSkill * 2) / 3);
    break;
  case 3:
  default:
    score = static_cast<uint16_t>((data.bestMoveSkill * 2 + data.readingSkill) / 3);
    break;
  }
  return clampValue<uint8_t>(static_cast<uint8_t>(min<uint16_t>(99, score / 4)), 0, 99);
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
  case GameId::ShieldSword:
    return &stickmanCombatGame;
  case GameId::BalloonBattle:
    return &balloonBattleGame;
  case GameId::GridBattle:
    return &triviaQuizGame;
  case GameId::PoleClimb:
    return &poleClimbGame;
  case GameId::Racing:
    return &racingGame;
  case GameId::DuckHunt:
    return &duckHuntGame;
  case GameId::QuickDraw:
    return &quickDrawGame;
  case GameId::AngryBirds:
    return &angryBirdsGame;
  case GameId::ChoiceOfLife:
    return &connectFourGame;
  case GameId::Gomoku:
    return &gomokuGame;
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
  if (activeGameResult.gameId == GameId::Gomoku)
  {
    storageService.recordGomokuSession(activeGameResult.difficulty, activeGameResult.completed);
    if (activeGameResult.difficulty == 3)
    {
      aiLearningService.recordSelfPlay(activeGameResult.completed);
      storageService.saveAiLearningStatus(aiLearningService.snapshot());
      aiLearningService.notePersisted(millis());
    }
  }
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
    mainMenuIndex = (mainMenuIndex + 1) % (sizeof(kMainMenuItems) / sizeof(kMainMenuItems[0]));
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
      changeState(AppState::GomokuLearn, millis());
      break;
    case 3:
      changeState(AppState::Leaderboard, millis());
      break;
    case 4:
      changeState(AppState::SelfTest, millis());
      selfTestInitialized = false;
      selfTestPassLatched = false;
      break;
    }
  }
}

void handleSensorMonitor(const InputSnapshot &input)
{
  if (input.btn2DownEdge)
  {
    char message[28];
    snprintf(message, sizeof(message), "Net %s", wifiService.statusLabel());
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
  const GameDescriptor &current = visibleGameDescriptor(gamesMenuIndex);
  const uint8_t currentGameIndex = static_cast<uint8_t>(current.id);

  if (gamesDifficultyMode)
  {
    if (input.btn1DownEdge)
    {
      selectedLaunchDifficulty[currentGameIndex] = (selectedLaunchDifficulty[currentGameIndex] + 1) % current.difficultyCount;
    }
    if (input.btn1Long)
    {
      gamesDifficultyMode = false;
    }
    if (input.btn2DownEdge)
    {
      gamesDifficultyMode = false;
      startGame(current.id, selectedLaunchDifficulty[currentGameIndex], nowMs);
    }
    return;
  }

  if (input.btn1DownEdge)
  {
    gamesMenuIndex = (gamesMenuIndex + 1) % visibleGameCount();
  }
  if (input.btn1Long)
  {
    changeState(AppState::MainMenu, millis());
  }
  if (input.btn2DownEdge)
  {
    const GameDescriptor &selected = visibleGameDescriptor(gamesMenuIndex);
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

void handleGomokuLearnMenu(const InputSnapshot &input, uint32_t nowMs)
{
  if (input.btn1DownEdge)
  {
    gomokuLearnPage = (gomokuLearnPage + 1) % 3;
  }
  if (input.btn1Long)
  {
    changeState(AppState::MainMenu, millis());
  }
  if (input.btn2DownEdge)
  {
    startGame(GameId::Gomoku, 3, nowMs);
  }
}

void handleLeaderboard(const InputSnapshot &input)
{
  const GameDescriptor &selected = visibleGameDescriptor(leaderboardIndex);
  const uint8_t selectedGameIndex = static_cast<uint8_t>(selected.id);
  if (input.btn1DownEdge)
  {
    leaderboardIndex = (leaderboardIndex + 1) % visibleGameCount();
  }
  if (input.btn1Long)
  {
    changeState(AppState::MainMenu, millis());
  }
  if (input.btn2DownEdge && selected.difficultyCount > 1)
  {
    selectedLeaderboardDifficulty[selectedGameIndex] = (selectedLeaderboardDifficulty[selectedGameIndex] + 1) % selected.difficultyCount;
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
  oled.print("WiFi:");
  oled.print(wifiService.statusLabel());
  oled.setCursor(0, 42);
  if (wifiService.connected())
  {
    oled.print("RSSI:");
    oled.print(wifiService.rssi());
  }
  else
  {
    oled.print("IP:");
    oled.print(wifiService.ipString());
  }
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
  display.print("WiFi:");
  display.print(wifiService.statusLabel());
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
  const uint8_t menuCount = sizeof(kMainMenuItems) / sizeof(kMainMenuItems[0]);
  for (uint8_t i = 0; i < menuCount; ++i)
  {
    const int y = 22 + i * 18;
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

void renderGomokuLearnMenu()
{
  const GomokuLearningData &learning = storageService.gomokuLearning();
  const uint8_t ability = aiLearningService.estimatedAbility(learning);
  char lineA[20] = {};
  char lineB[20] = {};
  char lineC[20] = {};

  tft.fillScreen(COLOR_BG);
  drawCenteredText(tft, 10, "AI LEARN", COLOR_TEXT, 1);

  tft.fillRoundRect(8, 22, 144, 18, 4, COLOR_PANEL);
  tft.setTextColor(COLOR_TEXT);
  tft.setCursor(12, 28);
  tft.print(aiLearningService.abilityTier(ability));
  tft.print(" ");
  tft.print(ability);
  tft.print("%");
  tft.setCursor(82, 28);
  tft.print(aiLearningService.networkLabel());

  tft.fillRoundRect(8, 46, 144, 58, 4, COLOR_PANEL);
  tft.setCursor(14, 52);
  if (gomokuLearnPage == 0)
  {
    tft.print("Parallel:");
    tft.print(aiLearningService.parallelJobs());
    tft.setCursor(14, 64);
    tft.print("Selfplay:");
    tft.print(aiLearningService.selfPlayWins());
    tft.print("/");
    tft.print(aiLearningService.selfPlayMatches());
    tft.setCursor(14, 76);
    tft.print("Saved: FLASH");
    tft.setCursor(14, 88);
    tft.print("Rules:");
    tft.print(aiLearningService.ruleSyncLabel());
  }
  else if (gomokuLearnPage == 1)
  {
    aiLearningService.taskLine(0, lineA, sizeof(lineA), lineB, sizeof(lineB));
    tft.print(lineA);
    tft.setCursor(78, 52);
    tft.print(lineB);
    tft.setCursor(14, 64);
    aiLearningService.taskLine(1, lineA, sizeof(lineA), lineB, sizeof(lineB));
    tft.print(lineA);
    tft.setCursor(78, 64);
    tft.print(lineB);
    tft.setCursor(14, 76);
    aiLearningService.taskLine(2, lineA, sizeof(lineA), lineB, sizeof(lineB));
    tft.print(lineA);
    tft.setCursor(78, 76);
    tft.print(lineB);
    tft.setCursor(14, 88);
    tft.print("Tactic lineage live");
  }
  else
  {
    aiLearningService.digestLine(0, lineA, sizeof(lineA), learning);
    aiLearningService.digestLine(1, lineB, sizeof(lineB), learning);
    aiLearningService.digestLine(2, lineC, sizeof(lineC), learning);
    tft.print(lineA);
    tft.setCursor(14, 64);
    tft.print(lineB);
    tft.setCursor(14, 76);
    tft.print(lineC);
    tft.setCursor(14, 88);
    tft.print("B2 selfplay lab");
  }

  tft.setTextColor(COLOR_DIM);
  tft.setCursor(12, 108);
  tft.print("B1 page  B2 self");
  tft.setCursor(20, kFooterY);
  tft.print("Hold B1 back");

  if (oledReady)
  {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 0);
    oled.print("TACTIC CORE");
    oled.setCursor(0, 12);
    oled.print("Net:");
    oled.print(aiLearningService.networkLabel());
    oled.setCursor(68, 12);
    oled.print("Jobs:");
    oled.print(aiLearningService.parallelJobs());
    oled.setCursor(0, 24);
    oled.print("Skill:");
    oled.print(aiLearningService.abilityTier(ability));
    oled.setCursor(68, 24);
    oled.print("%:");
    oled.print(ability);
    oled.setCursor(0, 40);
    aiLearningService.taskLine(gomokuLearnPage, lineA, sizeof(lineA), lineB, sizeof(lineB));
    oled.print(lineA);
    oled.setCursor(68, 40);
    oled.print(lineB);
    oled.setCursor(0, 56);
    oled.print("Save:");
    oled.print("OK ");
    oled.print(aiLearningService.ruleSyncLabel());
    oled.display();
  }
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
  tft.print("WiFi: ");
  tft.print(wifiService.statusLabel());
  tft.setCursor(16, 104);
  tft.print("Feed: ");
  tft.print(triviaNewsService.statusLabel());

  tft.setTextColor(COLOR_DIM);
  tft.setCursor(14, kFooterY);
  tft.print("H1 back  B2 net");

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
  const char *tempStatus = sensors.dhtOk ? rangeStatusLabel(sensors.temperatureC, 15.0f, 35.0f) : "FAIL";
  const char *humidityStatus = sensors.dhtOk ? rangeStatusLabel(sensors.humidityPct, 30.0f, 85.0f) : "FAIL";
  const uint16_t tempColor = sensors.dhtOk ? rangeStatusColor(sensors.temperatureC, 15.0f, 35.0f) : COLOR_DANGER;
  const uint16_t humidityColor = sensors.dhtOk ? rangeStatusColor(sensors.humidityPct, 30.0f, 85.0f) : COLOR_DANGER;

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
  tft.print("BTN:");
  tft.setTextColor(selfTestReport.buttonsOk ? COLOR_GOOD : COLOR_WARN);
  tft.print(selfTestReport.buttonsOk ? "OK" : "WAIT");

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
  tft.print("WiFi:");
  tft.setTextColor(wifiService.connected() ? COLOR_GOOD : COLOR_WARN);
  tft.print(wifiService.statusLabel());
  tft.setCursor(82, 74);
  tft.setTextColor(COLOR_TEXT);
  tft.print("Feed:");
  tft.setTextColor(COLOR_WARN);
  tft.print(triviaNewsService.statusLabel());

  tft.setCursor(8, 90);
  tft.setTextColor(COLOR_TEXT);
  tft.print("B1 GPIO");
  tft.print(Hardware.btn1Pin);
  tft.print(":");
  tft.setTextColor(selfTestBtn1Seen ? COLOR_GOOD : COLOR_WARN);
  tft.print(selfTestBtn1Seen ? "OK" : "..");

  tft.setCursor(8, 102);
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
    oled.print("WiFi:");
    oled.print(wifiService.statusLabel());
    oled.setCursor(64, 28);
    oled.print("Feed:");
    oled.print(triviaNewsService.statusLabel());
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
  const GameDescriptor &selected = visibleGameDescriptor(gamesMenuIndex);
  const uint8_t selectedGameIndex = static_cast<uint8_t>(selected.id);
  tft.fillScreen(COLOR_BG);
  drawCenteredText(tft, 10, "GAMES", COLOR_TEXT, 1);

  const int start = max(0, static_cast<int>(gamesMenuIndex) - 2);
  const int end = min(static_cast<int>(visibleGameCount()), start + 4);
  int row = 0;
  for (int i = start; i < end; ++i)
  {
    const int y = 22 + row * 18;
    const bool active = i == gamesMenuIndex;
    const GameDescriptor &rowDescriptor = visibleGameDescriptor(i);
    tft.fillRoundRect(8, y, hw::tftWidth - 16, 14, 4, active ? COLOR_PANEL : COLOR_BG);
    tft.drawRoundRect(8, y, hw::tftWidth - 16, 14, 4, active ? COLOR_ACCENT : COLOR_DIM);
    tft.setCursor(14, y + 3);
    tft.setTextColor(active ? COLOR_TEXT : COLOR_DIM);
    tft.print(rowDescriptor.title);
    if (active && rowDescriptor.id == GameId::Gomoku)
    {
      static const char *const kGomokuModeChip[] = {"2P", "AI", "HARD", "LRN"};
      tft.setCursor(110, y + 3);
      tft.print(kGomokuModeChip[selectedLaunchDifficulty[static_cast<uint8_t>(rowDescriptor.id)] % 4]);
    }
    if (!rowDescriptor.implemented)
    {
      tft.setCursor(112, y + 3);
      tft.print("Soon");
    }
    ++row;
  }

  tft.fillRect(8, 102, hw::tftWidth - 16, 24, COLOR_BG);
  tft.setTextColor(COLOR_TEXT);
  tft.setCursor(12, 106);
  if (selected.id == GameId::Gomoku)
  {
    const uint8_t mode = selectedLaunchDifficulty[selectedGameIndex] % selected.difficultyCount;
    const GomokuLearningData &learning = storageService.gomokuLearning();
    tft.print("Mode:");
    tft.print(selected.difficultyNames[mode]);
    tft.setCursor(98, 106);
    tft.print("Lv~");
    tft.print(gomokuLearningLevel(learning, mode));
    tft.setTextColor(gamesDifficultyMode ? COLOR_WARN : COLOR_DIM);
    tft.setCursor(12, kFooterY);
    tft.print("G:");
    tft.print(learning.totalGames);
    tft.print(" ");
    tft.print(gomokuModeFocus(mode));
    if (gamesDifficultyMode)
    {
      tft.setCursor(120, kFooterY);
      tft.print("OK");
    }
    else
    {
      tft.setCursor(118, kFooterY);
      tft.print("go");
    }
  }
  else if (gamesDifficultyMode)
  {
    tft.setTextColor(COLOR_WARN);
    tft.setCursor(12, kFooterY);
    tft.print(selected.id == GameId::Gomoku ? "Mode:" : "Diff:");
    tft.print(selected.difficultyNames[selectedLaunchDifficulty[selectedGameIndex]]);
  }
  else if (selected.difficultyCount > 1)
  {
    tft.setTextColor(COLOR_DIM);
    tft.setCursor(12, 106);
    tft.print(selected.id == GameId::Gomoku ? "Mode:" : "Diff:");
    tft.print(selected.difficultyNames[selectedLaunchDifficulty[selectedGameIndex]]);
    tft.setCursor(12, kFooterY);
    tft.print(selected.id == GameId::Gomoku ? "B1 mode  B2 OK" : "B1 diff  B2 OK");
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

  if (oledReady && selected.id == GameId::Gomoku)
  {
    const uint8_t mode = selectedLaunchDifficulty[selectedGameIndex] % selected.difficultyCount;
    const GomokuLearningData &learning = storageService.gomokuLearning();
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 0);
    oled.print("GOMOKU MENU");
    oled.setCursor(0, 12);
    oled.print("Mode:");
    oled.print(selected.difficultyNames[mode]);
    oled.setCursor(0, 24);
    oled.print("Focus:");
    oled.print(gomokuModeFocus(mode));
    oled.setCursor(0, 36);
    oled.print("Games:");
    oled.print(learning.totalGames);
    oled.setCursor(72, 36);
    oled.print("Lv~");
    oled.print(gomokuLearningLevel(learning, mode));
    oled.setCursor(0, 52);
    oled.print("Study:");
    oled.print(learning.studyGames);
    oled.display();
  }
  else
  {
    drawSensorSummaryOled(sensorService.snapshot(), "GAMES");
  }
}

void renderLeaderboards()
{
  const GameDescriptor &selected = visibleGameDescriptor(leaderboardIndex);
  const uint8_t difficulty = selectedLeaderboardDifficulty[static_cast<uint8_t>(selected.id)];

  tft.fillScreen(COLOR_BG);
  drawCenteredText(tft, 10, "SCORES", COLOR_TEXT, 1);
  tft.setCursor(8, 24);
  tft.setTextColor(COLOR_TEXT);
  tft.print(selected.title);
  tft.setCursor(8, 36);
  tft.setTextColor(COLOR_DIM);
  tft.print(selected.id == GameId::Gomoku ? "Mode: " : "Diff: ");
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
  if (selected.id == GameId::Gomoku)
  {
    const GomokuLearningData &learning = storageService.gomokuLearning();
    tft.setCursor(10, 106);
    tft.print("Learn Lv~");
    tft.print(gomokuLearningLevel(learning, difficulty));
    tft.setCursor(10, kFooterY);
    tft.print("G:");
    tft.print(learning.totalGames);
    tft.print(" S:");
    tft.print(learning.studyGames);
    tft.setCursor(108, kFooterY);
    tft.print("B2");
  }
  else
  {
    tft.setCursor(14, kFooterY);
    tft.print("B1 next  B2 diff");
  }

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
    oled.print(selected.id == GameId::Gomoku ? "Mode:" : "Diff:");
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
  if (activeGameResult.detail[0] != '\0')
  {
    tft.print(activeGameResult.detail);
  }
  else if (activeGameResult.winner != 0)
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
  case AppState::GomokuLearn:
    renderGomokuLearnMenu();
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
  wifiService.begin(kWifiSsid, kWifiPassword, kWifiBackupSsid, kWifiBackupPassword);

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
  aiLearningService.begin(bootStartedMs, storageService.aiLearningStatus());
  duckImageService.begin();
  selectedLaunchDifficulty[static_cast<uint8_t>(GameId::Gomoku)] = 3;
  selectedLeaderboardDifficulty[static_cast<uint8_t>(GameId::Gomoku)] = 3;
}

void loop()
{
  const uint32_t nowMs = millis();
  const InputSnapshot input = inputService.update(nowMs);
  latestInputSnapshot = input;
  sensorService.update(nowMs);
  wifiService.setDesired(true, nowMs);
  wifiService.update(nowMs);
  aiLearningService.update(nowMs, wifiService.connected(), storageService.gomokuLearning());
  const bool gomokuActive = (appState == AppState::GameRunning && activeGame == &gomokuGame);
  if (aiLearningService.backgroundSelfPlayDue(nowMs, gomokuActive))
  {
    aiLearningService.recordBackgroundSelfPlay(nowMs, wifiService.connected());
    storageService.recordBackgroundGomokuStudy(wifiService.connected());
  }
  if (aiLearningService.shouldPersist(nowMs))
  {
    storageService.saveAiLearningStatus(aiLearningService.snapshot());
    aiLearningService.notePersisted(nowMs);
  }
  duckImageService.update(nowMs, wifiService.connected());

  if (wifiService.consumeConnectionEvent())
  {
    showToast("WiFi connected", COLOR_GOOD, 1400);
  }

  if (appState != AppState::Boot && appState != AppState::LockScreen && appState != AppState::MainMenu && input.panicCombo)
  {
    exitToMainMenu();
    showToast("Returned to menu", COLOR_WARN);
  }

  if (appState == AppState::GameRunning && activeGame == &gomokuGame && input.btn1Long)
  {
    exitToMainMenu();
    showToast("Back to games", COLOR_WARN);
    return;
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
  case AppState::GomokuLearn:
    handleGomokuLearnMenu(input, nowMs);
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
