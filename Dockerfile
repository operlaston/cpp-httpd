# Use an official GCC image which includes make and g++
FROM gcc:latest

# Set the working directory to match the absolute paths in your C++ code
WORKDIR /myhttpd

# Copy your source code and Makefile
# (Assuming your source file is named myhttpd.cc based on the Makefile target)
COPY myhttpd.cc Makefile ./

# Copy the static assets directory into the container
COPY http-root-dir/ ./http-root-dir/

# Create logs file
RUN touch logs

# Compile the server using your Makefile
RUN make

# Expose the default port
EXPOSE 6969

# Command to run the server
CMD ["./myhttpd", "-p", "6969"]
