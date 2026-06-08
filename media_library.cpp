#include "media_library.h"

#include <SD.h>
#include <stdlib.h>
#include <string.h>

char* playlist[MAX_TRACKS];
int trackCount = 0;

char albums[MAX_ALBUMS][MAX_ALBUM_NAME_LEN];
int albumCount = 0;
int albumScroll = 0;
int browseTrackScroll = 0;

BrowseLevel browseLevel = BROWSE_ALBUMS;
int browseAlbumIdx = -1;
int browseTrackIndices[MAX_TRACKS];
int browseTrackCount = 0;

char currentAlbumFolder[MAX_ALBUM_NAME_LEN];

static MediaPumpFn sPumpFn = nullptr;

static void mediaPump(int maxLoops) {
  if (sPumpFn) sPumpFn(maxLoops);
}

void mediaSetPumpCallback(MediaPumpFn fn) {
  sPumpFn = fn;
}

bool isMP3(const char* fn) {
  int l = strlen(fn);
  return l >= 4 && strcasecmp(fn + l - 4, ".mp3") == 0;
}

bool isWAV(const char* fn) {
  int l = strlen(fn);
  return l >= 4 && strcasecmp(fn + l - 4, ".wav") == 0;
}

void getDisplayName(const char* path, char* out, int maxLen) {
  const char* name = strrchr(path, '/');
  if (!name) name = path;
  else name++;
  strncpy(out, name, maxLen - 1);
  out[maxLen - 1] = '\0';
  char* dot = strrchr(out, '.');
  if (dot) *dot = '\0';
  char* paren = strrchr(out, '(');
  if (paren && paren > out) {
    char* trim = paren - 1;
    while (trim > out && *trim == ' ') trim--;
    *(trim + 1) = '\0';
  }
}

void freePlaylist() {
  for (int i = 0; i < trackCount; i++) {
    free(playlist[i]);
    playlist[i] = nullptr;
  }
  trackCount = 0;
}

static void addFile(const char* path) {
  if (trackCount >= MAX_TRACKS) return;
  char* c = strdup(path);
  if (!c) return;
  playlist[trackCount++] = c;
}

static void scanDir(File dir, int depth) {
  if (depth > 2) return;
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;
    if (entry.isDirectory()) {
      scanDir(entry, depth + 1);
    } else {
      const char* name = entry.name();
      if (isMP3(name) || isWAV(name)) {
        const char* p = entry.path();
        if (p) addFile(p);
      }
    }
    entry.close();
    mediaPump(16);
  }
}

void scanSD() {
  File root = SD.open("/");
  if (!root || !root.isDirectory()) return;
  scanDir(root, 0);
  root.close();
}

void scanAlbums() {
  albumCount = 0;
  for (int i = 0; i < trackCount && albumCount < MAX_ALBUMS; i++) {
    const char* path = playlist[i];
    const char* lastSlash = strrchr(path, '/');
    if (!lastSlash || lastSlash == path) continue;

    int folderLen = (int)(lastSlash - path);
    const char* folderStart = path;
    for (int j = folderLen - 1; j >= 0; j--) {
      if (path[j] == '/') {
        folderStart = path + j + 1;
        break;
      }
    }
    int nameLen = (int)(lastSlash - folderStart);
    if (nameLen <= 0 || nameLen >= MAX_ALBUM_NAME_LEN) continue;

    char candidate[MAX_ALBUM_NAME_LEN];
    strncpy(candidate, folderStart, nameLen);
    candidate[nameLen] = '\0';
    if (strcmp(candidate, "System Volume Information") == 0) continue;

    bool exists = false;
    for (int a = 0; a < albumCount; a++) {
      if (strcmp(albums[a], candidate) == 0) {
        exists = true;
        break;
      }
    }
    if (!exists) {
      strncpy(albums[albumCount], candidate, MAX_ALBUM_NAME_LEN - 1);
      albums[albumCount][MAX_ALBUM_NAME_LEN - 1] = '\0';
      albumCount++;
    }
  }

  if (albumCount < MAX_ALBUMS - 1) {
    for (int i = albumCount; i > 0; i--) {
      strncpy(albums[i], albums[i - 1], MAX_ALBUM_NAME_LEN - 1);
      albums[i][MAX_ALBUM_NAME_LEN - 1] = '\0';
    }
    strncpy(albums[0], "[ All Tracks ]", MAX_ALBUM_NAME_LEN - 1);
    albums[0][MAX_ALBUM_NAME_LEN - 1] = '\0';
    albumCount++;
  }
}

static void sortBrowseTrackIndices() {
  if (browseTrackCount < 2) return;
  for (int i = 0; i < browseTrackCount - 1; i++) {
    for (int j = 0; j < browseTrackCount - 1 - i; j++) {
      int a = browseTrackIndices[j];
      int b = browseTrackIndices[j + 1];
      if (strcmp(playlist[a], playlist[b]) > 0) {
        int t = browseTrackIndices[j];
        browseTrackIndices[j] = browseTrackIndices[j + 1];
        browseTrackIndices[j + 1] = t;
      }
      if ((j & 3) == 0) mediaPump(24);
    }
  }
}

void loadBrowseAlbumTracks(const char* albumName) {
  browseTrackCount = 0;
  if (strcmp(albumName, "[ All Tracks ]") == 0) {
    for (int i = 0; i < trackCount && browseTrackCount < MAX_TRACKS; i++)
      browseTrackIndices[browseTrackCount++] = i;
    sortBrowseTrackIndices();
    return;
  }
  for (int i = 0; i < trackCount && browseTrackCount < MAX_TRACKS; i++) {
    const char* path = playlist[i];
    const char* lastSlash = strrchr(path, '/');
    if (!lastSlash) continue;
    const char* folderStart = path;
    int folderLen = (int)(lastSlash - path);
    for (int j = folderLen - 1; j >= 0; j--) {
      if (path[j] == '/') {
        folderStart = path + j + 1;
        break;
      }
    }
    int nameLen = (int)(lastSlash - folderStart);
    if (nameLen == (int)strlen(albumName) && strncmp(folderStart, albumName, nameLen) == 0)
      browseTrackIndices[browseTrackCount++] = i;
  }
  sortBrowseTrackIndices();
}

void setCurrentAlbumFromPath(const char* path) {
  const char* lastSlash = strrchr(path, '/');
  if (!lastSlash || lastSlash == path) {
    currentAlbumFolder[0] = '\0';
    return;
  }
  const char* folderStart = path;
  int folderLen = (int)(lastSlash - path);
  for (int j = folderLen - 1; j >= 0; j--) {
    if (path[j] == '/') {
      folderStart = path + j + 1;
      break;
    }
  }
  int nameLen = (int)(lastSlash - folderStart);
  if (nameLen <= 0 || nameLen >= MAX_ALBUM_NAME_LEN) {
    currentAlbumFolder[0] = '\0';
    return;
  }
  memcpy(currentAlbumFolder, folderStart, nameLen);
  currentAlbumFolder[nameLen] = '\0';
}
