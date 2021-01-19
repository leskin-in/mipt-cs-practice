# Simple file transfer application

This application allows to transfer files from one machine to another in multiple threads.

This is written in ~7 hours of *pure* (coffee breaks excluded) time.

Features:
* Unencoded TCP is used to transfer data
* Number of threads is variable (maximum is `255`), and is defined by client
* Server will send any file it can read

## Requirements
* UNIX-like OS **with POSIX threads support**
* Python 3 (developed under Python 3.8)
* GNU make
* C compiler able to link `glibc`

## Build
```shell script
make
```

## Run
### Client
```
$ ./client.py --help
usage: client.py [-h] address threads file ofile

File download client

positional arguments:
  address     server address, in host:port format
  threads     number of threads to use to receive file
  file        file to download
  ofile       output file

optional arguments:
  -h, --help  show this help message and exit
```

* `file` is the path to file to download on server. It is passed as a string
* `ofile` is the output file destination on local machine

Client must have access to `/tmp`. It creates a directory and temporary files in it.

### Server
```
$ ./server.py --help
usage: server.py [-h] port

File download server

positional arguments:
  port        server port

optional arguments:
  -h, --help  show this help message and exit
```

When the server is stopped (e.g. by `SIGINT`), all its child processes are also stopped. Their sockets are properly closed.

## Description of the implementation & notes
The implementation is straightforward, but is not quite polished. It works as described below; errors are handled, but there are probably cases which are not handled.

1. `server` listens on its `port` for incoming HTTP connections.
    * This is implemented by means of Python's `http.server`
1. `client` sends a POST HTTP request, whose path is `file`, and the content is JSON with number of threads
    * POST is used so that it is not cached, although GET is semantically better. I could set proper headers, but didn't do that in time. And now this requires implementation changes
1. `server` checks the requested `file` exists and is readable, and calculates its MD5 hash.
1. `server` starts a (asynchronous) subprocess `sender`, whose purpose is to send the `file` in multiple threads. The TCP port it will listen on is determined by `server` in advance
1. `server` sends the hash and the port of `sender` to `client`
1. `client` starts a (synchronous) subprocess `receiver`, whose purpose is to receive the `file` in multiple threads. The TCP port it should connect to is obtained from the server's response
1. `sender` opens a listening TCP connection on the port provided to it as a command line parameter, and awaits for `receiver`s. When a new receiver is connected, a thread is created
1. `receiver` creates a set of threads, and each is provided with an independent socket. They all connect to `sender`; after a successful connection, they send a single byte, which contains thread ID (from 0 to `threads - 1`)
    * This is why no more than `255` threads are allowed. `255` seems a reasonable limitation. Of course, the protocol can be changed to use more threads; but since this would require to deal with endianness, the single-byte implementation is used
1. `sender`'s thread obtains the thread ID (these may come in "wrong" order, of course), and calculates the offset from which it should start to read `file`. The file is split into (logical) blocks by `server` in advance by a simple expression: `block_size = size_of_file // threads + 1`
    * A production solution would split the file into blocks whose size corresponds to the size of a block on file system, or in memory
1. `sender` reads the file into a buffer, and then sends the buffer over HTTP
    * The buffer is of fixed size. In production, its size can be made variable
1. `receiver` receives the data into a buffer. It then writes the buffer into a "part" file, which is located in a subdirectory of `/tmp` determined by `client` in advance
1. When all data is transmitted, `sender` exits quitely. `receiver` does the same
1. `client` calls `cat` to concatenate all "part" files into the `ofile`
