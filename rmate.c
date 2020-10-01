#include <limits.h> //Import limit.h header file
#include <stdio.h>// IMports Stdio header file
#include <stdlib.h>//Import stlib header file
#include <ctype.h>//Import ctype header file
#include <unistd.h>//Import unistd header file
#include <errno.h>//Import errno header file
#include <string.h>//Import string header file
#include <netdb.h>//Import netdb header file
#include <fcntl.h>//Import fcnt1 header file
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#include "version.h"


#define HOST_ENV "RMATE_HOST"
#define PORT_ENV "RMATE_PORT"
#define DEFAULT_HOST "localhost"
#define DEFAULT_PORT "52698"

#define MAXDATASIZE 1024

enum CMD_STATE {
    CMD_HEADER,
    CMD_CMD,
    CMD_VAR,
    CMD_END
};

struct cmd {
    enum CMD_STATE state;
    enum {UNKNOWN, CLOSE, SAVE} cmd_type;
    char* filename;
    size_t file_len;
};

int connect_mate(const char* host, const char* port) {
	int sockfd = -1;  
	struct addrinfo hints, *servinfo, *p;
	int rv;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((rv = getaddrinfo(host, port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return -1;
	}

	// loop through all the results and connect to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			continue;
		}

		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
            sockfd = -1;
			continue;
		}

		break;
	}

	freeaddrinfo(servinfo); // all done with this structure
    
    return sockfd;
}

int send_open(int sockfd, const char* filename, int fd) {
    char *fdata;
    char resolved[PATH_MAX];
    struct stat st;
    
    if(fstat(fd, &st) == -1) {
        perror("stat");
        return -1;
    }
    
    if((fdata = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0)) == MAP_FAILED) {
        perror("mmap");
        return -1;
    }
    
    dprintf(sockfd, "open\n");
    dprintf(sockfd, "display-name: %s\n", filename);
    dprintf(sockfd, "real-path: %s\n", realpath(filename, resolved));
    dprintf(sockfd, "data-on-save: yes\n");
    dprintf(sockfd, "re-activate: yes\n");
    dprintf(sockfd, "token: %s\n", filename);
    dprintf(sockfd, "data: %zd\n", st.st_size);
    write(sockfd, fdata, st.st_size);
    dprintf(sockfd, "\n.\n");
    
    munmap(fdata, st.st_size);
    return 0;
}

int receive_save(int sockfd, char* rem_buf, size_t rem_buf_len, const char* filename, size_t filesize) {
    char *fdata;
    int fd, numbytes;

    if((fd = open(filename, O_RDWR)) == -1) {
        perror("open");
        return -1;
    }
    
    if(ftruncate(fd, filesize) == -1) {
        perror("ftruncate");
        return -1;
    }
    
    if((fdata = mmap(NULL, filesize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) {
        perror("mmap");
        return -1;
    }
    
    if(rem_buf_len > filesize)
        rem_buf_len = filesize;
    
    memcpy(fdata, rem_buf, rem_buf_len);
    if((numbytes = read(sockfd, fdata + rem_buf_len, filesize - rem_buf_len)) == -1) {
        perror("read");
        return -1;
    }
    
    if(munmap(fdata, filesize) == -1) {
        perror("munmap");
        return -1;
    }
        
    close(fd);
    return 0;
}

ssize_t readline(char* buf, size_t len) {
    char *cmd_str;
    ssize_t line_len;
    
    cmd_str = memchr(buf, '\n', len);
    if(!cmd_str)
        return -1;
    
    line_len = cmd_str - buf;
    if(line_len > 0 && cmd_str[-1] == '\r')
        cmd_str[-1] = '\0';
    cmd_str[0] = '\0';
    
    return line_len + 1;
}

void handle_var(const char* name, const char* value, struct cmd *cmd_state) {
    if(!strcmp(name, "token"))
        cmd_state->filename = strdup(value);
    
    if(!strcmp(name, "data"))
        cmd_state->file_len = strtoul(value, NULL, 10);
}

ssize_t handle_line(int sockfd, char* buf, size_t len, struct cmd *cmd_state) {
    ssize_t read_len = -1;
    size_t token_len;
    char *name, *value;
    
    switch(cmd_state->state) {
    case CMD_HEADER:
        if((read_len = readline(buf, len)) > 0) {
            cmd_state->state = CMD_CMD;
        }
        
        break;
    case CMD_CMD:
        if((read_len = readline(buf, len)) > 0 && *buf != '\0') {
            free(cmd_state->filename);
            memset(cmd_state, 0, sizeof(*cmd_state));
            
            if(!strncmp(buf, "close", read_len))
                cmd_state->cmd_type = CLOSE;
            
            if(!strncmp(buf, "save", read_len))
                cmd_state->cmd_type = SAVE;
            
            cmd_state->state = CMD_VAR;
        }
        
        break;
    case CMD_VAR:
        if((read_len = readline(buf, len)) < 0) 
            goto err;
        
        if(*buf == '\0')
            goto err;
        
        if((token_len = strcspn(buf, ":")) >= (size_t) read_len)
            goto err;
            
        cmd_state->state = CMD_VAR;
        name = buf;
        name[token_len] = '\0';
        value = name + token_len + 1;
        value += strspn(value, " ");
    
        handle_var(name, value, cmd_state);
        if(!strcmp(name, "data"))
            receive_save(sockfd, buf + read_len, len - read_len, cmd_state->filename, cmd_state->file_len);
        break;
        
        err:
        cmd_state->state = CMD_CMD;
        break;
    default:
        break;
    }
    
    return read_len;
}

ssize_t handle_cmds(int sockfd, char* buf, size_t len, struct cmd *cmd_state) {
    size_t total_read_len = 0;
    
    while(total_read_len < len) {
        ssize_t read_len;
        if((read_len = handle_line(sockfd, buf, len, cmd_state)) == -1)
            return -1;
        
        buf += read_len;
        total_read_len += read_len;
    }
    
    return total_read_len;
}

void version(void) {
  char cc[256];
  time_t tc = COMMIT_DATE;

  strftime(cc, sizeof(cc), "%Y-%m-%d", localtime(&tc));
  printf("rmate %s (%s)\n", BUILD_VERSION, cc);
  printf("Copyright (c) 2014 Mael Clerambault\n");
  exit(0);
}

void usage(void) {
  fprintf(stderr, "Usage: rmate [options] file\n");
  fprintf(stderr, "\nOptions:\n");
  fprintf(stderr, "  -h\t\tPrint this help\n");
  fprintf(stderr, "  -v\t\tPrint version informations\n");
  fprintf(stderr, "  -H HOST\tConnect to host. Defaults to $%s or %s.\n", HOST_ENV, DEFAULT_HOST);
  fprintf(stderr, "  -p PORT\tPort number to use for connection. Defaults to $%s or %s.\n", PORT_ENV, DEFAULT_PORT);
  fprintf(stderr, "  -w\t\tWait for file to be closed by TextMate.\n");
  exit(0);
}

int main(int argc, char *argv[])
{
    int ch;
	int sockfd, fd, numbytes;
    char *filename;
    char* host = getenv(HOST_ENV);
    char* port = getenv(PORT_ENV);
    int need_wait = 0;
    struct cmd cmd_state = {0};
    
    if(!host)
        host = DEFAULT_HOST;
    if(!port)
        port = DEFAULT_PORT;
    
    signal(SIGCHLD, SIG_IGN);

    while ((ch = getopt(argc, argv, "whvH:p:")) != -1) {
      switch(ch) {
        case 'w':
          need_wait = 1;
          break;
		case 'H':
          host = optarg;
          break;
    	case 'p':
          port = optarg;
          break;
        case 'v':
          version();
          break;
        case 'h':
        default:
          usage();
          break;
      }
    }
    argc -= optind; 
    argv += optind;
    
    if(argc < 1)
        usage();
    
    if(!need_wait && fork() > 0)
        exit(0);
    
    if((sockfd = connect_mate(host, port)) == -1) {
        fprintf(stderr, "Could not connect\n");
        return -1;
    }
    
    filename = argv[0];
    if((fd = open(filename, O_RDONLY)) == -1) {
        perror("open");
        return -1;
    }
    
    send_open(sockfd, filename, fd);
    close(fd);

    while(1) {
	    char buf[MAXDATASIZE];
        
        if((numbytes = read(sockfd, buf, MAXDATASIZE-1)) == -1) {
            perror("read");
            return -1;
        }
    
        if(numbytes == 0)
            break;
        buf[numbytes] = '\0';
        
        handle_cmds(sockfd, buf, numbytes, &cmd_state);
    }

	close(sockfd);

	return 0;
}
