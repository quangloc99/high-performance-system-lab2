#pragma once
#include <cstddef>
#include <vector>
extern int world_size;
extern int world_rank;

struct Worm {
    int row, col;
    int dir;
};
struct GameInfo {
    size_t width;
    size_t height;
    unsigned long iteration_count;
    Worm worm;
};


// these state are only used in the master thread/process
extern size_t total_visited_state;
extern int visited_state[1 << 5];
extern std::vector<int> rule;
static const int dr[] = {0, 1, 1, 0, -1, -1};
static const int dc[] = {1, 1, 0, -1, -1, 0};
static const int opposite_dir[] = {3, 4, 5, 0, 1, 2};

