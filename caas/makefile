all: simulate

CC = mpicc
CFLAGS = -Wall -fopenmp
SRCS = main.c base_station.c node.c
OBJS = $(SRCS:.c=.o)
TARGET = simulate

NP := 13
ARG1 := 3
ARG2 := 4
ARG3 := 20
ARG4 := 2

main.c: base_station.h node.h shared_constants.h shared_structs.h
base_station.c: base_station.h shared_constants.h shared_structs.h
node.c: base_station.h node.h shared_constants.h shared_structs.h

%.o: %.CC
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(CFLAGS)

run:
	mpirun -oversubscribe -np $(NP) ./simulate $(ARG1) $(ARG2) $(ARG3) $(ARG4)

run-caas-1:
	srun -n 5 ./simulate 2 2 $(ARG3) $(ARG4)
run-caas-2:
	srun -n 13 ./simulate 3 4 $(ARG3) $(ARG4)
run-caas-3:
	srun -n 26 ./simulate 5 5 $(ARG3) $(ARG4)
run-caas-4:
	srun -n 101 ./simulate 10 10 $(ARG3) $(ARG4)

clean:
	rm -f $(OBJ) simulate main.o node.o base_station.o