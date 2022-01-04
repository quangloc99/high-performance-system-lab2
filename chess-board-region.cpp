#include "chess-board-region.h"

ChessBoardRegion::ChessBoardRegion(size_t height_, size_t width_)
    : width(width_)
    , height(height_)
    , cell_state(height + 1, std::vector<char>(width + 1))
{}
