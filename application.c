#define _GNU_SOURCE
#define _OPEN_THREADS

#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <time.h>
#include <unistd.h>

#define BUFFER_SIZE 512            // size of buffer string to be sent via UDP
#define START_PORT 5555            // starting port for peers port assignment
#define LOCALHOST "127.0.0.1"      // server for testing
#define FILENAME "image_large.jpg" // name of the file to be transferred
#define NPEERS 5                   // number of peers in the network
#define MAX_CHUNCKS 1000000        // maximum number of chuncks in the file
#define QUEUE_LENGTH_MAX 2000      // maximum length of requests/response queue
#define SENDER_PEER_ID 0           // id of peer which is sender peer
#define LOCAL_DEBUG 1              // 1 if all peers are locally allocated, 0 if peers are configured manually

int MY_ID = 0; // id of this peer - defined as command line parameter

clock_t start, end;   // clock variables for time measurement
double cpu_time_used; // timer

pthread_mutex_t incoming_requests_mutex, outgoing_requests_mutex, chuncks_recieved_mutex; // mutexes for threads' usage of critical resourses

// information about a peer
struct PeerInfo
{
    char ip_address[20]; // string - IP address
    int port_recieve;    // port to send packets to this peer to
};

// information about the network - peers and links between them
struct NetworkInfo
{
    int peers_number;                // total number of peers in the network
    struct PeerInfo peers[NPEERS];   // information about each peer
    int peers_graph[NPEERS][NPEERS]; // graph of links between peers - peers_graph[i][j] = 1 if link exists, 0 otherwise
};

// chunck - part of the initial file to be sent
struct DataChunck
{
    int chunck_number;      // serial number of the chunck
    int chunck_size;        // size of this chunck
    char data[BUFFER_SIZE]; // data in this chunck
};

// metadata about file to be sent - distributed around peers by sender peer
struct FileMetaData
{
    int file_size;      // size of the file (in bytes)
    int chuncks_amount; // amount of chuncks this file is divided into
    char filename[50];  // name of this file (for saving purposes)
};

// information about the file - its data and additional log info
struct FileInfo
{
    int file_size;                         // size of the distributed file (in bytes)
    int chuncks_amount;                    // amount of chuncks this file is divided into
    int chuncks_status[MAX_CHUNCKS];       // for each chunck, is this chunck recieved yet or not
    struct DataChunck data[MAX_CHUNCKS];   // data of the file itself
    int chunck_recieved_from[MAX_CHUNCKS]; // from which peer did this peer recieve this chunck
    int chuncks_recieved;                  // how many chuncks are recieved by now
    char filename[50];                     // name of the file (for saving purposes)
};

struct FileInfo fileinfo;       // file information
struct NetworkInfo networkinfo; // network information

// Packets to be sent over network (UDP)
struct DataPacket
{
    int source_id;                 // id of source peer
    int destination_id;            // id of destination peer
    int type_bit;                  // 0 - request for chunck of data, 1 - response with chunck of data
    struct DataChunck data_chunck; // data chunck itself (filled only if type_bit = 1)
};

// Struct for queue
struct entry
{
    struct DataPacket data; // entry in queue
    STAILQ_ENTRY(entry)
    entries;
};

STAILQ_HEAD(stailhead, entry); // initialization for queue type

struct stailhead incoming_requests, outgoing_requests;          // queues for requests
int incoming_requests_length = 0, outgoing_requests_length = 0; // lengths of queues

// push DataPacket into queue
void queue_push(struct stailhead *queue, struct DataPacket temp)
{
    struct entry *n1; // initialize entry to pushed
    n1 = malloc(sizeof(struct entry));
    n1->data = temp;
    STAILQ_INSERT_TAIL(queue, n1, entries); // insert to tail of the queue
}

// peek the last element of the queue
struct DataPacket queue_peek(struct stailhead *queue)
{
    return STAILQ_FIRST(queue)->data;
}

// pop the first element from the queue
void queue_pop(struct stailhead *queue)
{
    STAILQ_REMOVE_HEAD(queue, entries);
}

// returns 1 if queue is empty, 0 if not
int queue_empty(struct stailhead *queue)
{
    return STAILQ_FIRST(queue) == NULL;
}

// edge in graph
struct edge
{
    int from; // from vertex
    int to;   // to vertex
};

// die on error and print error to console
void die(char *s)
{
    perror(s);
    exit(1);
}

// routine to reciever requests via UDP socket and push them to queue
void *recieve_requests()
{
    struct sockaddr_in si_me, si_other;
    int s, slen = sizeof(si_other), recv_len;
    struct DataPacket *temp = malloc(sizeof(struct DataPacket));
    //create a UDP socket
    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
    {
        die("socket");
    }
    // fill the structure
    memset((char *)&si_me, 0, sizeof(si_me));
    // initialize this peer's nework info
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(networkinfo.peers[MY_ID].port_recieve);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);

    //bind socket
    if (bind(s, (struct sockaddr *)&si_me, sizeof(si_me)) == -1)
    {
        die("bind");
    }
    // in endless loop, recieve messages and push them to the queue
    for (;;)
    {
        // recieve message from someone
        if ((recv_len = recvfrom(s, temp, sizeof(*temp), 0,
                                 (struct sockaddr *)&si_other, &slen)) < 0)
        {
            die("recvfrom()");
        };
        // lock incoming_requests queue to work with it
        pthread_mutex_lock(&incoming_requests_mutex);
        // if queue is small enough, push recieved request into it
        if (incoming_requests_length < QUEUE_LENGTH_MAX)
        {
            incoming_requests_length += 1;
            queue_push(&incoming_requests, *temp);
        }
        // unlock resourse for other threads
        pthread_mutex_unlock(&incoming_requests_mutex);
    }
}

// get requests from queue and send them
void *send_requests()
{
    struct sockaddr_in si_other;
    int s, slen = sizeof(si_other);
    struct DataPacket temp;
    // create UDP socket
    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
    {
        die("socket");
    }
    // initialize network info
    memset((char *)&si_other, 0, sizeof(si_other));
    si_other.sin_family = AF_INET;
    for (;;)
    {
        // lock outgoing_requests to use it
        pthread_mutex_lock(&outgoing_requests_mutex);
        // if queue is empty, we have nothing to work with
        if (queue_empty(&outgoing_requests))
        {
            // unlock resourse
            pthread_mutex_unlock(&outgoing_requests_mutex);
            continue;
        }
        // get fron element from the queue
        temp = queue_peek(&outgoing_requests);
        // get port on which to send data from packet to be sent
        int port = networkinfo.peers[temp.destination_id].port_recieve;
        si_other.sin_port = htons(port);
        queue_pop(&outgoing_requests);
        outgoing_requests_length -= 1;
        // unlock resourse for other threads
        pthread_mutex_unlock(&outgoing_requests_mutex);
        // set ip address for recipient
        if (inet_aton(networkinfo.peers[temp.destination_id].ip_address, &si_other.sin_addr) == 0)
        {
            fprintf(stderr, "inet_aton() failed\n");
            exit(1);
        }
        // send request to si_other peer
        sendto(s, (struct DataPacket *)&temp, sizeof(temp), 0, (struct sockaddr *)&si_other, slen);
    }
}

// save logging information into local files
void log_info()
{
    // stop measuring the time, save it
    end = clock();
    cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("Now I have all the data, helping others!\n");
    // save logging info into file
    FILE *file;
    char filename[42];
    sprintf(filename, "peer%dlog.txt", MY_ID);
    // open file
    file = fopen(filename, "w+");
    // log information
    fprintf(file, "Log info:\n");
    fprintf(file, "Peer id: %d\n", MY_ID);
    fprintf(file, "Seconds taken to recieve all packets: %f\n", cpu_time_used);
    fprintf(file, "Peers that delivered packets:\n");
    for (int i = 0; i < fileinfo.chuncks_amount; i++)
    {
        fprintf(file, " %d ", fileinfo.chunck_recieved_from[i]);
    }
    fprintf(file, "\n-----\n");
    fclose(file);
    // save recieved data into separate file
    sprintf(filename, "peer%d%s", MY_ID, fileinfo.filename);
    file = fopen(filename, "w+");
    // for each chunck, write it to file
    for (int i = 0; i < fileinfo.chuncks_amount; i++)
    {
        fwrite(fileinfo.data[i].data, sizeof(char), fileinfo.data[i].chunck_size, file);
    }
    fclose(file);
}

// routine to generate requests for data until we get all the file's chuncks
void *generate_requests()
{
    // while we don't possess every chunck, generate requests
    while (fileinfo.chuncks_recieved != fileinfo.chuncks_amount)
    {
        //debug printing
        //printf("#%d: have %d now\n", MY_ID, fileinfo.chuncks_recieved);
        // check each chunck
        for (int i = 0; i < fileinfo.chuncks_amount; i++)
        {
            // if this chunck is needed
            if (fileinfo.chuncks_status[i] != 1)
            {
                // debug printing
                // printf("#%d need chunck %d\n", MY_ID, i);
                // ask each peer
                for (int j = 0; j < networkinfo.peers_number; j++)
                {
                    //sleep(1);
                    //usleep(1000 * 10);
                    // we can only ask our neighbours in the graph
                    // ask with some probability to show that links may be not able to operate
                    //int ask = rand() % 2;
                    int ask = 1;
                    if (networkinfo.peers_graph[MY_ID][j] == 1 && ask == 1)
                    {
                        // send packet to this peer
                        struct DataPacket temp;
                        temp.type_bit = 0;                  // request for data
                        temp.source_id = MY_ID;             // from our ID
                        temp.destination_id = j;            // to id of peer
                        temp.data_chunck.chunck_number = i; // chunck i is desired
                        // lock outgoing queue to operate with it
                        pthread_mutex_lock(&outgoing_requests_mutex);
                        // push to queue only if it is small enough
                        if (outgoing_requests_length < QUEUE_LENGTH_MAX)
                        {
                            queue_push(&outgoing_requests, temp);
                            outgoing_requests_length += 1;
                        }
                        // unlock resourse for other threads
                        pthread_mutex_unlock(&outgoing_requests_mutex);
                    }
                }
            }
        }
    }
    // when all chuncks are recieved, output logging info
    log_info();
}

// generate responses to peers and put them in a queue
void *generate_responses()
{
    struct DataPacket temp;
    for (;;)
    {
        // lock queue to get exclusive access
        pthread_mutex_lock(&incoming_requests_mutex);
        // if queue is empty, we have nothing to work with
        if (queue_empty(&incoming_requests))
        {
            // unlock reourse for other threads
            pthread_mutex_unlock(&incoming_requests_mutex);
            continue;
        }
        // get from element of the queue
        temp = queue_peek(&incoming_requests);
        queue_pop(&incoming_requests);
        // decrease length of the queue
        incoming_requests_length -= 1;
        // unlock resourse for other threads
        pthread_mutex_unlock(&incoming_requests_mutex);
        // packet with info which we don't have
        if (temp.type_bit == 1 && fileinfo.chuncks_status[temp.data_chunck.chunck_number] != 1)
        {
            // print info
            printf("Got packet %d from %d\n", temp.data_chunck.chunck_number, temp.source_id);
            // write to fileinfo
            int chunck = temp.data_chunck.chunck_number;
            fileinfo.chuncks_status[chunck] = 1;
            // lock variable to increase number of chuncks to be
            pthread_mutex_lock(&chuncks_recieved_mutex);
            fileinfo.chuncks_recieved += 1;
            // unlock variable
            pthread_mutex_unlock(&chuncks_recieved_mutex);
            fileinfo.chunck_recieved_from[chunck] = temp.source_id;
            fileinfo.data[chunck] = temp.data_chunck;
        }
        else
        {
            // packet with request that we can answer
            if (temp.type_bit == 0 && fileinfo.chuncks_status[temp.data_chunck.chunck_number] == 1)
            {
                struct DataPacket reply;
                reply.source_id = MY_ID;                                           // our id
                reply.destination_id = temp.source_id;                             // destination peer id
                reply.type_bit = 1;                                                // packet with info
                reply.data_chunck = fileinfo.data[temp.data_chunck.chunck_number]; // data to send
                // lock queue for pushing into it
                pthread_mutex_lock(&outgoing_requests_mutex);
                // if queue is small enough, proceed
                if (outgoing_requests_length < QUEUE_LENGTH_MAX)
                {
                    outgoing_requests_length += 1;
                    queue_push(&outgoing_requests, reply);
                }
                // unlock resourse for other threads
                pthread_mutex_unlock(&outgoing_requests_mutex);
            }
        }
    }
}

// initialize mutexes in the program
void init_mutexes()
{
    pthread_mutex_init(&incoming_requests_mutex, NULL);
    pthread_mutex_init(&outgoing_requests_mutex, NULL);
    pthread_mutex_init(&chuncks_recieved_mutex, NULL);
}

// initialize queues
void init_queues()
{
    STAILQ_INIT(&outgoing_requests);
    STAILQ_INIT(&incoming_requests);
}

// initialize network information - necessary to know our peers' ip's, ports, etc.
void init_networkinfo()
{
    // if all peers are local
    if (LOCAL_DEBUG)
    {
        networkinfo.peers_number = NPEERS; // number of peers
        for (int i = 0; i < NPEERS; i++)
        {
            strcpy(networkinfo.peers[i].ip_address, LOCALHOST); // ip address of peer
            networkinfo.peers[i].port_recieve = START_PORT + i; // port of peer to send data to
        }
        // initializing peers availability graph - default is 0 for everything - network without connection
        for (int i = 0; i < networkinfo.peers_number; i++)
        {
            for (int j = 0; j < networkinfo.peers_number; j++)
            {
                if (i != j)
                    networkinfo.peers_graph[i][j] = 1;
                else
                    networkinfo.peers_graph[i][j] = 0;
            }
        }
        // adding some edges to network graph
        // int edges_number = 9;
        // struct edge edges[] = {
        //     {0, 1},
        //     {0, 2},
        //     {1, 3},
        //     {1, 4},
        //     {2, 3},
        //     {2, 7},
        //     {3, 5},
        //     {5, 7},
        //     {3, 6}};
        // // setting peers to be able to communicate
        // for (int i = 0; i < edges_number; i++)
        // {
        //     networkinfo.peers_graph[edges[i].from][edges[i].to] = 1;
        //     networkinfo.peers_graph[edges[i].to][edges[i].from] = 1;
        // }
    }
    else
    {
        // manually configure network information
        networkinfo.peers_number = 2;
        for (int i = 0; i < networkinfo.peers_number; i++)
        {
            for (int j = 0; j < networkinfo.peers_number; j++)
            {
                networkinfo.peers_graph[i][j] = 1;
            }
        }
        // ip addresses and ports assignment
        char *ip1 = "10.91.50.14";
        char *ip0 = "10.91.54.113";
        memcpy(networkinfo.peers[0].ip_address, ip0, strlen(ip0));
        networkinfo.peers[0].port_recieve = START_PORT;
        memcpy(networkinfo.peers[1].ip_address, ip1, strlen(ip1));
        networkinfo.peers[1].port_recieve = START_PORT;
    }
}

// initialize information about the file - sender peer initializes variables itself, other peers are getting this info from sender
void init_fileinfo()
{
    // if peer is sender peer, it is allowed to access the file
    if (MY_ID == SENDER_PEER_ID)
    {
        // copy file into fileinfo and extract filemetadata
        FILE *file;
        file = fopen(FILENAME, "rb");
        fseek(file, 0, SEEK_END);
        fileinfo.file_size = ftell(file);
        memcpy(fileinfo.filename, FILENAME, strlen(FILENAME));
        char buffer[BUFFER_SIZE];
        // initialize infomation about the distributed file
        int chunck = 0;
        for (chunck = 0; chunck * BUFFER_SIZE < fileinfo.file_size; chunck++)
        {
            // read each chunck, save into fileinfo
            fseek(file, (chunck * BUFFER_SIZE), SEEK_SET);
            int size_read = fread(buffer, sizeof(char), BUFFER_SIZE, file);
            fileinfo.data[chunck].chunck_number = chunck;
            memcpy(fileinfo.data[chunck].data, buffer, size_read);
            fileinfo.data[chunck].chunck_size = size_read;
            fileinfo.chunck_recieved_from[chunck] = MY_ID;
        }
        fileinfo.chuncks_amount = chunck;
        for (int i = 0; i < chunck; i++)
        {
            fileinfo.chuncks_status[i] = 1;
        }
        fileinfo.chuncks_recieved = chunck;

        // once file info is downloaded, send all info to peers via tcp
        struct FileMetaData fmd;
        fmd.file_size = fileinfo.file_size;
        fmd.chuncks_amount = fileinfo.chuncks_amount;
        memcpy(fmd.filename, FILENAME, sizeof(FILENAME));

        struct sockaddr_in server, client;
        int s, slen = sizeof(server);
        // create socket
        if ((s = socket(AF_INET, SOCK_STREAM, IPPROTO_IP)) == -1)
        {
            die("socket");
        }
        memset((char *)&server, 0, sizeof(server));
        // information about our server - sender peer
        server.sin_family = AF_INET;
        server.sin_addr.s_addr = htonl(INADDR_ANY);
        server.sin_port = htons(networkinfo.peers[MY_ID].port_recieve);
        int one = 1;
        // set socket to be reusable to be able to bind to it
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        // bind the socket
        if (bind(s, (struct sockaddr *)&server, sizeof(server)) < 0)
        {
            printf("fail to bind\n");
        };
        // listen to this socket
        listen(s, 11);
        printf("Waiting for peers\n");
        // accepting connections from clients
        for (int i = 0; i < networkinfo.peers_number - 1; i++)
        {
            // accept connection
            int s1 = accept(s, (struct sockaddr *)&client, (socklen_t *)&slen);
            // send FileMetaData
            send(s1, &fmd, sizeof(fmd), 0);
        }
        // close socket
        printf("All peers got their info\n");
        close(s);
    }
    else
    {
        // wait for info from sender peer via tcp
        struct FileMetaData fmd;
        struct sockaddr_in server = {0};
        int s, slen = sizeof(server);
        // create socket
        if ((s = socket(AF_INET, SOCK_STREAM, IPPROTO_IP)) == -1)
        {
            die("socket");
        }
        // fill server information - peer 0 is sender by default
        server.sin_addr.s_addr = inet_addr(networkinfo.peers[SENDER_PEER_ID].ip_address);
        server.sin_family = AF_INET;
        server.sin_port = htons(networkinfo.peers[SENDER_PEER_ID].port_recieve);
        // wait for connection
        int res;
        printf("Trying to connect\n");
        while ((res = connect(s, (struct sockaddr *)&server, sizeof(struct sockaddr_in))) < 0)
        {
        }
        printf("Success\n");
        // recieve fileinfo metadata
        recv(s, &fmd, sizeof(fmd), 0);
        // save it for further work
        fileinfo.file_size = fmd.file_size;
        fileinfo.chuncks_amount = fmd.chuncks_amount;
        memcpy(fileinfo.filename, fmd.filename, strlen(fmd.filename));
        // close socket
        close(s);
    }
}

int main(int argc, char *argv[])
{
    // this parameters are entered by bash script
    MY_ID = atoi(argv[1]);
    printf("I am peer #%d\n", MY_ID);
    printf("Initialization..\n");

    // initialization phase
    srand(time(NULL));
    init_mutexes();
    init_queues();
    init_networkinfo();

    // print debug information
    if (MY_ID == SENDER_PEER_ID)
    {
        printf("Sending file information to peers\n");
    }
    else
    {
        printf("Waiting for file information from distributing peer\n");
    }
    // initialize file information - locally of requesting sender peer
    init_fileinfo();

    // threads initialization
    pthread_t sender, reciever, requests_generator, response_generator;
    printf("Starting application...\n");
    start = clock();
    pthread_create(&sender, NULL, send_requests, NULL);
    pthread_create(&reciever, NULL, recieve_requests, NULL);
    pthread_create(&requests_generator, NULL, generate_requests, NULL);
    pthread_create(&response_generator, NULL, generate_responses, NULL);
    // join to all of the threads - endless loop
    pthread_join(sender, NULL);
    pthread_join(reciever, NULL);
    pthread_join(requests_generator, NULL);
    pthread_join(response_generator, NULL);
    // destroy mutexes on end - altough these lines of code are never reached
    pthread_mutex_destroy(&incoming_requests_mutex);
    pthread_mutex_destroy(&outgoing_requests_mutex);
    pthread_mutex_destroy(&chuncks_recieved_mutex);
    return 0;
}