#include <algorithm>
#include <utility>
#include <iostream>
#include <fstream>
#include <mpi.h>
#include <iomanip>
#include "state.h"
#include "chess-board-region.h"
#include "utils.h"


char processor_name[MPI_MAX_PROCESSOR_NAME];
int name_len;
int world_size, world_rank;
GameInfo game;

#define log std::cout << processor_name << ":" << world_rank << "; "

size_t total_visited_state;
int visited_state[1 << 5];
std::vector<int> rule;

std::vector<std::string> board_ascii;
size_t total_area = 0;
std::vector<ChessBoardRegion> regions;
std::vector<size_t> row_size, col_size;
std::vector<size_t> row_pos, col_pos;

void finalize_then_exit(int exit_code) {
    MPI_Finalize();
    exit(exit_code);
}

#define safe_assert(cond, msg) do { if (!(cond)) { \
        std::cerr << "Assertion error: " << #cond << ". " << msg << std::endl; \
        finalize_then_exit(1); \
    } } while (0)

void print_usage(int argc, char** argv) {
    using std::cout;
    using std::endl;
    cout << "Usage:" << endl;
    cout << "\t" << argv[0] << " <initial-state-file> <number-of-iteration>" << endl;
    cout << endl;
    cout << "The result will be written to stdout, so it can be redirected to file" << endl;
    cout << endl;
    cout << "The file format of the state is as follows:" << endl;
    cout << "\t<row-count> <column-count>" << endl;
    cout << "\t<worms-row-position> <worms-column-position> <worm-direction>" << endl;
    cout << "\t<number-of-rule>" << endl;
    cout << "\t<rule-0> <rule-1> <rule-2> ... <rule-n>" << endl;
    cout << "\t<number-of-visited-state>" << endl;
    cout << "\t<visited-state-0> <visited-state-1> ... <visited-state-n>" << endl;
    cout << "\t<board-description>" << endl;
    cout << endl;
    cout << "The visited state must be a decimal number, whose binary representation " << endl;
    cout << "represents the state. The bit length must be 5 (because the directoin 3 is skipped)." << endl;
    cout << endl;
    cout << "The board must be described as a board of (2 * row-count)x(2 * column-colun)" << endl;
    cout << "ASCII board. Every odd row is shifted to the left so it is seem like a rectangle board." << endl;
    cout << "Every cell's position must be represented by a *, and must be at even position (both row and column are even)" << endl;
    cout << "The other position described the connection between cells. The connection can be:" << endl;
    cout << "\t`-`, when connecting 2 cells in the same row, and can be used only in the even row." << endl;
    cout << "\t`|`, when connecting 2 cells in consecutive rows, and can be used only in the even column." << endl;
    cout << "\t`\\`, when connecting 2 cells in consecutive rows, and can be used only in the odd row and odd column." << endl;
}

void parse_state(int argc, char** argv) {
    std::string filename(argv[1]);
    game.iteration_count = std::stoul(argv[2]);
    std::ifstream inp(filename);
    if (!inp) {
        std::cerr << "Can not open file " << std::quoted(filename) << std::endl;
        finalize_then_exit(1);
    }
#define read(elm, label) do { if (!(inp >> elm)) { \
    std::cerr << "Error while parsing state file: Cannot read " << label << std::endl; \
    finalize_then_exit(1); \
} } while (0) 

    read(game.height, "<row-count>");
    read(game.width, "<column-count>");
    read(game.worm.row, "<worm-row-position>");
    read(game.worm.col, "<worm-col-position>");
    read(game.worm.dir, "<worm-direction>");
    safe_assert(0 <= game.worm.row && game.worm.row < (int)game.height, "worm's row position is out of range.");
    safe_assert(0 <= game.worm.col && game.worm.col < (int)game.width, "worm's column position is out of range.");
    safe_assert(0 <= game.worm.dir && game.worm.dir < 6, "worm's direction must be an integer between 0 and 5.");
    
    size_t rule_count;
    read(rule_count, "<number-of-rule>");
    rule.resize(rule_count);
    for (int i = 0; i < (int)rule_count; ++i) {
        read(rule[i], "<rule-" << i << ">");
        safe_assert(0 <= rule[i] && rule[i] < 6, "Rule must be an integer between 0 and 5");
        safe_assert(rule[i] != 3, "Rule must not be 2 (cannot go back)");
    }
    
    for (int i = 0; i < (1 << 5); ++i) {
        visited_state[i] = -1;
    }
    total_visited_state = 0;
    read(total_visited_state, "<number-of-visited-state>");
    
    for (int i = 0; i < (int)total_visited_state; ++i) {
        int cur_state;
        read(cur_state, "<visited-state-" << i << ">");
        visited_state[cur_state] = i;
    }
    board_ascii.resize(2 * game.height);
    inp >> std::ws;
    for (size_t i = 0; i < 2 * game.height; ++i) {
        if (!std::getline(inp, board_ascii[i])) {
            std::cerr << "Error while parsing state file: Cannot read the row #" << i + 1 << " of the board description."<< std::endl;
            finalize_then_exit(1);
        }
        safe_assert(board_ascii[i].size() >= 2 * game.width, "The size of the row #" << i + 1 << " must be twice the board size, but found" << board_ascii[i].size());
    }
#undef read
    
    if (std::getenv("DEBUG")) {
        for (size_t i = 0; i < 2 * game.height; ++i) {
            std::cout << std::quoted(board_ascii[i]) << std::endl;
        }
    }
}


void divide_regions() {
    row_size.resize(world_size);
    col_size.resize(world_size);
    row_pos.resize(world_size);
    col_pos.resize(world_size);
    for (size_t i = 0; i < (size_t) world_size; ++i) {
        row_size[i] = game.height / world_size + (i < game.height % world_size);
        col_size[i] = game.width / world_size + (i < game.width % world_size);
        row_pos[i] = i ? row_pos[i - 1] + row_size[i - 1] : 0;
        col_pos[i] = i ? col_pos[i - 1] + col_size[i - 1] : 0;
    }
    total_area = 0;
    for (int i = 0; i < world_size; ++i) {
        regions.emplace_back(row_size[i], col_size[(i + world_rank) % world_size]);
        total_area += regions.back().area();
    }
    if (std::getenv("DEBUG")) {
        log << "dividing regions: ";
        std::cout << total_area << "; ";
        for (int i = 0; i < world_size; ++i) {
            std::cout << regions[i].get_height() << "x" << regions[i].get_width() << ' ';
        }
        std::cout << std::endl;
    }
}

void divide_states() {
    if (world_rank == 0) {
        for (int other = 0; other < world_size; ++other) {
            std::vector<char> data;
            for (int reg = 0; reg < world_size; ++reg) {
                int col_reg = (reg + other) % world_size;
                for (size_t r = 0; r < row_size[reg]; ++r)
                for (size_t c = 0; c < col_size[col_reg]; ++c) {
                    int ascii_r = (r + row_pos[reg]) * 2;
                    int ascii_c = (c + col_pos[col_reg]) * 2;
                    char cur = 0;
                    cur = cur << 1 | (board_ascii[ascii_r + 1][ascii_c] == '|');
                    cur = cur << 1 | (board_ascii[ascii_r + 1][ascii_c + 1] == '\\');
                    cur = cur << 1 | (board_ascii[ascii_r][ascii_c + 1] == '=');
                    data.push_back(cur);
                }
            }
            if (std::getenv("DEBUG")) {
                log << "sending to " << other << ": ";
                std::cout << data.size() << ": ";
                for (auto x: data) std::cout << (int)x << ", ";
                std::cout << std::endl;
            }
            MPI_Send(data.data(), data.size(), MPI_BYTE, other, 0, MPI_COMM_WORLD);
        }
    }
    
    // receive the data
    std::vector<char> board_data(total_area);
    MPI_Recv(board_data.data(), total_area, MPI_BYTE, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    {
        auto it = board_data.begin();
        for (auto& reg: regions) {
            for (int r = 0; r < reg.get_height(); ++r)
            for (int c = 0; c < reg.get_width(); ++c) {
                reg(r, c) = *it++;
            }
        }
    }
}

void extract_received_vertical(const std::vector<char>& data) {
    auto it = data.begin();
    for (auto& reg: regions) {
        for (int i = 0; i < reg.get_height(); ++i) {
            reg(i, -1) = *it++;
        }
    }
}
void extract_received_horizontal(const std::vector<char>& data) {
    auto it = data.begin();
    for (auto& reg: regions) {
        for (int i = 0; i < reg.get_width(); ++i) {
            reg(-1, i) = *it++;
        }
    }
}

void send_state_to_neighbor_vertical() {
    if (std::getenv("DEBUG")) {
        log << "sending vertical" << std::endl;
    }
    std::vector<char> data;
    if (world_rank != 0) {
        data.resize(game.height);
        MPI_Recv(data.data(), game.height, MPI_BYTE, world_rank - 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        extract_received_vertical(data);
        data.clear();
    }
    data.reserve(game.height);
    for (auto& reg: regions) {
        for (int i = 0; i < reg.get_height(); ++i) {
            data.push_back(reg(i, reg.get_width() - 1));
        }
    }
    
    MPI_Send(data.data(), game.height, MPI_BYTE, (world_rank + 1) % world_size, 0, MPI_COMM_WORLD);
    if (world_rank == 0) {
        MPI_Recv(data.data(), game.height, MPI_BYTE, (world_size - 1 + world_size) % world_size, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        extract_received_vertical(data);
    }
}

void send_state_to_neighbor_horizontal() {
    if (std::getenv("DEBUG")) {
        log << "sending horizontal" << std::endl;
    }
    std::vector<char> data;
    if (world_rank != 0) {
        data.resize(game.width);
        MPI_Recv(data.data(), game.width, MPI_BYTE, (world_rank + 1) % world_size, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        extract_received_horizontal(data);
        data.clear();
    }
    data.reserve(game.width);
    for (int prv = world_size - 1, id = 0; id < world_size; prv = id++) {
        auto& reg = regions[prv];
        for (int i = 0; i < reg.get_width(); ++i) {
            data.push_back(reg(reg.get_height() - 1, i));
        }
    }
    MPI_Send(data.data(), game.width, MPI_BYTE, (world_rank - 1 + world_size) % world_size, 0, MPI_COMM_WORLD);
    if (world_rank == 0) {
        MPI_Recv(data.data(), game.width, MPI_BYTE, 1 % world_size, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        extract_received_horizontal(data);
    }
}

void send_inner() {
    // we must do twice since there might be some regions with non-positive area.
    for (int time = 0; time < 2; ++time) {
        for (int prv = world_size - 1, cur = 0; cur < world_size; prv = cur++) {
            regions[cur](-1, -1) = regions[prv](regions[prv].get_height() - 1, regions[prv].get_width() - 1);
        }
    }
}

int query_state(int state) {
    int reduced_state = state;
    reduced_state &= (1 << 3) - 1;
    int head = state >> 4;
    reduced_state |= head << 3;
    if (visited_state[reduced_state] == -1) {
        visited_state[reduced_state] = total_visited_state++;
    }
    int rule_id = visited_state[reduced_state];
    if (rule_id >= (int)rule.size()) {
        return -1;
    }
    int choice = rule[rule_id];
    if (GETBIT(state, choice)) {
        return -1;
    }
    return choice;
}

bool game_step() {
    send_state_to_neighbor_vertical();
    send_state_to_neighbor_horizontal();
    send_inner();
    if (std::getenv("DEBUG")) {
        log << "updating worm position " << std::endl;
    }
    int state = -1;
    for (int i = 0; i < world_size; ++i) {
        int r = i;
        int c = (i + world_rank) % world_size;
        if ((int)row_pos[r] > game.worm.row) continue;
        if ((int)col_pos[c] > game.worm.col) continue;
        if ((int)row_pos[r] + (int)row_size[r] <= game.worm.row) continue;
        if ((int)col_pos[c] + (int)col_size[c] <= game.worm.col) continue;
        state = regions[i].get_state(game.worm.row - row_pos[r], game.worm.col - col_pos[c]);
        break;
    }
    
    GameInfo old_game = game;
    MPI_Send(&state, 1, MPI_INT, 0, 0, MPI_COMM_WORLD);
    if (world_rank == 0) {
        for (int other = 0; other < world_size; ++other) {
            MPI_Recv(&state, 1, MPI_INT, other, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            if (state == -1) continue;
            int rotated_state = rotate_right(state, 6, game.worm.dir);
            int new_dir = query_state(rotated_state);
            if (new_dir == -1) {
                game.worm.dir = -1;
            } else {
                game.worm.dir += new_dir;
                if (game.worm.dir >= 6) game.worm.dir -= 6;
                game.worm.row += dr[game.worm.dir];
                game.worm.col += dc[game.worm.dir];
                if (game.worm.row < 0) game.worm.row += game.height;
                if (game.worm.row >= (int)game.height) game.worm.row -= game.height;
                if (game.worm.col < 0) game.worm.col += game.width;
                if (game.worm.col >= (int)game.width) game.worm.col -= game.width;
            }
            if (std::getenv("DEBUG")) {
                log << "state " << state << "; dir = " << new_dir << std::endl;
                log << "worm pos: " << game.worm.row << ' ' << game.worm.col << std::endl;
            }
        }
    }
    
    MPI_Bcast(&game, sizeof(GameInfo), MPI_BYTE, 0, MPI_COMM_WORLD);
    if (game.worm.dir == -1) return false;
    
    for (int i = 0; i < world_size; ++i) {
        int r = i;
        int c = (i + world_rank) % world_size;
        if ((int)row_pos[r] > game.worm.row) continue;
        if ((int)col_pos[c] > game.worm.col) continue;
        if ((int)(row_pos[r] + row_size[r]) <= game.worm.row) continue;
        if ((int)(col_pos[c] + col_size[c]) <= game.worm.col) continue;
        regions[i].upd_state(game.worm.row - row_pos[r], game.worm.col - col_pos[c], opposite_dir[game.worm.dir]);
        break;
    }
    
    for (int i = 0; i < world_size; ++i) {
        int r = i;
        int c = (i + world_rank) % world_size;
        if ((int)row_pos[r] > old_game.worm.row) continue;
        if ((int)col_pos[c] > old_game.worm.col) continue;
        if ((int)(row_pos[r] + row_size[r]) <= old_game.worm.row) continue;
        if ((int)(col_pos[c] + col_size[c]) <= old_game.worm.col) continue;
        regions[i].upd_state(old_game.worm.row - row_pos[r], old_game.worm.col - col_pos[c], game.worm.dir);
        break;
    }
    
    return true;
}

void combine_states() {
    std::vector<char> board_data;
    board_data.reserve(total_area);
    {
        for (auto& reg: regions) {
            for (int r = 0; r < reg.get_height(); ++r)
            for (int c = 0; c < reg.get_width(); ++c) {
                board_data.push_back(reg(r, c));
            }
        }
    }
    if (std::getenv("DEBUG")) {
        log << "area = " << total_area << std::endl;
    }
    MPI_Send(board_data.data(), total_area, MPI_BYTE, 0, 0, MPI_COMM_WORLD);
    if (world_rank == 0) {
        for (int other = 0; other < world_size; ++other) {
            int cur_area = 0;
            for (int reg = 0; reg < world_size; ++reg) {
                int col_reg = (reg + other) % world_size;
                cur_area += row_size[reg] * col_size[col_reg];
            }
            if (std::getenv("DEBUG")) {
                log << "Try receive from " << other << "; area = " << cur_area << std::endl;
            }
            std::vector<char> data(cur_area);
            MPI_Recv(data.data(), cur_area, MPI_BYTE, other, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            
            auto it = data.begin();
            for (int reg = 0; reg < world_size; ++reg) {
                int col_reg = (reg + other) % world_size;
                for (size_t r = 0; r < row_size[reg]; ++r)
                for (size_t c = 0; c < col_size[col_reg]; ++c) {
                    int ascii_r = (r + row_pos[reg]) * 2;
                    int ascii_c = (c + col_pos[col_reg]) * 2;
                    int cur = *it++;
                    // std::cout << "cur = " << cur << ' ' << ascii_r << ' ' << ascii_c << ' ' << GETBIT(cur, 2) << std::endl;
                    board_ascii[ascii_r][ascii_c] = '*';
                    board_ascii[ascii_r + 1][ascii_c] = GETBIT(cur, 2) ? '|' : ' ';
                    board_ascii[ascii_r + 1][ascii_c + 1] = GETBIT(cur, 1) ? '\\' : ' ';
                    board_ascii[ascii_r][ascii_c + 1] = GETBIT(cur, 0) ? '=' : ' ';
                }
            }
            if (std::getenv("DEBUG")) {
                log << "received from " << other << ": ";
                std::cout << data.size() << ": ";
                for (auto x: data) std::cout << (int)x << ", ";
                std::cout << std::endl;
            }
        }
    }
}

void print_state() {
    using std::cout;
    using std::endl;
    cout << game.height << ' ' << game.width << endl;
    cout << game.worm.row << ' ' << game.worm.col << ' ' << game.worm.dir << endl;
    cout << rule.size() << endl;
    for (auto x: rule) cout << x << ' ';
    cout << endl;
    std::vector<int> state(total_visited_state);
    for (int i = 0; i < (1 << 5); ++i) {
        if (visited_state[i] == -1) continue;
        state[visited_state[i]] = i;
    }
    cout << total_visited_state << endl;
    for (auto x: state) cout << x << ' ';
    cout << endl;
    for (auto& line: board_ascii) {
        cout << line << endl;
    }
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

    // Get the name of the processor
    MPI_Get_processor_name(processor_name, &name_len);

    if (argc < 3) {
        if (world_rank == 0) {
            print_usage(argc, argv);
        }
        finalize_then_exit(0);
    }
    
    if (world_rank == 0) {
        parse_state(argc, argv);
    }
    // broadcasting some info
    MPI_Bcast(&game, sizeof(GameInfo), MPI_BYTE, 0, MPI_COMM_WORLD);
    if (std::getenv("DEBUG")) {
        log << "Recived size: " << game.width << ' ' << game.height << "; iter count: " << game.iteration_count << std::endl;
    }

    divide_regions();
    divide_states();
    int step_count = 0;
    while (game.iteration_count-- > 0) {
        if (!game_step()) {
            break;
        }
        ++step_count;
    }
    combine_states();
    if (world_rank == 0) {
        print_state();
        std::cout << "Stepped iterations: " << step_count << std::endl;
    }

    finalize_then_exit(0);
}
