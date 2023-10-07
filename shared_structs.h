struct AlertReport
{
    int reporting_node;
    int reporting_node_availability;
    int neighbours[4];
    int neighbours_availability[4];
    int messages_exchanged_between_nodes;
    int second_order_neighbours[8];
    int iteration;
    char time_str[20];
    int neighbours_count;
    int row;
    int col;
    double node_comm_time;
    int type;
};