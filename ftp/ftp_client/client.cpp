#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm> // For std::replace
#include <random>    // For random port generation (for active mode)
#include <chrono>    // For high-resolution clock (for random seed)
#include <netdb.h>   // For gethostname, gethostbyname

#define SERVER_IP "127.0.0.1" // Default server IP address (localhost)
#define CONTROL_PORT 2121     // FTP控制连接端口

// Global state for data connection
bool passive_mode_enabled = true; // Default to passive mode
int active_data_listener_sock = -1; // For active mode: client's listening socket for data

// Function to send a command and receive a response
std::string send_command(int sock, const std::string& command) {
    std::string full_command = command + "\r\n";
    send(sock, full_command.c_str(), full_command.length(), 0);

    char buffer[4096] = {0}; // Increased buffer size for potentially longer responses
    memset(buffer, 0, sizeof(buffer));
    int valread = recv(sock, buffer, sizeof(buffer) - 1, 0); // -1 to ensure null termination
    if (valread <= 0) {
        std::cerr << "Server disconnected or error during recv." << std::endl;
        return ""; // Indicate error
    }
    return std::string(buffer);
}

// Function to open a data connection (for passive mode client: client connects to server)
int open_passive_data_connection_client(const std::string& ip, int port) {
    int data_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (data_sock == -1) {
        std::cerr << "Data socket creation failed." << std::endl;
        return -1;
    }

    sockaddr_in data_addr;
    data_addr.sin_family = AF_INET;
    data_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &data_addr.sin_addr) <= 0) {
        std::cerr << "Invalid IP address for data connection." << std::endl;
        close(data_sock);
        return -1;
    }

    if (connect(data_sock, (struct sockaddr *)&data_addr, sizeof(data_addr)) < 0) {
        perror("data connect failed");
        close(data_sock);
        return -1;
    }
    return data_sock;
}

// Function to open a data listener for active mode client (client listens, server connects to client)
int open_active_data_listener_client(std::string& client_ip, int& client_port) {
    int data_listener_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (data_listener_sock == -1) {
        std::cerr << "Active data listener socket creation failed." << std::endl;
        return -1;
    }

    // Allow reuse of address and port
    int opt = 1;
    if (setsockopt(data_listener_sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        perror("setsockopt active listener failed");
        close(data_listener_sock);
        return -1;
    }

    sockaddr_in data_addr;
    data_addr.sin_family = AF_INET;
    data_addr.sin_addr.s_addr = INADDR_ANY; // Listen on any available interface
    data_addr.sin_port = 0; // Let the system assign an ephemeral port

    if (bind(data_listener_sock, (struct sockaddr *)&data_addr, sizeof(data_addr)) < 0) {
        perror("active bind failed");
        close(data_listener_sock);
        return -1;
    }

    socklen_t addr_len = sizeof(data_addr);
    getsockname(data_listener_sock, (struct sockaddr*)&data_addr, &addr_len);
    client_port = ntohs(data_addr.sin_port);

    if (listen(data_listener_sock, 1) < 0) { // Only allow one pending connection from server
        perror("active listen failed");
        close(data_listener_sock);
        return -1;
    }

    // Get client's IP address (important for NAT scenarios)
    // For local testing, gethostname and gethostbyname usually give 127.0.0.1 or local LAN IP.
    // In a real-world scenario with NAT, this would require UPnP or a specific external IP configuration.
    char hostbuffer[256];
    gethostname(hostbuffer, sizeof(hostbuffer));
    struct hostent *host_entry;
    host_entry = gethostbyname(hostbuffer);
    if (host_entry == NULL) {
        perror("gethostbyname failed");
        close(data_listener_sock);
        return -1;
    }
    client_ip = inet_ntoa(*((struct in_addr*)host_entry->h_addr_list[0]));

    return data_listener_sock;
}

// Function to handle LIST/DIR command
void handle_list_command(int sock) {
    std::string response;
    int data_sock = -1;

    if (passive_mode_enabled) {
        response = send_command(sock, "PASV");
        std::cout << "Server: " << response;
        if (response.rfind("227", 0) == 0) {
            size_t start = response.find('(');
            size_t end = response.find(')');
            if (start != std::string::npos && end != std::string::npos) {
                std::string params_str = response.substr(start + 1, end - start - 1);
                std::vector<int> parts;
                std::stringstream ss(params_str);
                std::string segment;
                while(std::getline(ss, segment, ',')) {
                    parts.push_back(std::stoi(segment));
                }
                if (parts.size() == 6) {
                    std::string data_ip = std::to_string(parts[0]) + "." + std::to_string(parts[1]) + "." + std::to_string(parts[2]) + "." + std::to_string(parts[3]);
                    int data_port = parts[4] * 256 + parts[5];
                    data_sock = open_passive_data_connection_client(data_ip, data_port);
                } else {
                    std::cerr << "Failed to parse PASV response.\n";
                }
            } else {
                std::cerr << "Invalid PASV response format.\n";
            }
        } else {
            std::cerr << "Server denied PASV mode or sent invalid response.\n";
        }
    } else { // Active mode
        std::string client_ip;
        int client_data_port = 0;
        active_data_listener_sock = open_active_data_listener_client(client_ip, client_data_port);
        if (active_data_listener_sock != -1) {
            std::string ip_str = client_ip;
            std::replace(ip_str.begin(), ip_str.end(), '.', ',');
            int p1 = client_data_port / 256;
            int p2 = client_data_port % 256;
            std::string port_command = "PORT " + ip_str + "," + std::to_string(p1) + "," + std::to_string(p2);
            response = send_command(sock, port_command);
            std::cout << "Server: " << response;
            if (response.rfind("200", 0) != 0) {
                std::cerr << "Server denied PORT command or sent invalid response.\n";
                close(active_data_listener_sock);
                active_data_listener_sock = -1;
                return;
            }
            // Now server will connect to client's active_data_listener_sock
        } else {
            std::cerr << "Failed to set up active data listener.\n";
            return;
        }
    }

    if (data_sock != -1 || active_data_listener_sock != -1) {
        response = send_command(sock, "LIST"); // Send LIST command over control connection
        std::cout << "Server: " << response; // Expecting 150 response

        if (response.rfind("150", 0) == 0) {
            if (!passive_mode_enabled) { // In active mode, client needs to accept the connection
                data_sock = accept(active_data_listener_sock, NULL, NULL);
                close(active_data_listener_sock); // Close listener after accepting
                active_data_listener_sock = -1;
                if (data_sock < 0) {
                    perror("accept active data connection failed");
                    std::cerr << "Failed to accept data connection from server.\n";
                    return;
                }
            }
            char data_buffer[4096];
            memset(data_buffer, 0, sizeof(data_buffer));
            int bytes_read = recv(data_sock, data_buffer, sizeof(data_buffer) - 1, 0);
            if (bytes_read > 0) {
                std::cout << "Directory Listing:\n" << data_buffer << std::endl;
            } else if (bytes_read == 0) {
                std::cout << "Server sent empty directory listing.\n";
            } else {
                perror("recv data failed");
                std::cerr << "Error receiving directory listing.\n";
            }
            close(data_sock);

            // Get final response from server (e.g., 226 Directory send OK)
            response = send_command(sock, ""); // Send dummy to prompt final response
            std::cout << "Server: " << response;
        } else {
            std::cerr << "Server denied LIST command or data connection setup failed.\n";
        }
    } else {
        std::cerr << "Failed to establish data connection.\n";
    }
}


// Function to handle GET command (download)
void handle_get_command(int sock, const std::string& filename) {
    std::string response;
    int data_sock = -1;

    if (passive_mode_enabled) {
        response = send_command(sock, "PASV");
        std::cout << "Server: " << response;
        if (response.rfind("227", 0) == 0) {
            size_t start = response.find('(');
            size_t end = response.find(')');
            if (start != std::string::npos && end != std::string::npos) {
                std::string params_str = response.substr(start + 1, end - start - 1);
                std::vector<int> parts;
                std::stringstream ss(params_str);
                std::string segment;
                while(std::getline(ss, segment, ',')) {
                    parts.push_back(std::stoi(segment));
                }
                if (parts.size() == 6) {
                    std::string data_ip = std::to_string(parts[0]) + "." + std::to_string(parts[1]) + "." + std::to_string(parts[2]) + "." + std::to_string(parts[3]);
                    int data_port = parts[4] * 256 + parts[5];
                    data_sock = open_passive_data_connection_client(data_ip, data_port);
                }
            }
        }
    } else { // Active mode
        std::string client_ip;
        int client_data_port = 0;
        active_data_listener_sock = open_active_data_listener_client(client_ip, client_data_port);
        if (active_data_listener_sock != -1) {
            std::string ip_str = client_ip;
            std::replace(ip_str.begin(), ip_str.end(), '.', ',');
            int p1 = client_data_port / 256;
            int p2 = client_data_port % 256;
            std::string port_command = "PORT " + ip_str + "," + std::to_string(p1) + "," + std::to_string(p2);
            response = send_command(sock, port_command);
            std::cout << "Server: " << response;
            if (response.rfind("200", 0) != 0) {
                close(active_data_listener_sock);
                active_data_listener_sock = -1;
                std::cerr << "Server denied PORT command.\n";
                return;
            }
        } else {
            std::cerr << "Failed to set up active data listener.\n";
            return;
        }
    }

    if (data_sock != -1 || active_data_listener_sock != -1) {
        response = send_command(sock, "RETR " + filename); // Send RETR command
        std::cout << "Server: " << response; // Expecting 150 response

        if (response.rfind("150", 0) == 0) {
            if (!passive_mode_enabled) { // In active mode, client needs to accept the connection
                data_sock = accept(active_data_listener_sock, NULL, NULL);
                close(active_data_listener_sock);
                active_data_listener_sock = -1;
                 if (data_sock < 0) {
                    perror("accept active data connection failed");
                    std::cerr << "Failed to accept data connection from server.\n";
                    return;
                }
            }
            std::ofstream outfile(filename, std::ios::binary);
            if (outfile.is_open()) {
                char data_buffer[4096];
                int bytes_read;
                while ((bytes_read = recv(data_sock, data_buffer, sizeof(data_buffer), 0)) > 0) {
                    outfile.write(data_buffer, bytes_read);
                }
                if (bytes_read < 0) {
                    perror("recv file data failed");
                    std::cerr << "Error receiving file data.\n";
                } else {
                    std::cout << "File " << filename << " downloaded successfully.\n";
                }
                outfile.close();
            } else {
                std::cerr << "Failed to open local file for writing: " << filename << std::endl;
            }
            close(data_sock);

            // Get final response from server (e.g., 226 Transfer complete)
            response = send_command(sock, ""); // Dummy command to get final response
            std::cout << "Server: " << response;
        } else {
            std::cerr << "Server denied RETR command or data connection setup failed.\n";
        }
    } else {
        std::cerr << "Failed to establish data connection.\n";
    }
}


// Function to handle PUT command (upload)
void handle_put_command(int sock, const std::string& filename) {
    std::ifstream infile(filename, std::ios::binary);
    if (!infile.is_open()) {
        std::cerr << "Failed to open local file for reading: " << filename << std::endl;
        return;
    }

    std::string response;
    int data_sock = -1;

    if (passive_mode_enabled) {
        response = send_command(sock, "PASV");
        std::cout << "Server: " << response;
        if (response.rfind("227", 0) == 0) {
            size_t start = response.find('(');
            size_t end = response.find(')');
            if (start != std::string::npos && end != std::string::npos) {
                std::string params_str = response.substr(start + 1, end - start - 1);
                std::vector<int> parts;
                std::stringstream ss(params_str);
                std::string segment;
                while(std::getline(ss, segment, ',')) {
                    parts.push_back(std::stoi(segment));
                }
                if (parts.size() == 6) {
                    std::string data_ip = std::to_string(parts[0]) + "." + std::to_string(parts[1]) + "." + std::to_string(parts[2]) + "." + std::to_string(parts[3]);
                    int data_port = parts[4] * 256 + parts[5];
                    data_sock = open_passive_data_connection_client(data_ip, data_port);
                }
            }
        }
    } else { // Active mode
        std::string client_ip;
        int client_data_port = 0;
        active_data_listener_sock = open_active_data_listener_client(client_ip, client_data_port);
        if (active_data_listener_sock != -1) {
            std::string ip_str = client_ip;
            std::replace(ip_str.begin(), ip_str.end(), '.', ',');
            int p1 = client_data_port / 256;
            int p2 = client_data_port % 256;
            std::string port_command = "PORT " + ip_str + "," + std::to_string(p1) + "," + std::to_string(p2);
            response = send_command(sock, port_command);
            std::cout << "Server: " << response;
            if (response.rfind("200", 0) != 0) {
                close(active_data_listener_sock);
                active_data_listener_sock = -1;
                std::cerr << "Server denied PORT command.\n";
                return;
            }
        } else {
            std::cerr << "Failed to set up active data listener.\n";
            return;
        }
    }

    if (data_sock != -1 || active_data_listener_sock != -1) {
        response = send_command(sock, "STOR " + filename); // Send STOR command
        std::cout << "Server: " << response; // Expecting 150 response

        if (response.rfind("150", 0) == 0) {
            if (!passive_mode_enabled) { // In active mode, client needs to accept the connection
                data_sock = accept(active_data_listener_sock, NULL, NULL);
                close(active_data_listener_sock);
                active_data_listener_sock = -1;
                 if (data_sock < 0) {
                    perror("accept active data connection failed");
                    std::cerr << "Failed to accept data connection from server.\n";
                    return;
                }
            }
            char data_buffer[4096];
            while (!infile.eof()) {
                infile.read(data_buffer, sizeof(data_buffer));
                ssize_t bytes_sent = send(data_sock, data_buffer, infile.gcount(), 0);
                if (bytes_sent < 0) {
                    perror("send file data failed");
                    std::cerr << "Error sending file data.\n";
                    break;
                }
            }
            infile.close();
            close(data_sock);
            std::cout << "File " << filename << " uploaded successfully.\n";

            // Get final response from server (e.g., 226 Transfer complete)
            response = send_command(sock, ""); // Dummy command to get final response
            std::cout << "Server: " << response;
        } else {
            std::cerr << "Server denied STOR command or data connection setup failed.\n";
        }
    } else {
        std::cerr << "Failed to establish data connection.\n";
    }
}


int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[1024] = {0};

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        std::cout << "\n Socket creation error \n";
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(CONTROL_PORT);

    // Convert IP address from text to binary form
    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        std::cout << "\nInvalid address/ Address not supported \n";
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cout << "\nConnection Failed \n";
        return -1;
    }

    std::cout << "Connected to FTP server at " << SERVER_IP << ":" << CONTROL_PORT << std::endl;

    // Receive welcome message
    memset(buffer, 0, sizeof(buffer));
    recv(sock, buffer, 1024, 0);
    std::cout << "Server: " << buffer;

    std::string username, password;
    std::string response;

    // Login process
    while (true) {
        std::cout << "Username: ";
        std::getline(std::cin, username);
        response = send_command(sock, "USER " + username);
        std::cout << "Server: " << response;
        if (response.rfind("331", 0) != 0) { // Expected "331 User name okay"
            std::cerr << "Unexpected server response during USER command." << std::endl;
            close(sock);
            return -1;
        }

        std::cout << "Password: ";
        std::getline(std::cin, password);
        response = send_command(sock, "PASS " + password);
        std::cout << "Server: " << response;
        if (response.rfind("230", 0) == 0) { // Expected "230 User logged in"
            std::cout << "Login successful." << std::endl;
            break;
        } else if (response.rfind("530", 0) == 0) { // "530 Not logged in"
            std::cout << "Login failed. Please try again." << std::endl;
        } else {
            std::cerr << "Unexpected server response during PASS command." << std::endl;
            close(sock);
            return -1;
        }
    }

    std::string command_line;
    while (true) {
        std::cout << "ftp> ";
        std::getline(std::cin, command_line);

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

        if (command_name == "QUIT") {
            response = send_command(sock, "QUIT");
            std::cout << "Server: " << response;
            break;
        } else if (command_name == "PASV") {
            passive_mode_enabled = true;
            std::cout << "Passive mode enabled. All data transfers will use PASV.\n";
            // The actual PASV command to the server will be sent by LIST/GET/PUT handlers
        } else if (command_name == "PORT") {
            passive_mode_enabled = false;
            std::cout << "Active mode enabled. All data transfers will use PORT.\n";
            // The actual PORT command to the server will be sent by LIST/GET/PUT handlers
        } else if (command_name == "LS" || command_name == "DIR") {
            handle_list_command(sock);
        } else if (command_name == "GET") {
            handle_get_command(sock, argument);
        } else if (command_name == "PUT") {
            handle_put_command(sock, argument);
        } else if (command_name == "?" || command_name == "HELP") {
            std::cout << "Available commands:\n";
            std::cout << "  user <username>  - Specify username (used during login)\n";
            std::cout << "  pass <password>  - Specify password (used during login)\n";
            std::cout << "  ls / dir         - List directory contents (uses current mode)\n";
            std::cout << "  pwd              - Print working directory\n";
            std::cout << "  cd <directory>   - Change directory\n";
            std::cout << "  get <filename>   - Download a file (uses current mode)\n";
            std::cout << "  put <filename>   - Upload a file (uses current mode)\n";
            std::cout << "  pasv             - Set data transfer mode to passive (default)\n";
            std::cout << "  port             - Set data transfer mode to active\n";
            std::cout << "  quit             - Exit FTP client\n";
            std::cout << "  ? / help         - Display this help message\n";
        }
        else {
            response = send_command(sock, command_line);
            std::cout << "Server: " << response;
        }
    }

    close(sock);
    return 0;
}