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

1. **Static Assets**: You must have a directory named `http-root-dir` in the project root containing your web assets (HTML, images, etc.).
