# EMS Server

This project implements the EMS server, which allows interaction with clients through named pipes and supports signal interaction. The signals were employed for interaction with the server to trigger a specific event. This project has both a server and client side. The program's logic supports multiple threads (producer consumer buffer) with process.  

## Installation

To install and run the EMS server, follow these steps:

1. Clone the repository:

    ```bash
    git clone https://github.com/guilhermedcampos/event-management-server.git
    ```

2. Navigate to the project directory:

    ```bash
    cd ems
    ```

3. Compile the source code:

    ```bash
    make
    ```

## Getting Started


- **C Programming**: The server is written in C.

- **Named Pipes**: Interaction with client processes is facilitated through named pipes.

- **POSIX Threads (pthread)**: Used for implementing multi-threaded server logic.

- **Signals (SIGUSR1)**: Employed for interaction with the server to trigger event printing.

## Contributing

Contributions are welcome! Feel free to submit issues or pull requests.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.