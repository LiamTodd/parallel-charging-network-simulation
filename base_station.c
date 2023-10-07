#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "base_station.h"
#include "shared_constants.h"
#include "shared_structs.h"

int base_station_set_up(int argc, char *argv[], int *dims, int *simulation_seconds, int *availability_threshold)
{
    int m, n, processors;
    // base station: read in command-line args
    if (argc != 5)
    {
        printf("Error: Invalid number of arguments.\n");
        return 1;
    }
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
    return 0;
}

int base_station_lifecycle(int num_nodes, int simulation_seconds, MPI_Datatype alert_report_type, int cols, int availability_threshold)
{
    struct AlertReport report_list[MAX_REPORTS];
    int report_list_index = 0;
    int iterations = simulation_seconds * 10;
    int tenth_of_second = 100000;
    char time_log_str[20];
    struct tm *log_time_info;
    time_t log_time;
    FILE *fp;
    clock_t start_clock, end_clock;

    fp = fopen(LOG_FILE_NAME, "a");

    for (int i = 0; i < iterations; i++)
    {
        if (report_list_index > MAX_REPORTS)
        {
            break;
        }
        usleep(tenth_of_second);
        for (int node = 1; node < num_nodes + 1; node++)
        {
            int flag = 0;
            MPI_Status probe_status;
            MPI_Status recv_status;
            MPI_Iprobe(node, ALERT_TAG, MPI_COMM_WORLD, &flag, &probe_status);
            if (flag)
            {
                fprintf(fp, "\n\tREPORT RECEIVED (Iteration %d):\n", i);
                struct AlertReport report;
                start_clock = clock();
                MPI_Recv(&report, 1, alert_report_type, node, ALERT_TAG, MPI_COMM_WORLD, &recv_status);
                end_clock = clock();
                report.iteration = i;
                report_list[report_list_index] = report;
                if (report_list_index > MAX_REPORTS)
                {
                    printf("Max Reports Reached. Exiting.\n");
                    break;
                }
                time(&log_time);
                log_time_info = localtime(&log_time);
                strftime(time_log_str, sizeof(time_log_str), "%Y-%m-%d %H:%M:%S", log_time_info);
                fprintf(fp, "\t\tReporting node: %d\n\t\tAlert time: %s\n\t\tLogged time: %s\n\t\tAdjacent nodes: %d\n\t\tAvailability threshold: %d\n", report.reporting_node, report.time_str, time_log_str, report.neighbours_count, availability_threshold);
                fprintf(fp, "\n\t\t%-15s %-15s %-15s %-15s %-15s\n", "Reporting Node", "Row Coord", "Col Coord", "Total Ports", "Available Ports");
                fprintf(fp, "\t\t%-15d %-15d %-15d %-15d %-15d\n", report.reporting_node, report.reporting_node / cols, report.reporting_node % cols, PORTS_PER_NODE, report.reporting_node_availability);
                fprintf(fp, "\n\t\t%-15s %-15s %-15s %-15s %-15s\n", "Adjacent Node", "Row Coord", "Col Coord", "Total Ports", "Available Ports");
                int send_reply = 1;
                for (int j = 0; j < MAX_NEIGHBOURS; j++)
                {
                    if (report.neighbours[j] != MPI_PROC_NULL)
                    {
                        fprintf(fp, "\t\t%-15d %-15d %-15d %-15d %-15d\n", report.neighbours[j], report.neighbours[j] / cols, report.neighbours[j] % cols, PORTS_PER_NODE, report.neighbours_availability[j]);
                        if (report.neighbours_availability[j] > availability_threshold)
                        {
                            // no need to respond to node, as at least one of its neighbours have sufficient availability
                            send_reply = 0;
                        }
                    }
                }
                fprintf(fp, "\n\t\t%-40s %-15s %-15s\n", "Nearby Nodes (second order neighbours)", "Row Coord", "Col Coord");

                // check availability of second-order neighbours
                int available_so_neighbours[MAX_SECOND_ORDER_NEIGHBOURS];
                for (int k = 0; k < MAX_SECOND_ORDER_NEIGHBOURS; k++)
                {
                    if (report.second_order_neighbours[k] != MPI_PROC_NULL)
                    {
                        fprintf(fp, "\t\t%-40d %-15d %-15d\n", report.second_order_neighbours[k], report.second_order_neighbours[k] / cols, report.second_order_neighbours[k] % cols);
                    }
                    // see if second-order neighbour has sent a report in the last 20 iterations
                    int so_neighbour = report.second_order_neighbours[k];
                    int check_index = report_list_index;
                    int check_iteration = i;
                    available_so_neighbours[k] = report.second_order_neighbours[k];

                    // check the past 20 iterations (max 10 iterations occur per second)
                    while (check_iteration > i - 20 && check_index >= 0)
                    {
                        if (report_list[check_index].reporting_node == so_neighbour)
                        {
                            // this node has sent a report, so it is considered unavailable
                            available_so_neighbours[k] = MPI_PROC_NULL;
                        }
                        check_iteration = report_list[check_index].iteration;
                        check_index--;
                    }
                }
                fprintf(fp, "\n\t\tAvailable nearby nodes (no report received in last 20 iterations):");
                int nearby_available = 0;
                for (int l = 0; l < MAX_SECOND_ORDER_NEIGHBOURS; l++)
                {
                    if (available_so_neighbours[l] != MPI_PROC_NULL)
                    {
                        fprintf(fp, " %d,", available_so_neighbours[l]);
                        nearby_available = 1;
                    }
                }
                fprintf(fp, "\n");
                if (send_reply)
                {
                    // send second-order neighbour availability back to reporting node
                    MPI_Send(&available_so_neighbours, MAX_SECOND_ORDER_NEIGHBOURS, MPI_INT, node, BASE_STATION_REPLY_TAG, MPI_COMM_WORLD);
                    end_clock = clock();
                }
                fprintf(fp, "\t\tCommunication time between nodes: %.5fms\n", report.node_comm_time * 1000);
                fprintf(fp, "\t\tCommunincation time between base station and reporting node: %.5fms\n", (double)(end_clock - start_clock) / CLOCKS_PER_SEC * 1000);
                fprintf(fp, "\t\tTotal messages sent between reporting node and base station: %d\n", send_reply == 1 ? 2 : 1);
                fprintf(fp, "\t\tAction taken by base station following report: ");
                if (send_reply)
                {
                    if (nearby_available)
                    {
                        fprintf(fp, "Replied to reporting node: Notifying of available nearby nodes.\n");
                    }
                    else
                    {
                        fprintf(fp, "Replied to reporting node: Notifying that no nearby nodes are available.\n");
                    }
                }
                else
                {
                    fprintf(fp, "Did not reply to reporting node (its neighbour(s) had sufficient availability).\n");
                }

                report_list_index++;
            }
        }
    }
    fclose(fp);
    int termination_signal = TERMINATION_SIGNAL;
    for (int i = 1; i < num_nodes + 1; i++)
    {
        MPI_Send(&termination_signal, 1, MPI_INT, i, TERMINATION_TAG, MPI_COMM_WORLD);
    }
    return 0;
}