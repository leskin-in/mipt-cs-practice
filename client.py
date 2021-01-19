#!/usr/bin/env python3

import argparse
import socket
import http.client
import json
import sys
import subprocess
import time
import os
import hashlib
import shutil


# Buffer size for hash calculation
HASH_BUFFER_SIZE = 4096


def main():
    parser = argparse.ArgumentParser(description='File download client')
    parser.add_argument('address', help='server address, in host:port format')
    parser.add_argument('threads', type=int, help='number of threads to use to receive file')
    parser.add_argument('file', help='file to download')
    parser.add_argument('ofile', help='output file')
    args = parser.parse_args()

    assert args.threads > 0

    # Request metadata
    connection = http.client.HTTPConnection(args.address)
    connection.request('POST', args.file, body=json.dumps({
        'threads': args.threads
    }))
    http_response_raw = connection.getresponse()
    if (http_response_raw.status != http.HTTPStatus.OK):
        raise http.client.HTTPException(f'{http_response_raw.reason} [{http_response_raw.status}]')
    server_response_raw = http_response_raw.read()
    http_response_raw.close()
    connection.close()

    server_response = json.loads(server_response_raw)
    path = "/tmp/{}_{}".format(int(time.time()), os.getpid())

    # Receive data
    os.mkdir(path)
    print("Running subprocess", "./receiver", args.address.split(":", 1)[0], str(server_response['port']), str(args.threads), path)
    subprocess.run(
        [
            "./receiver", 
            args.address.split(":", 1)[0], 
            str(server_response['port']), 
            str(args.threads), 
            path
        ],
        check=True
    )

    # Restore the file
    with open(args.ofile, 'wb') as ofile:
        subprocess.run(
            'cat {}'.format(path + '/*'),
            shell=True,
            stdout=ofile,
            check=True
        )

    # Drop parts
    shutil.rmtree(path, ignore_errors=True)

    # Check hash
    hashobj = hashlib.md5()
    with open(args.ofile, 'rb') as f:
        while True:
            chunk = f.read(HASH_BUFFER_SIZE)
            if not chunk:
                break
            hashobj.update(chunk)
    calculated_hash = hashobj.hexdigest()
    if calculated_hash != server_response['hash']:
        raise RuntimeError('Received file\'s hash \'{}\' does not match the expected one \'{}\'', calculated_hash, server_response['hash'])


if __name__ == '__main__':
    main()
