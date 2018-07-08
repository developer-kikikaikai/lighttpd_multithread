#for socket
import os, sys, socket, copy, time
#for exec command
import subprocess

class CommandService:

    #constructor, set socket path
    def __init__(self, id):
        self._socket_path="/var/run/command_service_"+ str(id) + ".sock"
        self._delimiter="__ComSvc__"

    #for server
    def run_server(self):
        if self._is_start():
            print("Already start server")
            return
        else:
            print("Not start server")

        print("Start server")
        try:
            waitsock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
            waitsock.bind(self._socket_path)
        except socket.error as msg:
            print(msg)
            waitsock.close()
            os.unlink(self._socket_path)
            return

        run=True
        while run:
            try:
                req_cmd_byte = waitsock.recv(8192)
                if not req_cmd_byte:
                    print("Exit")
                    break
                self._exec_request(req_cmd_byte.decode('utf-8').split(self._delimiter))
            except socket.error as msg:
                print(msg)
                run=False
            except:
                print("Failed to call")
                run=False

        waitsock.close()
        os.unlink(self._socket_path)
        print("Exit server")

    def _is_start(self):
        return os.path.exists(self._socket_path)

    #parse request, call command and send response
    def _exec_request(self, req_cmd):
        command=req_cmd[0]
        envs=req_cmd[1]
        body=req_cmd[2]
        sockpath=req_cmd[3]
        print("_exec_request:commnd:" + command)
        print("envs:" + envs)
        print("body:" + body)
        print("socket:" + sockpath)

        #keep old
        keep_env={}
        none_env=[]
        for env in envs.split(' '):
            try:
                #separate key and value
                index = env.index('=')
                key=env[:index]
                value=env[index+1:]

                #keep old first
                if key in os.environ:
                    keep_env[key]=os.environ.get(key)
                else:
                    none_env.append(key)

                #Update value
                os.environ[key]=value
            except:
                continue
        callresp = self._call_cmd_with_env(command.split(' ') , body)

        if len(callresp):
            self._send(callresp, sockpath)
        else:
            self._send(b"status: 404\r\n\r\n", sockpath)

        #reset env
        #os.environ = copy.deepcopy(original_env)
        for key,value in keep_env.items():
            os.environ[key] = value
        for remove_key in none_env:
            os.environ.pop(remove_key)

    def _call_cmd_with_env(self, args, body):
        p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        try:
            if len(body) != 0:
                p.stdin.write(open(body, "r").read().encode ('ascii'))
        except:
            print("skip\n")
        return p.communicate()[0]

    #for client
    def send_to_server(self, command, query, body, response_sockpath):
        request=command + self._delimiter + query + self._delimiter + body + self._delimiter + response_sockpath
        self._send(request.encode('utf-8'), self._socket_path)

    def send_to_exit_server(self):
        self._send(b"", self._socket_path)

    def _send(self, request, sockpath):
        try:
            sendsock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
            sendsock.sendto(request, sockpath)
        except socket.error as msg:
            print(msg)
        finally:
            time.sleep(1)
            sendsock.close()
