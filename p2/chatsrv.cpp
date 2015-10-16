#include <iostream>
#include <map>
#include <cstdio>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>

/* Linux */

enum { TCP_ERR, SO_REUSEADDR_ERR, CREATE_ERR, BIND_ERR, NONBLOCK_ERR, LISTEN_ERR, EPOLL_ERR, BUF_MAX = 1024 };

class MasterSocket
{
public:
    MasterSocket(short int port);
    MasterSocket() {};
    ~MasterSocket();
    static void print_err_msg(int err);
    void run(int max_events);
private:
    int msfd;
    std::map <int, int> handled;
    static int set_nonblock(int fd);  
    int epfd;
    void accept_connection();
    void recieve_message(std::map <int, int>::iterator it);
    union cast_to_void {
        void *ptr;
        std::map <int, int>::iterator it;
        cast_to_void() {};
        ~cast_to_void() {};
    };
    struct epoll_event *events;
};

MasterSocket::~MasterSocket()
{
    close(this->msfd);
    for (auto i: this->handled) {
        shutdown(i.first, SHUT_RDWR);
        close(i.first);
    }
    free(this->events);
    close(this->epfd);
}

MasterSocket::MasterSocket(short int port)
{
    this->msfd = socket(AF_INET, SOCK_STREAM, 0);
    if (this->msfd < 0)
        throw int(CREATE_ERR);

    struct sockaddr_in stsa;
    memset(&stsa, 0, sizeof(stsa));
    stsa.sin_family = AF_INET;
    stsa.sin_port = htons(port);
    stsa.sin_addr.s_addr = htonl(INADDR_ANY);

    int optval = 1;
    if (setsockopt(this->msfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) != 0)
        throw int(SO_REUSEADDR_ERR);

        
    if (bind(this->msfd, (struct sockaddr *) &stsa, sizeof(stsa)) != 0)
        throw int(BIND_ERR);

    if (set_nonblock(this->msfd) != 0)
        throw int(NONBLOCK_ERR);

    if (listen(this->msfd, SOMAXCONN) != 0)
        throw int(LISTEN_ERR);

    this->epfd = epoll_create1(0);
    if (this->epfd < 0)
        throw int(EPOLL_ERR);
    
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.ptr = NULL;

    if (epoll_ctl(this->epfd, EPOLL_CTL_ADD, this->msfd, &event) != 0)
        throw int(EPOLL_ERR);
}

int
MasterSocket::set_nonblock(int fd)
{
	int flags;
	if ((flags = fcntl(fd, F_GETFL, 0)) == -1)
		flags = 0;
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void
MasterSocket::print_err_msg(int err)
{
        switch (err) {
        case TCP_ERR: std::cerr << "seems like TCP not supported";
            break;
        case SO_REUSEADDR_ERR: std::cerr << "setting SO_REUSEADDR";
            break;
        case CREATE_ERR: std::cerr << "create";
            break;
        case BIND_ERR: std::cerr << "bind";
            break;
        case NONBLOCK_ERR: std::cerr << "set_nonblock";
            break;
        case LISTEN_ERR: std::cerr << "listen";
            break;
        case EPOLL_ERR: std::cerr << "epoll";
            break;
        default:
            std::cerr << "unknown";
        }
        std::cerr << " error: " << strerror(errno) << std::endl;
}

void
MasterSocket::run(int max_events)
{
    this->events = (struct epoll_event *) calloc(max_events, sizeof(*this->events));
    while (true) {
        int N = epoll_wait(this->epfd, this->events, max_events, -1);
        for (int i = 0; i < N; i++) {
            if (this->events[i].data.ptr == NULL) {
                this->accept_connection();
                continue;
            }
            std::map <int, int>::iterator it;
            {
                cast_to_void t;
                t.ptr = this->events[i].data.ptr;
                it = t.it;
            }
            this->recieve_message(it);
        }
    }
}

void
MasterSocket::accept_connection()
{
     struct sockaddr_in stsa;
     socklen_t size = sizeof(stsa);
     memset(&stsa, 0, sizeof(stsa));
     int fd = accept(this->msfd, (struct sockaddr *) &stsa, &size);
     if (fd < 0) {
         std::cerr << "error accepting connection" << std::endl;
         return;
     }
     auto tmp = this->handled.insert(std::make_pair(fd, stsa.sin_addr.s_addr));
     if (tmp.second == false) {
         std::cerr << "the same file descriptors" << std::endl;
         return;
     }
     struct epoll_event event;
     event.events = EPOLLIN;
     {
         cast_to_void t; // Is there best way?
         t.it = tmp.first;
         event.data.ptr = t.ptr;
     }
     
     if (set_nonblock(fd)) {
         std::cerr << "error setting nonblock" << std::endl;
         this->handled.erase(tmp.first);
         return;
     }
     
     if (epoll_ctl(this->epfd, EPOLL_CTL_ADD, fd, &event) != 0) {
         std::cerr << "error register new file descriptor on the epoll instance" << std::endl;
         this->handled.erase(tmp.first);
         return;
     }
     char buf[BUF_MAX];

     char *ip = ((char *) &tmp.first->second);
     int ip_start, len;
     std::snprintf(buf, sizeof(buf),
                   "Welcome, %n%hhu.%hhu.%hhu.%hhu%n", &ip_start, ip[0], ip[1], ip[2], ip[3], &len);
     std::cout << "accepted connection from " << buf + ip_start << std::endl;
     if (send(fd, buf, len, 0) != len) {
         std::cerr << "welcome message not delivered to " << buf + ip_start << std::endl;
     }
}

void
MasterSocket::recieve_message(std::map <int, int>::iterator it)
{
    char buf[BUF_MAX + 100];
    int ip_end;
    char *ip = (char *) &(it->second);
    snprintf(buf, sizeof(buf), "%hhu.%hhu.%hhu.%hhu: %n", ip[0], ip[1], ip[2], ip[3], &ip_end);
    int buf_sz;
    buf_sz = (int) recv(it->first, buf + ip_end, BUF_MAX, 0);
    if (buf_sz < 0) {
        buf[ip_end - 2] = '\0';
        std::cerr << "error occured while recieving from " << buf << std::endl;
        return;
    }
    if (buf_sz == 0) {
        shutdown(it->first, SHUT_RDWR);
        close(it->first);
        this->handled.erase(it);
        buf[ip_end - 2] = '\0';
        std::cout << "connection connection terminated (" << buf << ")" << std::endl;
        return;
    }
    buf[buf_sz + ip_end] = 0;
    std::cout << buf << std::endl; // changed!
    for (auto i: this->handled) {
        if (send(i.first, buf, buf_sz + ip_end, 0) != buf_sz + ip_end) {
            char tmp[BUF_MAX];
            char *ip_from, *ip_to;
            ip_from = (char *) &it->second;
            ip_to = (char *) &i.second;
            snprintf(tmp, sizeof(tmp),
                     "message from %hhu.%hhu.%hhu.%hhu not delivered to %hhu.%hhu.%hhu.%hhu",
                     ip_from[0], ip_from[1], ip_from[2], ip_from[3],
                     ip_to[0], ip_to[1], ip_to[2], ip_to[3]);
            std::cerr << tmp << std::endl;
        }
    }
}

int
main()
{
    if (sizeof(void *) != sizeof(std::map <int, int>::iterator)) {
        std::cout << "Sorry, this computer is not supported" << std::endl;
        return 0;
    }

    enum { PORT_NUM = 3100 };
    try {
        MasterSocket fd = MasterSocket(PORT_NUM);
        fd.run(32);
    } catch (int i) {
        MasterSocket::print_err_msg(i);
    }
}
