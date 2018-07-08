#!/usr/bin/python3.6
import sys

from CommandService import CommandService
def main(args):
    command=CommandService(args[1])
    command.run_server()

if __name__ == '__main__':
    main(sys.argv)
