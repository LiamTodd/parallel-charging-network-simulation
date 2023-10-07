all: simulate

CC = mpicc
NP := 13
ARG1 := 3
ARG2 := 4
ARG3 := 20
ARG4 := 2

simulate: main.c base_station.c base_station.h node.c node.h shared_constants.h shared_structs.h
	$(CC) -Wall main.c base_station.c node.c -fopenmp -o simulate

run:
	mpirun -oversubscribe -np $(NP) ./simulate $(ARG1) $(ARG2) $(ARG3) $(ARG4)