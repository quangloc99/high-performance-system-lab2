CPP=mpic++
FLAGS=-std=c++17 -Wall -g

build:
	mkdir build
	
build/chess-board-region.o: build chess-board-region.h chess-board-region.cpp
	$(CPP) $(FLAGS) chess-board-region.cpp -c -o build/chess-board-region.o

main: build main.cpp build/chess-board-region.o
	$(CPP) $(FLAGS) main.cpp build/chess-board-region.o -o build/main
