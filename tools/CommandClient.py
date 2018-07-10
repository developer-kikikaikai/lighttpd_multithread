#!/usr/bin/python3.6
import sys

from CommandService import CommandService
def main(args):
    argle = len(args)
    command=CommandService(args[argle - 1])
    #command, env, body, socket
    if argle == 6:
        command.send_to_server(args[1], args[2], args[3], args[4])
    else:
        command.send_to_exit_server()

if __name__ == '__main__':
    main(sys.argv)
