all: simulate

CC = mpicc
CFLAGS = -Wall -fopenmp
SRCS = main.c base_station.c node.c helpers.c
OBJS = $(SRCS:.c=.o)
TARGET = simulate

STIME := 20
THRESH := 2

main.c: base_station.h node.h shared_constants.h shared_structs.h helpers.h
base_station.c: base_station.h shared_constants.h shared_structs.h
node.c: base_station.h node.h shared_constants.h shared_structs.h
helpers.c: shared_constants.h

%.o: %.CC
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(CFLAGS)

run-local-1:
	mpirun -oversubscribe -np 5 ./simulate 2 2 $(STIME) $(THRESH) local_2_2.txt
run-local-2:
	mpirun -oversubscribe -np 10 ./simulate 3 3 $(STIME) $(THRESH) local_3_3.txt
run-local-3:
	mpirun -oversubscribe -np 17 ./simulate 4 4 $(STIME) $(THRESH) local_4_4.txt
run-local-4:
	mpirun -oversubscribe -np 26 ./simulate 5 5 $(STIME) $(THRESH) local_5_5.txt
run-local-5:
	mpirun -oversubscribe -np 13 ./simulate 2 6 $(STIME) $(THRESH) local_2_6.txt
run-local-6:
	mpirun -oversubscribe -np 13 ./simulate 1 12 $(STIME) $(THRESH) local_1_12.txt
run-local-7:
	mpirun -oversubscribe -np 13 ./simulate 3 4 $(STIME) $(THRESH) local_3_4.txt

run-caas-1:
	srun -n 5 ./simulate 2 2 $(STIME) $(THRESH) caas_2_2.txt
run-caas-2:
	srun -n 10 ./simulate 3 3 $(STIME) $(THRESH) caas_3_3.txt
run-caas-3:
	srun -n 17 ./simulate 4 4 $(STIME) $(THRESH) caas_4_4.txt
run-caas-4:
	srun -n 26 ./simulate 5 5 $(STIME) $(THRESH) caas_5_5.txt
run-caas-5:
	srun -n 13 ./simulate 2 6 $(STIME) $(THRESH) caas_2_6.txt
run-caas-6:
	srun -n 13 ./simulate 1 12 $(STIME) $(THRESH) caas_1_12.txt
run-caas-7:
	srun -n 13 ./simulate 3 4 $(STIME) $(THRESH) caas_3_4.txt

clean:
	rm -f $(OBJ) simulate main.o node.o base_station.o helpers.o