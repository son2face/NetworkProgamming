//NGUYEN THANH SON - 15022886
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
#include <netdb.h>
#include <dirent.h>
#include <sys/stat.h>
#include <vector>
#include <sys/time.h>
#include <chrono>
#include <iomanip>
#include <climits>

using namespace std;
using namespace std::chrono;

struct ServerParam {
    string serAddr;
    int port;
};

class ThreadParam {
public:
    struct sockaddr_in *clientAddress{};
    int *newsockfd{};
};

struct Data {
    char *data;
    int start;
    int end;
    pthread_t *thread;
    int time;
};

struct DownloadSocketInfo {
    int socket;
    vector<Data> segments{};
};


struct FileData {
    char *name;
    int size;
};

vector<FileData> scanDir(char *dirName);    // Quét các file có trong một thư mục

void error(const char *msg);    // Hiện thông báo lỗi

int statFilesize(const char *fileName);    // Lấy kích thước của file

int sendDataServerIndex(ServerParam *serverParam, int clientPort);  // Gửi các file hiện có lên Server Index và đăng kí port nhận yêu cầu tải file

static void *requestFile(void *arg);    // Lấy thông tin các peer đang chứa file và kích thước file từ Server Index

static void *download(void *arg);   //Tải data trong một khoảng nào đó từ 1 peer cho 1 connect đã được kết nối từ trước, đồng thời đo thời gian tải.

static void *receiveRequest(void *arg);  // Luồng xử lý yêu cầu dữ liệu

long long getMilisecond();  // Lấy thời gian hiện tại theo dạng milisecond

int connectPeer(ServerParam serverParam, char *fileName);   // Tạo kết nối đến peer khác và yêu cầu chuẩn bị file để tải.

int openFile(char *fileName);  // Mở file để ghi

pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;
ServerParam indexServerParam{};
ServerParam requestServerParam{};
const int messageSize = 256;
string splitCharacter = "/";   // kí tự phân cách giữa các phần trong các thông báo
//TODO: chỉnh lại cái này sau
char folderName[] = "files";    // tên folder lưu trữ file
char folderName2[] = "files";   // tên folder lưu file nhận được (nhằm tạo thuận tiện cho quá trình debug)
int denominator = 4;    // mẫu số dùng để lấy chia phần dữ liệu file
int clientPort;     // cổng dùng để lắng nghe yêu cầu

int main() {
    string serAddr = "localhost";
    int indexServerPort = 9999;
    int requestServerPort = 8888;
    cout << "Enter Index Server IP: ";
    cin >> serAddr;
    cin.clear();
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
    cout << "Enter Exchange Port Number: ";
    cin >> clientPort;
    while (clientPort == indexServerPort || clientPort == requestServerPort || clientPort == 0) {
        cin.clear();
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
        cout << "Please choose another port: ";
        cin >> clientPort;
    }
    int sockfd, newsockfd;
    socklen_t client;
    struct sockaddr_in serv_addr{}, clientAddress{};
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    int option = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
    if (sockfd < 0)
        perror("ERROR opening socket");
    bzero((char *) &serv_addr, sizeof(serv_addr));
    indexServerParam.serAddr = serAddr;
    indexServerParam.port = indexServerPort;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(static_cast<uint16_t>(clientPort));
    if (bind(sockfd, (struct sockaddr *) &serv_addr,
             sizeof(serv_addr)) < 0)
        error("ERROR on binding");
    listen(sockfd, 5);
    pthread_mutex_lock(&counter_mutex);
    cout << "FileTransfer Running Port: " << clientPort << endl;
    pthread_mutex_unlock(&counter_mutex);
    sendDataServerIndex(&indexServerParam, clientPort);
    requestServerParam.serAddr = serAddr;
    requestServerParam.port = requestServerPort;
    pthread_t makeRequestThread;
    pthread_create(&makeRequestThread, nullptr, &requestFile, (void *) &requestServerParam);
    char message[messageSize];
    while (true) {
        bzero(message, messageSize);
        client = sizeof(clientAddress);
        newsockfd = accept(sockfd, (struct sockaddr *) &clientAddress, &client);
        ThreadParam threadParam{};
        threadParam.clientAddress = &clientAddress;
        threadParam.newsockfd = &newsockfd;
        pthread_t tid;
        pthread_create(&tid, nullptr, &receiveRequest, (void *) &threadParam);
    }
}

int sendDataServerIndex(ServerParam *serverParam, int clientPort) {
    int sockfd, n;
    struct sockaddr_in serv_addr{};
    struct hostent *server;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");
    server = gethostbyname(serverParam->serAddr.c_str());
    if (server == nullptr) {
        fprintf(stderr, "ERROR, no such host\n");
        exit(0);
    }
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy(server->h_addr,
          (char *) &serv_addr.sin_addr.s_addr,
          static_cast<size_t>(server->h_length));
    serv_addr.sin_port = htons(static_cast<uint16_t>(serverParam->port));
    cout << "Connecting to Index Server..." << endl;
    if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR connecting");
    cout << "Connected!" << endl << endl;
    string message = to_string(clientPort);
    message += splitCharacter;
    vector<FileData> files = scanDir(const_cast<char *>(folderName));
    cout << endl << "Scanned Files:" << endl;
    for (auto &file : files) {
        message += string(file.name);
        cout << "name: " << left << setw(20) << file.name;
        cout << "size: " << file.size << endl;
        message += splitCharacter;
        message += to_string(file.size);
        message += splitCharacter;
    }
    cout << endl;
    cout << "Sending FileInfo..." << endl;
    n = (int) write(sockfd, message.c_str(), message.size());
    if (n < 0)
        perror("ERROR writing to socket");
    cout << "Success!" << endl << endl;
    close(sockfd);
    return clientPort;
}

long long getMilisecond() {
    milliseconds ms1 = duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()
    );
    return ms1.count();
}

static void *requestFile(void *arg) {
    ServerParam serverParam = *((ServerParam *) arg);
    const int responseSize = 8096;
    int sockfd, n;
    struct sockaddr_in serv_addr{};
    struct hostent *server;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");
    server = gethostbyname(serverParam.serAddr.c_str());
    if (server == nullptr) {
        fprintf(stderr, "ERROR, no such host\n");
        exit(0);
    }
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy(server->h_addr,
          (char *) &serv_addr.sin_addr.s_addr,
          static_cast<size_t>(server->h_length));
    serv_addr.sin_port = htons(static_cast<uint16_t>(serverParam.port));
    cout << "Connecting to FindIndexServer..." << endl;
    if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR connecting");
    cout << "Connected!" << endl << endl;
    cin.clear();
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
    char response[responseSize];
    char message[messageSize];
    while (true) {
        cout << "Enter Filename: ";
        bzero(message, messageSize);
        cin.getline(message, messageSize);
        if (strcmp(message, "QUIT") == 0) exit(0);
        cout << "Sending Request...";
        auto startTime = static_cast<int>(getMilisecond());
        n = (int) write(sockfd, message, strlen(message));
        if (n < 0)
            perror("ERROR writing to socket 1");
        cout << "sent!" << endl;
        int read_return;
        bzero(response, responseSize);
        read_return = (int) read(sockfd, response, responseSize);
        if (read_return == -1) {
            error("read file error");
        }
        vector<ServerParam> peers;
        string messageString(response);
        messageString.substr(0, messageString.find(splitCharacter));
        int j = 0;
        size_t pos = 0;
        string token;
        pos = messageString.find(splitCharacter);
        token = messageString.substr(0, pos);
        int fileSize = atoi(token.c_str());
        cout << "------RESPONSE------" << endl;
        cout << "FileSize: " << fileSize << endl;
        if (fileSize == 0) {
            cout << "File Not Found!" << endl << endl;
            continue;
        }
        messageString.erase(0, pos + splitCharacter.length());
        ServerParam serverParam1{};
        while ((pos = messageString.find(splitCharacter)) != string::npos) {
            token = messageString.substr(0, pos);
            if (j == 0) {
                serverParam1.serAddr = token;
                j = 1;
            } else {
                j = 0;
                int port = atoi(token.c_str());
                serverParam1.port = port;
                peers.push_back(serverParam1);
                serverParam1 = {};
            }
            messageString.erase(0, pos + splitCharacter.length());
        }
        cout << endl << "PEERS: " << endl;
        for (auto &peer : peers) {
            cout << "IP: " << left << setw(20) << peer.serAddr;
            cout << "Port: " << peer.port << endl;
        }
        cout << endl;
        vector<DownloadSocketInfo> sockfds{};
        for (auto &peer : peers) {
            int result = connectPeer(peer, message);
            if (result != 0) {
                DownloadSocketInfo downloadSocketInfo{};
                downloadSocketInfo.socket = result;
                sockfds.push_back(downloadSocketInfo);
            }
        }
        //TODO: check
        if (sockfds.empty()) {
            cout << "Cannot connect to any peers!" << endl;
            continue;
        }
        int segSizeOriginal = fileSize / denominator;
        auto testSize = static_cast<int>(segSizeOriginal / sockfds.size());
        auto segSize = static_cast<int>(testSize * sockfds.size());
        if (segSize < 1000) {
            cout << "File too small, use solo mode..." << endl;
            Data data{};
            data.start = 0;
            data.end = fileSize;
            data.data = new char[fileSize];
            if (data.data == nullptr) {
                printf("Could not allocate required memory 3\n");
                //TODO: Cần check thêm
                exit(1);
            }
            sockfds[0].segments.push_back(data);
            download((void *) &sockfds[0]);
            int filefd = openFile(message);
            if (write(filefd, sockfds[0].segments[0].data, static_cast<size_t>(fileSize)) == -1) {
                error("write1");
            }
            close(filefd);
        } else {
            for (int i = 0; i < sockfds.size(); i++) {
                int start = testSize * i;
                int end = i == sockfds.size() - 1 ? segSizeOriginal : testSize * (i + 1);
                Data data{};
                data.start = start;
                data.end = end;
                data.data = new char[end - start];
                if (data.data == nullptr) {
                    printf("Could not allocate required memory 4\n");
                    //TODO: Cần check thêm
                    exit(1);
                }
                data.thread = new pthread_t{};
                sockfds[i].segments.push_back(data);
                pthread_create(data.thread, nullptr, &download, (void *) &sockfds[i]);
            }
            for (auto &sock : sockfds) {
                pthread_join(*sock.segments[0].thread, nullptr);
            }
            int leftSize = fileSize - segSizeOriginal;
            int start = segSizeOriginal;
            float sbt = 0;
            for (auto &sock : sockfds) {
                sbt += (float) 1 / sock.segments[0].time;
            }
            for (int i = 0; i < sockfds.size(); i++) {
                DownloadSocketInfo *downloadSocketInfo = &sockfds[i];
                int end;
                if ((*downloadSocketInfo).segments[0].time == 0 || i == sockfds.size() - 1) {
                    end = fileSize;
                } else {
                    float x = (float) 1 / (*downloadSocketInfo).segments[0].time / sbt;
                    auto dataSize = static_cast<int>(x * leftSize);
                    cout << "xxxx" << x << endl;
                    if (dataSize == 0) {
                        continue;
                    };
                    end = start + dataSize;
                }
                Data data{};
                data.start = start;
                data.end = end;
                cout << "end--" << end - start << endl;
                data.data = new char[end - start];
                if (data.data == nullptr) {
                    cout << "i" << i << endl;
                    printf("Could not allocate required memory 1\n");
                    //TODO: Cần check thêm
                    exit(1);
                }
                data.thread = new pthread_t{};
                (*downloadSocketInfo).segments.push_back(data);
                pthread_create(data.thread, nullptr, &download, (void *) &sockfds[i]);
                if ((*downloadSocketInfo).segments[0].time == 0) {
                    break;
                }
                start = end;
            }
            int filefd = openFile(message);
            for (auto &sock : sockfds) {
                if (write(filefd, sock.segments[0].data, static_cast<size_t>(sock.segments[0].end - sock.segments[0].start)) == -1) {
                    error("write");
                }
            }
            for (auto &sock : sockfds) {
                if (sock.segments.size() == 2){
                    pthread_join(*sock.segments[1].thread, nullptr);
                    if (write(filefd, sock.segments[1].data, static_cast<size_t>(sock.segments[1].end - sock.segments[1].start)) == -1) {
                        error("write");
                    }
                } else break;
            }
            close(filefd);
            cout << endl;
        }
        for (auto &sock : sockfds) {
            close(sock.socket);
        }
        auto endTime = static_cast<int>(getMilisecond());
        cout << "Total Time: " << endTime - startTime << "(ms)" << endl;
        sendDataServerIndex(&indexServerParam, clientPort);
        cout << "done!" << endl;
    }
}

static void *download(void *arg) {
    auto *downloadSocketInfo = (DownloadSocketInfo *) arg;
    Data *lastSegment = &(*downloadSocketInfo).segments.back();
    string message = to_string((*lastSegment).start) + splitCharacter +
                     to_string((*lastSegment).end);
    auto n = (int) write((*downloadSocketInfo).socket, message.c_str(), message.size());
    if (n < 0)
        perror("ERROR writing to socket 2");
    auto startTime = static_cast<int>(getMilisecond());
    int total = (*lastSegment).end - (*lastSegment).start;
    int downloadedBytes = 0;
    while (downloadedBytes < total) {
        n = (int) read((*downloadSocketInfo).socket, &(*lastSegment).data[downloadedBytes],
                       static_cast<size_t>(total - downloadedBytes));
        if (n == -1) {
            cout << n << endl << downloadedBytes << endl << total;
            error("read 1");
        }
        downloadedBytes += n;
    }
    auto endTime = static_cast<int>(getMilisecond());
    (*lastSegment).time = endTime - startTime;
    cout << "Download Time: " << (*lastSegment).time << " (ms)" << endl;
}

int openFile(char *fileName) {
    string location = string(folderName2) + splitCharacter + string(fileName);
    int filefd = open(location.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (filefd == -1) {
        error("open");
    }
    return filefd;
}

int connectPeer(ServerParam serverParam, char *fileName) {
    int sockfd = 0;
    struct sockaddr_in serv_addr{};
    struct hostent *server;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");
    server = gethostbyname(serverParam.serAddr.c_str());
    if (server == nullptr) {
        fprintf(stderr, "ERROR, no such host\n");
        exit(0);
    }
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy(server->h_addr,
          (char *) &serv_addr.sin_addr.s_addr,
          static_cast<size_t>(server->h_length));
    serv_addr.sin_port = htons(static_cast<uint16_t>(serverParam.port));
    if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        cout << (
                ("ERROR connecting to " + serverParam.serAddr + ":" + to_string(serverParam.port) + "\n").c_str());
        return 0;
    }
    auto n = (int) write(sockfd, fileName, messageSize);
    if (n < 0)
        perror("ERROR writing to socket 3");
    cout << "Connected to " << serverParam.serAddr << ":" << serverParam.port << endl;
    return sockfd;
}

static void *receiveRequest(void *arg) {
    ThreadParam threadParam = *((ThreadParam *) arg);
    pthread_detach(pthread_self());
    char message[messageSize];
    int newsockfd = *threadParam.newsockfd;
    struct sockaddr_in clientAddress = *threadParam.clientAddress;
    auto n = (int) read(newsockfd, message, messageSize);
    if (n <= 0) {
        cout << "Read Data Error!!:: " << inet_ntoa(clientAddress.sin_addr) << ":" << clientAddress.sin_port << endl;
        close(newsockfd);
        return nullptr;
    }
    int filefd;
    string fileLocation = string(folderName) + "/" + string(message);
    filefd = open(fileLocation.c_str(), O_RDONLY);
    if (filefd == -1) {
        cout << "Open File " << fileLocation << " error!" << endl;
        close(newsockfd);
        return nullptr;
        //TODO: Check trường hợp bị xóa file bằng tay
    }
    cout << "Connected:: " << inet_ntoa(clientAddress.sin_addr) << ":" << clientAddress.sin_port << endl;
    while (true) {
        bzero(message, messageSize);
        n = (int) read(newsockfd, message, messageSize);
        if (n <= 0) {
            cout << "Disconnected:: " << inet_ntoa(clientAddress.sin_addr) << ":" << clientAddress.sin_port << endl;
            close(newsockfd);
            break;
        } else {
            cout << endl << inet_ntoa(clientAddress.sin_addr) << ":" << clientAddress.sin_port << " message: "
                 << message << endl << endl;
            if (strcmp(message, "QUIT") == 0) break;
            string messageString(message);
            messageString.substr(0, messageString.find(splitCharacter));
            size_t pos = 0;
            string token;
            pos = messageString.find(splitCharacter);
            token = messageString.substr(0, pos);
            int start = atoi(token.c_str());
            messageString.erase(0, pos + splitCharacter.length());
            pos = messageString.find(splitCharacter);
            token = messageString.substr(0, pos);
            int end = atoi(token.c_str());
            int length = end - start;
            //TODO: Có thể dẫn đến tràn memory, không khởi tạo được, có thể chia nhỏ ra để tải sau
            auto *buffer = new char[length];
            lseek(filefd, start, SEEK_SET);
            int readedBytes = 0;
            n = (int) read(filefd, buffer, static_cast<size_t>(length - readedBytes));
            if (n == 0)
                break;
            if (n == -1) {
                cout << "Error Reading File " << endl;
                close(newsockfd);
                return nullptr;
            }
            int writeBytes = 0;
            n = (int) write(newsockfd, &buffer[writeBytes], static_cast<size_t>(length - writeBytes));
            if (n == -1) {
                cout << "Error Writing Socket 4" << endl;
                close(newsockfd);
                return nullptr;
            }
        }
    }
    close(newsockfd);
    return nullptr;
}

void error(const char *msg) {
    perror(msg);
    exit(0);
}

int statFilesize(const char *fileName) {
    struct stat statbuf{};
    if (stat(fileName, &statbuf) == -1) {
        printf("failed to stat %s\n", fileName);
        return -1;
    }
    return static_cast<int>(statbuf.st_size);
}

vector<FileData> scanDir(char *dirName) {
    struct dirent *dp = nullptr;
    DIR *d = nullptr;
    d = opendir(dirName);
    vector<FileData> result;
    if (d == nullptr) {
        perror("Couldn't open directory");
        return (result);
    }
    while ((dp = readdir(d))) {
        if (strcmp(dp->d_name, ".") != 0 && strcmp(dp->d_name, "..") != 0) {
            string fileLocation = string(dirName) + "/" + string(dp->d_name);
            FileData fileData{};
            fileData.name = dp->d_name;
            fileData.size = statFilesize(fileLocation.c_str());
            result.push_back(fileData);
        }
    }
    closedir(d);
    return (result);
}

