#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include "base_station.h"
#include "node.h"
#include "shared_constants.h"

int main(int argc, char *argv[])
{
    int global_rank, worker_rank, provided, simulation_seconds;
    MPI_Comm worker_comm, cart_comm;
    int dims[CARTESIAN_DIMENSIONS], coord[CARTESIAN_DIMENSIONS], neighbours[MAX_NEIGHBOURS];

    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE)
    {
        printf("The threading support level is lesser than that demanded.\n");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    MPI_Comm_rank(MPI_COMM_WORLD, &global_rank);
    MPI_Comm_split(MPI_COMM_WORLD, global_rank == BASE_STATION_RANK, 0, &worker_comm);

    // base station set-up
    if (global_rank == BASE_STATION_RANK)
    {
        if (base_station_set_up(argc, argv, dims, &simulation_seconds) != 0)
        {
            printf("Error setting up base station.\n");
            return 1;
        };
    }
    // broadcast dims to workers
    MPI_Bcast(&dims, CARTESIAN_DIMENSIONS, MPI_INT, BASE_STATION_RANK, MPI_COMM_WORLD);
    // node set-up
    if (global_rank != BASE_STATION_RANK)
    {
        if (node_set_up(&worker_comm, &cart_comm, dims, coord, neighbours, &worker_rank) != 0)
        {
            printf("Error setting up node.\n");
            return 1;
        }
        // printf("Node global rank: %d\nNode grid rank: %d\nNode cartesian coordinates: %d, %d\nNeighbours: %d %d %d %d\n\n", global_rank, worker_rank, coord[0], coord[1], neighbours[0], neighbours[1], neighbours[2], neighbours[3]);
    }
    MPI_Barrier(MPI_COMM_WORLD);

    // lifecycle loops
    if (global_rank == BASE_STATION_RANK)
    {
        if (base_station_lifecycle(dims[0] * dims[1], simulation_seconds) != 0)
        {
            printf("Error in base station lifecycle.\n");
            return 1;
        }
    }
    else
    {
        if (node_lifecycle(neighbours, &cart_comm, worker_rank) != 0)
        {
            printf("Error in node lifecycle.\n");
            return 1;
        }
    }

    // clean up
    if (global_rank != BASE_STATION_RANK)
    {
        MPI_Comm_free(&cart_comm);
        MPI_Comm_free(&worker_comm);
    }
    MPI_Finalize();

    return 0;
}