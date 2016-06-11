#include "tcp.hpp"
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <map>

uint16_t server_seq;
uint16_t server_ack;
uint16_t client_ack;

enum {SLOWSTART, CONGESTIONADVOIDANCE, FASTRECOVERY};

int state = SLOWSTART;
int ssthreshPackets = SSTHRESH/BUFSIZE;
int ssthresh = SSTHRESH;
int cwndPackets = 1;
int cwnd = INIT_WINDOW_SIZE;
int unackedPackets = 0;
double timeout = 0.5;
double estimatedRTT, devRTT, adaptiveRTO;
uint16_t handshake_client_sequence;

void updateCwnd()
{
    switch (state)
    {
        case SLOWSTART:
        {
            cwnd += BUFSIZE;
            cwndPackets++;
            if (cwnd >= ssthresh)
                state = CONGESTIONADVOIDANCE;
            break;
        }
        case CONGESTIONADVOIDANCE:
        {
            cwnd += (BUFSIZE*BUFSIZE)/cwnd;
            cwndPackets = cwnd / BUFSIZE;
            break;
        }
        case FASTRECOVERY:
        {
            cwnd = ssthresh;
            cwndPackets = ssthreshPackets;
            state = CONGESTIONADVOIDANCE;
            break;
        }
        default:
            break;
    }
}

/*  The server waits for client to send its initial sequence number,
 send its own initial sequence number,
 and returns the client's initial sequence number. */
uint16_t handshake(int sockfd, struct sockaddr_in &clientaddr, socklen_t clientlen)
{
    unsigned char handshake_buf[HEADERSIZE];
    segment syn, ack;
    
    // receive syn
    long recv_len;
    if ((recv_len = recvfrom(sockfd, handshake_buf, HEADERSIZE, 0,
                             (struct sockaddr *) &clientaddr, &clientlen)) < 8)
    {
        cerr << "syn error" << endl;
        return USHRT_MAX;
    }

    syn.decode(handshake_buf, HEADERSIZE);
    if (syn.getFlagsyn())
    {
        server_seq = server_ack = seq_rand(MAX_SEQ_NUM);

        bool firstSyn = true;
        do {// send syn-ack
            segment synack;
            synack.setSeqnum(server_seq);
            setReplyAck(syn, synack, 1);
            client_ack = synack.getAcknum();
            synack.setFlagsyn();
            synack.setFlagack();
            
            unsigned char *handshake_buf2 = synack.encode(NULL, 0);
            sendto(sockfd, handshake_buf2, HEADERSIZE, 0,
                   (struct sockaddr *) &clientaddr, clientlen);
            
            if (firstSyn)
            {
                cout << "Sending packet " << server_seq << " " << cwnd << " " << ssthresh
                << " SYN" << endl;
                firstSyn = false;
            }
            else
            {
                cout << "Sending packet " << server_seq << " " << cwnd << " " << ssthresh
                << " Retransmission SYN" << endl;
            }
            
            clock_t clock_begin, clock_end;
            clock_begin = clock_end = clock();
            double elapsed_secs = 0.0;
            
            // receive ack
            while ((recv_len = recvfrom(sockfd, handshake_buf, HEADERSIZE, MSG_DONTWAIT,
                                        (struct sockaddr *) &clientaddr, &clientlen)) == -1)
            {
                if (errno != EWOULDBLOCK && errno != EAGAIN)
                    perror("recvfrom");
                clock_end = clock();
                elapsed_secs = double(clock_end - clock_begin) / CLOCKS_PER_SEC;
                if (elapsed_secs >= timeout)
                    break;
            }
            if (elapsed_secs < timeout)
            {
                ack.decode(handshake_buf, HEADERSIZE);
                if (ack.getFlagsyn())
                    continue;
                else
                    break;
            }
            
        } while (true);
        
        server_seq = (server_seq + 1) % MAX_SEQ_NUM;
        
        if (ack.getFlagack() && ack.getAcknum() == server_seq)
        {
            cout << "Receiving packet " << ack.getAcknum() << endl;
            
            server_ack = ack.getAcknum();
            return client_ack;
        }
        else
        {
            cout << "ACK flag: " << ack.getFlagack() << endl;
            cout << "SYN flag: " << ack.getFlagsyn() << endl;
            cout << "ack num received: " << ack.getAcknum() << endl;
            cout << "global_seq: " << server_seq << endl;
            cerr << "ack flag error or acknum error" << endl;
            return USHRT_MAX;
        }
    }
    else
    {
        cerr << "syn flag error" << endl;
        return USHRT_MAX;
    }
}

void teardown(int sockfd, struct sockaddr_in &clientaddr, socklen_t clientlen, int finAck, int finSeq) {
    //send fin ack
    segment fin;
    fin.setSeqnum(server_seq++);
    //cout << "Sending fin packet " << global_seq-1 << " " << cwndPackets << " " << ssthreshPackets << endl;
    fin.setFlagfin();
    unsigned char *fin_buf = fin.encode(NULL, 0);
    sendto(sockfd, fin_buf, HEADERSIZE, 0,
           (struct sockaddr *) &clientaddr, clientlen);
    cout << "Sending packet " << fin.getSeqnum() << " " << cwnd << " " << ssthresh <<" FIN"<< endl;
    
    //receive fin ack
    clock_t clock_s, clock_e;
    clock_s = clock_e = clock();
    segment r;
    bool received = false;
    double elapsed = double(clock_e - clock_s) / CLOCKS_PER_SEC;
    while(!received){
        while(elapsed < timeout){
            unsigned char recv[HEADERSIZE];
            long n = recvfrom(sockfd, recv, HEADERSIZE, MSG_DONTWAIT, (struct sockaddr *) &clientaddr, &clientlen);
            if(n == 8){
                
                r.decode(recv, HEADERSIZE);
                if(r.getFlagfin() && r.getFlagack() && (r.getAcknum() == server_seq)){
                    received = true;
                    //cout<<"Receiving fin ack "<<r.getAcknum()<<endl;
                    break;
                }
            }
            clock_e = clock();
            elapsed = double(clock_e - clock_s) / CLOCKS_PER_SEC;
        }
        
        //if time out and still not received, resend fin buf
        if(!received){
            sendto(sockfd, fin_buf, HEADERSIZE, 0, (struct sockaddr *) &clientaddr, clientlen);
            //cout << "Resending data packet " << global_seq-1 << " " << cwndPackets << " " << ssthreshPackets << endl;
            //reset timer
            //cout<<elapsed<<endl;
            clock_s = clock_e = clock();
            elapsed = double(clock_e - clock_s) / CLOCKS_PER_SEC;
        }
    }
    
    //send final ack
    //cout<<"194"<<endl;
    segment ack;
    setReplyAck(r, ack, 1);
    ack.setFlagack();
    unsigned char *ack_buf = ack.encode(NULL, 0);
    sendto(sockfd, ack_buf, HEADERSIZE, 0, (struct sockaddr *) &clientaddr, clientlen);
    //cout << "Sending final ack packet " << handshake_client_sequence+1 << " " << cwndPackets << " " << ssthreshPackets << endl;
}

int main(int argc, char **argv) {
    int sockfd; /* socket */
    int portno; /* port to listen on */
    struct sockaddr_in clientaddr; /* client addr */
    socklen_t clientlen; /* byte size of client's address */
    struct sockaddr_in serveraddr; /* server's addr */
    int optval; /* flag value for setsockopt */
    int fd;
    long file_size;
    long total_read = 0;
    unsigned char file_buf[MAX_SEQ_NUM_HALF];
    unsigned long lastbyteSent, lastbyteAcked, maxbyte;
    unsigned char *lastbyteSentPtr, *lastbyteAckedPtr, *maxbytePtr;
    clock_t clock_start, clock_end;
    bool eof = false;
    int dupAck = 0;
    map<uint16_t, clock_t> time_map;
    
    /* check command line arguments */
    if (argc != 3)
        error("Usage: ./server PORT-NUMBER FILE-NAME");
    portno = atoi(argv[1]);
    
    /* socket: create the parent socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");
    
    optval = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval , sizeof(int)) == -1){
        perror("setsockopt");
        return 1;
    };
    
    /* build the server's Internet address */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);
    
    /* bind: associate the parent socket with a port */
    if (::bind(sockfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) == -1){
        perror("bind");
        return 2;
    }
    
    clientlen = sizeof(clientaddr);
    
    if (handshake(sockfd, clientaddr, clientlen) == USHRT_MAX)
        return 3;
    
    if ((fd = open(argv[2], O_RDONLY, 0644)) == -1){
        perror("open");
        return 4;
    }
    if ((file_size = lseek(fd, 0, SEEK_END)) == -1){
        perror("lseek");
        return 5;
    }
    if (lseek(fd, 0, SEEK_SET) == -1){
        perror("lseek");
        return 6;
    }
    
    maxbyte = lastbyteSent = lastbyteAcked = 0;
    maxbytePtr = lastbyteSentPtr = lastbyteAckedPtr = file_buf;
    clock_start = clock_end = clock();
    
    bool firstRTT = true;
    
    while (!(eof && lastbyteAcked == maxbyte))
    {
        for ( ; (lastbyteSent < maxbyte) && (unackedPackets < cwndPackets);
             (lastbyteSent += BUFSIZE) && (unackedPackets++))
        {
            segment seg;
            seg.setSeqnum(server_seq);
            int send_size;
            if ((maxbyte-lastbyteSent)/BUFSIZE >= 1)
                send_size = BUFSIZE;
            else
                send_size = (int)(maxbyte - lastbyteSent);
            
            if (lastbyteSentPtr + send_size > file_buf + MAX_SEQ_NUM_HALF)
            {
                unsigned char temp[BUFSIZE];
                long send_part2 = (lastbyteSentPtr + send_size) - (file_buf + MAX_SEQ_NUM_HALF);
                long send_part1 = send_size - send_part2;
                memcpy((char*)temp, (char*)lastbyteSentPtr, send_part1);
                memcpy((char*)(temp+send_part1), (char*)file_buf, send_part2);
                unsigned char *send_buf = seg.encode(temp, send_size);
                sendto(sockfd, send_buf, send_size+HEADERSIZE, 0, (struct sockaddr *)&clientaddr, clientlen);
                lastbyteSentPtr = lastbyteSentPtr + send_size - MAX_SEQ_NUM_HALF;
            }
            else
            {
                unsigned char *send_buf = seg.encode(lastbyteSentPtr, send_size);
                sendto(sockfd, send_buf, send_size+HEADERSIZE, 0, (struct sockaddr *)&clientaddr, clientlen);
                lastbyteSentPtr = lastbyteSentPtr + send_size;
            }
            
            clock_t now = clock();
            time_map[server_seq] = now;
            
            cout << "Sending packet " << server_seq << " " << cwnd << " " << ssthresh << endl;
            server_seq = (server_seq + send_size) % MAX_SEQ_NUM;
        }
        
        while (true)
        {
            unsigned char recv_buf[HEADERSIZE];
            long recv_len;
            if ((recv_len = recvfrom(sockfd, recv_buf, HEADERSIZE, MSG_DONTWAIT,
                                     (struct sockaddr *) &clientaddr, &clientlen)) == -1)
            {
                if (errno != EWOULDBLOCK && errno != EAGAIN)
                    perror("recvfrom");
                break;
            }
            
            segment ack;
            ack.decode(recv_buf, HEADERSIZE);
            if (ack.getFlagack())
            {
                cout << "Receiving packet " << ack.getAcknum() << endl;
                
                if (ack.getAcknum() != server_ack)
                {
                    map<uint16_t, clock_t>::iterator it = time_map.find(server_ack);
                    if (it != time_map.end())
                    {
                        clock_t now, then;
                        now = clock();
                        then = it->second;
                        time_map.erase(server_ack);
                        
                        double sampleRTT = double(now - then) / CLOCKS_PER_SEC;
                        
                        if (firstRTT)
                        {
                            estimatedRTT = sampleRTT;
                            devRTT = sampleRTT / 2;
                            adaptiveRTO = estimatedRTT + 4 * devRTT;
                            timeout = adaptiveRTO;
                            firstRTT = false;
                        }
                        else
                        {
                            double difference = sampleRTT - estimatedRTT >= 0 ?
                                                sampleRTT - estimatedRTT : estimatedRTT - sampleRTT;
                            estimatedRTT = 0.875 * estimatedRTT + 0.125 * sampleRTT;
                            devRTT = 0.75 * devRTT + 0.25 * difference;
                            adaptiveRTO = estimatedRTT + 4 * devRTT;
                            timeout = adaptiveRTO;
                        }
                    }
                    
                    uint16_t diff;
                    if (ack.getAcknum() > server_ack)
                        diff = ack.getAcknum() - server_ack;
                    else
                        diff = MAX_SEQ_NUM - server_ack + ack.getAcknum();
                    
                    lastbyteAcked += diff;
                    if (lastbyteAckedPtr + diff > file_buf + MAX_SEQ_NUM_HALF)
                        lastbyteAckedPtr = lastbyteAckedPtr + diff - MAX_SEQ_NUM_HALF;
                    else
                        lastbyteAckedPtr = lastbyteAckedPtr + diff;
                    
                    server_ack = ack.getAcknum();
                    int num_acked = diff/BUFSIZE;
                    unackedPackets -= num_acked;
                    for (int i = 0; i < num_acked; i++)
                        updateCwnd();
                    
                    clock_start = clock_end = clock();
                    dupAck = 0;
                }
                else
                {
                    if (state != FASTRECOVERY)
                    {
                        dupAck++;
                        if (dupAck == 3)
                        {
                            state = FASTRECOVERY;
                            dupAck = 0;
                            
                            ssthresh = cwnd/2 < BUFSIZE ? BUFSIZE : cwnd/2;
                            ssthreshPackets = ssthresh / BUFSIZE;
                            cwnd = ssthresh + BUFSIZE*3;
                            cwndPackets = cwnd / BUFSIZE;
                            
                            segment seg;
                            seg.setSeqnum(server_ack);
                            int send_size;
                            if ((maxbyte-lastbyteAcked)/BUFSIZE >= 1)
                                send_size = BUFSIZE;
                            else
                                send_size = (int)(maxbyte - lastbyteAcked);
                            
                            if (lastbyteAckedPtr + send_size > file_buf + MAX_SEQ_NUM_HALF)
                            {
                                unsigned char temp[BUFSIZE];
                                long send_part2 = (lastbyteAckedPtr + send_size) - (file_buf + MAX_SEQ_NUM_HALF);
                                long send_part1 = send_size - send_part2;
                                memcpy((char*)temp, (char*)lastbyteAckedPtr, send_part1);
                                memcpy((char*)(temp+send_part1), (char*)file_buf, send_part2);
                                unsigned char *send_buf = seg.encode(temp, send_size);
                                sendto(sockfd, send_buf, send_size+8, 0, (struct sockaddr *) &clientaddr, clientlen);
                            }
                            else
                            {
                                unsigned char *send_buf = seg.encode(lastbyteAckedPtr, send_size);
                                sendto(sockfd, send_buf, send_size+8, 0, (struct sockaddr *) &clientaddr, clientlen);
                            }
                            
                            cout << "Sending packet " << server_ack << " " << cwnd << " "
                            << ssthresh << " Retransmission" << endl;
                            clock_start = clock_end = clock();
                            time_map.erase(server_ack);
                        }
                    }
                    else
                    {
                        cwnd += BUFSIZE;
                        cwndPackets += 1;
                    }
                }
            }
        }
        
        clock_end = clock();
        double elapsed_secs = double(clock_end - clock_start) / CLOCKS_PER_SEC;
        if (elapsed_secs >= timeout)
        {
            state = SLOWSTART;
            dupAck = 0;
            
            ssthresh = cwnd/2 < BUFSIZE ? BUFSIZE : cwnd/2;
            ssthreshPackets = ssthresh / BUFSIZE;
            cwnd = BUFSIZE;
            cwndPackets = 1;
            
            segment seg;
            seg.setSeqnum(server_ack);
            int send_size;
            if ((maxbyte-lastbyteAcked)/BUFSIZE >= 1)
                send_size = BUFSIZE;
            else
                send_size = (int)(maxbyte - lastbyteAcked);
            
            if (lastbyteAckedPtr + send_size > file_buf + MAX_SEQ_NUM_HALF)
            {
                unsigned char temp[BUFSIZE];
                long send_part2 = (lastbyteAckedPtr + send_size) - (file_buf + MAX_SEQ_NUM_HALF);
                long send_part1 = send_size - send_part2;
                memcpy((char*)temp, (char*)lastbyteAckedPtr, send_part1);
                memcpy((char*)(temp+send_part1), (char*)file_buf, send_part2);
                unsigned char *send_buf = seg.encode(temp, send_size);
                sendto(sockfd, send_buf, send_size+8, 0, (struct sockaddr *) &clientaddr, clientlen);
            }
            else
            {
                unsigned char *send_buf = seg.encode(lastbyteAckedPtr, send_size);
                sendto(sockfd, send_buf, send_size+8, 0, (struct sockaddr *) &clientaddr, clientlen);
            }
            
            cout << "Sending packet " << server_ack << " " << cwnd << " "
            << ssthresh << " Retransmission" << endl;
            clock_start = clock_end = clock();
            
            timeout *= 2;
            time_map.erase(server_ack);
        }
        
        if (maxbyte - lastbyteAcked <= MAX_SEQ_NUM_HALF)
        {
            long bytes_left = MAX_SEQ_NUM_HALF - (maxbyte - lastbyteAcked);
            long bytes_read = 0;
            long read_len;
            if (MAX_SEQ_NUM_HALF-(maxbytePtr-file_buf) < (bytes_left))
            {
                unsigned long part1 = MAX_SEQ_NUM_HALF - (maxbytePtr - file_buf);
                unsigned long part2 = bytes_left - part1;
                read_len = read(fd, maxbytePtr, part1);
                bytes_read += read_len;
                read_len = read(fd, file_buf, part2);
                bytes_read += read_len;
            }
            else
            {
                read_len = read(fd, maxbytePtr, bytes_left);
                bytes_read += read_len;
            }
            
            maxbyte += bytes_read;
            if (maxbytePtr + bytes_read > file_buf + MAX_SEQ_NUM_HALF)
                maxbytePtr = maxbytePtr + bytes_read - MAX_SEQ_NUM_HALF;
            else
                maxbytePtr = maxbytePtr + bytes_read;
            
            total_read += bytes_read;
            if (total_read == file_size)
                eof = true;
        }
    }
    
    teardown(sockfd, clientaddr, clientlen, 0, server_seq);
    
}
