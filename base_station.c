#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "shared_constants.h"
#include "shared_structs.h"

int base_station_set_up(int argc, char *argv[], int *dims, int *simulation_seconds)
{
    int m, n, processors;
    // base station: read in command-line args
    if (argc != 4)
    {
        printf("Error: Invalid number of arguments.\n");
        return 1;
    }
    m = atoi(argv[1]);
    n = atoi(argv[2]);
    *simulation_seconds = atoi(argv[3]);
    if (m == 0 || n == 0)
    {
        printf("Error: Invalid values of n and m. n and m must be positive integers.\n");
        return 1;
    }
    if (*simulation_seconds < 1)
    {
        printf("Error: Invalid value of simulation seconds. Must be a positive integer.\n");
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

int base_station_lifecycle(int num_nodes, int simulation_seconds, MPI_Datatype neighbour_available_report_type)
{
    for (int i = 0; i < simulation_seconds; i++)
    {
        sleep(1);
        for (int i = 1; i < num_nodes + 1; i++)
        {
            int flag = 0;
            MPI_Status probe_status;
            MPI_Status recv_status;
            MPI_Iprobe(i, INDICATE_NEIGHBOUR_AVAILABLE_TAG, MPI_COMM_WORLD, &flag, &probe_status);
            if (flag)
            {
                printf("Base station received indication from %d\n", i-1);
                struct NeighbourAvailableReport available_report;
                MPI_Recv(&available_report, 1, neighbour_available_report_type, i, INDICATE_NEIGHBOUR_AVAILABLE_TAG, MPI_COMM_WORLD, &recv_status);
            }
        }
    }
    int termination_signal = TERMINATION_SIGNAL;
    for (int i = 1; i < num_nodes + 1; i++)
    {
        MPI_Send(&termination_signal, 1, MPI_INT, i, TERMINATION_TAG, MPI_COMM_WORLD);
    }
    return 0;
}