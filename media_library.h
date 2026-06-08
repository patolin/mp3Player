#ifndef MEDIA_LIBRARY_H
#define MEDIA_LIBRARY_H

#include <Arduino.h>

#define MAX_TRACKS 300
#define MAX_ALBUMS 32
#define MAX_ALBUM_NAME_LEN 48

typedef void (*MediaPumpFn)(int maxLoops);

extern char* playlist[MAX_TRACKS];
extern int trackCount;

extern char albums[MAX_ALBUMS][MAX_ALBUM_NAME_LEN];
extern int albumCount;
extern int albumScroll;
extern int browseTrackScroll;

enum BrowseLevel { BROWSE_ALBUMS, BROWSE_TRACKS };
extern BrowseLevel browseLevel;
extern int browseAlbumIdx;
extern int browseTrackIndices[MAX_TRACKS];
extern int browseTrackCount;

extern char currentAlbumFolder[MAX_ALBUM_NAME_LEN];

void mediaSetPumpCallback(MediaPumpFn fn);

bool isMP3(const char* fn);
bool isWAV(const char* fn);
void getDisplayName(const char* path, char* out, int maxLen);

void freePlaylist();
void scanSD();
void scanAlbums();
void loadBrowseAlbumTracks(const char* albumName);
void setCurrentAlbumFromPath(const char* path);

#endif
