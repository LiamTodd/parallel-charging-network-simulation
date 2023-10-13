#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include "shared_constants.h"
#include "shared_structs.h"
#include "helpers.h"

int set_up(char *argv[], int *dims, int *simulation_seconds, int *availability_threshold, MPI_Datatype *alert_report_type)
{
    int m, n, processors;
    m = atoi(argv[1]);
    n = atoi(argv[2]);
    *simulation_seconds = atoi(argv[3]);
    *availability_threshold = atoi(argv[4]);

    if (m == 0 || n == 0)
    {
        printf("Error: Invalid values of n and m. n and m must be positive integers.\n");
        return 1;
    }
    if (*simulation_seconds < 1)
    {
        printf("Error: Invalid value of simulation seconds. Must be a positive integer.\n");
        return 1;
    }
    if (*availability_threshold < 0 || *availability_threshold > PORTS_PER_NODE)
    {
        printf("Error: Invalid availability threshold. Must be between 0 and PORTS_PER_NODE.\n");
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

    if (set_up_data_types(alert_report_type) != 0)
    {
        printf("Error: Something went wrong while setting up the MPI datatypes.\n");
        return 1;
    }

    return 0;
}

int set_up_data_types(MPI_Datatype *alert_report_type)
{
    int block_lengths[17];
    MPI_Aint offsets[17];
    MPI_Datatype types[17];

    offsets[0] = offsetof(struct AlertReport, reporting_node);
    block_lengths[0] = 1;
    types[0] = MPI_INT;

    offsets[1] = offsetof(struct AlertReport, reporting_node_availability);
    block_lengths[1] = 1;
    types[1] = MPI_INT;

    offsets[2] = offsetof(struct AlertReport, neighbours);
    block_lengths[2] = 4;
    types[2] = MPI_INT;

    offsets[3] = offsetof(struct AlertReport, neighbours_availability);
    block_lengths[3] = 4;
    types[3] = MPI_INT;

    offsets[4] = offsetof(struct AlertReport, messages_exchanged_between_nodes);
    block_lengths[4] = 1;
    types[4] = MPI_INT;

    offsets[5] = offsetof(struct AlertReport, second_order_neighbours);
    block_lengths[5] = 8;
    types[5] = MPI_INT;

    offsets[6] = offsetof(struct AlertReport, iteration);
    block_lengths[6] = 1;
    types[6] = MPI_INT;

    offsets[7] = offsetof(struct AlertReport, time_str);
    block_lengths[7] = 20;
    types[7] = MPI_CHAR;

    offsets[8] = offsetof(struct AlertReport, neighbours_count);
    block_lengths[8] = 1;
    types[8] = MPI_INT;

    offsets[9] = offsetof(struct AlertReport, row);
    block_lengths[9] = 1;
    types[9] = MPI_INT;

    offsets[10] = offsetof(struct AlertReport, col);
    block_lengths[10] = 1;
    types[10] = MPI_INT;

    offsets[11] = offsetof(struct AlertReport, node_comm_time);
    block_lengths[11] = 1;
    types[11] = MPI_DOUBLE;

    offsets[12] = offsetof(struct AlertReport, type);
    block_lengths[12] = 1;
    types[12] = MPI_INT;

    offsets[13] = offsetof(struct AlertReport, report_received);
    block_lengths[13] = 1;
    types[13] = MPI_DOUBLE;

    offsets[14] = offsetof(struct AlertReport, report_processed);
    block_lengths[14] = 1;
    types[14] = MPI_DOUBLE;

    offsets[15] = offsetof(struct AlertReport, node_base_station_comm_start);
    block_lengths[15] = 1;
    types[15] = MPI_DOUBLE;

    offsets[16] = offsetof(struct AlertReport, node_base_station_comm_end);
    block_lengths[16] = 1;
    types[16] = MPI_DOUBLE;

    MPI_Type_create_struct(17, block_lengths, offsets, types, alert_report_type);
    MPI_Type_commit(alert_report_type);
    return 0;
}