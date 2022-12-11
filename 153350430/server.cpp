/*
 * File for implementation of the server
 * CSF Assignment 5
 * Madeline Estey (mestey1@jhu.edu)
 * Owen Reed (oreed2@jhu.edu)
 */

#include <pthread.h>
#include <iostream>
#include <sstream>
#include <memory>
#include <set>
#include <vector>
#include <cctype>
#include <cassert>
#include "message.h"
#include "connection.h"
#include "user.h"
#include "room.h"
#include "guard.h"
#include "server.h"
#include "csapp.h"




////////////////////////////////////////////////////////////////////////
// Server implementation data types
////////////////////////////////////////////////////////////////////////

struct ConnInfo {
  int clientfd;
  Server *server;
};

////////////////////////////////////////////////////////////////////////
// Client thread functions
////////////////////////////////////////////////////////////////////////

namespace {


/*
Worker function for doing threads, will call ether chat with sender or recv. inits a connection.
*/
void *worker(void *arg) {
  //      use a static cast to convert arg from a void* to
  //       whatever pointer type describes the object(s) needed
  //       to communicate with a client (sender or receiver)
  int detach_result = pthread_detach(pthread_self());
  if (detach_result != 0) {
    std::cerr << "Error detaching thread: " << detach_result << std::endl;
    // Handle error here for failed detach
  }
  struct ConnInfo *info = (ConnInfo*) arg;
  //        read login message (should be tagged either with
  //       TAG_SLOGIN or TAG_RLOGIN), send response
  Connection* conn = new Connection((info->clientfd));
  char message[550] = "FAILED:To Send";
  bool receive_result = conn->receive(message);
  if (receive_result == false) { //handle error for send
    std::cerr << "Error receiving message from client" << std::endl;
    conn->close();
    delete info;
    return NULL;
  }
  //strip and format message
  std::string formatted_message(message);
  std::string delimiter = ":";
  std::string tag = formatted_message.substr(0, formatted_message.find(delimiter)); 
  std::string username = formatted_message.substr(formatted_message.find(delimiter) + 1, formatted_message.length()); 
  //       depending on whether the client logged in as a sender or
  //       receiver, communicate with the client (implementing
  //       separate helper functions for each of these possibilities
  //       is a good idea)
  if(tag == "rlogin") { //parse login message tag for recv
    if (!conn->send("ok:hello")) {//message ok because we got a good login
      std::cerr << "Error sending message to client recv" << std::endl;
      conn->close();
      delete info;
      return NULL;
    }
    User *user = new User(username,false); //init user as recv
    info->server->chat_with_receiver(user,info->clientfd,conn); //move into recv loop
    delete user;
  } else if(tag == "slogin") { //parse login message tag for sender
    if (!conn->send("ok:hello")) {//message ok because we got a good login
      std::cerr << "Error sending message to client sender" << std::endl;
      conn->close();
      delete conn;
      delete info;
      return NULL;
    }
    User *user = new User(username,true); //init user as sender
    info->server->chat_with_sender(user,info->clientfd,conn); //move into sender loop
    delete user;
  } else {
    //error with bad tag case
    conn->send("err:bad_login");
    std::cerr << "BAD FIRST TAG: " << tag << std::endl;
    conn->close();
    delete conn;
    delete info;
    return NULL;
  }
  conn->close();
  delete conn;
  delete info;
  return NULL;
}
}

////////////////////////////////////////////////////////////////////////
// Server member function implementation
////////////////////////////////////////////////////////////////////////

/*
Server constructor by port
*/
Server::Server(int port)
  : m_port(port)
  , m_ssock(-1) {
  // initialize mutex 
  pthread_mutex_init(&m_lock, NULL); //pthread_mutex_t *, const pthread_mutexattr_t *_Nullable
}


/*
Server destructor needs to kill mutex and the socket connection.
*/
Server::~Server() {
  // destroy mutex
  pthread_mutex_destroy(&m_lock);
  close(m_ssock);
}

/*
Listen function inits and open listen fd to for the server.
*/
bool Server::listen() {
  //Use open_listenfd to create the server socket, return true
  //If successful, false if not
  // Convert number to a string
  std::string temp_port = std::to_string(this->m_port); 
  // Convert string to char Array
  char const* port = temp_port.c_str();
  //Open socket connection and verify correctness 
  this->m_ssock = open_listenfd(port);
  if (m_ssock < 0) {
    return false;
  }
  return true;
}


/*
Function to manage all client requests, calls accept in a loop. Creates new threads.
*/
void Server::handle_client_requests() {
  //Infinite loop calling accept or Accept, starting a new
  //Pthread for each connected client
  //How to deal w serverfd
  while(1) { // should we instead check when max number of connections is reached?
    int clientfd = Accept(m_ssock, NULL, NULL);
    if (clientfd < 0) {
      std::cerr << "Could Not Accept Server\n";
    } else {
      // create struct to pass the connection object and 
      // other data to the client thread using the aux parameter
      // of pthread_create
      struct ConnInfo *info = new ConnInfo();
      info->clientfd = clientfd;
      info->server = this;

      /* start new thread to handle client connection */
      pthread_t thr_id;
      if (pthread_create(&thr_id, NULL, worker, info) != 0) {
        std::cerr << "Error\n";
        delete info;
      }
    }
  }
  // Create a user object in each client thread to track the pending messages
  // and register it to a room when client sends join request
}


/*
Helper function to either find a room object or create a new one if need be.
*/
Room *Server::find_or_create_room(const std::string &room_name) {
  //return a pointer to the unique Room object representing
  //the named chat room, creating a new one if necessary
  //checks to see if a room exits in the map
  if (m_rooms.count(room_name)>0) {
    //If it does we return the room
    return m_rooms[room_name]; 
  } else { //if it doesnt
    Room* new_room = new Room(room_name);
    //locks the rooom map
    Guard g(m_lock);
    //create a new room by constructor w/ roomname
    m_rooms[room_name] = new_room; 
    return new_room;
  }
}




/*
Function to manage chat with sender. Loops the convo until the sender quits.
*/
void Server::chat_with_sender(User *user, int client_fd, Connection* conn) {
  // see sequence diagrams in part 1 for how to implement
  // terminate the loop and tear down the client thread if any message fails to send
  bool convo_valid = true;
  Room *cur_room = nullptr;
  while (convo_valid) { //server main loop
    //take message in
    char message[550]; 
    conn->receive(message);
    //format and split message
    std::string formatted_message(message);
    std::string new_delimiter = ":";
    std::string new_tag = formatted_message.substr(0, formatted_message.find(new_delimiter));
    std::string content = formatted_message.substr(formatted_message.find(new_delimiter) + 1, formatted_message.length()); 
    //parese tag
    if (new_tag == "join") {
      //process join room
      this->leave(user,cur_room); //leave current room before we join another
      cur_room = nullptr; //validate that currently not in a room
      cur_room = this->join(user,content); //join new room
      if (cur_room != nullptr) {
        conn->send("ok:good join");
      } else {
        conn->send("err:failed to join");
      }
    } else if (new_tag == "sendall") {
      //process sendall 
      bool success = this->sendall(user,cur_room,content);
      if (success) {
        conn->send("ok:it is sent");
      } else {
        conn->send("err:failed to send");
      }
    } else if (new_tag == "leave") {
      //process leave
      bool success = this->leave(user,cur_room); 
      if (success) {
        cur_room = nullptr;
        conn->send("ok:ya gone");
      } else {
        conn->send("err:not in a room");
      }
    } else if (new_tag == "quit") {
      //process quit
      bool success = this->quit(user,cur_room); 
      if (success) {
        cur_room = nullptr;
        conn->send("ok:no whyyyyy");
        convo_valid = false;
      } else {
        conn->send("err:quit failed");
      }
    } else { //Error case for bad tag input
      std::cerr<<"BAD TAG"<<std::endl;
      this->quit(user,cur_room); 
      conn->send("err:ur bad my guy");
      convo_valid = false;
    }
  }
  conn->close(); 
}


  Room *Server::join(User *user,std::string room_name) {
    Room *target_room = find_or_create_room(room_name);
    target_room->add_member(user);
    return target_room;
  }

bool Server::sendall(User *user, Room *cur_room,std::string message) {
  //  std::cout << "in send" <<std::endl;
  if ((cur_room == nullptr) || (m_rooms.count(cur_room->get_room_name()) <= 0)) { //checks to see if a room exits in the map
    return false; //if it does we return the room
  } else {
    cur_room->broadcast_message(user->username,message);
    return true;
  }
}


bool Server::leave(User *user, Room *cur_room) {
  if ((cur_room == nullptr) || (m_rooms.count(cur_room->get_room_name()) <= 0)) { //checks to see if a room exits in the map
    return false; //if it does we return the room
  } else {
    cur_room->remove_member(user);
    return true;
  }
}


bool Server::quit(User *user, Room *cur_room) {
  leave(user,cur_room);
  delete user;
  return true;
}

/*
Function to manage chatting with a receiver.
*/
void Server::chat_with_receiver(User *user, int client_fd, Connection* conn) {
  // terminate the loop and tear down the client thread if any message transmission fails, or if a valid quit message is received
  bool convo_valid = true;
  Room *cur_room = nullptr;
  char message[550];
  conn->receive(message);
  std::string formatted_message(message);
  std::string new_delimiter = ":";
  std::string new_tag = formatted_message.substr(0, formatted_message.find(new_delimiter));
  std::string content = formatted_message.substr(formatted_message.find(new_delimiter) + 1, formatted_message.length()); 
  if (new_tag == "join") {
    //process join room
    cur_room = this->join(user,content);
    conn->send("ok:good join");
  } else {
    conn->send("err:bad join tag");
    conn->close();
    this->quit(user,cur_room);
    return;
  } 
  while (convo_valid) {
    Message *message_to_send = user->mqueue.dequeue();
    if (message_to_send != nullptr) {
      std::string message_as_string = message_to_send->tag+":"+message_to_send->data;
      delete message_to_send;
      convo_valid = conn->send(message_as_string.c_str());
    }
  }
  conn->close();
  this->quit(user,cur_room);
  return;
}




