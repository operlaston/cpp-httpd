# cpp-httpd
An http server written for the CS252: Systems Programming Course at Purdue with some slight modifications.
It supports basic http authentication, cgi-script execution, and directory browsing with sorting of modified date,
name, and size (which is identical to Apache Server)

## Prerequisites

<!-- Depending on how you choose to run the server, you will need the following dependencies installed on your system: -->

<!-- ### For Local Execution -->
<!-- * **C++ Compiler**: GCC (`g++`) supporting C++11 or higher. -->
<!-- * **Make**: To use the provided Makefile. -->
<!-- * **POSIX Threads (pthreads)**: Required for the threading and thread pool concurrency models (standard on most Linux/macOS distributions). -->

<!-- ### For Docker Execution -->
* **Docker**: To build the container image.
* **Docker Compose**: To orchestrate the volume mounts and port mapping.

## Setup

Before running the server, ensure that your project directory is structured correctly.

* **Static Assets**: You must have a directory named `http-root-dir` in the project root containing your web assets (HTML, images, etc.).
* **Root**: The server also expects there to be a http-root-dir/htdocs/index.html to route to when '/' is requested.

## Run

You can build and run the server using `docker compose up --build`<br>
When you want to stop the container, run `docker compose down`

## Additional Information
* **Port Number** The server runs on port 6969
* **Stats** You can access server statistics by requesting /stats
* **Logs** You can access server logs by requesting /logs
* **Concurrency Modes** There are technically 3 concurrency modes (pool of threads, thread-per-request, and fork-per-request), but the docker container will always run pool of threads.
