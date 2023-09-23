all: simulate

CC = mpicc
NP := 13
ARG1 := 3
ARG2 := 4

simulate: main.c base_station.c base_station.h node.c node.h
	$(CC) -Wall main.c base_station.c node.c -fopenmp -o simulate

run:
	mpirun -oversubscribe -np $(NP) ./simulate $(ARG1) $(ARG2)