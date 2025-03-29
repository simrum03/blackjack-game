# Blackjack game

## Table of Contents
1. [Introduction](#introduction)
2. [Program Operation](#program-operation)
3. [Gameplay](#gameplay)
4. [Ranking](#ranking)
5. [Compilation and Execution](#compilation-and-execution)

---

## Introduction
The project implements a multiplayer Blackjack card game, where the server manages the gameplay and clients act as players. The server handles multiple simultaneous connections and allows individual games. It runs as a background daemon and broadcasts its address using multicast. Client names are tracked, and the server provides a ranking of wins, draws, and losses.

---

## Program Operation
Every 5 seconds, the server sends its IP address (IPv4 or IPv6) via multicast:
- IPv4: 239.255.255.250
- IPv6: ff02::1
The client listens for the server's multicast address and connects via TCP upon receiving it.

Upon connection, the client sees:
```
Successfully connected using IPvX
Connected to server at <server address>
```
The server then asks the client for a name and presents three options:
```
Welcome, <name>! Choose an option:
1. Play Blackjack
2. View Rankings
3. Exit
```
The game result is recorded in the ranking after completion.

---

## Gameplay
Rules follow standard Blackjack. Players draw cards until they choose to "stand" or exceed 21 points (bust). The dealer (server) draws cards after the player's turn.
Possible outcomes:
- Win - Player's score is higher than the dealer's.
- Loss - Player's score is lower than the dealer's.
- Draw - Both scores are equal.

---

## Ranking
The ranking is displayed as:
```
<name> - W: x, D: y, L: z
```
The server remembers results after re-login.

---

## Compilation and Execution
### Configure rsyslog daemon:
1. Add the following to the config file:
```
blackjack.* /var/log/blackjack
```
2. Create the log file:
```
sudo touch /var/log/blackjack
sudo chmod a+rwx /var/log/blackjack
```
3. Restart rsyslog:
```
sudo service rsyslog restart
```
### Compile the server:
```
gcc server_blackjack.c -o server -pthread
```
### Compile the client:
```
gcc client_blackjack.c -o client
```
### Run the server:
```
./server <ip version>
```
### Run the client:
```
./client
```
