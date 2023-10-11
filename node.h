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
#define PORTS_PER_NODE 5
#define EXTRA_THREADS 3
#define QUEUE_INTERVAL 1
#define TALLY_INTERVAL 2
#define AVAILABILITY_INTERVAL 0.5

struct TimestampData
{
    char time_str[20];
    int ports[PORTS_PER_NODE];
};

int node_set_up(MPI_Comm *worker_comm, MPI_Comm *cart_comm, int *dims, int *coord, int *neighbours, int *second_order_neighbours, int *worker_rank);

int node_lifecycle(int *neighbours, int *second_order_neighbours, MPI_Comm *cart_comm, int worker_rank, MPI_Datatype alert_report_type, int availability_threshold);

void charging_port_iteration(int thread_num, int worker_rank, struct TimestampData timestamp_queue[MAX_TIMESTAMP_DATAPOINTS], int *queue_index);

void node_time_keeper_iteration(struct TimestampData timestamp_queue[MAX_TIMESTAMP_DATAPOINTS], int *queue_index, int *exit_flag);

void responder_iteration(struct TimestampData timestamp_queue[MAX_TIMESTAMP_DATAPOINTS], int *queue_index, int *neighbours, MPI_Comm *cart_comm, int *exit_flag);

void reporter_iteration(struct TimestampData timestamp_queue[MAX_TIMESTAMP_DATAPOINTS], int *queue_index, int *neighbours, MPI_Comm *cart_comm, int availability_threshold, int *exit_flag, int worker_rank, int *second_order_neighbours, MPI_Datatype alert_report_type);
