#!/usr/bin/env python3
# A pretend home router for tests of the game's UPnP port forwarding: it
# answers SSDP searches (sent straight to it: COOP_UPNP_SSDP=127.0.0.1:<udp
# port>), serves a device description and takes AddPortMapping and
# DeletePortMapping. It doesn't forward anything (on one machine the port is
# reachable anyway); it writes what it was asked to <log>.
#
#   tools/fake_igd.py <udp port> <http port> <log> [refuse]
#
# "refuse" answers every mapping with UPnP error 718 (port taken), as a
# router whose port is used by another computer.

import http.server
import re
import socket
import sys
import threading

udp_port = int(sys.argv[1])
http_port = int(sys.argv[2])
log_path = sys.argv[3]
refuse = len(sys.argv) > 4 and sys.argv[4] == "refuse"

SERVICE = "urn:schemas-upnp-org:service:WANIPConnection:1"
DESCRIPTION = """<?xml version="1.0"?>
<root xmlns="urn:schemas-upnp-org:device-1-0">
<device><deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType>
<deviceList><device><deviceType>urn:schemas-upnp-org:device:WANDevice:1</deviceType>
<deviceList><device><deviceType>urn:schemas-upnp-org:device:WANConnectionDevice:1</deviceType>
<serviceList><service><serviceType>%s</serviceType><serviceId>urn:upnp-org:serviceId:WANIPConn1</serviceId>
<controlURL>/ctl/IPConn</controlURL><eventSubURL>/evt/IPConn</eventSubURL><SCPDURL>/WANIPCn.xml</SCPDURL></service></serviceList>
</device></deviceList></device></deviceList></device></root>
""" % SERVICE


def log(line):
    with open(log_path, "a") as f:
        f.write(line + "\n")


def ssdp():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", udp_port))
    while True:
        data, address = sock.recvfrom(2048)
        if b"M-SEARCH" in data:
            answer = ("HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=120\r\nST: %s\r\nUSN: uuid:fake::igd\r\n"
                      "LOCATION: http://127.0.0.1:%d/rootDesc.xml\r\nSERVER: fake/1.0 UPnP/1.1\r\n\r\n") % (SERVICE, http_port)
            sock.sendto(answer.encode(), address)


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body = DESCRIPTION.encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/xml")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        data = self.rfile.read(int(self.headers.get("Content-Length", "0"))).decode()
        action = self.headers.get("SOAPAction", "").strip('"').split("#")[-1]
        fields = dict(re.findall(r"<(New\w+)>([^<]*)</New\w+>", data))
        log("%s %s" % (action, " ".join("%s=%s" % item for item in sorted(fields.items()))))
        if action == "AddPortMapping" and refuse:
            body = ("<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body><s:Fault>"
                    "<faultcode>s:Client</faultcode><faultstring>UPnPError</faultstring><detail><UPnPError xmlns=\"urn:schemas-upnp-org:control-1-0\">"
                    "<errorCode>718</errorCode><errorDescription>ConflictInMappingEntry</errorDescription></UPnPError></detail></s:Fault></s:Body></s:Envelope>").encode()
            self.send_response(500)
        else:
            body = ("<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body>"
                    "<u:%sResponse xmlns:u=\"%s\"></u:%sResponse></s:Body></s:Envelope>" % (action, SERVICE, action)).encode()
            self.send_response(200)
        self.send_header("Content-Type", "text/xml")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        pass


threading.Thread(target=ssdp, daemon=True).start()
http.server.ThreadingHTTPServer(("127.0.0.1", http_port), Handler).serve_forever()
