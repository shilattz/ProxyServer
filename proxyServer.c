#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <netdb.h>
#include "threadpool.h"

#define MAX_REQUEST_LEN 2048
#define MAX_FILTER_LEN 256
#define MAX_HOST_LEN 256
#define MAX_METHOD_LEN 2048
#define MAX_PATH_LEN 2048
#define MAX_PROTOCOL_LEN 2048
#define MAX_IP_LEN 46 // maximum length of IPv6 address in textual representation
#define MAX_RESPONSE_LEN 1024 // Maximum response length (in bytes)
#define TIMEOUT_SECS 10 // Timeout value in seconds


void *handle_client(void *args);
char *filter_file;

int main(int argc, char *argv[]) {

    if (argc != 5) {
        printf( "Usage: proxyServer <port> <pool-size> <max-number-of-request> <filter>\n");
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);
    int pool_size = atoi(argv[2]);
    int max_requests = atoi(argv[3]);
    filter_file = argv[4];

    // Check if the filter file exists
//    if (access(filter_file, F_OK) != 0) {
//        printf( "Usage: <port> <pool-size> <max-number-of-request> <filter>\n");
//        return EXIT_FAILURE;
//    }
//    if(port<0 || pool_size<0 || max_requests<0){
//        perror( "Usage: <port> <pool-size> <max-number-of-request> <filter>\n");
//        return EXIT_FAILURE;
//    }


    threadpool *pool = create_threadpool(pool_size);
    if (pool == NULL) {
        perror( "Failed to create thread pool\n");
        return EXIT_FAILURE;
    }

    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("Socket creation failed\n");
        destroy_threadpool(pool);
        return EXIT_FAILURE;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed\n");
        destroy_threadpool(pool);
        close(server_fd);
        return EXIT_FAILURE;
    }

    if (listen(server_fd, max_requests) < 0) {
        perror("Listen failed\n");
        destroy_threadpool(pool);
        close(server_fd);
        return EXIT_FAILURE;
    }

  //  printf("Proxy server running on port %d...\n", port);

    int requests_handled = 0;
    while (requests_handled < max_requests) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("Accept failed\n");
            continue;
        }

        // Create thread to handle client connection
        dispatch(pool, (dispatch_fn) handle_client, (void*)(intptr_t)client_fd);

        requests_handled++;
    }

    destroy_threadpool(pool);
    close(server_fd);

    return EXIT_SUCCESS;
}

int parse_port(char* path) {
    char temp[MAX_PATH_LEN];
    int default_port = 80;
    int port;
    // Remove "http://" from the URL
    strcpy(temp, path + 7);

    // Extract hostname
    char *port_separator = strchr(temp, ':');
    if (port_separator != NULL) {

        // Check if the port that insert is a number
        char *port_str = port_separator + 1;
        while (*port_str != '\0' && *port_str != '/') {
//            if (!isdigit(*port_str)) {
//                fprintf(stderr, "Invalid port number specified in the URL.\n");
//                exit(EXIT_FAILURE);
//            }
            port_str++;
        }

        // Extract port from the substring after the port separator
        port= atoi(port_separator + 1);

    } else {
        // Use the default port
        port = default_port;
    }
    return port;
}

int parse_request(const char *request_str2,char* method1,char* path1,char* protocol1, char* host1) {

    char request_str[strlen(request_str2)];
    strcpy(request_str,request_str2);

    if (request_str2 == NULL )
        return 0;

    char *token = strtok(request_str, " \r\n");

    if (token == NULL)
        return 0;

    strncpy(method1, token, strlen(token));
    method1[strlen(token)] = '\0';
   // printf("%s\n",method1);

    token = strtok(NULL, " \r\n");
    if (token == NULL)
        return 0;

    strncpy(path1, token, strlen(token));
    path1[strlen(token)] = '\0';
   //  printf("%s\n",path1);

    int port1= parse_port(path1);

    token = strtok(NULL, " \r\n");
    if (token == NULL)
        return 0;

    strncpy(protocol1, token, strlen(token));
      protocol1[strlen(token)] = '\0';
    //printf("%s\n",protocol1);
    // Check if the protocol is not "HTTP/1.0" or "HTTP/1.1"
    if (strcmp(protocol1, "HTTP/1.0") != 0 && strcmp(protocol1, "HTTP/1.1") != 0) {
       // printf("Invalid protocol: %s\n", request->protocol);
        return 0;
    }
    // Read Host header
    while ((token = strtok(NULL, " \r\n"))) {
        if (strcmp(token, "Host:") == 0) {
            token = strtok(NULL, " \r\n");
            if (token == NULL)
                return 0;

            strncpy(host1, token, strlen(token));
            host1[strlen(token)] = '\0';
          //  printf("%s\n",host1);
            return port1;
        }
    }

    return 0;
}


int is_method_supported(const char *method) {
    return strcmp(method, "GET") == 0;
}

int is_valid_host(const char *host) {

    FILE *filter_fp = fopen(filter_file, "r");
    if (filter_fp == NULL) {
        perror("Error opening filter file");
        return 500;
    }

    //   FilterConfig filter;
    // memset(filter.filter, 0, MAX_FILTER_LEN);
    char line[MAX_FILTER_LEN];
    while (fgets(line, MAX_FILTER_LEN, filter_fp) != NULL) {
        // printf("%s\n",line);
        if (strstr(line , host) !=NULL) {
            fclose(filter_fp);
            return 1; // Forbidden
        }
    }
    fclose(filter_fp);
    return 0;
}

int is_ip_in_filter(const char *ip) {

   // printf("Checking IP: %s\n", ip);
    FILE *filter_fp = fopen(filter_file, "r");
    if (filter_fp == NULL) {
        perror("Error opening filter file");
        return 500;
    }

    struct in_addr input_addr;
    if (inet_pton(AF_INET, ip, &input_addr) != 1) {
        fprintf(stderr, "Invalid input IP address: %s\n", ip);
        fclose(filter_fp);
        return 500;
    }

    char line[MAX_FILTER_LEN];
    while (fgets(line, MAX_FILTER_LEN, filter_fp) != NULL) {
        char *token = strtok(line, "/");
        if (token == NULL) {
            //  fprintf(stderr, "Skipping line (missing IP address)\n");
            continue;
        }

        char *subnet_str = strtok(NULL, "/");
        int subnet_bits = 32; // Default to no subnet (matching any IP)

        if (subnet_str != NULL) {
            subnet_bits = atoi(subnet_str);
            if (subnet_bits < 0 || subnet_bits > 32) {
                //  fprintf(stderr, "Invalid subnet size: %s\n", subnet_str);
                continue; // Skip invalid subnet sizes
            }
        }

        // Convert IP address to binary form
        struct in_addr addr;
        if (inet_pton(AF_INET, token, &addr) != 1) {
            // fprintf(stderr, "Invalid IP address: %s\n", token);
            continue; // Skip invalid IP addresses
        }

        // Calculate network address
        uint32_t ip_network = ntohl(addr.s_addr) >> (32 - subnet_bits);
        uint32_t input_network = ntohl(input_addr.s_addr) >> (32 - subnet_bits);

       // printf("IP Network: %u\n", ip_network);
        //printf("Input Network: %u\n", input_network);

        if (ip_network == input_network) {
            fclose(filter_fp);
            return 1; // Forbidden
        }
    }

    fclose(filter_fp);
    return 0; // Not forbidden
}

char * currentDate() {
    time_t rawtime;
    struct tm* time_info;
    char *buffer = (char *)malloc(100); // Allocate memory dynamically

    if (buffer == NULL) {
        perror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }
    time(&rawtime);
    time_info = gmtime(&rawtime);

    strftime(buffer, 100, "Date: %a, %d %b %Y %H:%M:%S GMT", time_info);

    char *formattedDate = strdup(buffer);  // Duplicate the string
    free(buffer);  // Free the original buffer
    return formattedDate;
}

void modified_request(const char* request_buf, char* modified_request_buf){

    const char *url_start = strstr(request_buf, "http://");
    if (url_start == NULL) {
        // If "http://" is not found, copy the original request buffer
        strcpy(modified_request_buf, request_buf);
       // return;
    }

    // Find the end of the URL
   else {
        const char *url_end = strstr(url_start + 7, "/");
        if (url_end == NULL) {
            // If no '/' is found after "http://", copy the original request buffer
            strcpy(modified_request_buf, request_buf);
            return;
        }
        // Copy the part of the original request buffer before the URL
        strncpy(modified_request_buf, request_buf, url_start - request_buf);
        modified_request_buf[url_start - request_buf] = '\0';
        strcat(modified_request_buf, url_end);
    }
  //in order to close the connection
   char *connection_close = strstr(modified_request_buf, "Connection: close\r\n");
   if(connection_close == NULL) {
       char *connection_keep_alive = strstr(modified_request_buf, "Connection: keep-alive\r\n");
       if (connection_keep_alive != NULL) {
           // Find the end of the line
           char *line_end = strchr(connection_keep_alive, '\r');
           if (line_end == NULL) {
               line_end = strchr(connection_keep_alive, '\n');
           }

           char *keep_alive_end = strstr(connection_keep_alive, "keep-alive");
           if (keep_alive_end != NULL) {
               strcpy(keep_alive_end, "close");
               // Move the rest of the request up to overwrite the remainder of the old line
               memmove(keep_alive_end + strlen("close"), line_end, strlen(line_end) + 1);
           }
           //  }

       } else {
           // If "Connection: keep-alive" doesn't exist, add "Connection: close" before the empty line
           char *empty_line = strstr(modified_request_buf, "\r\n\r\n");
           if (empty_line != NULL) {
               strcpy(empty_line, "\r\nConnection: close\r\n\r\n");
           }
       }
   }
    //modified_request_buf[strlen(modified_request_buf)] = '\0'; // Just to be safe
}
void generate_error_response(char* response, int error_type){
    char* date=currentDate();
    char type[50];
    int length;
    char string[50];
    char str[100]="<HTML><HEAD><TITLE>%s</TITLE></HEAD><BODY><H4>%s</H4>%s</BODY></HTML>";
    int str_len= strlen(str);
    switch(error_type){
        case 400:
            strcpy(type,"400 Bad Request");
            type[strlen(type)]='\0';
            // length=113;
            strcpy(string,"Bad Request.");
            string[strlen(string)]='\0';
            length=str_len+ strlen(type)+ strlen(type)+ strlen(string);
           // printf("%d\n",length);
            break;
        case 501:
            strcpy(type,"501 Not Supported");
            type[strlen(type)]='\0';
            //length=129;
            strcpy(string,"Method is not supported.");
            string[strlen(string)]='\0';
            length=str_len+ strlen(type)+ strlen(type)+ strlen(string);
            break;
        case 404:
            strcpy(type,"404 Not Found");
            type[strlen(type)]='\0';
            //length=112;
            strcpy(string,"File not found.");
            string[strlen(string)]='\0';
            length=str_len+ strlen(type)+ strlen(type)+ strlen(string);
            break;
        case 403:
            strcpy(type,"403 Forbidden");
            type[strlen(type)]='\0';
            //length=111;
            strcpy(string,"Access denied.");
            string[strlen(string)]='\0';
            length=str_len+ strlen(type)+ strlen(type)+ strlen(string);
            break;
        case 500:
            strcpy(type,"500 Internal Server Error");
            type[strlen(type)]='\0';
            //length=144;
            strcpy(string,"Some server side error.");
            string[strlen(string)]='\0';
            length=str_len+ strlen(type)+ strlen(type)+ strlen(string);
            break;
    }
    snprintf(response, MAX_RESPONSE_LEN, "HTTP/1.1 %s\r\n"
                                         "Server: webserver/1.0\r\n"
                                         "%s\r\n"
                                         "Content-Type: text/html\r\n"
                                         "Content-Length: %d\r\n"
                                         "Connection: close\r\n"
                                         "\r\n"
                                         "<HTML><HEAD><TITLE>%s</TITLE></HEAD>\r\n"
                                         "<BODY><H4>%s</H4>\r\n"
                                         "%s\r\n"
                                         "</BODY></HTML>\n",type,date,length,type,type,string);
    response[strlen(response)]='\0';
    free(date);
}

void connect_and_forward_request(const char *host, const char *request_buf, ssize_t request_len, int client_fd, int port) {
    //printf("%s\n",host);
    char response[MAX_RESPONSE_LEN];

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        generate_error_response(response,500);
        send(client_fd, response, strlen(response)+1, 0);
        return;
       // exit(EXIT_FAILURE);
    }

    struct hostent *server = gethostbyname(host);
    if (server == NULL) {
        herror("Error failed to resolve: gethostbyname");
        close(sockfd);
        generate_error_response(response,500);
        send(client_fd, response, strlen(response), 0);
        return;
       // exit(EXIT_FAILURE);//instead 500
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    //printf("%d\n",port);
    server_addr.sin_port = htons(port);
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sockfd);
        generate_error_response(response,500);
        send(client_fd, response, strlen(response), 0);
        return;
       // exit(EXIT_FAILURE);//instead 500
    }

    int bytes_sent1;
    bytes_sent1 = send(sockfd, request_buf, request_len, 0);
    if (bytes_sent1 < 0) {
        perror("Error sending request");
        close(sockfd);
        generate_error_response(response,500);
        send(client_fd, response, strlen(response), 0);
        return;
       // exit(EXIT_FAILURE);//instead 500
    }

    int bytes_received;
    while ((bytes_received = recv(sockfd, response ,MAX_RESPONSE_LEN-1, 0)) > 0  ) {
        response[bytes_received] = '\0';
       // printf("%s\n",response);
        // Send the response to the client
        ssize_t bytes_sent = send(client_fd, response, bytes_received, 0);
        if (bytes_sent < bytes_received) {
            perror("Sending response to client failed");
            generate_error_response(response,500);
            send(client_fd, response, strlen(response), 0);
            if((bytes_received = recv(sockfd, response ,MAX_RESPONSE_LEN-1, 0))<=0)
                send(client_fd, "\n", strlen(response), 0);
            return;
        }
    }
    if (bytes_received < 0) {
        perror("Error receiving response");
        close(sockfd);
        generate_error_response(response,500);
        send(client_fd, response, strlen(response), 0);
        return;
      //  exit(EXIT_FAILURE);//instead 500
    }

    close(sockfd);
}

void *handle_client(void *args) {
    int client_fd = (int)(intptr_t)args;
    char recieve[MAX_REQUEST_LEN]="\0";
    char request_buf[MAX_REQUEST_LEN] ="\0";
    int bytes_received=0;


    char method1[MAX_METHOD_LEN]="\0";
    char path1[MAX_PATH_LEN]="\0";
    char protocol1[MAX_PROTOCOL_LEN]="\0";
    char host1[MAX_HOST_LEN]="\0";
    int  port1=0;

    while ((bytes_received = recv(client_fd, recieve, sizeof(recieve), 0)) > 0) {
        recieve[bytes_received] = '\0';
        strcat(request_buf, recieve);
        char* endloop = strstr(request_buf, "\r\n\r\n");
        if (endloop != NULL) {
            break;
        }
    }

    char response[MAX_RESPONSE_LEN]="\0";
    port1=parse_request(request_buf,method1,path1,protocol1,host1);
    if (port1==0) {
        generate_error_response(response, 400);
        send(client_fd, response, strlen(response), 0);
        close(client_fd);
        return (void*)0;
    }

    if (!is_method_supported(method1)) {
        generate_error_response(response, 501);
        send(client_fd, response, strlen(response), 0);
        close(client_fd);
        return (void*)0;
    }

    struct hostent *server = gethostbyname(host1);
    if (server == NULL) {
        generate_error_response(response, 404);
        send(client_fd, response, strlen(response), 0);
        close(client_fd);
        return (void*)0;
    }

    char ip[MAX_IP_LEN];
    inet_ntop(AF_INET, server->h_addr, ip, sizeof(ip));

    int valid_host = is_valid_host(host1);
    int ip_in = is_ip_in_filter(ip);

    if (valid_host == 1 || ip_in == 1) {
        generate_error_response(response, 403);
        send(client_fd, response, strlen(response), 0);
        close(client_fd);
        return (void*)0;
    }

    if (valid_host == 500 || ip_in == 500) {
        generate_error_response(response, 500);
        send(client_fd, response, strlen(response), 0);
        close(client_fd);
        return (void*)0;
    }

    char modified_request_buf[strlen(request_buf)];
    modified_request(request_buf, modified_request_buf);

    connect_and_forward_request(host1, modified_request_buf, strlen(modified_request_buf), client_fd, port1);

    close(client_fd);  // Close the client file descriptor

    return (void*)0;
}


