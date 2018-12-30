/* A simple server in the internet domain using TCP
   The port number is passed as an argument */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>
#include <arpa/inet.h>
#include <limits>
#include <fcntl.h>
#include <wait.h>
#include <vector>
#include <iomanip>

using namespace std;

struct FileData {
    string name;
    string size;
};

struct ClientInfo {
    string clientIp;
    string port;
    vector<FileData> files;
};

struct Client {
    struct sockaddr_in *clientAddress{};
    int *newsockfd{};
};


string getList(char *fileName); // Lấy danh sách các peer chứa file và tạo thành message

const string splitCharacter = "/";   //Kí tự dùng để ngăn cách

vector<ClientInfo> clients; // Lưu thông tin các peer

pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;

static void *updateFileIndex(void *arg);    // Luồng xử lý tác vụ cập nhật danh sách file

static void *findFileThread(void *arg);    // Luồng lắng nghe tác vụ tìm kiếm file được yêu cầu

static void *findFileIndex(void *arg);      // Luồng xử lý tác vụ tìm kiếm file được yêu cầu

int updateInfo(char *adrr, char *mess);     // Hàm thực hiện cập nhật danh sách file

const int messageSize = 8096;

int main() {
    int updatePort = 9999;
    int findPort = 8888;
    pthread_t updateThread;
    pthread_create(&updateThread, nullptr, &updateFileIndex, (void *) &updatePort);
    pthread_t findThread;
    pthread_create(&findThread, nullptr, &findFileIndex, (void *) &findPort);
    pthread_join(updateThread, nullptr);
}

static void *updateFileIndex(void *arg) {
    const int serverIndexPort = *(int *) arg;
    int sockfd, newsockfd;
    socklen_t client;
    struct sockaddr_in serv_addr{}, clientAddress{};
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    int option = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
    if (sockfd < 0)
        perror("ERROR opening socket");
    bzero((char *) &serv_addr, sizeof(serv_addr));
    pthread_mutex_lock(&counter_mutex);
    cout << "UpdateFileIndex Running Port: " << serverIndexPort << endl;
    pthread_mutex_unlock(&counter_mutex);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(serverIndexPort);
    if (bind(sockfd, (struct sockaddr *) &serv_addr,
             sizeof(serv_addr)) < 0)
        perror("ERROR on binding");
    listen(sockfd, 5);
    char message[messageSize];
    while (true) {
        bzero(message, messageSize);
        client = sizeof(clientAddress);
        newsockfd = accept(sockfd, (struct sockaddr *) &clientAddress, &client);
        cout << "New Client:: " << inet_ntoa(clientAddress.sin_addr) << ":" << clientAddress.sin_port
             << endl;
        if (newsockfd < 0)
            perror("ERROR on accept");
        int n = (int) read(newsockfd, message, messageSize);
        if (n <= 0) {
            cout << "Disconnected:: " << inet_ntoa(clientAddress.sin_addr) << ":" << clientAddress.sin_port << endl;
            close(newsockfd);
        }
        updateInfo(inet_ntoa(clientAddress.sin_addr), message);
        close(newsockfd);
    }
}

static void *findFileThread(void *arg) {
    Client client = *((Client *) arg);
    pthread_detach(pthread_self());
    char message[messageSize];
    struct sockaddr_in clientAddress = *client.clientAddress;
    int newsockfd = *client.newsockfd;
    if (newsockfd < 0) {
        perror("ERROR on accept");
        close(newsockfd);
        return nullptr;
    }
    while (true) {
        bzero(message, messageSize);
        auto n = (int) read(newsockfd, message, messageSize);
        pthread_mutex_lock(&counter_mutex);
        if (n <= 0) {
            cout << "Disconnected:: " << inet_ntoa(clientAddress.sin_addr) << ":" << clientAddress.sin_port << endl;
            break;
        }
        cout << inet_ntoa(clientAddress.sin_addr) << ":"
             << clientAddress.sin_port << "--message--" << message << endl;
        string result = getList(message);
        pthread_mutex_unlock(&counter_mutex);
        n = (int) write(newsockfd, result.c_str(), result.size());
        pthread_mutex_lock(&counter_mutex);
        if (n < 0) {
            perror("ERROR writing to socket");
            break;
        }
        cout << "Response :" << result << endl;
        pthread_mutex_unlock(&counter_mutex);
    }
    close(newsockfd);
}

static void *findFileIndex(void *arg) {
    const int serverIndexPort = *(int *) arg;
    const int messageSize = 8096;
    int sockfd, newsockfd;
    socklen_t client;
    struct sockaddr_in serv_addr{}, clientAddress{};
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    int option = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
    if (sockfd < 0)
        perror("ERROR opening socket");
    bzero((char *) &serv_addr, sizeof(serv_addr));
    pthread_mutex_lock(&counter_mutex);
    cout << "FindFileIndex Running Port: " << serverIndexPort << endl;
    pthread_mutex_unlock(&counter_mutex);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(serverIndexPort);
    if (bind(sockfd, (struct sockaddr *) &serv_addr,
             sizeof(serv_addr)) < 0)
        perror("ERROR on binding");
    listen(sockfd, 5);
    char message[messageSize];
    while (true) {
        bzero(message, messageSize);
        client = sizeof(clientAddress);
        newsockfd = accept(sockfd, (struct sockaddr *) &clientAddress, &client);
        Client client1;
        client1.clientAddress = &clientAddress;
        client1.newsockfd = &newsockfd;
        pthread_t tidd;
        pthread_create(&tidd, nullptr, &findFileThread, (void *) &client1);
    }
}


int updateInfo(char *adrr, char *mess) {
    string message(mess);
    cout << "---------------------Updating................" << endl;
    message.substr(0, message.find(splitCharacter));
    size_t pos = 0;
    string token;
    pos = message.find(splitCharacter);
    token = message.substr(0, pos);
    ClientInfo clientInfo{};
    clientInfo.clientIp = adrr;
    clientInfo.port = token;
    message.erase(0, pos + splitCharacter.length());
    int j = 0;
    FileData fileData{};
    while ((pos = message.find(splitCharacter)) != std::string::npos) {
        token = message.substr(0, pos);
        if (j == 0) {
            fileData.name = token;
            j = 1;
        } else {
            j = 0;
            fileData.size = token;
            clientInfo.files.push_back(fileData);
            fileData = {};
        }
        message.erase(0, pos + splitCharacter.length());
    }
    bool check = false;
    for (auto &client : clients) {
        if (client.clientIp == clientInfo.clientIp && client.port == clientInfo.port) {
            check = true;
            client = clientInfo;
            break;
        }
    }
    if (!check) clients.push_back(clientInfo);
    for (int j = 0; j < clients.size(); j++) {
        cout << j + 1 << "." << endl;
        cout << "client IP: " << left << setw(15) << clients[j].clientIp;
        cout << "port: " << clients[j].port << endl << endl;
        cout << "files: " << endl;
        for (auto &file : clients[j].files) {
            cout << "name: " << left << setw(20) << file.name;
            cout << "size: " << file.size << endl;
        }
        cout << endl << endl;
    }
    cout << "---------------------Updated!..............." << endl << endl;
}

string getList(char *fileName) {
    string result;
    string fileSize = "0" + splitCharacter;
    for (auto &client : clients) {
        for (auto &file : client.files) {
            if (file.name == string(fileName)) {
                fileSize = file.size + splitCharacter;
                result += client.clientIp + splitCharacter + client.port + splitCharacter;
                break;
            }
        }
    }
    result = fileSize + result;
    return result;
}