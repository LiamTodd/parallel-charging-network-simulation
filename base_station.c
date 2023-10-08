#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <omp.h>
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
    int termination_signal = TERMINATION_SIGNAL, so_neighbour, check_index, current_iteration, check_iteration, report_list_index = -1, report_list_logging_index = -1, iterations = simulation_seconds * 10, i, j, k, l, probe_flag, send_reply, nearby_available, exit_flag = 0, node, thread_num, alert_count = 0, report_count = 0;
    int available_so_neighbours[MAX_SECOND_ORDER_NEIGHBOURS];
    char time_log_str[20];
    struct tm *log_time_info;
    time_t log_time;
    FILE *fp;
    struct AlertReport recv_report, log_report;

    fp = fopen(LOG_FILE_NAME, "a");

    omp_set_num_threads(2);
#pragma omp parallel shared(report_list_index, report_list, exit_flag)
    {
        thread_num = omp_get_thread_num();
        if (thread_num == 0)
        {
            // This thread is responsible for probing nodes for alerts and recording them into an array
            for (i = 0; i < iterations; i++)
            {
                if (report_list_index > MAX_REPORTS)
                {
                    printf("Max reports reached. Exiting.\n");
                    break;
                }
                usleep(LATENCY_TENTH_OF_SECOND);
                for (node = 1; node < num_nodes + 1; node++)
                {
                    probe_flag = 0;
                    MPI_Status probe_status;
                    MPI_Status recv_status;
                    MPI_Iprobe(node, ALERT_TAG, MPI_COMM_WORLD, &probe_flag, &probe_status);
                    if (probe_flag)
                    {
                        MPI_Recv(&recv_report, 1, alert_report_type, node, ALERT_TAG, MPI_COMM_WORLD, &recv_status);
                        recv_report.report_received = clock();

                        if (report_list_index + 1 > MAX_REPORTS)
                        {
                            break;
                        }
#pragma omp critical
                        {
                            report_list_index++;
                            recv_report.iteration = i;
                            report_list[report_list_index] = recv_report;
                        }
                    }
                }
            }
#pragma omp critical
            {
                exit_flag = 1;
            }
        }
        if (thread_num == 1)
        {
            // This thread processes, logs, and responds to reports
            //
            while (exit_flag == 0 || report_list_index > report_list_logging_index)
            {
                usleep(LATENCY_TENTH_OF_SECOND);
                if (report_list_index > report_list_logging_index)
                {
                    // there is a new report to process
                    report_list_logging_index++;

                    log_report = report_list[report_list_logging_index];
                    fprintf(fp, "\n\tMESSAGE RECEIVED (Iteration %d):\n", log_report.iteration);
                    if (log_report.type == REPORT_TYPE)
                    {
                        fprintf(fp, "\tMessage type: REPORT (reporter's neighbours have sufficient availability)\n");
                    }
                    else if (log_report.type == ALERT_TYPE)
                    {
                        fprintf(fp, "\tMessage type: ALERT (reporter's neighbours have insufficient availability)\n");
                    }
                    time(&log_time);
                    log_time_info = localtime(&log_time);
                    strftime(time_log_str, sizeof(time_log_str), "%Y-%m-%d %H:%M:%S", log_time_info);
                    fprintf(fp, "\t\tReporting node: %d\n\t\tAlert time: %s\n\t\tLogged time: %s\n\t\tAdjacent nodes: %d\n\t\tAvailability threshold: %d\n", log_report.reporting_node, log_report.time_str, time_log_str, log_report.neighbours_count, availability_threshold);
                    fprintf(fp, "\n\t\t%-15s %-15s %-15s %-15s %-15s\n", "Reporting Node", "Row Coord", "Col Coord", "Total Ports", "Available Ports");
                    fprintf(fp, "\t\t%-15d %-15d %-15d %-15d %-15d\n", log_report.reporting_node, log_report.reporting_node / cols, log_report.reporting_node % cols, PORTS_PER_NODE, log_report.reporting_node_availability);
                    fprintf(fp, "\n\t\t%-15s %-15s %-15s %-15s %-15s\n", "Adjacent Node", "Row Coord", "Col Coord", "Total Ports", "Available Ports");
                    send_reply = 1;
                    for (j = 0; j < MAX_NEIGHBOURS; j++)
                    {
                        if (log_report.neighbours[j] != MPI_PROC_NULL)
                        {
                            fprintf(fp, "\t\t%-15d %-15d %-15d %-15d %-15d\n", log_report.neighbours[j], log_report.neighbours[j] / cols, log_report.neighbours[j] % cols, PORTS_PER_NODE, log_report.neighbours_availability[j]);
                            if (log_report.neighbours_availability[j] > availability_threshold)
                            {
                                // no need to respond to node, as at least one of its neighbours have sufficient availability
                                send_reply = 0;
                            }
                        }
                    }
                    fprintf(fp, "\n\t\t%-40s %-15s %-15s\n", "Nearby Nodes (second order neighbours)", "Row Coord", "Col Coord");

                    // check availability of second-order neighbours
                    for (k = 0; k < MAX_SECOND_ORDER_NEIGHBOURS; k++)
                    {
                        if (log_report.second_order_neighbours[k] != MPI_PROC_NULL)
                        {
                            fprintf(fp, "\t\t%-40d %-15d %-15d\n", log_report.second_order_neighbours[k], log_report.second_order_neighbours[k] / cols, log_report.second_order_neighbours[k] % cols);
                        }
                        // see if second-order neighbour has sent a report in the last 20 iterations
                        so_neighbour = log_report.second_order_neighbours[k];
                        check_index = report_list_logging_index;
                        current_iteration = report_list[report_list_logging_index].iteration;
                        check_iteration = current_iteration;
                        available_so_neighbours[k] = log_report.second_order_neighbours[k];

                        // check the past MAX_BACK_CHECK iterations (note that (max) 10 iterations occur per second)
                        while (check_iteration > current_iteration - MAX_BACK_CHECK && check_index >= 0)
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
                    nearby_available = 0;
                    for (l = 0; l < MAX_SECOND_ORDER_NEIGHBOURS; l++)
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
                        MPI_Send(&available_so_neighbours, MAX_SECOND_ORDER_NEIGHBOURS, MPI_INT, log_report.reporting_node, BASE_STATION_REPLY_TAG, MPI_COMM_WORLD);
                    }
                    log_report.report_processed = clock();
                    fprintf(fp, "\t\tCommunication time between nodes: %.5fms\n", log_report.node_comm_time);
                    fprintf(fp, "\t\tTime between report received and report processed: %.5fms\n", (double)(log_report.report_processed - log_report.report_received) * 1000 / CLOCKS_PER_SEC);
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
                        alert_count++;
                    }
                    else
                    {
                        fprintf(fp, "Did not reply to reporting node (its neighbour(s) had sufficient availability).\n");
                        report_count++;
                    }
                }
            }
        }
    }

    fprintf(fp, "\nSummary:\n\tTotal messages received: %d\n\tTotal messages processed: %d\n\tTotal report messages: %d\n\tTotal alert messages: %d\n\tTotal outgoing messages: %d\n", report_list_index + 1, report_list_logging_index + 1, report_count, alert_count, alert_count);
    fprintf(fp, "Checks:\n\ttotal messages received = total messages processed = total report messages + total alert messages\n\ttotal alert messages = total outgoing messages\n");
    fclose(fp);
    for (int i = 1; i < num_nodes + 1; i++)
    {
        MPI_Send(&termination_signal, 1, MPI_INT, i, TERMINATION_TAG, MPI_COMM_WORLD);
    }
    return 0;
}