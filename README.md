# S16_CS118_project2

Overview
The purpose of this project is to learn the basics of TCP protocol, including connection establishment and congestion control. You will need to implement a simple data transfer client and server applications, using UDP sockets as an unreliable transport. Your client should receive data even though some packets are dropped or delayed.

Task Description
The developed client should establish connection to the developed server, after which the server will need to transfer data to the client. Individual packets sent by the client and the server may be lost or reordered. Therefore, your task is to make reliable data transfer by implementing parts of TCP such as the sequence number, the acknowledgement, and the congestion control.

The project will be graded by testing transfer a single file over the unreliable link. Your second project does not need to be extended from your first project (if you extend, you will get extra credit, see below). Instead, you can simply implement a single threaded server that starts data transfer as soon as the connection is established.


Command-line specification for client and server program:

  ./client SERVER-HOST-OR-IP PORT-NUMBER

  ./server PORT-NUMBER FILE-NAME