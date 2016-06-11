#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <cstring>
#include <cassert>
#include <bitset>
#include <climits>
#include <iostream>
#include <string>
#include <sys/time.h>

using namespace std;

#define MSS 1032    // The maximum packet size including all the headers
#define INIT_WINDOW_SIZE 1024 // Initial window size: 1024 byte
#define MAX_SEQ_NUM 30720 // Maximum sequence number: 30 Kbytes
#define MAX_SEQ_NUM_HALF 15360
#define SSTHRESH 15360   // Initial slow start threshold: 30 Kbytes
#define RETRANS_TIMEOUT 500 // Retransmission time out value: 500 ms
#define BUFSIZE 1024
#define DATASIZE 1024
#define HEADERSIZE 8

inline void error (string msg)
{
  cerr << msg << endl;
  exit(1);
}

struct TcpHeader {
  uint16_t seqNo;
  uint16_t ackNo;
  uint16_t rcvWin;
  uint8_t reserved;
  uint8_t flags;
};

struct segment {
    
  unsigned char buffer[MSS+1];
  TcpHeader header;
    
  //constructor
  segment();
    
  //encode and decode
  unsigned char* encode(unsigned char* payload, int n);
  void decode(unsigned char* buf, int n);
    
  //set functions
  void setSeqnum(uint16_t seq);
  void setAcknum(uint16_t ack);
  void setRcvwin(uint16_t rcv);
  void setFlagack();
  void setFlagsyn();
  void setFlagfin();
    
  //get functions
  uint16_t getSeqnum();
  uint16_t getAcknum();
  uint16_t getRcvwin();
  bool getFlagack();
  bool getFlagsyn();
  bool getFlagfin();
  unsigned char* getData();
};

//constructor
segment::segment(){
  header.seqNo = 0x0000;
  header.ackNo = 0x0000;
  header.rcvWin = 0x7800;
  header.reserved = 0x00;
  header.flags = 0x00;
  memset(buffer, 0, MSS+1);
  //buffer[MSS-1] = '\0';
}

//encode and decode
//input is data, output is the tcp segment
unsigned char* segment::encode(unsigned char* payload, int n){
    
  if(n > (MSS - HEADERSIZE)){
    error("Input data excess the max segment size");
  }
    
  //encode the header
  unsigned char tmp[8];
  //bitset<16> b(565);
  //uint8_t b_u8 = b.to_ulong()>>8 & 0xff;
  //unsigned char b_c = b_u8;
  //bitset<8> b2(b_u8);
  bitset<16> seq(header.seqNo);
  bitset<16> ack(header.ackNo);
  bitset<16> win(header.rcvWin);
  bitset<8> flag(header.flags);
    
  tmp[0] = (seq.to_ulong() >> 8) & 0xFF;
  tmp[1] = (seq.to_ulong()) & 0xFF;
  tmp[2] = (ack.to_ulong() >> 8) & 0xFF;
  tmp[3] = (ack.to_ulong()) & 0xFF;
  tmp[4] = (win.to_ulong() >> 8) & 0xFF;
  tmp[5] = (win.to_ulong()) & 0xFF;
  tmp[6] = (header.reserved) & 0xFF;
  tmp[7] = (flag.to_ulong()) & 0xFF;
  memcpy((char*)buffer, (char*)tmp, HEADERSIZE);
    
  //encode data
  if(!(payload == NULL || n==0)){
    memcpy((char*) buffer+HEADERSIZE, (char*) payload, n);
  }
    
  //return encoded result
  return buffer;
}

void segment::decode(unsigned char* buf, int n){
  if(n > (MSS)){
    error("Input data excess the max segment size");
  }
    
  //decode the header
    
  header.seqNo = buf[1] + (buf[0]<<8);
  //cout<<"seqNo is "<<header.seqNo<<endl;
  header.ackNo = buf[3] + (buf[2]<<8);
  header.rcvWin = buf[5] + (buf[4]<<8);
  header.reserved = buf[6];
  header.flags = buf[7];
    
  //decode data
  memcpy((char*)buffer, (char*)buf, n);
  //buffer[n] = '\0';
}

//set functions
void segment::setSeqnum(uint16_t seq){
  header.seqNo = seq;
}

void segment::setAcknum(uint16_t ack){
  header.ackNo = ack;
}

void segment::setRcvwin(uint16_t rcv){
  header.rcvWin = rcv;
}

void segment::setFlagack(){
  header.flags |= 0x04;
}

void segment::setFlagsyn(){
  header.flags |= 0x02;
}

void segment::setFlagfin(){
  header.flags |=0x01;
}

//get functions
uint16_t segment::getSeqnum(){
  return header.seqNo;
}

uint16_t segment::getAcknum(){
  return header.ackNo;
}

uint16_t segment::getRcvwin(){
  return header.rcvWin;
}

bool segment::getFlagack(){
  if(header.flags & 0x04){
    return true;
  }
  return false;
}

bool segment::getFlagsyn(){
  if(header.flags & 0x02){
    return true;
  }
  return false;
}

bool segment::getFlagfin(){
  if(header.flags & 0x01){
    return true;
  }
  return false;
}

unsigned char* segment::getData(){
  return buffer+HEADERSIZE;
}

void debugaux(unsigned char ch)
{
  for (int i = 7; i >=0 ; i--)
    {
      int out = 0x01 & (ch >> i);
      cout << out;
    }
  cout << endl;
}

void debug(unsigned char array[])
{
  for (int i = 0; i < 8; i++)
    {
      debugaux(array[i]);
    }
}

/* sender: the segment received
 receiver: the segment to be sent
 n: the number of ack to be increased
*/
void setReplyAck(segment &sender, segment &receiver, uint16_t n)
{
  uint16_t seqnum = sender.getSeqnum();
  uint16_t acknum = (seqnum+n) % MAX_SEQ_NUM;
  receiver.setAcknum(acknum);
  receiver.setFlagack();
}

uint16_t seq_rand(uint16_t max)
{
  struct timeval time; 
  gettimeofday(&time,NULL);
  srand((time.tv_sec * 1000) + (time.tv_usec / 1000));
  return rand()%max;
}
