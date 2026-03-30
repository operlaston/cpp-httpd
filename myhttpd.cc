#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#define PORT (6969)
#define QUEUE_LENGTH (10)
#define BUF_SIZE (4096)
#define THREAD_POOL_SIZE (4)
#define PROJECT_ROOT ("/myhttpd/http-root-dir")
#define ROUTE_DEFAULT ("/myhttpd/http-root-dir/htdocs")
#define LOGS_PATH ("/myhttpd/logs")
#define BASE_ROUTE ("/myhttpd")

pthread_mutex_t mutex;
int serverfd;
double max_request_time = 0;
double min_request_time = 100;
std::string max_request_url;
std::string min_request_url;
std::string current_request_url = "";
int port;
struct timespec server_start, server_end;
int num_requests;

struct file_info {
  std::string name;
  std::string last_modified;
  size_t size;
  char type;
};

enum Mode { ITERATIVE, FORK, THREAD, POOL };

enum Mode mode;

bool is_authorized(std::string request_header) {
  // username:password
  // base64 encoding
  if (request_header.find("Authorization: Basic dXNlcm5hbWU6cGFzc3dvcmQ") !=
      std::string::npos)
    return true;
  return false;
}

void sort_files(std::vector<struct file_info> &files, const char column,
                const char order) {
  // bubble sort
  for (int i = 0; i < files.size(); i++) {
    bool swapped = false;
    for (int j = 0; j < files.size() - i - 1; j++) {
      struct file_info file_a = files[j];
      struct file_info file_b = files[j + 1];
      int result = 0;
      if (column == 'N') { // sort by name
        result = file_a.name.compare(file_b.name);
      } else if (column == 'M') { // sort by last modified
        result = file_a.last_modified.compare(file_b.last_modified);
      } else { // sort by size
        result = file_a.size - file_b.size;
      }

      if (order == 'D') { // descending
        result = result * -1;
      }

      if (result > 0) {
        swapped = true;
        std::swap(files[j], files[j + 1]);
      }
    }
    if (!swapped) {
      break;
    }
  }
}

void *handle_request(void *clientfd_ptr) {
  int clientfd = *((int *)clientfd_ptr);
  char buf[BUF_SIZE];
  memset(buf, 0, BUF_SIZE);
  int num_bytes = read(clientfd, buf, BUF_SIZE - 1);
  buf[BUF_SIZE - 1] = 0;
  num_requests++;

  std::string request(buf);
  // std::cout << request << std::endl;

  if (request.find("Authorization:") == std::string::npos) {
    std::string response_header =
        "HTTP/1.1 401 Unauthorized\r\n"
        "WWW-Authenticate: Basic realm=\"myhttpd-cs252\"\r\n\r\n";
    if (write(clientfd, response_header.c_str(), response_header.size()) < 0) {
      close(clientfd);
      free(clientfd_ptr);
      return NULL;
    }
    close(clientfd);
    free(clientfd_ptr);
    return NULL;
  }

  if (!is_authorized(request)) {
    std::string response_header = "HTTP/1.1 401 Unauthorized\r\n"
                                  "Content-type: text/html\r\n\r\n";
    if (write(clientfd, response_header.c_str(), response_header.size()) < 0) {
      close(clientfd);
      free(clientfd_ptr);
      return NULL;
    }
    std::string response_body = "<html><h1>401 Unauthorized<h1><html>";
    if (write(clientfd, response_body.c_str(), response_body.size()) < 0) {
      close(clientfd);
      free(clientfd_ptr);
      return NULL;
    }
    close(clientfd);
    free(clientfd_ptr);
    return NULL;
  }

  char *route_start = strchr(buf, ' ');

  if (route_start == NULL) {
    write(clientfd, "HTTP/1.1 400 Bad Request\r\n\r\n", 28);
    close(clientfd);
    free(clientfd_ptr);
    return NULL;
  }

  route_start++;
  char *route_end = strchr(route_start, ' ');

  if (route_end == NULL) {
    if (write(clientfd, "HTTP/1.1 400 Bad Request\r\n\r\n", 28) < 0) {
      close(clientfd);
      free(clientfd_ptr);
      return NULL;
    }
    close(clientfd);
    free(clientfd_ptr);
    return NULL;
  }

  char raw_route[route_end - route_start + 1];
  strncpy(raw_route, route_start, route_end - route_start);
  raw_route[route_end - route_start] = 0;
  std::string cpp_raw_route(raw_route);
  int logsfd = open(LOGS_PATH, O_RDWR | O_APPEND | O_CREAT, 0666);
  if (write(logsfd, cpp_raw_route.c_str(), cpp_raw_route.size()) < 0) {
    close(clientfd);
    free(clientfd_ptr);
    close(logsfd);
    return NULL;
  }
  write(logsfd, "\n", 1);
  close(logsfd);
  current_request_url =
      "http://localhost:" + std::to_string(port) + cpp_raw_route;

  if (cpp_raw_route.substr(0, 9) == "/cgi-bin/" && cpp_raw_route.size() > 9) {
    if (cpp_raw_route.find("/..") == std::string::npos &&
        cpp_raw_route.find("../") == std::string::npos) {
      std::string script_path = PROJECT_ROOT + cpp_raw_route;
      int ret = fork();
      if (ret == 0) { // child process
        size_t params_index = script_path.find("?");
        setenv("REQUEST_METHOD", "GET", 1);
        if (params_index != std::string::npos) {
          std::string cgi_params = script_path.substr(params_index + 1);
          script_path = script_path.substr(0, params_index);
          const char *query_string = cgi_params.c_str();
          setenv("QUERY_STRING", query_string, 1);
        }
        dup2(clientfd, 1); // redirect 1 (stdout) to clientfd
        std::string cgi_response_header = "HTTP/1.1 200 Document follows\r\n"
                                          "Server: cs252\r\n";
        if (write(clientfd, cgi_response_header.c_str(),
                  cgi_response_header.size()) < 0) {
          close(clientfd);
          free(clientfd_ptr);
          return NULL;
        }
        execl(script_path.c_str(), script_path.c_str(), NULL);
        perror("exec");
        close(clientfd);
        free(clientfd_ptr);
        exit(1);
      } else if (ret < 0) {
        perror("fork");
        close(clientfd);
        free(clientfd_ptr);
        exit(1);
      }
    }

    close(clientfd);
    free(clientfd_ptr);
    return NULL;
  }

  char *params_start = strchr(route_start, '?');
  if (params_start != NULL && params_start < route_end) {
    route_end = params_start;
  }

  int route_len = route_end - route_start;
  char route[route_len + 1];
  strncpy(route, route_start, route_len);
  route[route_len] = 0;

  std::string cpp_route(route);

  std::string content_type = "text/html";
  if (cpp_route.find(".png") != std::string::npos) {
    content_type = "image/png";
  } else if (cpp_route.find(".gif") != std::string::npos) {
    content_type = "image/gif";
  } else if (cpp_route.find(".svg") != std::string::npos) {
    content_type = "image/svg+xml";
  }

  std::string response_header = "HTTP/1.1 200 OK\r\n"
                                "Server: cs252\r\n"
                                "Content-type: " +
                                content_type + "\r\n\r\n";

  if (write(clientfd, response_header.c_str(), response_header.size()) < 0) {
    close(clientfd);
    free(clientfd_ptr);
    return NULL;
  }

  // check if requested index
  if (cpp_route == "/") {
    int indexfd = open("./http-root-dir/htdocs/index.html", O_RDONLY);
    struct stat index_stat;
    fstat(indexfd, &index_stat);
    size_t index_size = index_stat.st_size;
    off_t offset = 0;
    sendfile(clientfd, indexfd, &offset, index_size);
    close(indexfd);
    close(clientfd);
    free(clientfd_ptr);
    return NULL;
  }

  if (cpp_route == "/stats") {
    clock_gettime(CLOCK_MONOTONIC, &server_end);
    double elapsed = (server_end.tv_sec - server_start.tv_sec) +
                     (server_end.tv_nsec - server_start.tv_nsec) / 1e9;
    std::string server_stats = "";
    std::string max_service_time =
        max_request_time == 0 ? "N/A" : std::to_string(max_request_time);
    std::string min_service_time =
        min_request_time == 100 ? "N/A" : std::to_string(min_request_time);
    server_stats =
        "<div>Web server written by: Matthew Lee</div>"
        "<div>Uptime: " +
        std::to_string(elapsed) + "s" +
        "</div><div>Number of requests since server started: " +
        std::to_string(num_requests) +
        "</div><div>Minimum service time: " + min_service_time + "s" +
        "</div><div>Minimum service time url: " + min_request_url +
        "</div><div>Maximum service time: " + max_service_time + "s" +
        "</div><div>Maximum service time url: " + max_request_url + "</div>";
    write(clientfd, server_stats.c_str(), server_stats.size());

    close(clientfd);
    free(clientfd_ptr);
    return NULL;
  }

  else if (cpp_route == "/logs") {
    int logsfd2 = open(LOGS_PATH, O_RDWR | O_APPEND);
    char c;
    while (read(logsfd2, &c, 1) > 0) {
      if (c == '\n') {
        if (write(clientfd, "<div></div>", 11) < 0) {
          close(clientfd);
          free(clientfd_ptr);
          return NULL;
        }
      } else {
        if (write(clientfd, &c, 1) < 0) {
          close(clientfd);
          free(clientfd_ptr);
          return NULL;
        }
      }
    }

    close(logsfd2);
    close(clientfd);
    free(clientfd_ptr);
    return NULL;
  }

  const char *root_dir = PROJECT_ROOT;
  char expanded_route[256];
  memset(expanded_route, 0, 256);
  strcpy(expanded_route, root_dir);
  strcat(expanded_route, route);
  char real_path[256];
  memset(real_path, 0, 256);
  realpath(expanded_route, real_path);

  char root_real_path[256];
  memset(root_real_path, 0, 256);
  realpath(root_dir, root_real_path);

  if (strlen(root_real_path) <= strlen(real_path) &&
      strncmp(real_path, root_real_path, strlen(root_real_path)) == 0) {
    std::string full_route = "./http-root-dir" + cpp_route;
    // std::cout << "requested " << cpp_route << std::endl;

    // first check if it is a directory

    // if (full_route[full_route.size() - 1] == '/') full_route =
    // full_route.substr(0, full_route.size() - 1);
    bool fallback = false;
    std::string fallback_route = "./http-root-dir/htdocs" + cpp_route;
    DIR *dir = opendir(full_route.c_str());
    if (dir == NULL) {
      fallback = true;
      // if (default_route[default_route.size() - 1] == '/') default_route =
      // default_route.substr(0, default_route.size() - 1);
      dir = opendir(fallback_route.c_str());
    }

    if (dir != NULL) {
      std::vector<struct file_info> files;

      struct dirent *ent;
      while ((ent = readdir(dir)) != NULL) {
        std::string f_path = full_route + "/" + ent->d_name;
        if (fallback)
          f_path = fallback_route + "/" + ent->d_name;
        struct stat f_stat;
        struct file_info f_info;
        if (stat(f_path.c_str(), &f_stat) == -1) {
          perror("stat");
          close(clientfd);
          free(clientfd_ptr);
          return NULL;
        }

        f_info.name = ent->d_name;
        if (f_info.name == "." ||
            f_info.name == "..") { // skip references to parent and self
          continue;
        }
        char timebuf[100];
        struct tm *tm_info = localtime(&f_stat.st_mtime);
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M", tm_info);
        f_info.last_modified = timebuf;
        if (S_ISDIR(f_stat.st_mode)) {
          f_info.type = 'd';
          f_info.size = 0;
        } else {
          f_info.type = 'f';
          f_info.size = f_stat.st_size;
        }

        files.push_back(f_info);
      }

      // std::string cpp_raw_route(raw_route);
      size_t params_index = cpp_raw_route.find('?');
      char column = 0;
      char order = 0;
      if (params_index != std::string::npos) {
        // sort
        std::string params = cpp_raw_route.substr(params_index);
        // params format: ?C=X;O=Y
        column = params[3];
        order = params[7];
      }

      if (column != 'D' && column != 0) {
        sort_files(files, column, order);
      }

      // create HTML directory browser

      std::string response_body = "<h1>Index of " + full_route + "</h1>";
      response_body += "<table><tbody>"
                       "<tr><th valign=\"top\"><img src=\"/icons/blank.gif\" "
                       "alt=\"[ICON]\"></th>";
      std::string sorting_links = "";
      if (column == 'N' && order == 'A') {
        sorting_links += "<th><a href=\"?C=N;O=D\">Name</a></th>";
      } else
        sorting_links += "<th><a href=\"?C=N;O=A\">Name</a></th>";

      if (column == 'M' && order == 'A') {
        sorting_links += "<th><a href=\"?C=M;O=D\">Last Modified</a></th>";
      } else
        sorting_links += "<th><a href=\"?C=M;O=A\">Last Modified</a></th>";

      if (column == 'S' && order == 'A') {
        sorting_links += "<th><a href=\"?C=S;O=D\">Size</a></th>";
      } else
        sorting_links += "<th><a href=\"?C=S;O=A\">Size</a></th>";

      sorting_links += "<th><a href=\"?C=D;O=A\">Description</a></th>";
      response_body = response_body + sorting_links + "</tr>";

      int new_route_length = 0;
      bool trailing_slash = true;
      // std::cout << cpp_route << std::endl;

      for (int i = cpp_route.size() - 1; i >= 0; i--) {
        if (cpp_route[i] == '/') {
          new_route_length = i;
          if (!trailing_slash)
            break;
        } else {
          trailing_slash = false;
        }
      }

      std::string parent_route = "";
      if (new_route_length == 0) {
        parent_route = "/";
      } else
        parent_route = cpp_route.substr(0, new_route_length);

      response_body = response_body +
                      "<tr><th colspan=\"5\"><hr></th></tr>"
                      "<tr><td valign=\"top\"><img src=\"/icons/back.gif\" "
                      "alt=\"[PARENTDIR]\"></td><td><a href=\"" +
                      parent_route + "\">Parent Directory</a></td>" +
                      "<td> </td><td align=\"right\">-</td><td> </td></tr>";

      for (struct file_info f : files) {
        std::string icon_path =
            f.type == 'd' ? "/icons/folder.gif" : "/icons/unknown.gif";
        const char *filename = f.name.c_str();
        char *extension = strrchr((char *)filename, '.');
        if (extension) {
          std::string cpp_ext(extension);
          if (cpp_ext == ".tar") {
            icon_path = "/icons/tar.gif";
          } else if (cpp_ext == ".cc" || cpp_ext == ".c" || cpp_ext == ".h" ||
                     cpp_ext == ".pl" || cpp_ext == ".tcl" ||
                     cpp_ext == ".html") {
            icon_path = "/icons/text.gif";
          } else if (cpp_ext == ".xbm" || cpp_ext == ".gif" ||
                     cpp_ext == ".png" || cpp_ext == ".jpg") {
            icon_path = "/icons/image2.gif";
          }
        }

        std::string file_size = f.size == 0 ? "-" : std::to_string(f.size);
        std::string file_href = cpp_route + "/" + f.name;
        if (cpp_route[cpp_route.size() - 1] == '/')
          file_href = cpp_route + f.name;

        response_body =
            response_body + "<tr><td valign=\"top\"><img src=\"" + icon_path +
            "\" alt=\"[ ]\"></td>"
            "<td><a href=\"" +
            file_href + "\">" + f.name + "</a></td><td align=\"right\">" +
            f.last_modified + "</td><td align=\"right\">" + file_size +
            "</td><td> </td></tr>";
      }

      response_body = response_body + "<tr><th colspan=\"5\"><hr></th></tr>";
      response_body += "</tbody></table>";

      write(clientfd, response_body.c_str(), response_body.size());

      closedir(dir);
      close(clientfd);
      free(clientfd_ptr);
      return NULL;
    }

    // if not a directory, check if it is a file
    int docfd = open(full_route.c_str(), O_RDONLY);

    if (docfd != -1) {
      struct stat doc_stat;
      fstat(docfd, &doc_stat);
      size_t doc_size = doc_stat.st_size;
      off_t offset = 0;
      sendfile(clientfd, docfd, &offset, doc_size);
      close(docfd);
    } else {
      full_route = "./http-root-dir/htdocs" + cpp_route;
      docfd = open(full_route.c_str(), O_RDONLY);
      if (docfd != -1) {
        struct stat doc_stat;
        fstat(docfd, &doc_stat);
        size_t doc_size = doc_stat.st_size;
        off_t offset = 0;
        sendfile(clientfd, docfd, &offset, doc_size);
        close(docfd);
      }
    }
  }

  close(clientfd);
  free(clientfd_ptr);
  return NULL;
}

void *thread_loop(void *server_fd) {
  serverfd = *((int *)server_fd);
  while (1) {
    struct sockaddr_in clientaddr;
    socklen_t clientaddrsize = sizeof(clientaddr);

    pthread_mutex_lock(&mutex);
    int clientfd =
        accept(serverfd, (struct sockaddr *)&clientaddr, &clientaddrsize);
    pthread_mutex_unlock(&mutex);

    if (clientfd < 0) {
      perror("accept failed");
      continue;
    }

    // printf("accepted a connection\n");

    int *client_fd = (int *)malloc(sizeof(int));
    *client_fd = clientfd;
    handle_request((void *)client_fd);
  }
  return NULL;
}

void sigHandler(int signo, siginfo_t *info, void *ucontext) {
  if (signo == SIGINT) {
    close(serverfd);
    exit(1);
  } else if (signo == SIGCHLD) {
    while (waitpid(-1, NULL, WNOHANG) > 0)
      ;
  } else if (signo == SIGPIPE) {
  }
}

int main(int argc, char *argv[]) {
  // Add your HTTP implementation here
  if (argc < 2) {
    printf("usage: myhttpd [-f|-t|-p]  [<port>]\n");
    return 1;
  }
  port = PORT;

  struct sigaction signalAction;
  signalAction.sa_sigaction = sigHandler;
  sigemptyset(&signalAction.sa_mask);
  signalAction.sa_flags = SA_RESTART | SA_SIGINFO;
  int siginterr = sigaction(SIGINT, &signalAction, NULL);
  int sigchlderr = sigaction(SIGCHLD, &signalAction, NULL);
  int sigpipeerr = sigaction(SIGPIPE, &signalAction, NULL);
  if (siginterr || sigchlderr || sigpipeerr) {
    perror("sigaction");
    exit(-1);
  }

  if (argc == 2) {
    char *arg1 = argv[1];
    if (arg1[0] == '-') {
      if (strlen(arg1) != 2) {
        printf("usage: myhttpd [-f|-t|-p]  [<port>]\n");
        return 1;
      }
      if (arg1[1] == 'f') {
        mode = FORK;
      } else if (arg1[1] == 't') {
        mode = THREAD;
      } else if (arg1[1] == 'p') {
        mode = POOL;
      } else {
        printf("usage: myhttpd [-f|-t|-p]  [<port>]\n");
        return 1;
      }
    } else {
      port = atoi(argv[1]);
      if (port <= 1024 || port >= 65536) {
        printf("port must be an integer between > 1024 and < 65536\n");
        return 1;
      }
    }
  }

  else if (argc == 3) {
    std::string arg1(argv[1]);
    if (arg1 == "-f") {
      mode = FORK;
    } else if (arg1 == "-t") {
      mode = THREAD;
    } else if (arg1 == "-p") {
      mode = POOL;
    } else {
      printf("usage: myhttpd [-f|-t|-p]  [<port>]\n");
      return 1;
    }

    port = atoi(argv[2]);
    if (port <= 1024 || port >= 65536) {
      printf("port must be an integer between > 1024 and < 65536\n");
      return 1;
    }
  }

  else if (argc > 3) {
    printf("usage: myhttpd [-f|-t|-p]  [<port>]\n");
    return 1;
  }

  printf("mode is %d\n", mode);
  printf("0 = iterative, 1 = fork, 2 = thread, 3 = pool of threads\n");

  std::cout << "port " << port << std::endl;
  struct sockaddr_in serveraddr;
  memset(&serveraddr, 0, sizeof(struct sockaddr_in));
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_addr.s_addr = INADDR_ANY;
  serveraddr.sin_port = htons((u_short)port);

  serverfd = socket(PF_INET, SOCK_STREAM, 0);
  if (serverfd < 0) {
    perror("socket");
    exit(1);
  }

  int optval = 1;
  int err = setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, (char *)&optval,
                       sizeof(int));
  if (err < 0) {
    perror("setsockopt");
    exit(1);
  }

  err = bind(serverfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
  if (err < 0) {
    perror("bind");
    exit(1);
  }

  err = listen(serverfd, QUEUE_LENGTH);
  if (err < 0) {
    perror("listen");
    exit(1);
  }

  printf("server started...\n");
  clock_gettime(CLOCK_MONOTONIC, &server_start);

  if (mode == POOL) {
    pthread_mutex_init(&mutex, NULL);
    pthread_t threads[THREAD_POOL_SIZE];
    int *server_fd = (int *)malloc(sizeof(int));
    *server_fd = serverfd;
    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
      pthread_attr_t attr;      // attributes/settings of the thread
      pthread_attr_init(&attr); // initialize default attributes
      // pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED); // auto
      // resource cleanup
      pthread_create(&threads[i], &attr, thread_loop,
                     (void *)server_fd); // create the thread
    }
    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
      pthread_join(threads[i], NULL);
    }
  }

  else {
    while (1) {
      struct sockaddr_in clientaddr;
      socklen_t clientaddrsize = sizeof(clientaddr);
      int *clientfd_ptr = (int *)malloc(sizeof(int));
      int clientfd =
          accept(serverfd, (struct sockaddr *)&clientaddr, &clientaddrsize);
      if (clientfd < 0) {
        perror("accept failed");
        continue;
      }

      // logging source host (ip address of requester)
      int logsfd = open(LOGS_PATH, O_RDWR | O_APPEND);
      if (logsfd < 0) {
        perror("open");
        exit(1);
      }
      char ipstr[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &clientaddr.sin_addr, ipstr, sizeof(ipstr));
      write(logsfd, ipstr, sizeof(ipstr));
      write(logsfd, " ", 1);
      close(logsfd);

      printf("accepted a connection\n");

      // start time for request
      clock_t start, end;
      double cpu_time_used;
      start = clock();

      *clientfd_ptr = clientfd;

      if (mode == FORK) {
        int ret = fork();
        if (ret == 0) {
          // printf("I am child process\n");
          handle_request((void *)clientfd_ptr);
          exit(0);
        } else if (ret < 0) {
          perror("fork");
          return 1;
        }
        close(clientfd);
        free(clientfd_ptr);
      } else if (mode == THREAD) {
        pthread_t thread;         // the thread itself
        pthread_attr_t attr;      // attributes/settings of the thread
        pthread_attr_init(&attr); // initialize default attributes
        pthread_attr_setdetachstate(
            &attr, PTHREAD_CREATE_DETACHED); // auto resource cleanup
        pthread_create(&thread, &attr, handle_request,
                       (void *)clientfd_ptr); // create the thread
      } else {
        handle_request((void *)clientfd_ptr);
      }

      end = clock();
      cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;
      pthread_mutex_lock(&mutex);
      if (cpu_time_used > max_request_time) {
        max_request_time = cpu_time_used;
        max_request_url = current_request_url;
      }
      if (cpu_time_used < min_request_time) {
        min_request_time = cpu_time_used;
        min_request_url = current_request_url;
      }
      pthread_mutex_unlock(&mutex);
    }
  }

  return 1;
}
