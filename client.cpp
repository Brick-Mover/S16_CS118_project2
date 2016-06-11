/*
 * usage: ./client SERVER-HOST-OR-IP PORT-NUMBER
 */
#include "tcp.hpp"
#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <string>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <math.h>
#include <fcntl.h>
using namespace std;

const int RWNDSIZE = 15;
const uint16_t INIT_SEQ_NUM = seq_rand(MAX_SEQ_NUM);     // Replace with a random number later
const uint16_t INIT_ACK_NUM = 0;
int rwnd_occupied[RWNDSIZE];    // 0: unoccupied, 1: occupued, -1: eof
int to_be_acked = 0;   // integer range between 0~14 that indicates the start of circular buffer
static int residue = 0;
double timeout = 0.5;

// (re)initialize receive window
void initialize_rwnd(unsigned char* recv_buf) {
    for (int i = 0; i < RWNDSIZE; ++i)
        rwnd_occupied[i] = 0;
    bzero(recv_buf, DATASIZE * RWNDSIZE);
}


// @returns the reveive window size at this moment
int rwnd_size() {
    int result = 0;
    for (int i = 0; i < RWNDSIZE; ++i)
        if (rwnd_occupied[i] == 0) {
            result++;
        }
    return result;
}


// @returns the first N consecutive acks(1 <= N <= 15)
// called when the receive seq = expect seq
int consecutive_acked() {
    int result = 0;
    for (int t = to_be_acked; t < RWNDSIZE + to_be_acked; t++) {
        int i = t % RWNDSIZE;
        if (rwnd_occupied[i] == 1)
            result++;
        else
            break;
        
    }
    return result;
}


uint16_t add(uint16_t ack, uint16_t inc) {
    to_be_acked = (int)ceil(to_be_acked + inc / DATASIZE) % RWNDSIZE;
    return (ack + inc) % MAX_SEQ_NUM;
}


int replyWithAck(int sockfd, const struct sockaddr_in& server, int ack_num, bool retrans) {
    segment* reply = new segment();
    reply->setFlagack();
    reply->setFlagfin();
    reply->setAcknum(ack_num);
    reply->setRcvwin(rwnd_size());
    unsigned char* send_buf = reply->encode(NULL, 0);
    int n = sendto(sockfd, send_buf, HEADERSIZE, 0,
                   (struct sockaddr *)&server, sizeof(server));
    if (retrans == false)
        cout << "Sending packet " << ack_num << endl;
    else
        cout << "Sending packet " << ack_num << " Retransmission" << endl;
    return n;
}

int replyWithFin(int sockfd, const struct sockaddr_in& server, int ack_num, bool retrans) {
    segment* reply = new segment();
    reply->setFlagack();
    reply->setFlagfin();
    reply->setAcknum(ack_num);
    reply->setRcvwin(rwnd_size());
    unsigned char* send_buf = reply->encode(NULL, 0);
    int n = sendto(sockfd, send_buf, HEADERSIZE, 0,
                   (struct sockaddr *)&server, sizeof(server));
    if(retrans == false){
        cout<< "Sending packet " << reply->getAcknum() << " FIN"<< endl;
    }
    else{
        cout<< "Sending packet " << reply->getAcknum() << " FIN Retransmission" << endl;
    }
    return n;
}



/*  The client send its initial sequence number,
 and returns the server's initial sequence number. */
uint16_t handshake(int sockfd, const struct sockaddr_in& server) {
    
    unsigned char recv_buf[BUFSIZE + 1];
    bzero(recv_buf, BUFSIZE);
    
    // handshake with server, send initial sequence number and port number
    segment estab_connection;
    estab_connection.setSeqnum(INIT_SEQ_NUM);
    estab_connection.setAcknum(INIT_ACK_NUM);
    estab_connection.setFlagsyn();
    
    unsigned char* send_buf;
    send_buf = estab_connection.encode(NULL, 0);
    
    socklen_t serverlen = sizeof(server);
    
    int n = 0;
    //cout << "sending " << endl;
    n = sendto(sockfd, send_buf, 8, 0,
               (struct sockaddr *)&server, serverlen);
    if (n < 0)
        error("ERROR in send: handshake");
    
    cout << "Sending packet SYN\n";
    
    //time out for the reply ack
    clock_t clock_s, clock_e;
    clock_s = clock_e = clock();
    bool received = false;
    double elapsed = double(clock_e - clock_s) / CLOCKS_PER_SEC;
    while(!received) {
        while(elapsed < timeout) {
            int n = recvfrom(sockfd, recv_buf, BUFSIZE, MSG_DONTWAIT, (struct sockaddr *)&server, &serverlen);
            if(n == 8) {
                segment r;
                r.decode(recv_buf, HEADERSIZE);
                cout << "received seq num: " << r.getSeqnum() << endl;
                if(r.getFlagack() && r.getFlagsyn() && (r.getAcknum() == (INIT_SEQ_NUM+1)))
                {
                    received = true;
                    break;
                }
            }
            clock_e = clock();
            elapsed = double(clock_e - clock_s) / CLOCKS_PER_SEC;
            
        }
        
        //if time out and still not received, resend fin buf
        if(!received){
            send_buf = estab_connection.encode(NULL, 0);
            n = sendto(sockfd, send_buf, 8, 0, (struct sockaddr *)&server, serverlen);
            if (n < 0)
                error("ERROR in send: handshake");
            
            cout << "Sending packet Retransmission SYN \n";
            
            //reset timer
            clock_s = clock_e = clock();
            elapsed = double(clock_e - clock_s) / CLOCKS_PER_SEC;
        }
    }
    
    
    segment response;
    response.decode(recv_buf, n);
    
    segment handshake_ack;
    
    handshake_ack.setFlagack();
    setReplyAck(response, handshake_ack, 1);
    send_buf = handshake_ack.encode(NULL, 0);
    
    n = sendto(sockfd, send_buf, 8, 0,
               (struct sockaddr *)&server, serverlen);
    if (n < 0)
        error("ERROR in send: handshake");
    
    cout << "Sending packet " << handshake_ack.getAcknum() << endl;
    
    return response.getSeqnum();
}



int main(int argc, char **argv) {
    
    int sockfd, portno, n;
    socklen_t serverlen;
    struct sockaddr_in serveraddr;
    struct hostent *server;
    char *hostname;
    unsigned char recv_buf[DATASIZE * RWNDSIZE];  // 30720/2 = 15340 = 15 * 1024(+8)
    // A circular buffer to handle out of order packets
    bzero(recv_buf, DATASIZE * RWNDSIZE);
    unsigned char mss_buf[MSS];             // a temp buf to store a single data packet
    initialize_rwnd(recv_buf);
    
    /* check command line arguments */
    if (argc != 3)
        error("usage: ./client SERVER-HOST-OR-IP PORT-NUMBER, you idiot!");
    
    hostname = argv[1];
    portno = atoi(argv[2]);
    
    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");
    
    /* gethostbyname: get the server's DNS entry */
    server = gethostbyname(hostname);
    if (server == NULL)
        error("ERROR no such hostname");
    
    /* build the server's Internet address */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)server->h_addr,
          (char *)&serveraddr.sin_addr.s_addr, server->h_length);
    serveraddr.sin_port = htons(portno);
    serverlen = sizeof(serveraddr);
    
    
    uint16_t InitSeq = handshake(sockfd, serveraddr);    // if unsuccessful, client will hang
    
    
    int write_fd = open("received.data", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (write_fd < 0)
        perror("open");
    
    
    uint16_t NextExpSeq = add(InitSeq, 1);  // update next expected sequence number
    
    
    while(true) {
        bzero (mss_buf, MSS);
        
        /* get the server's reply */
        n = recvfrom(sockfd, mss_buf, MSS, 0, (struct sockaddr *)&serveraddr, &serverlen);

        if (n < 8)
            error("ERROR in recvfrom");
        else if (n < MSS)
            residue = n - 8;
        
        segment temp;
        temp.decode(mss_buf, n);
        
        uint16_t recv_seq = temp.getSeqnum();
        
        cout << "Receiving packet " << recv_seq << endl;

        if (n == 8 && temp.getFlagfin() == 1)
            break;
        
        unsigned char* seg_data = temp.getData();

        int pos = ((recv_seq + MAX_SEQ_NUM - NextExpSeq) % MAX_SEQ_NUM) / DATASIZE;
        int buf_pos = (to_be_acked + pos) % RWNDSIZE;
        
        
        // CASE 1: out of order, and data doesn't fit into buffer,
        // discard data, and send desired Seq immediately
        if (pos >= RWNDSIZE) {
            int t = replyWithAck(sockfd, serveraddr, NextExpSeq, true);
            if (t < 0)
                perror("sendto");
            continue;
        }
        
        // CASE 2: out of order, but data fits into buffer,
        // update buffer, and stores data into recv_buf
        // send desired Seq immediately
        else if (pos != 0) {
            if (n != MSS)
                rwnd_occupied[buf_pos] = -1;
            else
                rwnd_occupied[buf_pos] = 1;
            
            memcpy(&recv_buf[buf_pos * DATASIZE], seg_data, n - HEADERSIZE);
            replyWithAck(sockfd, serveraddr, NextExpSeq, true);
        }
        
        // CASE 3: in order packet, update recv_buf
        // write to file up to the first unacked packet
        else if (pos == 0) {
            memcpy(&recv_buf[buf_pos * DATASIZE], seg_data, n - HEADERSIZE);
            
            // write to the first unacked packet
            // note if the last packet is incomplete, write residue(0 if not eof)
            if (n == MSS) {
                rwnd_occupied[buf_pos] = 1;
                
                int acked = consecutive_acked();
                int first_unacked_packet = (to_be_acked + acked) % RWNDSIZE; // >= 1
                
                // first write whole chunks, and reset data written
                int temp = to_be_acked;
                
                NextExpSeq = add(NextExpSeq, acked * DATASIZE + residue);
                
                for (int t = temp; t < temp + acked; t++) {
                    int i = t % RWNDSIZE;
                    if (write(write_fd, &recv_buf[i*DATASIZE], DATASIZE) < 0)
                        perror("write");
                    rwnd_occupied[i] = 0;
                    
                }
                // write the remaining parts
                if (residue != 0 && rwnd_occupied[first_unacked_packet] == -1) {
                    if (write(write_fd, &recv_buf[first_unacked_packet * DATASIZE], residue) < 0)
                        perror("write");
                    rwnd_occupied[first_unacked_packet] = 0;
                }
                replyWithAck(sockfd, serveraddr, NextExpSeq, false);
            }
            
            else {  // this is the end of file, simply write
                if (write(write_fd, &recv_buf[to_be_acked*DATASIZE], residue) < 0)
                    perror("write");
                NextExpSeq = add(NextExpSeq, residue);
                replyWithAck(sockfd, serveraddr, NextExpSeq, false);
            }

        } 

    }

    replyWithFin(sockfd, serveraddr, NextExpSeq+1, false);                        
    //time out for the reply ack
    clock_t clock_s, clock_e;
    clock_s = clock_e = clock();
    bool received = false;
    double elapsed = double(clock_e - clock_s) / CLOCKS_PER_SEC;
    int count = 0;
    while(!received){
        segment r;
        while(elapsed < timeout) {
            unsigned char recv[HEADERSIZE];
            int n = recvfrom(sockfd, recv, HEADERSIZE, MSG_DONTWAIT, (struct sockaddr *) &serveraddr, &serverlen);
            if(n == 8) {
                
                r.decode(recv, HEADERSIZE);
                                    
                //if(r.getFlagack() && (r.getAcknum() == (INIT_SEQ_NUM+1))) {
                if(r.getFlagfin()){
                    replyWithFin(sockfd, serveraddr, NextExpSeq+1, true);
                }
                if(r.getFlagack()) {
                    received = true;
                    //cout<<"Receiving final ack "<<r.getAcknum()<<endl;
                    break;
                }
            }
                                
            clock_e = clock();
            elapsed = double(clock_e - clock_s) / CLOCKS_PER_SEC;
        }
                            
        //if time out and still not received, resend fin buf
        if(!received) {
            if(r.getFlagfin()){
               replyWithFin(sockfd, serveraddr, NextExpSeq+1, true);
            }
            cerr << "waiting for timeout...\n";                
            count++;
            if(count == 7) {
                cerr << "Connection closed due to timeout. (File transfer complete)"<<endl;
                break;
            }
                                
            //reset timer
            clock_s = clock_e = clock();
            elapsed = double(clock_e - clock_s) / CLOCKS_PER_SEC;
        } 
    }

    return 0;
    
}