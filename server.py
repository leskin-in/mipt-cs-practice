#!/usr/bin/env python3

import argparse
import json
import sys
import subprocess
import time
import os
import hashlib
import shutil
import http.server
import socket
import traceback


# Buffer size for hash calculation
HASH_BUFFER_SIZE = 4096


def handle_file(path: str, n_threads: int) -> int:
    """
    Prepare a process to send the given 'file' using the given 'n_threads' 
    threads.
    
    Returns the port used.

    Raises exceptions.
    """

    # Determine port
    port = None
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind(('', 0))
        port = s.getsockname()[1]
    
    # Determine size of one part
    file_size = os.path.getsize(path)
    part_size = (file_size // n_threads) + 1

    # Run sender
    print("Running subprocess", './sender', str(port), path, str(part_size), str(n_threads))
    process = subprocess.Popen(
        [
            './sender',
            str(port),
            path,
            str(part_size),
            str(n_threads)
        ],
        close_fds=True
    )
    
    time.sleep(1)

    return port


class FileServerRequestHandler(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        try:
            target_file = self.path

            data_raw_length = int(self.headers['Content-Length'])
            data_raw = self.rfile.read(data_raw_length)
            args = json.loads(data_raw)
            
            # Check file exists
            if not os.path.exists(target_file) or not os.path.isfile(target_file):
                self.send_error(http.server.HTTPStatus.NOT_FOUND)
                return

            # Check file is readable and calculate hash
            calculated_hash = None
            hashobj = hashlib.md5()
            try:
                with open(target_file, 'rb') as f:
                    while True:
                        chunk = f.read(HASH_BUFFER_SIZE)
                        if not chunk:
                            break
                        hashobj.update(chunk)
            except Exception as e:
                self.send_error(http.server.HTTPStatus.FORBIDDEN)
                traceback.print_exc()
                return
            calculated_hash = hashobj.hexdigest()
            
            port = None
            try:
                port = handle_file(target_file, args['threads'])
            except Exception as e:
                self.send_error(http.server.HTTPStatus.INTERNAL_SERVER_ERROR)
                traceback.print_exc()
                return

            self.send_response(http.server.HTTPStatus.OK)
            self.send_header('Content-type', 'text/plain')
            self.end_headers()
            self.wfile.write(json.dumps({
                'hash': calculated_hash,
                'port': port
            }).encode('utf-8'))

        except:
            self.send_error(http.server.HTTPStatus.INTERNAL_SERVER_ERROR)
            traceback.print_exc()

        return


def main():
    parser = argparse.ArgumentParser(description='File download server')
    parser.add_argument('port', help='server port', type=int)
    args = parser.parse_args()

    server = http.server.HTTPServer(('', args.port), FileServerRequestHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    server.server_close()


if __name__ == '__main__':
    main()
