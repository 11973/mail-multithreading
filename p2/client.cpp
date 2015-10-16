#include <iostream>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <poll.h>
#include <cstdio>
#include <arpa/inet.h>


enum { SOCKET_CR_ERR, CONNECT_ERR, INCORRECT_IP, INCORRECT_PORT, CONNECTION_TERMINATED, BUF_MAX = 1024 };

class Client
{
public:
    Client(const char *ipstr, const char *portstr);
    ~Client();
    void run();
    static void print_err_msg(int err);
private:
    int srvfd;
    struct pollfd *pfd;
    char buf[BUF_MAX];
    int buf_size;
    void get_input();
    void print_message();
};

Client::~Client()
{
    shutdown(this->srvfd, SHUT_RDWR);
    close(this->srvfd);
    free(this->pfd);
}

Client::Client(const char *ipstr, const char *portstr)
{
    this->srvfd = socket(AF_INET, SOCK_STREAM, 0);
    if (srvfd < 0)
        throw int(SOCKET_CR_ERR);
    
    struct sockaddr_in stsa;
    memset(&stsa, 0, sizeof(stsa));

    if (inet_pton(AF_INET, ipstr, &stsa.sin_addr) != 1)
        throw int(INCORRECT_IP);
    
    if (sscanf(portstr, "%hd", &stsa.sin_port) != 1)
        throw int(INCORRECT_PORT);
    
    stsa.sin_family = AF_INET;
    stsa.sin_port = htons(stsa.sin_port);

    if (connect(this->srvfd, (struct sockaddr *) &stsa, sizeof(stsa)) != 0)
        throw int(CONNECT_ERR);

    this->buf_size = 0;
}

void
Client::run()
{
    enum { PFD_SIZE = 2 };
    this->pfd = (struct pollfd *) calloc(PFD_SIZE, sizeof(*this->pfd));

    memset(this->pfd, 0, sizeof(*this->pfd) * PFD_SIZE);
    this->pfd[0].fd = STDIN_FILENO;
    this->pfd[0].events = POLLIN;

    this->pfd[1].fd = this->srvfd;
    this->pfd[1].events = POLLIN;

    while (1) {
        poll(this->pfd, PFD_SIZE, -1);
        if (this->pfd[0].revents != 0) {
            this->get_input();
        }
        if (this->pfd[1].revents != 0) {
            this->print_message();
        }
    }
}

void
Client::get_input()
{
    if (this->buf_size >= BUF_MAX) {
        if (send(this->srvfd, buf, this->buf_size, 0) != this->buf_size) {
            std::cerr << "error sending data to host" << std::endl; // is that possible?
            return;
        }
        std::cout << "\x1b[1A\x1b[2K" << std::endl;
        this->buf_size = 0;
    }
    
    int t = (int) read(STDIN_FILENO, this->buf, BUF_MAX - this->buf_size);
    if (t == 0) {
        std::cout << "We will miss you" << std::endl;
        exit(0);
    }

    buf_size += t;
    if (this->buf[this->buf_size - 1] == '\n') {
        this->buf_size--;
        this->buf[this->buf_size] = '\0';
    
        if (this->buf[this->buf_size - 1] == '\0') {
            this->buf_size--;
            this->buf[this->buf_size] = '\0';
        }
        
        if (send(this->srvfd, buf, this->buf_size, 0) != this->buf_size) {
            std::cerr << "error sending data to host" << std::endl; // is that possible?
            return;
        }
        std::cout << "\x1b[1A\x1b[2K" << std::flush;
        this->buf_size = 0;
    }
}

void
Client::print_message()
{
    char buf[BUF_MAX + 1];
    int t = (int) recv(this->srvfd, buf, BUF_MAX, 0);
    if (t == 0) {
        std::cout << "connection terminated by host" << std::endl;
        throw int(CONNECTION_TERMINATED);
    }
    if (t < 0) {
        std::cout << "error occured while recieving data from host" << std::endl;
        return;
    }
    buf[t] = '\0';
    std::cout << buf << std::endl;
}

void
Client::print_err_msg(int err)
{
    switch (err) {
    case CONNECTION_TERMINATED: std::cout << "connection terminated by host :(\nwe will miss you" << std::endl;
        return;
    case SOCKET_CR_ERR: std::cerr << "create";
        break;
    case CONNECT_ERR: std::cerr << "connect";
        break;
    case INCORRECT_IP: std::cerr << "incorrect IP";
        break;
    case INCORRECT_PORT: std::cerr << "incorrect port";
        break;
    default:
        std::cerr << "unknown";
    }
    std::cerr << " error: " << strerror(errno) << std::endl;
}

int
main(int argc, char **argv)
{
    if (argc != 3) {
        std::cout << "usage: " << argv[0] << " <ip> <port>" << std::endl;
        return 0;
    }

    try {
        Client fd(argv[1], argv[2]);
        fd.run();
    } catch (int err) {
        Client::print_err_msg(err);
    }
}
