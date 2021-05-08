#define _GNU_SOURCE
#define _OPEN_THREADS

#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <time.h>
#include <unistd.h>

#define BUFLEN 10 //Max length of string chunck
#define START_PORT 5555
#define LOCALHOST "127.0.0.1"
#define DATA_TO_SEND "Very beautiful string to distribute over a small peer-connected network!"
#define NPEERS 5
#define MAX_CHUNCKS 100
#define QUEUE_LENGTH_MAX 10

int MY_ID = 0;
int IS_SENDER = 1;

clock_t start, end;
double cpu_time_used;

pthread_mutex_t incoming_requests_mutex, outgoing_requests_mutex, chuncks_recieved_mutex;

struct PeerInfo
{
    char ip_address[20];
    int port_recieve;
    int port_send;
};

struct NetworkInfo
{
    int peers_number;
    struct PeerInfo peers[NPEERS];
};

// sending string for now
struct DataChunck
{
    int chunck_number;
    char data[BUFLEN];
};

struct FileInfo
{
    int file_size;
    int chuncks_amount;
    int chuncks_status[MAX_CHUNCKS];
    struct DataChunck data[MAX_CHUNCKS];
    int chunck_recieved_from[MAX_CHUNCKS];
    int chuncks_recieved;
};

struct FileInfo fileinfo;
struct NetworkInfo networkinfo;

struct DataPacket
{
    int source_id;
    int destination_id;
    int type_bit; // 0 if request for data, 1 if response with data
    struct DataChunck data_chunck;
};

struct entry
{
    struct DataPacket data;
    STAILQ_ENTRY(entry)
    entries;
};

STAILQ_HEAD(stailhead, entry);

struct stailhead incoming_requests, outgoing_requests;
int incoming_requests_length = 0, outgoing_requests_length = 0;

void queue_push(struct stailhead *queue, struct DataPacket temp)
{

    struct entry *n1;
    n1 = malloc(sizeof(struct entry));
    n1->data = temp;
    STAILQ_INSERT_TAIL(queue, n1, entries);
}

struct DataPacket queue_peek(struct stailhead *queue)
{
    return STAILQ_FIRST(queue)->data;
}

void queue_pop(struct stailhead *queue)
{
    STAILQ_REMOVE_HEAD(queue, entries);
}

int queue_empty(struct stailhead *queue)
{
    return STAILQ_FIRST(queue) == NULL;
}

void die(char *s)
{
    perror(s);
    exit(1);
}

void *recieve_requests()
{
    // recvfrom(), push to the queue
    struct sockaddr_in si_me, si_other;
    int s, slen = sizeof(si_other), recv_len;
    struct DataPacket *temp = malloc(sizeof(struct DataPacket));
    //create a UDP socket
    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
    {
        die("socket");
    }
    // zero out the structure
    memset((char *)&si_me, 0, sizeof(si_me));

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
        if ((recv_len = recvfrom(s, temp, sizeof(*temp), 0,
                                 (struct sockaddr *)&si_other, &slen)) < 0)
        {
            die("recvfrom()");
        };
        printf("#%d: got from %d", MY_ID, temp->source_id);
        pthread_mutex_lock(&incoming_requests_mutex);
        if (incoming_requests_length < QUEUE_LENGTH_MAX)
        {
            incoming_requests_length += 1;
            queue_push(&incoming_requests, *temp);
        }
        pthread_mutex_unlock(&incoming_requests_mutex);
    }
}

void *send_requests()
{
    // get head of outgoing and sendto()
    // send peer a udp packet asking for a specific chunck
    struct sockaddr_in si_other;
    int s, slen = sizeof(si_other);
    struct DataPacket temp;
    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
    {
        die("socket");
    }
    memset((char *)&si_other, 0, sizeof(si_other));
    si_other.sin_family = AF_INET;
    for (;;)
    {
        pthread_mutex_lock(&outgoing_requests_mutex);
        if (queue_empty(&outgoing_requests))
        {
            pthread_mutex_unlock(&outgoing_requests_mutex);
            continue;
        }
        temp = queue_peek(&outgoing_requests);
        int port = networkinfo.peers[temp.destination_id].port_recieve;
        si_other.sin_port = htons(port);
        queue_pop(&outgoing_requests);
        outgoing_requests_length -= 1;
        pthread_mutex_unlock(&outgoing_requests_mutex);
        if (inet_aton(networkinfo.peers[temp.destination_id].ip_address, &si_other.sin_addr) == 0)
        {
            fprintf(stderr, "inet_aton() failed\n");
            exit(1);
        }
        printf("Sending request from %d to %d\n", temp.source_id, temp.destination_id);
        sendto(s, (struct DataPacket *)&temp, sizeof(temp), 0, (struct sockaddr *)&si_other, slen);
    }
}

void log_info(){
    // when we posess all chunck, print the data string
    end = clock();
    cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
    printf("Now I have all the data, helping others!\n");
    // log data into file
    FILE *fp;
    char filename[42];
    sprintf(filename, "peer%dlog.txt", MY_ID);
    fp = fopen(filename, "w+");
    fprintf(fp, "Log info:\n");
    fprintf(fp, "Peer id: %d\n", MY_ID);
    fprintf(fp, "Seconds taken to recieve all packets: %f\n", cpu_time_used);
    fprintf(fp, "Peers that delivered packets:\n");
    for (int i = 0; i < fileinfo.chuncks_amount; i++){
        fprintf(fp, " %d ", fileinfo.chunck_recieved_from[i]);
    }
    fprintf(fp, "\n");
    fprintf(fp, "Data that was delivered:\n");
    for (int i = 0; i < fileinfo.chuncks_amount; i++){
        fprintf(fp, "%s", fileinfo.data[i].data);
    }
    fprintf(fp, "\n-----\n");
    fclose(fp);
}

void *generate_requests()
{
    // while we miss some data, ask everyone about it
    while (fileinfo.chuncks_recieved != fileinfo.chuncks_amount)
    {
        printf("#%d: have %d now\n", MY_ID, fileinfo.chuncks_recieved);
        for (int i = 0; i < fileinfo.chuncks_amount; i++)
        {
            // if this chunck is in need
            if (fileinfo.chuncks_status[i] != 1)
            {
                // printf("#%d need chunck %d\n", MY_ID, i);
                for (int j = networkinfo.peers_number - 1; j >= 0; j--)
                {
                    //sleep(1);
                    usleep(1000 * 100);
                    // we cannot ask ourselves
                    if (j != MY_ID)
                    {
                        // send packet to this peer
                        struct DataPacket temp;
                        temp.type_bit = 0;
                        temp.source_id = MY_ID;
                        temp.destination_id = j;
                        temp.data_chunck.chunck_number = i;
                        pthread_mutex_lock(&outgoing_requests_mutex);
                        if (outgoing_requests_length < QUEUE_LENGTH_MAX)
                        {
                            queue_push(&outgoing_requests, temp);
                            outgoing_requests_length += 1;
                        }
                        pthread_mutex_unlock(&outgoing_requests_mutex);
                    }
                }
            }
        }
    }
    log_info();
}

void *generate_responses()
{
    struct DataPacket temp;
    for (;;)
    {
        pthread_mutex_lock(&incoming_requests_mutex);
        if (queue_empty(&incoming_requests))
        {
            pthread_mutex_unlock(&incoming_requests_mutex);
            continue;
        }
        temp = queue_peek(&incoming_requests);
        queue_pop(&incoming_requests);
        incoming_requests_length -= 1;
        pthread_mutex_unlock(&incoming_requests_mutex);
        if (temp.type_bit == 1)
            printf("Got packet %d from %d\n", temp.data_chunck.chunck_number, temp.source_id);
        // for (int i = 0; i < fileinfo.chuncks_amount; i++)
        // {
        //     printf("%d", fileinfo.chuncks_status[i]);
        // }
        // printf("\n");
        if (temp.type_bit == 1 && fileinfo.chuncks_status[temp.data_chunck.chunck_number] != 1)
        // packet with info which we don't have
        {
            // write info to database
            int chunck = temp.data_chunck.chunck_number;
            fileinfo.chuncks_status[chunck] = 1;
            pthread_mutex_lock(&chuncks_recieved_mutex);
            fileinfo.chuncks_recieved += 1;
            pthread_mutex_unlock(&chuncks_recieved_mutex);
            fileinfo.chunck_recieved_from[chunck] = temp.source_id;
            fileinfo.data[chunck] = temp.data_chunck;
        }
        else
        {

            // packet with request that we can answer ?
            if (temp.type_bit == 0 && fileinfo.chuncks_status[temp.data_chunck.chunck_number] == 1)
            {
                //if able to respond, do it
                struct DataPacket reply;
                reply.source_id = MY_ID;
                reply.destination_id = temp.source_id;
                reply.type_bit = 1;
                reply.data_chunck = fileinfo.data[temp.data_chunck.chunck_number];
                pthread_mutex_lock(&outgoing_requests_mutex);
                if (outgoing_requests_length < QUEUE_LENGTH_MAX)
                {
                    outgoing_requests_length += 1;
                    queue_push(&outgoing_requests, reply);
                }
                pthread_mutex_unlock(&outgoing_requests_mutex);
            }
        }
    }
}
int main(int argc, char *argv[])
{
    // this parameters are entered by bash script
    MY_ID = atoi(argv[1]);
    IS_SENDER = atoi(argv[2]);
    printf("I am peer #%d, sender=%d\n", MY_ID, IS_SENDER);
    printf("Initialization..\n");
    pthread_mutex_init(&incoming_requests_mutex, NULL);
    pthread_mutex_init(&outgoing_requests_mutex, NULL);
    pthread_mutex_init(&chuncks_recieved_mutex, NULL);
    STAILQ_INIT(&outgoing_requests);
    STAILQ_INIT(&incoming_requests);
    // NETWORK INFO SECTION
    // initializing local clients' ports
    networkinfo.peers_number = NPEERS;
    for (int i = 0; i < NPEERS; i++)
    {
        strcpy(networkinfo.peers[i].ip_address, LOCALHOST);
        networkinfo.peers[i].port_recieve = START_PORT + 2 * i;
        networkinfo.peers[i].port_send = START_PORT + 2 * i + 1;
    }

    // initialize infomation about the distributed file
    fileinfo.file_size = strlen(DATA_TO_SEND);
    fileinfo.chuncks_amount = (fileinfo.file_size + BUFLEN - 1) / BUFLEN;
    for (int i = 0; i < fileinfo.chuncks_amount; i++)
    {
        fileinfo.chuncks_status[i] = IS_SENDER;
    }
    fileinfo.chuncks_recieved = fileinfo.chuncks_amount * IS_SENDER;
    // peer has all the data wanted or not?
    if (IS_SENDER)
    {
        // fill the array with data that we actually have
        for (int i = 0; i < fileinfo.chuncks_amount; i++)
        {
            fileinfo.chunck_recieved_from[i] = MY_ID;
            fileinfo.data[i].chunck_number = i;
            for (int j = 0; j < BUFLEN; j++)
            {
                if (i * BUFLEN + j < fileinfo.file_size)
                {
                    fileinfo.data[i].data[j] = DATA_TO_SEND[i * BUFLEN + j];
                }
                else
                {
                    fileinfo.data[i].data[j] = '\0';
                }
            }
        }
    }

    pthread_t sender, reciever, requests_generator, response_generator;
    printf("Starting application...\n");
    start = clock();
    pthread_create(&sender, NULL, send_requests, NULL);
    pthread_create(&reciever, NULL, recieve_requests, NULL);
    pthread_create(&requests_generator, NULL, generate_requests, NULL);
    pthread_create(&response_generator, NULL, generate_responses, NULL);
    pthread_join(sender, NULL);
    pthread_join(reciever, NULL);
    pthread_join(requests_generator, NULL);
    pthread_join(response_generator, NULL);
    pthread_mutex_destroy(&incoming_requests_mutex);
    pthread_mutex_destroy(&outgoing_requests_mutex);
    pthread_mutex_destroy(&chuncks_recieved_mutex);
    return 0;
}