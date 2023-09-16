#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>

#define BASE_STATION_RANK 0
#define CARTESIAN_DIMENSIONS 2

int main(int argc, char *argv[])
{
    int global_rank, worker_rank, grid_size, reorder = 1, n, m, processors;
    MPI_Comm worker_comm, cart_comm;
    int wrap_around[CARTESIAN_DIMENSIONS] = {0, 0}, dims[CARTESIAN_DIMENSIONS], coord[CARTESIAN_DIMENSIONS];

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &global_rank);
    MPI_Comm_split(MPI_COMM_WORLD, global_rank == BASE_STATION_RANK, 0, &worker_comm);

    // set-up
    if (global_rank == BASE_STATION_RANK)
    {
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
    }
    // broadcast dims to workers
    MPI_Bcast(&dims, CARTESIAN_DIMENSIONS, MPI_INT, BASE_STATION_RANK, MPI_COMM_WORLD);
    if (global_rank != BASE_STATION_RANK)
    {
        // nodes: set up cartesian grid
        grid_size = dims[0] * dims[1];
        // create cartesian grid of worker nodes
        MPI_Dims_create(grid_size, CARTESIAN_DIMENSIONS, dims);
        if (MPI_Cart_create(worker_comm, CARTESIAN_DIMENSIONS, dims, wrap_around, reorder, &cart_comm) != 0)
        {
            printf("Error: unable to create cartesian grid.");
            return 1;
        }
        // get node's coordinates
        MPI_Comm_rank(worker_comm, &worker_rank);
        MPI_Cart_coords(cart_comm, worker_rank, CARTESIAN_DIMENSIONS, coord);
        printf("Node global rank: %d\nNode grid rank: %d\nNode cartesian coordinates: %d, %d\n\n", global_rank, worker_rank, coord[0], coord[1]);
    }

    // lifecycle loops
    if (global_rank == BASE_STATION_RANK)
    {
        printf("Base station doing its thang\n");
    }
    else
    {
        printf("Node doing its thang\n");
    }
    // clean up
    // MPI_Comm_free(&worker_comm);
    // MPI_Comm_free(&cart_comm);
    MPI_Finalize();

    return 0;
}