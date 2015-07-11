# -*- coding: cp1252 -*-
# <PythonProxy.py>
#
#Copyright (c) <2009> <Fábio Domingues - fnds3000 in gmail.com>
#
#Permission is hereby granted, free of charge, to any person
#obtaining a copy of this software and associated documentation
#files (the "Software"), to deal in the Software without
#restriction, including without limitation the rights to use,
#copy, modify, merge, publish, distribute, sublicense, and/or sell
#copies of the Software, and to permit persons to whom the
#Software is furnished to do so, subject to the following
#conditions:
#
#The above copyright notice and this permission notice shall be
#included in all copies or substantial portions of the Software.
#
#THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
#EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
#OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
#NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
#HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
#WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
#FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
#OTHER DEALINGS IN THE SOFTWARE.

"""\
Copyright (c) <2009> <Fábio Domingues - fnds3000 in gmail.com> <MIT Licence>

                  **************************************
                 *** Python Proxy - A Fast HTTP proxy ***
                  **************************************

Neste momento este proxy é um Elie Proxy.

Suporta os métodos HTTP:
 - OPTIONS;
 - GET;
 - HEAD;
 - POST;
 - PUT;
 - DELETE;
 - TRACE;
 - CONENCT.

Suporta:
 - Conexões dos cliente em IPv4 ou IPv6;
 - Conexões ao alvo em IPv4 e IPv6;
 - Conexões todo o tipo de transmissão de dados TCP (CONNECT tunneling),
     p.e. ligações SSL, como é o caso do HTTPS.

A fazer:
 - Verificar se o input vindo do cliente está correcto;
   - Enviar os devidos HTTP erros se não, ou simplesmente quebrar a ligação;
 - Criar um gestor de erros;
 - Criar ficheiro log de erros;
 - Colocar excepções nos sítios onde é previsível a ocorrência de erros,
     p.e.sockets e ficheiros;
 - Rever tudo e melhorar a estrutura do programar e colocar nomes adequados nas
     variáveis e métodos;
 - Comentar o programa decentemente;
 - Doc Strings.

Funcionalidades futuras:
 - Adiconar a funcionalidade de proxy anónimo e transparente;
 - Suportar FTP?.


(!) Atenção o que se segue só tem efeito em conexões não CONNECT, para estas o
 proxy é sempre Elite.

Qual a diferença entre um proxy Elite, Anónimo e Transparente?
 - Um proxy elite é totalmente anónimo, o servidor que o recebe não consegue ter
     conhecimento da existência do proxy e não recebe o endereço IP do cliente;
 - Quando é usado um proxy anónimo o servidor sabe que o cliente está a usar um
     proxy mas não sabe o endereço IP do cliente;
     É enviado o cabeçalho HTTP "Proxy-agent".
 - Um proxy transparente fornece ao servidor o IP do cliente e um informação que
     se está a usar um proxy.
     São enviados os cabeçalhos HTTP "Proxy-agent" e "HTTP_X_FORWARDED_FOR".

"""

import socket, thread, select, sys, base64, time, errno

__version__ = '0.1.0 Draft 1'
BUFLEN = 8192
VERSION = 'Python Proxy/'+__version__
HTTPVER = 'HTTP/1.1'

username = None
password = None

class ConnectionHandler:
    def __init__(self, connection, address, timeout):
        self.client = connection
        self.client_buffer = ''
        self.timeout = timeout
        self.method, self.path, self.protocol = self.get_base_header()
        global username
        global password
        if username != None:
            auth = base64.b64encode(username + ':' + password)
            if not 'Proxy-Authorization: Basic ' + auth in self.client_buffer:
                print 'PROXY - failed authentication: %s' % self.client_buffer
                self.client.send(HTTPVER+' 401 Authentication Failed\n'+
                                'Proxy-agent: %s\n\n'%VERSION)
                self.client.close()
                return
        try:
            if self.method == 'CONNECT':
                 self.method_CONNECT()
            elif self.method in ('OPTIONS', 'GET', 'HEAD', 'POST', 'PUT',
                                 'DELETE', 'TRACE'):
                self.method_others()
        except:
            try:
                self.client.send(HTTPVER+' 502 Connection failed\n'+
                                'Proxy-agent: %s\n\n'%VERSION)
            except Exception, e:
                print 'PROXY - ', e
            self.client.close()
            return

        self.client.close()
        self.target.close()

    def get_base_header(self):
        retries = 0
        while 1:
            try:
                self.client_buffer += self.client.recv(BUFLEN)
            except socket.error, e:
                err = e.args[0]
                if (err == errno.EAGAIN or err == errno.EWOULDBLOCK) and retries < 20:
                    time.sleep(0.5)
                    retries += 1
                    continue
                raise e
            end = self.client_buffer.find('\r\n\r\n')
            if end!=-1:
                break
        line_end = self.client_buffer.find('\n')
        print 'PROXY - %s' % self.client_buffer[:line_end]#debug
        data = (self.client_buffer[:line_end+1]).split()
        self.client_buffer = self.client_buffer[line_end+1:]
        return data

    def method_CONNECT(self):
        self._connect_target(self.path)
        self.client.send(HTTPVER+' 200 Connection established\n'+
                         'Proxy-agent: %s\n\n'%VERSION)
        self.client_buffer = ''
        self._read_write()

    def method_others(self):
        self.path = self.path[7:]
        i = self.path.find('/')
        host = self.path[:i]
        path = self.path[i:]
        self._connect_target(host)
        self.target.send('%s %s %s\n' % (self.method, path, self.protocol)+
                         self.client_buffer)
        self.client_buffer = ''
        self._read_write()

    def _connect_target(self, host):
        i = host.find(':')
        if i!=-1:
            port = int(host[i+1:])
            host = host[:i]
        else:
            port = 80
        (soc_family, _, _, _, address) = socket.getaddrinfo(host, port)[0]
        self.target = socket.socket(soc_family)
        self.target.connect(address)

    def _read_write(self):
        time_out_max = self.timeout/3
        socs = [self.client, self.target]
        count = 0
        while 1:
            count += 1
            (recv, _, error) = select.select(socs, [], socs, 3)
            if error:
                break
            if recv:
                for in_ in recv:
                    data = in_.recv(BUFLEN)
                    if in_ is self.client:
                        out = self.target
                    else:
                        out = self.client
                    if data:
                        out.send(data)
                        count = 0
            if count == time_out_max:
                break

def start_server(host='localhost', port=8080, IPv6=False, timeout=100,
                  handler=ConnectionHandler):
    if IPv6==True:
        soc_type=socket.AF_INET6
    else:
        soc_type=socket.AF_INET
    soc = socket.socket(soc_type)
    soc.settimeout(120)
    print "PROXY - Serving on %s:%d."%(host, port)#debug
    soc.bind((host, port))
    soc.listen(0)
    while 1:
        thread.start_new_thread(handler, soc.accept()+(timeout,))

if __name__ == '__main__':
    listen_port = 8080
    i = 1
    while i < len(sys.argv):
        if sys.argv[i] == '--port':
            listen_port = int(sys.argv[i+1])
            i += 1
        elif sys.argv[i] == '--username':
            username = sys.argv[i+1]
            i += 1
        elif sys.argv[i] == '--password':
            password = sys.argv[i+1]
            i += 1
        else:
            if sys.argv[i] != '--help': print('PROXY - unknown option "%s"' % sys.argv[i])
            print('usage: http.py [--port <listen-port>]')
            sys.exit(1)
        i += 1
    start_server(port=listen_port)

