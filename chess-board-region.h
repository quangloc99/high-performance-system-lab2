#pragma once
#include <cstddef>
#include <vector>
#include <cstdlib>
#include <iostream>
#include "utils.h"
#include "state.h"

class ChessBoardRegion {
    size_t width;
    size_t height; 
    std::vector<std::vector<char>> cell_state;
public:
    ChessBoardRegion(size_t height_, size_t row_);
    
    inline int get_width() const { return width; }
    inline int get_height() const { return height; }
    
    inline char operator()(int row, int col) const {
        return cell_state[row + 1][col + 1];
    }
    inline char & operator()(int row, int col) {
        return cell_state[row + 1][col + 1];
    }
    
    inline int area() const {
        return width * height;
    }
    
    inline char get_state(int row, int col) {
        char res = operator()(row, col);
        if (GETBIT(operator()(row, col - 1), 0)) {
            res |= 1 << 3;
        }
        if (GETBIT(operator()(row - 1, col - 1), 1)) {
            res |= 1 << 4;
        }
        if (GETBIT(operator()(row - 1, col), 2)) {
            res |= 1 << 5;
        }
        
        return res;
    }
    
    inline void upd_state(int row, int col, int dir) {
        if (std::getenv("DEBUG")) {
            std::cout << "updating " << row << ' ' << col << " += " << dir << std::endl;
        }
        if (dir >= 3) {
            row += dr[dir];
            col += dc[dir];
            upd_state(row, col, opposite_dir[dir]);
            return ;
        }
        char& cur = operator()(row, col);
        cur |= 1 << dir;
    }
};
