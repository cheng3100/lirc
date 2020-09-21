LIRC
====

Light IRC room application writed by C. Include a server and client.

## Build

Run GNU make in the repository

`make`

## Usage

Firstly start a irc server.

`./server <server-port>`

Then connect a irc client to the server.

`./client <server-ip> <server-port>`


## Features

* Accept multiple client (max 100)
* Send private messages
* support chat command.

## Chat commands

| Command       | Parameter             |                                     |
| ------------- | --------------------- | ----------------------------------- |
| /quit         |                       | Leave the chatroom                  |
| /rname        | [chatname]            | Set the chatroom name               |
| /uname        | [nickname]            | Change user name                    |
| /msg          | [uid]                 | Enter private mode                  |
| /com          |                       | Enter common mode					  |
| /list         |                       | Show active clients                 |
| /help         |                       | Show this help                      |


