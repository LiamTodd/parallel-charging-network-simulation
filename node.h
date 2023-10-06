#define SHIFT_ROW 0
#define SHIFT_COL 1
#define DISP 1
#define CARTESIAN_DIMENSIONS 2
#define MAX_TIMESTAMP_DATAPOINTS 5
#define ALERT_NEIGHBOUR_SIGNAL 100
#define AVAILABLE 1
#define UNAVAILABLE 0
#define NEIGHBOUR_ALERT_TAG 0
#define NEIGHBOUR_AVAILABILITY_TAG 1

struct TimestampData
{
    char time_str[20];
    int available_ports;
};

int node_set_up(MPI_Comm *worker_comm, MPI_Comm *cart_comm, int *dims, int *coord, int *neighbours, int *second_order_neighbours, int *worker_rank);

int node_lifecycle(int *neighbours, int *second_order_neighbours, MPI_Comm *cart_comm, int worker_rank, MPI_Datatype alert_report_type);