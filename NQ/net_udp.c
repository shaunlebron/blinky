/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <unistd.h>
#include <net/if.h>

#include "common.h"
#include "console.h"
#include "net.h"
#include "net_udp.h"
#include "quakedef.h"
#include "sys.h"

/* socket for fielding new connections */
static int net_acceptsocket = -1;
static int net_controlsocket;
static int net_broadcastsocket = 0;
static struct sockaddr_in broadcastaddr;

/*
 * There are three addresses that we may use in different ways:
 *   myAddr	- This is the "default" address returned by the OS
 *   localAddr	- This is an address to advertise in CCREP_SERVER_INFO
 *		 and CCREP_ACCEPT response packets, rather than the
 *		 default address (sometimes the default address is not
 *		 suitable for LAN clients; i.e. loopback address). Set
 *		 on the command line using the "-localip" option.
 *   bindAddr	- The address to which we bind our network socket. The
 *		 default is INADDR_ANY, but in some cases we may want
 *		 to only listen on a particular address. Set on the
 *		 command line using the "-ip" option.
 */
static struct in_addr myAddr;
static struct in_addr localAddr;
static struct in_addr bindAddr;
static char ifname[IFNAMSIZ];


static int
udp_scan_iface(int sock)
{
    struct ifconf ifc;
    struct ifreq *ifr;
    char buf[8192];
    int i, n;
    struct sockaddr_in *iaddr;
    struct in_addr addr;

    if (COM_CheckParm("-noifscan"))
	return -1;

    ifc.ifc_len = sizeof(buf);
    ifc.ifc_buf = buf;

    if (ioctl(sock, SIOCGIFCONF, &ifc) == -1) {
	Con_Printf("%s: SIOCGIFCONF failed\n", __func__);
	return -1;
    }

    ifr = ifc.ifc_req;
    n = ifc.ifc_len / sizeof(struct ifreq);

    for (i = 0; i < n; i++) {
	if (ioctl(sock, SIOCGIFADDR, &ifr[i]) == -1)
	    continue;
	iaddr = (struct sockaddr_in *)&ifr[i].ifr_addr;
	Con_DPrintf("%s: %s\n", ifr[i].ifr_name, inet_ntoa(iaddr->sin_addr));
	addr.s_addr = iaddr->sin_addr.s_addr;
	if (addr.s_addr != htonl(INADDR_LOOPBACK)) {
	    myAddr.s_addr = addr.s_addr;
	    strcpy (ifname, ifr[i].ifr_name);
	    return 0;
	}
    }

    return -1;
}

int
UDP_Init(void)
{
    int i, err;
    struct hostent *local;
    char buff[MAXHOSTNAMELEN];
    struct qsockaddr addr;
    char *colon;

    if (COM_CheckParm("-noudp"))
	return -1;

    /* determine my name & address */
    myAddr.s_addr = htonl(INADDR_LOOPBACK);
    err = gethostname(buff, MAXHOSTNAMELEN);
    if (err) {
	Con_Printf("%s: WARNING: gethostname failed (%s)\n", __func__,
		   strerror(errno));
    } else {
	buff[MAXHOSTNAMELEN - 1] = 0;
	local = gethostbyname(buff);
	if (!local) {
	    Con_Printf("%s: WARNING: gethostbyname failed (%s)\n", __func__,
			hstrerror(h_errno));
	} else if (local->h_addrtype != AF_INET) {
	    Con_Printf("%s: address from gethostbyname not IPv4\n", __func__);
	} else {
	    myAddr = *(struct in_addr *)local->h_addr_list[0];
	}
    }

    i = COM_CheckParm("-ip");
    if (i && i < com_argc - 1) {
	bindAddr.s_addr = inet_addr(com_argv[i + 1]);
	if (bindAddr.s_addr == INADDR_NONE)
	    Sys_Error("%s: %s is not a valid IP address", __func__,
		      com_argv[i + 1]);
	Con_Printf("Binding to IP Interface Address of %s\n", com_argv[i + 1]);
    } else {
	bindAddr.s_addr = INADDR_NONE;
    }

    i = COM_CheckParm("-localip");
    if (i && i < com_argc - 1) {
	localAddr.s_addr = inet_addr(com_argv[i + 1]);
	if (localAddr.s_addr == INADDR_NONE)
	    Sys_Error("%s: %s is not a valid IP address", __func__,
		      com_argv[i + 1]);
	Con_Printf("Advertising %s as the local IP in response packets\n",
		   com_argv[i + 1]);
    } else {
	localAddr.s_addr = INADDR_NONE;
    }

    net_controlsocket = UDP_OpenSocket(0);
    if (net_controlsocket == -1) {
	Con_Printf("%s: Unable to open control socket, UDP disabled\n",
		   __func__);
	return -1;
    }

    /* myAddr may resolve to 127.0.0.1, see if we can do any better */
    memset (ifname, 0, sizeof(ifname));
    if (myAddr.s_addr == htonl(INADDR_LOOPBACK)) {
	if (udp_scan_iface(net_controlsocket) == 0)
	    Con_Printf ("UDP, Local address: %s (%s)\n", inet_ntoa(myAddr),
			ifname);
    }
    if (ifname[0] == 0) {
	Con_Printf ("UDP, Local address: %s\n", inet_ntoa(myAddr));
    }

    broadcastaddr.sin_family = AF_INET;
    broadcastaddr.sin_addr.s_addr = INADDR_BROADCAST;
    broadcastaddr.sin_port = htons(net_hostport);

    UDP_GetSocketAddr(net_controlsocket, &addr);
    strcpy(my_tcpip_address, UDP_AddrToString(&addr));
    colon = strrchr(my_tcpip_address, ':');
    if (colon)
	*colon = 0;

    Con_Printf("UDP Initialized (%s)\n", my_tcpip_address);
    tcpipAvailable = true;

    return net_controlsocket;
}


void
UDP_Shutdown(void)
{
    UDP_Listen(false);
    UDP_CloseSocket(net_controlsocket);
}


void
UDP_Listen(qboolean state)
{
    /* enable listening */
    if (state) {
	if (net_acceptsocket != -1)
	    return;
	if ((net_acceptsocket = UDP_OpenSocket(net_hostport)) == -1)
	    Sys_Error("%s: Unable to open accept socket", __func__);
	return;
    }
    /* disable listening */
    if (net_acceptsocket == -1)
	return;
    UDP_CloseSocket(net_acceptsocket);
    net_acceptsocket = -1;
}


int
UDP_OpenSocket(int port)
{
    int newsocket;
    struct sockaddr_in address;
    int _true = 1;

    if ((newsocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
	return -1;
    if (ioctl(newsocket, FIONBIO, &_true) == -1)
	goto ErrorReturn;

    address.sin_family = AF_INET;
    if (bindAddr.s_addr != INADDR_NONE)
	address.sin_addr.s_addr = bindAddr.s_addr;
    else
	address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons((unsigned short)port);
    if (bind(newsocket, (struct sockaddr *)&address, sizeof(address)) == -1)
	goto ErrorReturn;

    return newsocket;

  ErrorReturn:
    close(newsocket);
    return -1;
}


int
UDP_CloseSocket(int socket)
{
    if (socket == net_broadcastsocket)
	net_broadcastsocket = 0;
    return close(socket);
}


/*
 * ============
 * PartialIPAddress
 *
 * this lets you type only as much of the net address as required,
 * using the local network components to fill in the rest
 * ============
 */
static int
PartialIPAddress(char *in, struct qsockaddr *hostaddr)
{
    char buff[256];
    char *b;
    int addr;
    int num;
    int mask;
    int run;
    int port;

    buff[0] = '.';
    b = buff;
    strcpy(buff + 1, in);
    if (buff[1] == '.')
	b++;

    addr = 0;
    mask = -1;
    while (*b == '.') {
	b++;
	num = 0;
	run = 0;
	while (!(*b < '0' || *b > '9')) {
	    num = num * 10 + *b++ - '0';
	    if (++run > 3)
		return -1;
	}
	if ((*b < '0' || *b > '9') && *b != '.' && *b != ':' && *b != 0)
	    return -1;
	if (num < 0 || num > 255)
	    return -1;
	mask <<= 8;
	addr = (addr << 8) + num;
    }

    if (*b++ == ':')
	port = Q_atoi(b);
    else
	port = net_hostport;

    hostaddr->sa_family = AF_INET;
    ((struct sockaddr_in *)hostaddr)->sin_port = htons((short)port);
    ((struct sockaddr_in *)hostaddr)->sin_addr.s_addr =
	(myAddr.s_addr & htonl(mask)) | htonl(addr);

    return 0;
}


int
UDP_Connect(int socket, struct qsockaddr *addr)
{
    return 0;
}


int
UDP_CheckNewConnections(void)
{
    unsigned long available;
    struct sockaddr_in from;
    socklen_t fromlen;
    char buff[1];

    if (net_acceptsocket == -1)
	return -1;

    if (ioctl(net_acceptsocket, FIONREAD, &available) == -1)
	Sys_Error("%s: ioctlsocket (FIONREAD) failed", __func__);
    if (available)
	return net_acceptsocket;
    /* quietly absorb empty packets */
    recvfrom (net_acceptsocket, buff, 0, 0, (struct sockaddr *)&from, &fromlen);
    return -1;
}


int
UDP_Read(int socket, byte *buf, int len, struct qsockaddr *addr)
{
    socklen_t addrlen = sizeof(struct qsockaddr);
    int ret;

    ret = recvfrom(socket, buf, len, 0, (struct sockaddr *)addr, &addrlen);
    if (ret == -1 && (errno == EWOULDBLOCK || errno == ECONNREFUSED))
	return 0;
    return ret;
}


static int
UDP_MakeSocketBroadcastCapable(int socket)
{
    int i = 1;

    /* make this socket broadcast capable */
    if (setsockopt(socket, SOL_SOCKET, SO_BROADCAST, &i, sizeof(i))
	< 0)
	return -1;
    net_broadcastsocket = socket;

    return 0;
}


int
UDP_Broadcast(int socket, byte *buf, int len)
{
    int ret;

    if (socket != net_broadcastsocket) {
	if (net_broadcastsocket != 0)
	    Sys_Error("Attempted to use multiple broadcasts sockets");
	ret = UDP_MakeSocketBroadcastCapable(socket);
	if (ret == -1) {
	    Con_Printf("Unable to make socket broadcast capable\n");
	    return ret;
	}
    }

    return UDP_Write(socket, buf, len, (struct qsockaddr *)&broadcastaddr);
}


int
UDP_Write(int socket, byte *buf, int len, struct qsockaddr *addr)
{
    int ret;

    ret =
	sendto(socket, buf, len, 0, (struct sockaddr *)addr,
	       sizeof(struct qsockaddr));
    if (ret == -1 && errno == EWOULDBLOCK)
	return 0;
    return ret;
}


char *
UDP_AddrToString(struct qsockaddr *addr)
{
    static char buffer[22];
    int haddr;

    haddr = ntohl(((struct sockaddr_in *)addr)->sin_addr.s_addr);
    sprintf(buffer, "%d.%d.%d.%d:%d", (haddr >> 24) & 0xff,
	    (haddr >> 16) & 0xff, (haddr >> 8) & 0xff, haddr & 0xff,
	    ntohs(((struct sockaddr_in *)addr)->sin_port));
    return buffer;
}


int
UDP_StringToAddr(char *string, struct qsockaddr *addr)
{
    int ha1, ha2, ha3, ha4, hp;
    int ipaddr;

    sscanf(string, "%d.%d.%d.%d:%d", &ha1, &ha2, &ha3, &ha4, &hp);
    ipaddr = (ha1 << 24) | (ha2 << 16) | (ha3 << 8) | ha4;

    addr->sa_family = AF_INET;
    ((struct sockaddr_in *)addr)->sin_addr.s_addr = htonl(ipaddr);
    ((struct sockaddr_in *)addr)->sin_port = htons(hp);
    return 0;
}


int
UDP_GetSocketAddr(int socket, struct qsockaddr *addr)
{
    struct sockaddr_in *address = (struct sockaddr_in *)addr;
    socklen_t len = sizeof(struct qsockaddr);
    struct in_addr a;

    memset(address, 0, len);
    getsockname(socket, (struct sockaddr *)address, &len);

    /*
     * The returned IP is embedded in our repsonse to a broadcast request for
     * server info from clients. The server admin may wish to advertise a
     * specific IP for various reasons, so allow the "default" address
     * returned by the OS to be overridden.
     */
    if (localAddr.s_addr != INADDR_NONE)
	address->sin_addr.s_addr = localAddr.s_addr;
    else {
	a = address->sin_addr;
	if (!a.s_addr || a.s_addr == htonl(INADDR_LOOPBACK))
	    address->sin_addr.s_addr = myAddr.s_addr;
    }

    return 0;
}


int
UDP_GetNameFromAddr(struct qsockaddr *addr, char *name)
{
    struct hostent *hostentry;

    hostentry =
	gethostbyaddr((char *)&((struct sockaddr_in *)addr)->sin_addr,
		      sizeof(struct in_addr), AF_INET);
    if (hostentry) {
	strncpy(name, (char *)hostentry->h_name, NET_NAMELEN - 1);
	return 0;
    }

    strcpy(name, UDP_AddrToString(addr));
    return 0;
}


int
UDP_GetAddrFromName(char *name, struct qsockaddr *addr)
{
    struct hostent *hostentry;

    if (name[0] >= '0' && name[0] <= '9')
	return PartialIPAddress(name, addr);

    hostentry = gethostbyname(name);
    if (!hostentry)
	return -1;

    addr->sa_family = AF_INET;
    ((struct sockaddr_in *)addr)->sin_port = htons(net_hostport);
    ((struct sockaddr_in *)addr)->sin_addr.s_addr =
	*(int *)hostentry->h_addr_list[0];

    return 0;
}


int
UDP_AddrCompare(struct qsockaddr *addr1, struct qsockaddr *addr2)
{
    if (addr1->sa_family != addr2->sa_family)
	return -1;

    if (((struct sockaddr_in *)addr1)->sin_addr.s_addr !=
	((struct sockaddr_in *)addr2)->sin_addr.s_addr)
	return -1;

    if (((struct sockaddr_in *)addr1)->sin_port !=
	((struct sockaddr_in *)addr2)->sin_port)
	return 1;

    return 0;
}


int
UDP_GetSocketPort(struct qsockaddr *addr)
{
    return ntohs(((struct sockaddr_in *)addr)->sin_port);
}


int
UDP_SetSocketPort(struct qsockaddr *addr, int port)
{
    ((struct sockaddr_in *)addr)->sin_port = htons(port);
    return 0;
}
