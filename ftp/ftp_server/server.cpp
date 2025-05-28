#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string>
#include <cstring>
#include <pthread.h>
#include <map>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <vector>
#include <algorithm> // For std::replace
#include <netdb.h>   // For gethostname, gethostbyname

#define CONTROL_PORT 2121 // FTP 控制连接端口

namespace fs = std::filesystem; // For C++17 filesystem operations

// Simple user database
std::map<std::string, std::string> users = {
    {"user1", "pass1"},
    {"admin", "adminpass"},
    {"root","12345"}
};

// Structure to hold client-specific state
struct ClientState {
    int control_socket;
    std::string current_dir;
    bool logged_in;
    std::string username;
    // Data connection related
    int data_socket_fd; // For active mode: client's accepted data socket; for passive mode: server's listening data socket
    std::string data_ip; // For active mode: client's IP to connect to
    int data_port;       // For active mode: client's port to connect to
    bool passive_mode;   // true for PASV (server listens), false for PORT (server connects to client)

    ClientState() : control_socket(-1), current_dir(""), logged_in(false), username(""),
                    data_socket_fd(-1), data_ip(""), data_port(0), passive_mode(false) {}
};

// Map to store client states, protected by a mutex
std::map<int, ClientState> client_states;
pthread_mutex_t client_states_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex for client_states map

// Function to send a response to the client
void send_response(int client_socket, const std::string& response) {
    send(client_socket, response.c_str(), response.length(), 0);
}

// Function to open a data connection for active mode (server connects to client)
int open_active_data_connection(int control_socket, const std::string& client_ip, int client_port) {
    int data_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (data_sock == -1) {
        perror("data socket creation failed");
        send_response(control_socket, "425 Can't open data connection.\r\n");
        return -1;
    }

    sockaddr_in data_addr;
    data_addr.sin_family = AF_INET;
    data_addr.sin_port = htons(client_port);
    if (inet_pton(AF_INET, client_ip.c_str(), &data_addr.sin_addr) <= 0) {
        send_response(control_socket, "425 Invalid IP address for data connection.\r\n");
        close(data_sock);
        return -1;
    }

    if (connect(data_sock, (struct sockaddr *)&data_addr, sizeof(data_addr)) < 0) {
        perror("data connect failed");
        send_response(control_socket, "425 Can't open data connection.\r\n");
        close(data_sock);
        return -1;
    }
    return data_sock;
}

// Function to set up a listener for passive mode (server listens, client connects to server)
int open_passive_data_listener(int control_socket, std::string& server_ip, int& data_port) {
    int data_listener_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (data_listener_sock == -1) {
        send_response(control_socket, "421 Service not available, can't open data listener.\r\n");
        return -1;
    }

    // Allow reuse of address and port
    int opt = 1;
    if (setsockopt(data_listener_sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        perror("setsockopt passive listener failed");
        close(data_listener_sock);
        send_response(control_socket, "421 Service not available, setsockopt failed.\r\n");
        return -1;
    }

    sockaddr_in data_addr;
    data_addr.sin_family = AF_INET;
    data_addr.sin_addr.s_addr = INADDR_ANY;
    data_addr.sin_port = 0; // Let the system assign an ephemeral port

    if (bind(data_listener_sock, (struct sockaddr *)&data_addr, sizeof(data_addr)) < 0) {
        perror("passive bind failed");
        send_response(control_socket, "421 Service not available, bind failed.\r\n");
        close(data_listener_sock);
        return -1;
    }

    socklen_t addr_len = sizeof(data_addr);
    getsockname(data_listener_sock, (struct sockaddr*)&data_addr, &addr_len);
    data_port = ntohs(data_addr.sin_port);

    if (listen(data_listener_sock, 1) < 0) { // Only allow one pending connection for passive mode
        perror("passive listen failed");
        send_response(control_socket, "421 Service not available, listen failed.\r\n");
        close(data_listener_sock);
        return -1;
    }

    // Get server's IP address (important for NAT scenarios)
    char hostbuffer[256];
    gethostname(hostbuffer, sizeof(hostbuffer));
    struct hostent *host_entry;
    host_entry = gethostbyname(hostbuffer);
    if (host_entry == NULL) {
        perror("gethostbyname failed");
        send_response(control_socket, "421 Service not available, cannot determine server IP.\r\n");
        close(data_listener_sock);
        return -1;
    }
    server_ip = inet_ntoa(*((struct in_addr*)host_entry->h_addr_list[0]));

    // Format the PASV response: 227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)
    // IP: h1.h2.h3.h4, Port: p1*256 + p2
    std::string ip_str = server_ip;
    std::replace(ip_str.begin(), ip_str.end(), '.', ',');
    int p1 = data_port / 256;
    int p2 = data_port % 256;
    std::string pasv_response = "227 Entering Passive Mode (" + ip_str + "," + std::to_string(p1) + "," + std::to_string(p2) + ").\r\n";
    send_response(control_socket, pasv_response);

    return data_listener_sock;
}

// Function to accept a data connection for passive mode
int accept_passive_data_connection(int data_listener_sock) {
    sockaddr_in client_data_addr;
    socklen_t client_data_addrlen = sizeof(client_data_addr);
    int new_data_sock = accept(data_listener_sock, (struct sockaddr*)&client_data_addr, &client_data_addrlen);
    if (new_data_sock < 0) {
        perror("accept passive data connection failed");
        return -1;
    }
    return new_data_sock;
}

// Handle LIST command using data connection
void handle_list_data(ClientState& client_state) {
    int data_sock = -1;
    if (client_state.passive_mode) {
        send_response(client_state.control_socket, "150 Opening ASCII mode data connection for file list.\r\n");
        data_sock = accept_passive_data_connection(client_state.data_socket_fd);
        close(client_state.data_socket_fd); // Close the listener after accepting
        client_state.data_socket_fd = -1; // Reset to indicate listener is closed
    } else { // Active mode
        send_response(client_state.control_socket, "150 Opening ASCII mode data connection for file list.\r\n");
        data_sock = open_active_data_connection(client_state.control_socket, client_state.data_ip, client_state.data_port);
    }

    if (data_sock == -1) {
        // Error response already sent by open_active_data_connection or accept_passive_data_connection
        return;
    }

    std::string file_list;
    try {
        for (const auto& entry : fs::directory_iterator(client_state.current_dir)) {
            file_list += entry.path().filename().string() + "\r\n";
        }
        send(data_sock, file_list.c_str(), file_list.length(), 0);
        send_response(client_state.control_socket, "226 Directory send OK.\r\n");
    } catch (const fs::filesystem_error& e) {
        send_response(client_state.control_socket, "550 Failed to list directory.\r\n");
    }
    close(data_sock);
}

// Handle RETR command using data connection
void handle_retr_data(ClientState& client_state, const std::string& filename) {
    int data_sock = -1;
    if (client_state.passive_mode) {
        send_response(client_state.control_socket, "150 Opening BINARY mode data connection for " + filename + ".\r\n");
        data_sock = accept_passive_data_connection(client_state.data_socket_fd);
        close(client_state.data_socket_fd);
        client_state.data_socket_fd = -1;
    } else {
        send_response(client_state.control_socket, "150 Opening BINARY mode data connection for " + filename + ".\r\n");
        data_sock = open_active_data_connection(client_state.control_socket, client_state.data_ip, client_state.data_port);
    }

    if (data_sock == -1) {
        return;
    }

    std::string full_path = client_state.current_dir + "/" + filename;
    std::ifstream file(full_path, std::ios::binary);
    if (file.is_open()) {
        char buffer[4096];
        while (!file.eof()) {
            file.read(buffer, sizeof(buffer));
            ssize_t bytes_sent = send(data_sock, buffer, file.gcount(), 0);
            if (bytes_sent < 0) {
                perror("send file data failed");
                send_response(client_state.control_socket, "426 Connection closed; transfer aborted.\r\n");
                break;
            }
        }
        file.close();
        send_response(client_state.control_socket, "226 Transfer complete.\r\n");
    } else {
        send_response(client_state.control_socket, "550 Failed to open file.\r\n");
    }
    close(data_sock);
}

// Handle STOR command using data connection
void handle_stor_data(ClientState& client_state, const std::string& filename) {
    int data_sock = -1;
    if (client_state.passive_mode) {
        send_response(client_state.control_socket, "150 Ok to send data.\r\n");
        data_sock = accept_passive_data_connection(client_state.data_socket_fd);
        close(client_state.data_socket_fd);
        client_state.data_socket_fd = -1;
    } else {
        send_response(client_state.control_socket, "150 Ok to send data.\r\n");
        data_sock = open_active_data_connection(client_state.control_socket, client_state.data_ip, client_state.data_port);
    }

    if (data_sock == -1) {
        return;
    }

    std::string full_path = client_state.current_dir + "/" + filename;
    std::ofstream file(full_path, std::ios::binary);
    if (file.is_open()) {
        char buffer[4096];
        int bytes_received;
        while ((bytes_received = recv(data_sock, buffer, sizeof(buffer), 0)) > 0) {
            file.write(buffer, bytes_received);
        }
        if (bytes_received < 0) {
             perror("recv file data failed");
             send_response(client_state.control_socket, "426 Connection closed; transfer aborted.\r\n");
        } else {
            send_response(client_state.control_socket, "226 Transfer complete.\r\n");
        }
        file.close();
    } else {
        send_response(client_state.control_socket, "550 Failed to create file.\r\n");
    }
    close(data_sock);
}

// Handle PWD command
void handle_pwd(int client_socket, const std::string& current_dir) {
    std::string response = "257 \"" + current_dir + "\" is current directory.\r\n";
    send_response(client_socket, response);
}

// Handle CWD command
void handle_cwd(int client_socket, std::string& current_dir, const std::string& path) {
    fs::path new_path = fs::path(current_dir) / path; // Resolve relative path first

    // Attempt to canonicalize the path, handling ".." and symlinks
    try {
        fs::path canonical_path = fs::canonical(new_path);
        if (fs::is_directory(canonical_path)) {
            current_dir = canonical_path.string();
            send_response(client_socket, "250 Directory successfully changed.\r\n");
        } else {
            send_response(client_socket, "550 Not a directory.\r\n");
        }
    } catch (const fs::filesystem_error& e) {
        send_response(client_socket, "550 Failed to change directory: " + std::string(e.what()) + "\r\n");
    }
}

// Thread function to handle a single client connection
void *client_handler_thread(void *socket_desc) {
    int client_socket = *(int*)socket_desc;
    delete (int*)socket_desc; // Free the dynamically allocated memory

    // Initialize client state
    pthread_mutex_lock(&client_states_mutex);
    client_states[client_socket] = ClientState(); // Use default constructor for initialization
    client_states[client_socket].control_socket = client_socket;
    client_states[client_socket].current_dir = fs::current_path().string(); // Set initial directory
    ClientState& client_state = client_states[client_socket]; // Get a reference to the client's state
    pthread_mutex_unlock(&client_states_mutex);

    char buffer[1024] = {0};
    send_response(client_socket, "220 Welcome to Simple FTP Server.\r\n");

    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int valread = recv(client_socket, buffer, 1024, 0);
        if (valread <= 0) {
            std::cout << "Client " << client_socket << " disconnected." << std::endl;
            break;
        }

        std::string command_line(buffer);
        // Remove trailing \r\n
        if (!command_line.empty() && command_line.back() == '\n') command_line.pop_back();
        if (!command_line.empty() && command_line.back() == '\r') command_line.pop_back();

        std::cout << "Client " << client_socket << " Received: " << command_line << std::endl;

        std::string command_name;
        std::string argument;
        size_t first_space = command_line.find(' ');
        if (first_space != std::string::npos) {
            command_name = command_line.substr(0, first_space);
            argument = command_line.substr(first_space + 1);
        } else {
            command_name = command_line;
        }

        // Convert command name to uppercase for case-insensitive comparison
        for (char &c : command_name) {
            c = toupper(c);
        }

        if (command_name == "USER") {
            client_state.username = argument;
            send_response(client_socket, "331 User name okay, need password.\r\n");
        } else if (command_name == "PASS") {
            if (users.count(client_state.username) && users[client_state.username] == argument) {
                client_state.logged_in = true;
                send_response(client_socket, "230 User logged in, proceed.\r\n");
            } else {
                client_state.logged_in = false;
                send_response(client_socket, "530 Not logged in.\r\n");
            }
        } else if (command_name == "QUIT") {
            send_response(client_socket, "221 Goodbye.\r\n");
            break;
        } else if (!client_state.logged_in && !(command_name == "USER" || command_name == "PASS")) {
            send_response(client_socket, "530 Not logged in.\r\n");
        } else if (command_name == "PORT") {
            // Parse PORT command: PORT h1,h2,h3,h4,p1,p2
            std::vector<int> parts;
            std::stringstream ss(argument);
            std::string segment;
            while(std::getline(ss, segment, ',')) {
                parts.push_back(std::stoi(segment));
            }
            if (parts.size() == 6) {
                client_state.data_ip = std::to_string(parts[0]) + "." + std::to_string(parts[1]) + "." + std::to_string(parts[2]) + "." + std::to_string(parts[3]);
                client_state.data_port = parts[4] * 256 + parts[5];
                client_state.passive_mode = false; // Set to active mode
                send_response(client_socket, "200 PORT command successful.\r\n");
            } else {
                send_response(client_socket, "501 Syntax error in parameters or arguments.\r\n");
            }
        } else if (command_name == "PASV") {
            // In PASV, server listens for a connection
            std::string server_ip;
            int passive_port = 0;
            int data_listener_sock = open_passive_data_listener(client_socket, server_ip, passive_port);
            if (data_listener_sock != -1) {
                client_state.data_socket_fd = data_listener_sock; // Store the listener socket
                client_state.passive_mode = true; // Set to passive mode
            }
            // Response already sent by open_passive_data_listener
        }
        // Data transfer commands now use data connection
        else if (command_name == "LIST" || command_name == "NLST" || command_name == "DIR") {
            // Check if data connection details are set (either PORT or PASV was used)
            if (client_state.data_socket_fd != -1 || (!client_state.passive_mode && !client_state.data_ip.empty() && client_state.data_port != 0)) {
                handle_list_data(client_state);
            } else {
                send_response(client_socket, "425 Use PORT or PASV first.\r\n");
            }
        } else if (command_name == "RETR") {
            if (client_state.data_socket_fd != -1 || (!client_state.passive_mode && !client_state.data_ip.empty() && client_state.data_port != 0)) {
                handle_retr_data(client_state, argument);
            } else {
                send_response(client_socket, "425 Use PORT or PASV first.\r\n");
            }
        } else if (command_name == "STOR") {
            if (client_state.data_socket_fd != -1 || (!client_state.passive_mode && !client_state.data_ip.empty() && client_state.data_port != 0)) {
                handle_stor_data(client_state, argument);
            } else {
                send_response(client_socket, "425 Use PORT or PASV first.\r\n");
            }
        }
        // Other non-data transfer commands
        else if (command_name == "PWD" || command_name == "XPWD") {
            handle_pwd(client_socket, client_state.current_dir);
        } else if (command_name == "CWD" || command_name == "XCWD") {
            handle_cwd(client_socket, client_state.current_dir, argument);
        } else if (command_name == "SYST") {
            send_response(client_socket, "215 UNIX Type: L8\r\n"); // Standard response for SYST
        } else if (command_name == "TYPE") {
            // TYPE A (ASCII) or TYPE I (Binary Image)
            if (argument == "A" || argument == "I") {
                send_response(client_socket, "200 Type set to " + argument + ".\r\n");
            } else {
                send_response(client_socket, "504 Command not implemented for that parameter.\r\n");
            }
        }
        else {
            send_response(client_socket, "500 Unknown command.\r\n");
        }
    }
    // Clean up client state on disconnect
    pthread_mutex_lock(&client_states_mutex);
    if (client_state.data_socket_fd != -1) {
        close(client_state.data_socket_fd);
    }
    client_states.erase(client_socket);
    pthread_mutex_unlock(&client_states_mutex);
    close(client_socket);
    pthread_exit(NULL);
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    // Create socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Forcefully attach socket to the port, even if in TIME_WAIT state
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Listen on all available interfaces
    address.sin_port = htons(CONTROL_PORT);

    // Bind the socket to the specified port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_fd, 5) < 0) { // Max 5 pending connections
        perror("listen");
        exit(EXIT_FAILURE);
    }

    std::cout << "FTP Server listening on port " << CONTROL_PORT << std::endl;

    while (true) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            // Non-fatal error, continue listening for other connections
            continue;
        }
        std::cout << "New connection accepted from " << inet_ntoa(address.sin_addr) << ":" << ntohs(address.sin_port) << std::endl;

        pthread_t thread_id;
        int *p_new_sock = new int; // Dynamically allocate memory for socket descriptor
        *p_new_sock = new_socket;

        if (pthread_create(&thread_id, NULL, client_handler_thread, (void*)p_new_sock) < 0) {
            perror("could not create thread");
            close(new_socket); // Close the socket if thread creation fails
            delete p_new_sock; // Free allocated memory
            continue;
        }
        pthread_detach(thread_id); // Detach the thread to clean up resources automatically
    }

    close(server_fd);
    return 0;
}