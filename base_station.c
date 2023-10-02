#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include "shared_constants.h"

int base_station_set_up(int argc, char *argv[], int *dims)
{
    int m, n, processors;
    // base station: read in command-line args
    if (argc != 3)
    {
        printf("Error: Invalid number of arguments.\n");
        return 1;
    }
    m = atoi(argv[1]);
    n = atoi(argv[2]);
    if (m == 0 || n == 0)
    {
        printf("Error: Invalid values of n and m. n and m must be positive integers.\n");
        return 1;
    }
    // ensure that processors can accommodate dimensions
    MPI_Comm_size(MPI_COMM_WORLD, &processors);
    if (processors != m * n + 1)
    {
        printf("Error: Dimensions cannot be accommodated by the number of processors. Number of processors must equal n * m + 1.\n");
        return 1;
    }
    dims[0] = m;
    dims[1] = n;
    return 0;
}

int base_station_lifecycle(int num_nodes)
{
    for (int i = 0; i < 10; i++)
    {
        sleep(1);
    }
    int termination_signal = TERMINATION_SIGNAL;
    for (int i = 1; i < num_nodes + 1; i++)
    {
        MPI_Send(&termination_signal, 1, MPI_INT, i, TERMINATION_TAG, MPI_COMM_WORLD);
    }
    return 0;
}