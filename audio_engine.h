#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H

#include <Arduino.h>
#include "BluetoothA2DPSource.h"

#define RING_SIZE 4096
#define VIS_BUF_LEN 512
#define VIS_N 256
#define NUM_VIS_BARS 16

enum AudioType { AUDIO_NONE, AUDIO_MP3, AUDIO_WAV };
enum PlayerState { STATE_STOPPED, STATE_PLAYING, STATE_PAUSED };

typedef void (*AudioSinkVolumeFn)(int sinkVolume127);

extern AudioType currentType;
extern PlayerState playerState;
extern int currentTrack;
extern uint32_t cachedDurationSec;
extern uint32_t cachedWavRateHz;
extern int volumePercent;

extern float visBandVal[NUM_VIS_BARS];
extern float visBandEnv[NUM_VIS_BARS];

void audioInit();
void audioSetSinkVolumeCallback(AudioSinkVolumeFn fn);

int32_t audioBtCallback(Frame* frame, int32_t count);
void audioPumpPlayingMax(int maxLoops);

void applyVolumePercent();
uint32_t elapsedPlaybackSec();
void formatTimeHMS(char* buf, size_t n, uint32_t sec);

void stopTrack(bool flushRing = true);
void startTrack(int orderIdx, bool gapless = false);
void nextTrack(bool gapless = false);
void prevTrack();
void startPlayingFromPlaylistIndex(int pi);
void togglePause();

void computeVisBands();
bool audioServicePlaybackLoop();

#endif
