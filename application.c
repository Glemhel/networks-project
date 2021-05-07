#define _GNU_SOURCE

#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <unistd.h>

#define BUFLEN 4 //Max length of string chunck
#define SERVER "127.0.0.1"
#define SECRET "qwertyuiasdfghjk"
#define MY_ID 1
#define IS_SENDER 0
#define PORT_RECIEVE 5001 //The PORT_RECIEVE on which to listen for incoming data
#define PORT_SEND 5002    //The PORT_SEND on which to send data
#define MAX_CLIENTS 2
#define MAX_CHUNCKS 4

struct NetworkInfo
{
    int peers_number;
    char *peers_ip[MAX_CLIENTS];
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
    int chuncks_recieved;
};

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

struct FileInfo fileinfo;
struct NetworkInfo networkinfo;

void die(char *s)
{
    perror(s);
    exit(1);
}
/*
void ask_peer(int chunck, int peer)
{
    // send peer a udp packet asking for a specific chunck
    struct sockaddr_in si_other;
    int s, i, slen = sizeof(si_other);
    char buf[BUFLEN];
    char message[BUFLEN];
    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
    {
        die("socket");
    }

    memset((char *)&si_other, 0, sizeof(si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(PORT_RECIEVE);

    if (inet_aton(SERVER, &si_other.sin_addr) == 0)
    {
        fprintf(stderr, "inet_aton() failed\n");
        exit(1);
    }
    message[0] = '1';
    sendto(s, message, strlen(message), 0, (struct sockaddr *)&si_other, slen);
}
*/
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
    si_me.sin_port = htons(PORT_RECIEVE);
    //printf("I am here\n");
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);

    //bind socket to PORT_RECIEVE
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

        struct entry *n1;
        n1 = malloc(sizeof(struct entry));
        n1->data = *temp;
        STAILQ_INSERT_TAIL(&incoming_requests, n1, entries);
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
    si_other.sin_port = htons(PORT_SEND);
    for (;;)
    {
        while (!STAILQ_FIRST(&outgoing_requests))
        {
            pthread_yield();
        }
        temp = STAILQ_FIRST(&outgoing_requests)->data;
        STAILQ_REMOVE_HEAD(&outgoing_requests, entries);
        if (inet_aton(networkinfo.peers_ip[temp.destination_id], &si_other.sin_addr) == 0)
        {
            fprintf(stderr, "inet_aton() failed\n");
            exit(1);
        }
        //printf("%s\n", networkinfo.peers_ip[temp.destination_id]);
        sendto(s, (struct DataPacket *)&temp, sizeof(temp), 0, (struct sockaddr *)&si_other, slen);
        //printf("!!!\n");
    }
}

void *generate_requests()
{
    while (fileinfo.chuncks_recieved != fileinfo.chuncks_amount)
    {
        for (int i = 0; i < fileinfo.chuncks_amount; i++)
        {
            if (fileinfo.chuncks_status[i] != 1)
            {
                for (int j = 0; j < networkinfo.peers_number; j++)
                {
                    if (j != MY_ID)
                    {
                        // send packet to this peer
                        struct DataPacket temp;
                        temp.type_bit = 0;
                        temp.source_id = MY_ID;
                        temp.destination_id = j;
                        temp.data_chunck.chunck_number = i;
                        struct entry *n1;
                        n1 = malloc(sizeof(struct entry));
                        n1->data = temp;
                        //printf("I am here with %d and %d\n", i, j);
                        STAILQ_INSERT_TAIL(&outgoing_requests, n1, entries);
                        //printf("Passed\n");
                    }
                }
            }
        }
    }
    for (int i = 0; i < fileinfo.chuncks_amount; i++)
    {
        printf("%s", fileinfo.data[i].data);
    }
    printf("\nGood!\n");
}

void *generate_responses()
{
    struct DataPacket temp;
    for (;;)
    {
        while (!STAILQ_FIRST(&incoming_requests))
        {
            pthread_yield();
        }
        temp = STAILQ_FIRST(&incoming_requests)->data;
        STAILQ_REMOVE_HEAD(&incoming_requests, entries);
        if (temp.type_bit == 1)
        {
            // write into database
            int chunck = temp.data_chunck.chunck_number;
            fileinfo.chuncks_status[chunck] = 1;
            fileinfo.chuncks_recieved += 1;
            fileinfo.data[chunck] = temp.data_chunck;
        }
        else
        {
            if (fileinfo.chuncks_status[temp.data_chunck.chunck_number] == 1)
            {
                // can respond
                struct DataPacket reply;
                reply.data_chunck.chunck_number = temp.data_chunck.chunck_number;
                reply.source_id = MY_ID;
                reply.destination_id = temp.source_id;
                reply.type_bit = 1;
                reply.data_chunck = fileinfo.data[temp.data_chunck.chunck_number];
                struct entry *n1;
                n1 = malloc(sizeof(struct entry));
                n1->data = reply;
                STAILQ_INSERT_TAIL(&outgoing_requests, n1, entries);
            }
        }
    }
}
int main(void)
{
    STAILQ_INIT(&outgoing_requests);
    STAILQ_INIT(&incoming_requests);
    // Network info init
    networkinfo.peers_number = 2;
    //networkinfo.peers_ip[0] = "192.168.1.70";
    //networkinfo.peers_ip[1] = "192.168.1.62";
    networkinfo.peers_ip[0] = networkinfo.peers_ip[1] = "127.0.0.1";
    if (IS_SENDER)
    {
        // fill the array of data
        fileinfo.file_size = 16;
        fileinfo.chuncks_amount = 4;
        for (int i = 0; i < 4; i++)
        {
            fileinfo.chuncks_status[i] = 1;
        }
        fileinfo.chuncks_recieved = 4;
        for (int i = 0; i < 4; i++)
        {
            fileinfo.data[i].chunck_number = i;
            for (int j = 0; j < BUFLEN; j++)
            {
                fileinfo.data[i].data[j] = SECRET[i * 4 + j];
            }
        }
    }
    else
    {
        fileinfo.file_size = 16;
        fileinfo.chuncks_amount = 4;
        for (int i = 0; i < 4; i++)
        {
            fileinfo.chuncks_status[i] = 0;
        }
        fileinfo.chuncks_recieved = 0;
        for (int i = 0; i < 4; i++)
        {
            fileinfo.data[i].chunck_number = i;
            for (int j = 0; j < BUFLEN; j++)
            {
                fileinfo.data[i].data[j] = '#';
            }
        }
    }
    pthread_t sender, reciever, requests_generator, response_generator;
    printf("starting threads\n");
    pthread_create(&sender, NULL, send_requests, NULL);
    pthread_create(&reciever, NULL, recieve_requests, NULL);
    pthread_create(&requests_generator, NULL, generate_requests, NULL);
    pthread_create(&response_generator, NULL, generate_responses, NULL);
    pthread_join(sender, NULL);
    pthread_join(reciever, NULL);
    pthread_join(requests_generator, NULL);
    pthread_join(response_generator, NULL);
    // if we are here, all chunck are got, print them
    return 0;
}