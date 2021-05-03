/*
	Simple udp server
*/
#include<stdio.h>
#include<string.h>
#include<stdlib.h> 
#include<arpa/inet.h>
#include<sys/socket.h>

#define BUFLEN 512	//Max length of buffer
#define SERVER "127.0.0.1"
#define PORT 8888	//The port on which to listen for incoming data
#define MAX_CLIENTS 100
#define MAX_CHUNCKS 100


struct NetworkInfo{
    int peers_number;
    char* peers_ip[MAX_CLIENTS];
};

struct FileInfo{
    int file_size;
    int chuncks_amount;
    int chuncks_status[MAX_CHUNCKS];
    int chuncks_recieved;
};

struct FileInfo fileinfo;
struct NetworkInfo networkinfo;
    

void die(char *s)
{
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



void asnwer_to_peers(){
    // listen to incoming packets
    struct sockaddr_in si_me, si_other;
	
	int s, i, slen = sizeof(si_other) , recv_len;
	char buf[BUFLEN];
	
	//create a UDP socket
	if ((s=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
	{
		die("socket");
	}
	
	// zero out the structure
	memset((char *) &si_me, 0, sizeof(si_me));
	
	si_me.sin_family = AF_INET;
	si_me.sin_port = htons(PORT);
	si_me.sin_addr.s_addr = htonl(INADDR_ANY);
	
	//bind socket to port
	if(bind(s ,(struct sockaddr*)&si_me, sizeof(si_me) ) == -1)
	{
		die("bind");
	}
    for (;;){
        if ((recv_len = recvfrom(s, buf, BUFLEN, 0, (struct sockaddr *) &si_other, &slen)) == -1)
		{
			die("recvfrom()");
		}
        printf("Received packet from %s:%d\n", inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port));
		printf("Data: %s\n" , buf);
    }
}



void process_requests(){

}

void send_requests(){
       
}


int main(void){

    pthread_t sender, reciever;
    pthread_create(&sender, NULL, send_requests, NULL);
    pthread_create(&reciever, NULL, process_requests, NULL);
	return 0;
}