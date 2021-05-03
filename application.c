#include<stdio.h>
#include<string.h>
#include<stdlib.h> 
#include<pthread.h>
#include<arpa/inet.h>
#include<sys/socket.h>
#include <sys/queue.h>

#define BUFLEN 512	//Max length of buffer
#define SERVER "127.0.0.1"
#define PORT 8888	//The port on which to listen for incoming data
#define MAX_CLIENTS 100
#define MAX_CHUNCKS 100

struct NetworkInfo{
    int peers_number;
    char* peers_ip[MAX_CLIENTS];
};

// sending string for now
struct DataChunck{
    int chunck_number;
    char data[BUFLEN];
};

struct FileInfo{
    int file_size;
    int chuncks_amount;
    int chuncks_status[MAX_CHUNCKS];
    struct DataChunck data[MAX_CHUNCKS];
    int chuncks_recieved;
};

struct DataPacket {
    int type_bit; // 0 if request for data, 1 if response with data
    struct DataChunck data_chunck;
};


struct entry {
    struct DataPacket data;
    STAILQ_ENTRY(entry) entries;
};

STAILQ_HEAD(stailhead, entry);
struct stailhead incoming_requests, outgoing_requests;


struct FileInfo fileinfo;
struct NetworkInfo networkinfo;



void die(char *s) {
	perror(s);
	exit(1);
}

void ask_peer(int chunck, int peer){
    // send peer a udp packet asking for a specific chunck
    struct sockaddr_in si_other;
	int s, i, slen=sizeof(si_other);
	char buf[BUFLEN];
	char message[BUFLEN];
    if ( (s=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
	{
		die("socket");
	}

    memset((char *) &si_other, 0, sizeof(si_other));
	si_other.sin_family = AF_INET;
	si_other.sin_port = htons(PORT);

    if (inet_aton(SERVER , &si_other.sin_addr) == 0) 
	{
		fprintf(stderr, "inet_aton() failed\n");
		exit(1);
	}
    message[0] = '1';
    sendto(s, message, strlen(message) , 0 , (struct sockaddr *) &si_other, slen);

}


void get_from_peers(){
     while (fileinfo.chuncks_recieved != fileinfo.chuncks_amount){
        for (int i = 0; i < fileinfo.chuncks_amount; i++){
            if (fileinfo.chuncks_status[i] != 1){
                for (int j = 0; j < networkinfo.peers_number; j++){
                    // send udp packet to ask for the packet
                    // this should start a new thread to communicate directly with that very peer
                    ask_peer(i, j);
                }
            }
        }
    }
}

void recieve_requests(){
    // recvfrom(), push to the queue
    struct sockaddr_in si_me, si_other;
	int s, i, slen = sizeof(si_other), recv_len;
	struct DataPacket* temp = malloc(sizeof(struct DataPacket));
	//create a UDP socket
	if ((s=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
		die("socket");
	}
	// zero out the structure
	memset((char *) &si_me, 0, sizeof(si_me));
	
	si_me.sin_family = AF_INET;
	si_me.sin_port = htons(PORT);
	si_me.sin_addr.s_addr = htonl(INADDR_ANY);
	
	//bind socket to port
	if(bind(s ,(struct sockaddr*)&si_me, sizeof(si_me) ) == -1) {
		die("bind");
	}
    // in endless loop, recieve messages and push them to the queue
    for (;;){
         if ((recv_len = recvfrom(s, temp, sizeof(*temp), 0,
            (struct sockaddr *) &si_other, &slen)) < 0) {
			die("recvfrom()");
		};
        struct entry *n1;
        n1 = malloc(sizeof(struct entry));  
        n1->data = *temp;
        STAILQ_INSERT_TAIL(&incoming_requests, n1, entries);
    }   
}


void send_requests(){
    // get head of outgoing and sendto()
}




int main(void){
    pthread_t sender, reciever;
    pthread_create(&sender, NULL, send_requests, NULL);
    pthread_create(&reciever, NULL, recieve_requests, NULL);
    for (;;){
        // get from head of queue
        // add answer to send queue
        for (int i = 0; i < fileinfo.chuncks_amount; i++){
            if (fileinfo.chuncks_status[i] != 1){
                for (int j = 0; j < networkinfo.peers_number; j++){
                    // send udp packet to ask for the packet
                    // this should start a new thread to communicate directly with that very peer
                    ask_peer(i, j);
                }
            }
        }
    }
	return 0;
}