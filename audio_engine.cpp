#include "audio_engine.h"

#include <AudioFileSourceSD.h>
#include <AudioGeneratorMP3.h>
#include <AudioGeneratorWAV.h>
#include <AudioOutput.h>
#include <SD.h>
#include <math.h>

#include "media_library.h"

static int16_t ring[RING_SIZE * 2];
static volatile size_t rbHead = 0;
static volatile size_t rbTail = 0;

static int16_t visRing[VIS_BUF_LEN];
static volatile uint32_t visWritePos = 0;
float visBandVal[NUM_VIS_BARS];
float visBandEnv[NUM_VIS_BARS];

static const float VIS_FREQ_HZ[NUM_VIS_BARS] = {
  80.f, 125.f, 200.f, 320.f, 500.f, 800.f, 1250.f, 2000.f,
  3000.f, 4500.f, 6000.f, 8000.f, 10000.f, 12000.f, 15000.f, 18000.f
};

static inline size_t rbAvail() {
  size_t h = rbHead, t = rbTail;
  return (h >= t) ? (h - t) : (RING_SIZE - t + h);
}

class RingBufOutput : public AudioOutput {
public:
  float gain = 0.7f;

  bool begin() override { return true; }
  bool stop() override { return true; }

  bool ConsumeSample(int16_t sample[2]) override {
    size_t next = (rbHead + 1) % RING_SIZE;
    if (next == rbTail) return false;
    int32_t l = (int32_t)(sample[0] * gain);
    int32_t r = (int32_t)(sample[1] * gain);
    ring[rbHead * 2] = constrain(l, -32768, 32767);
    ring[rbHead * 2 + 1] = constrain(r, -32768, 32767);
    rbHead = next;
    visRing[visWritePos & (VIS_BUF_LEN - 1)] = (int16_t)((l + r) >> 1);
    visWritePos++;
    return true;
  }
};

static RingBufOutput* audioOut = nullptr;
static AudioFileSourceSD* audioFile = nullptr;
static AudioGeneratorMP3* mp3 = nullptr;
static AudioGeneratorWAV* wav = nullptr;

AudioType currentType = AUDIO_NONE;
PlayerState playerState = STATE_STOPPED;
int currentTrack = 0;

static unsigned long trackWallStartMs = 0;
static unsigned long accumulatedPauseMs = 0;
static unsigned long pauseBeganMs = 0;
uint32_t cachedDurationSec = 0;
uint32_t cachedWavRateHz = 0;
int volumePercent = 30;

static AudioSinkVolumeFn sSinkVolumeFn = nullptr;

void audioSetSinkVolumeCallback(AudioSinkVolumeFn fn) {
  sSinkVolumeFn = fn;
}

void audioInit() {
  if (!audioOut) {
    audioOut = new RingBufOutput();
    audioOut->gain = 0.7f;
  }
  applyVolumePercent();
}

int32_t audioBtCallback(Frame* frame, int32_t count) {
  int32_t avail = (int32_t)rbAvail();
  int32_t toSend = min(count, avail);
  for (int32_t i = 0; i < toSend; i++) {
    frame[i].channel1 = ring[rbTail * 2];
    frame[i].channel2 = ring[rbTail * 2 + 1];
    rbTail = (rbTail + 1) % RING_SIZE;
  }
  for (int32_t i = toSend; i < count; i++) {
    frame[i].channel1 = 0;
    frame[i].channel2 = 0;
  }
  return count;
}

void audioPumpPlayingMax(int maxLoops) {
  if (playerState != STATE_PLAYING) return;
  const int target = (RING_SIZE * 3) / 4;
  int loops = 0;
  while ((int)rbAvail() < target && loops < maxLoops) {
    bool ok = false;
    if (currentType == AUDIO_MP3 && mp3 && mp3->isRunning()) ok = mp3->loop();
    else if (currentType == AUDIO_WAV && wav && wav->isRunning()) ok = wav->loop();
    if (!ok) break;
    loops++;
  }
}

static uint32_t wavParseDurationAndRate(const char* path, uint32_t* outRate) {
  if (outRate) *outRate = 0;
  File f = SD.open(path, FILE_READ);
  if (!f) return 0;
  uint8_t riff[12];
  if (f.read(riff, 12) < 12 || memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) {
    f.close();
    return 0;
  }
  uint16_t numCh = 0, bitsPerSample = 0;
  uint32_t sampleRate = 0, dataSize = 0;
  bool haveData = false;

  int wavPump = 0;
  while (f.available()) {
    if ((++wavPump & 3) == 0) audioPumpPlayingMax(32);
    uint8_t cid[4], szb[4];
    if (f.read(cid, 4) < 4 || f.read(szb, 4) < 4) break;
    uint32_t chunkSize = (uint32_t)szb[0] | ((uint32_t)szb[1] << 8) | ((uint32_t)szb[2] << 16) | ((uint32_t)szb[3] << 24);

    if (memcmp(cid, "fmt ", 4) == 0) {
      if (chunkSize < 16) {
        f.seek(chunkSize + (chunkSize & 1u), SeekCur);
        continue;
      }
      uint8_t fmt[16];
      if (f.read(fmt, 16) < 16) break;
      numCh = (uint16_t)(fmt[2] | (fmt[3] << 8));
      sampleRate = (uint32_t)fmt[4] | ((uint32_t)fmt[5] << 8) | ((uint32_t)fmt[6] << 16) | ((uint32_t)fmt[7] << 24);
      bitsPerSample = (uint16_t)(fmt[14] | (fmt[15] << 8));
      uint32_t skip = chunkSize - 16;
      if (skip > 0) f.seek(skip, SeekCur);
      if (chunkSize & 1u) f.seek(1, SeekCur);
    } else if (memcmp(cid, "data", 4) == 0) {
      dataSize = chunkSize;
      haveData = true;
      break;
    } else {
      f.seek(chunkSize + (chunkSize & 1u), SeekCur);
    }
  }
  f.close();
  if (outRate) *outRate = sampleRate;
  uint32_t bps = sampleRate * (uint32_t)numCh * ((uint32_t)bitsPerSample / 8u);
  if (bps == 0 || !haveData) return 0;
  return dataSize / bps;
}

void formatTimeHMS(char* buf, size_t n, uint32_t sec) {
  uint32_t h = sec / 3600u;
  uint32_t m = (sec % 3600u) / 60u;
  uint32_t s = sec % 60u;
  snprintf(buf, n, "%02lu:%02lu:%02lu", (unsigned long)h, (unsigned long)m, (unsigned long)s);
}

void applyVolumePercent() {
  volumePercent = constrain(volumePercent, 0, 100);
  if (audioOut) audioOut->gain = (float)volumePercent / 100.0f;
  if (sSinkVolumeFn) sSinkVolumeFn((int)((volumePercent * 127) / 100));
}

static uint32_t clampElapsedSec(uint32_t el) {
  if (cachedDurationSec > 0 && el > cachedDurationSec) return cachedDurationSec;
  return el;
}

uint32_t elapsedPlaybackSec() {
  if (playerState == STATE_STOPPED || trackWallStartMs == 0) return 0;
  if (playerState == STATE_PAUSED && pauseBeganMs >= trackWallStartMs) {
    return clampElapsedSec((uint32_t)((pauseBeganMs - trackWallStartMs - accumulatedPauseMs) / 1000ul));
  }
  if (playerState == STATE_PLAYING) {
    return clampElapsedSec((uint32_t)((millis() - trackWallStartMs - accumulatedPauseMs) / 1000ul));
  }
  return 0;
}

void stopTrack(bool flushRing) {
  if (mp3 && mp3->isRunning()) mp3->stop();
  if (wav && wav->isRunning()) wav->stop();
  if (mp3) {
    delete mp3;
    mp3 = nullptr;
  }
  if (wav) {
    delete wav;
    wav = nullptr;
  }
  if (audioFile) {
    delete audioFile;
    audioFile = nullptr;
  }
  currentType = AUDIO_NONE;
  playerState = STATE_STOPPED;
  if (flushRing) {
    rbHead = 0;
    rbTail = 0;
  }
  trackWallStartMs = 0;
  accumulatedPauseMs = 0;
  pauseBeganMs = 0;
  cachedDurationSec = 0;
  cachedWavRateHz = 0;
}

void startTrack(int orderIdx, bool gapless) {
  if (trackCount <= 0) return;
  int idx = orderIdx;
  if (idx < 0) idx = trackCount - 1;
  if (idx >= trackCount) idx = 0;
  currentTrack = idx;

  stopTrack(!gapless);

  const char* path = playlist[currentTrack];
  setCurrentAlbumFromPath(path);

  audioFile = new AudioFileSourceSD(path);
  if (!audioFile) return;

  if (isWAV(path)) {
    wav = new AudioGeneratorWAV();
    wav->begin(audioFile, audioOut);
    currentType = AUDIO_WAV;
  } else {
    mp3 = new AudioGeneratorMP3();
    mp3->begin(audioFile, audioOut);
    currentType = AUDIO_MP3;
  }

  playerState = STATE_PLAYING;
  trackWallStartMs = millis();
  accumulatedPauseMs = 0;
  pauseBeganMs = 0;
  cachedWavRateHz = 0;

  if (currentType == AUDIO_WAV) {
    cachedDurationSec = wavParseDurationAndRate(path, &cachedWavRateHz);
  } else if (audioFile && currentType == AUDIO_MP3) {
    uint32_t sz = audioFile->getSize();
    cachedDurationSec = (sz > 0) ? (sz / 16000u) : 0;
    if (cachedDurationSec == 0 && sz > 8000u) cachedDurationSec = 1;
  } else {
    cachedDurationSec = 0;
  }

  int loops = 0;
  int target = gapless ? (RING_SIZE / 4) : ((RING_SIZE * 3) / 4);
  while ((int)rbAvail() < target && loops < 1024) {
    bool ok = false;
    if (currentType == AUDIO_MP3 && mp3 && mp3->isRunning()) ok = mp3->loop();
    else if (currentType == AUDIO_WAV && wav && wav->isRunning()) ok = wav->loop();
    if (!ok) break;
    loops++;
  }
}

void nextTrack(bool gapless) {
  startTrack(currentTrack + 1, gapless);
}

void prevTrack() {
  startTrack(currentTrack > 0 ? currentTrack - 1 : trackCount - 1, false);
}

void startPlayingFromPlaylistIndex(int pi) {
  if (pi < 0 || pi >= trackCount) return;
  startTrack(pi, false);
}

void togglePause() {
  if (playerState == STATE_PLAYING) {
    playerState = STATE_PAUSED;
    pauseBeganMs = millis();
  } else if (playerState == STATE_PAUSED) {
    if (pauseBeganMs) {
      accumulatedPauseMs += (millis() - pauseBeganMs);
      pauseBeganMs = 0;
    }
    playerState = STATE_PLAYING;
  }
}

static float visGoertzelMag(const float* x, int N, float freqHz, float sampleRateHz) {
  const float PI_F = 3.14159265f;
  if (freqHz <= 0 || freqHz >= sampleRateHz * 0.48f) return 0;
  float k = (0.5f + ((float)N * freqHz) / sampleRateHz);
  int ki = (int)k;
  if (ki < 1) ki = 1;
  if (ki >= N) ki = N - 1;
  float omega = (2.0f * PI_F * (float)ki) / (float)N;
  float sine = sinf(omega);
  float cosine = cosf(omega);
  float coeff = 2.0f * cosine;
  float q0 = 0, q1 = 0, q2 = 0;
  for (int i = 0; i < N; i++) {
    q0 = coeff * q1 - q2 + x[i];
    q2 = q1;
    q1 = q0;
  }
  float real = q1 - q2 * cosine;
  float imag = q2 * sine;
  return sqrtf(real * real + imag * imag) / (float)N;
}

static float visSampleRateHz() {
  if (currentType == AUDIO_WAV && cachedWavRateHz > 0) return (float)cachedWavRateHz;
  return 44100.0f;
}

void computeVisBands() {
  if (visWritePos < (uint32_t)VIS_N) return;
  float wf[VIS_N];
  uint32_t end = visWritePos;
  for (int i = 0; i < VIS_N; i++) {
    uint32_t idx = (end - VIS_N + i) & (VIS_BUF_LEN - 1);
    float s = (float)visRing[idx];
    float w = 0.54f - 0.46f * cosf(2.0f * 3.14159265f * i / (float)(VIS_N - 1));
    wf[i] = s * w;
  }
  float sr = visSampleRateHz();
  float raw[NUM_VIS_BARS];
  for (int b = 0; b < NUM_VIS_BARS; b++) {
    float fh = VIS_FREQ_HZ[b];
    if (fh >= sr * 0.45f) fh = sr * 0.45f;
    raw[b] = visGoertzelMag(wf, VIS_N, fh, sr);
  }

  for (int b = 0; b < NUM_VIS_BARS; b++) {
    float treble = 0.55f + (float)b * 0.32f;
    float scaled = raw[b] * treble;
    visBandEnv[b] = visBandEnv[b] * 0.90f + scaled * 0.10f;
    if (visBandEnv[b] < 1e-7f) visBandEnv[b] = 1e-7f;
    float t = scaled / (visBandEnv[b] * 1.35f + 1e-6f);
    t = powf(t, 0.55f);
    if (t > 1.0f) t = 1.0f;
    if (t > visBandVal[b]) visBandVal[b] = visBandVal[b] * 0.28f + t * 0.72f;
    else visBandVal[b] = visBandVal[b] * 0.72f + t * 0.28f;
  }
}

bool audioServicePlaybackLoop() {
  if (playerState != STATE_PLAYING) return false;

  bool decoderAlive = false;
  if (currentType == AUDIO_MP3 && mp3) decoderAlive = mp3->isRunning();
  else if (currentType == AUDIO_WAV && wav) decoderAlive = wav->isRunning();

  if (decoderAlive) {
    int loops = 0;
    int target = (RING_SIZE * 3) / 4;
    bool loopFailed = false;
    while ((int)rbAvail() < target && loops < 1024) {
      bool ok = false;
      if (currentType == AUDIO_MP3 && mp3 && mp3->isRunning()) ok = mp3->loop();
      else if (currentType == AUDIO_WAV && wav && wav->isRunning()) ok = wav->loop();
      if (!ok) {
        loopFailed = true;
        break;
      }
      loops++;
    }

    if (loopFailed) {
      if (mp3 && mp3->isRunning()) mp3->stop();
      if (wav && wav->isRunning()) wav->stop();
    }
    return false;
  }

  if (rbAvail() < 200) {
    nextTrack(true);
    return true;
  }
  return false;
}
