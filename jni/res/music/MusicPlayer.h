#pragma once
#include <vector>
#include <string>
#include <atomic>

struct Song {
    std::string name, id, picurl, singers, jumpurl;
};

enum class PlayState { Idle, Loading, Playing, Paused };

struct Playlist {
    std::vector<Song> songs;
    int currentIndex = -1;
    PlayState state = PlayState::Idle;
    float progress = 0.0f, duration = 0.0f;
    bool shuffle = false, repeat = false;
};

extern Playlist g_Playlist;
extern std::vector<Song> g_SongList;
extern std::atomic<float> g_BeatIntensity;

void 音乐窗口();
void CleanupMusicPlayer();
