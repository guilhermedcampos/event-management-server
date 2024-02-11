# EMS Server

This program consists in a Operating Systems project designed to explore interaction with named pipes, interaction via signals send by console, multi-threading, synchronization, and file programming with POSIX. The primary goal is to read inputs from files, produce outputs, and allow the configuration of the number of active processes and threads for each process. This project has both a server and client side, in contrary to the other EMS project. Each client is allowed to send a request, represented by a .jobs file. Multiple clients can access the server at the same time, but each client can only send one request at a time. The server reads the request through pipes and writes to the client through response pipes the expected output for a specific request file. The Event Management Serever provides a foundation for managing events and reservations. It supports commands to create events, reserve seats, show the current state of events, list events, introduce delays, and synchronizes it's threads with the use of a cyclic producer consumer buffer to handle a queue of requests. As previously mentioned, the pipes allow the client and server to communicate the input/output and also the state of requests.  
The signals were employed for additional interaction with the server to trigger a specific event. We made a minor enhancement by introducing my_read and my_write functions, which are resilient to signals. These functions are designed to resume their actions if they're interrupted by signals during execution, ensuring they don't terminate abruptly.

## Prerequisites

 **gcc:** The GNU Compiler Collection.
 
 **make**


## Installation

To install and run the EMS server, follow these steps:

1. Clone the repository:

    ```bash
    git clone https://github.com/guilhermedcampos/event-management-server.git
    ```

2. Navigate to the project directory and compile the source code:

    ```bash
    cd src
    make
    ```
3. Run the server in a terminal:

    ```bash
    ./server/ems <server pipe path>
    ```

4. Once finished, run make clean. Since the server pipe does not have a logic to finish (infinite loop), its advised to add "rm -f <server pipe path>*" so the server pipe is cleaned after a make clean.

    ```bash
    make clean
    ```

## Client Interaction

Clients can send requests to the server by opening a terminal and sending the following command:

    ```bash
    ./client/client <request pipe path> <response pipe path> <server pipe path> <.jobs file path>
    ```

Example of usage: 

    ```bash
    ./server/ems my_pipe
    ./client/client p1 p2 my_pipe jobs/a.jobs 
    ```

We included a folder with some examples of requests clients may make (/src/jobs). Consult the Command Syntax section to create your own requests.

## Sending signals

Our server allows clients to send a SIGUSR1 to the server, that will fire a command that prints all current events in the server's event list.
To send the SIGUSR 1, find the id of server's pipe by doing:

    ```bash
    ps aux | grep server/ems
    ```

The id is the first integer found in the output. In sequence, type:

    ```bash
    kill -SIGUSR1 <process id>
    ```

## Command Syntax

The program parses the following commands in the input files:

    CREATE <event_id> <num_rows> <num_columns>
    
        Create a new event with a specified ID, number of rows, and number of columns.
        CREATE 1 10 20
    
    RESERVE <event_id> [(<x1>,<y1>) (<x2>,<y2>) ...]
    
        Reserve one or more seats in an existing event.
        RESERVE 1 [(1,1) (1,2) (1,3)]
    
    SHOW <event_id>
    
        Print the current state of all seats in an event.
        SHOW 1
    
    LIST
    
        List all created events.
        LIST
    
    WAIT <delay>
    
        Introduce a delay in seconds.
        WAIT 2
    
    HELP
    
        Display information about available commands.
        HELP

## Future features

One thing we would like to add to our program is a logic that closes all the client's pipes when a CTRL-C is sent via terminal. As it stands, when a client disconnects suddently with the use of a CTRL-C, the pipes remain zombie in the server's folder. A logic that catches a SIGINT and closes the client's pipes before exiting would be a great addition.
