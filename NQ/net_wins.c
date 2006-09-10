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

#include "console.h"
#include "net.h"
#include "net_wins.h"
#include "quakedef.h"
#include "sys.h"
#include "winquake.h"

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

int winsock_initialized = 0;
WSADATA winsockdata;

static double blocktime;


static BOOL PASCAL FAR
BlockingHook(void)
{
    MSG msg;
    BOOL ret;

    if ((Sys_DoubleTime() - blocktime) > 2.0) {
	WSACancelBlockingCall();
	return FALSE;
    }

    /* get the next message, if any */
    ret = (BOOL)PeekMessage(&msg, NULL, 0, 0, PM_REMOVE);

    /* if we got one, process it */
    if (ret) {
	TranslateMessage(&msg);
	DispatchMessage(&msg);
    }

    /* TRUE if we got a message */
    return ret;
}


int
WINS_Init(void)
{
    int i;
    int err;
    char buff[MAXHOSTNAMELEN];
    char *colon;
    struct hostent *local;
    struct qsockaddr addr;

    if (COM_CheckParm("-noudp"))
	return -1;

    if (!winsock_initialized) {
	err = WSAStartup(MAKEWORD(1,1), &winsockdata);
	if (err) {
	    Con_SafePrintf("Winsock initialization failed.\n");
	    return -1;
	}
    }
    winsock_initialized++;

    /* determine my name & address */
    myAddr.s_addr = htonl(INADDR_LOOPBACK);
    err = gethostname(buff, MAXHOSTNAMELEN);
    if (err) {
	Con_Printf("%s: WARNING: gethostname failed.\n", __func__);
    } else {
	buff[MAXHOSTNAMELEN - 1] = 0;
	blocktime = Sys_DoubleTime();
	WSASetBlockingHook(BlockingHook);
	local = gethostbyname(buff);
	WSAUnhookBlockingHook();
	if (!local) {
	    Con_Printf("%s: WARNING: gethostbyname timed out.\n", __func__);
	} else if (local->h_addrtype != AF_INET) {
	    Con_Printf("%s: address from gethostbyname not IPv4\n", __func__);
	} else {
	    myAddr = *(struct in_addr *)local->h_addr_list[0];
	}
    }
    Con_Printf ("UDP, Local address: %s\n", inet_ntoa(myAddr));

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

    net_controlsocket = WINS_OpenSocket(0);
    if (net_controlsocket == -1) {
	Con_Printf("%s: Unable to open control socket\n", __func__);
	if (--winsock_initialized == 0)
	    WSACleanup();
	return -1;
    }

    broadcastaddr.sin_family = AF_INET;
    broadcastaddr.sin_addr.s_addr = INADDR_BROADCAST;
    broadcastaddr.sin_port = htons((unsigned short)net_hostport);

    WINS_GetSocketAddr(net_controlsocket, &addr);
    strcpy(my_tcpip_address, WINS_AddrToString(&addr));
    colon = strrchr(my_tcpip_address, ':');
    if (colon)
	*colon = 0;

    Con_Printf("Winsock TCP/IP Initialized (%s)\n", my_tcpip_address);
    tcpipAvailable = true;

    return net_controlsocket;
}


void
WINS_Shutdown(void)
{
    WINS_Listen(false);
    WINS_CloseSocket(net_controlsocket);
    if (--winsock_initialized == 0)
	WSACleanup();
}


void
WINS_Listen(qboolean state)
{
    /* enable listening */
    if (state) {
	if (net_acceptsocket != -1)
	    return;
	if ((net_acceptsocket = WINS_OpenSocket(net_hostport)) == -1)
	    Sys_Error("%s: Unable to open accept socket", __func__);
	return;
    }

    /* disable listening */
    if (net_acceptsocket == -1)
	return;

    WINS_CloseSocket(net_acceptsocket);
    net_acceptsocket = -1;
}


int
WINS_OpenSocket(int port)
{
    int newsocket;
    struct sockaddr_in address;
    u_long _true = 1;

    if ((newsocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
	return -1;
    if (ioctlsocket(newsocket, FIONBIO, &_true) == -1)
	goto ErrorReturn;

    address.sin_family = AF_INET;
    if (bindAddr.s_addr != INADDR_NONE)
	address.sin_addr.s_addr = bindAddr.s_addr;
    else
	address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons((unsigned short)port);
    if (bind(newsocket, (struct sockaddr *)&address, sizeof(address)) == 0)
	return newsocket;

    if (tcpipAvailable)
	Sys_Error("Unable to bind to %s",
		   WINS_AddrToString((struct qsockaddr *)&address));
    else /* we are still in init phase, no need to error */
	Con_Printf("Unable to bind to %s\n",
		   WINS_AddrToString((struct qsockaddr *)&address));

  ErrorReturn:
    closesocket(newsocket);
    return -1;
}


int
WINS_CloseSocket(int socket)
{
    if (socket == net_broadcastsocket)
	net_broadcastsocket = 0;
    return closesocket(socket);
}


/*
 * ============
 * PartialIPAddress
 *
 * this lets you type only as much of the net address as required, using the
 * local network components to fill in the rest
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
WINS_Connect(int socket, struct qsockaddr *addr)
{
    return 0;
}


int
WINS_CheckNewConnections(void)
{
    char buf[4096];
    int ret;

    if (net_acceptsocket == -1)
	return -1;

    ret = recvfrom(net_acceptsocket, buf, sizeof(buf), MSG_PEEK, NULL, NULL);
    if (ret >= 0)
	return net_acceptsocket;

    return -1;
}


int
WINS_Read(int socket, byte *buf, int len, struct qsockaddr *addr)
{
    int addrlen = sizeof(struct qsockaddr);
    int ret;

    ret = recvfrom(socket, (char *)buf, len, 0, (struct sockaddr *)addr,
		   &addrlen);
    if (ret == -1) {
	int err = WSAGetLastError();

	if (err == WSAEWOULDBLOCK || err == WSAECONNREFUSED)
	    return 0;
    }
    return ret;
}


static int
WINS_MakeSocketBroadcastCapable(int socket)
{
    int i = 1;

    /* make this socket broadcast capable */
    if (setsockopt
	(socket, SOL_SOCKET, SO_BROADCAST, (char *)&i, sizeof(i)) < 0)
	return -1;
    net_broadcastsocket = socket;

    return 0;
}


int
WINS_Broadcast(int socket, byte *buf, int len)
{
    int ret;

    if (socket != net_broadcastsocket) {
	if (net_broadcastsocket != 0)
	    Sys_Error("Attempted to use multiple broadcasts sockets");
	ret = WINS_MakeSocketBroadcastCapable(socket);
	if (ret == -1) {
	    Con_Printf("Unable to make socket broadcast capable\n");
	    return ret;
	}
    }

    return WINS_Write(socket, buf, len, (struct qsockaddr *)&broadcastaddr);
}


int
WINS_Write(int socket, byte *buf, int len, struct qsockaddr *addr)
{
    int ret;

    ret = sendto(socket, (char *)buf, len, 0, (struct sockaddr *)addr,
		 sizeof(struct qsockaddr));
    if (ret == -1)
	if (WSAGetLastError() == WSAEWOULDBLOCK)
	    return 0;

    return ret;
}


char *
WINS_AddrToString(struct qsockaddr *addr)
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
WINS_StringToAddr(char *string, struct qsockaddr *addr)
{
    int ha1, ha2, ha3, ha4, hp;
    int ipaddr;

    sscanf(string, "%d.%d.%d.%d:%d", &ha1, &ha2, &ha3, &ha4, &hp);
    ipaddr = (ha1 << 24) | (ha2 << 16) | (ha3 << 8) | ha4;

    addr->sa_family = AF_INET;
    ((struct sockaddr_in *)addr)->sin_addr.s_addr = htonl(ipaddr);
    ((struct sockaddr_in *)addr)->sin_port = htons((unsigned short)hp);
    return 0;
}


int
WINS_GetSocketAddr(int socket, struct qsockaddr *addr)
{
    struct sockaddr_in *address = (struct sockaddr_in *)addr;
    int addrlen = sizeof(struct qsockaddr);
    struct in_addr a;

    memset(addr, 0, sizeof(struct qsockaddr));
    getsockname(socket, (struct sockaddr *)addr, &addrlen);

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
WINS_GetNameFromAddr(struct qsockaddr *addr, char *name)
{
    struct hostent *hostentry;

    hostentry =
	gethostbyaddr((char *)&((struct sockaddr_in *)addr)->sin_addr,
		       sizeof(struct in_addr), AF_INET);
    if (hostentry) {
	strncpy(name, (char *)hostentry->h_name, NET_NAMELEN - 1);
	return 0;
    }

    strcpy(name, WINS_AddrToString(addr));
    return 0;
}


int
WINS_GetAddrFromName(char *name, struct qsockaddr *addr)
{
    struct hostent *hostentry;

    if (name[0] >= '0' && name[0] <= '9')
	return PartialIPAddress(name, addr);

    hostentry = gethostbyname(name);
    if (!hostentry)
	return -1;

    addr->sa_family = AF_INET;
    ((struct sockaddr_in *)addr)->sin_port =
	htons((unsigned short)net_hostport);
    ((struct sockaddr_in *)addr)->sin_addr.s_addr =
	*(int *)hostentry->h_addr_list[0];

    return 0;
}


int
WINS_AddrCompare(struct qsockaddr *addr1, struct qsockaddr *addr2)
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
WINS_GetSocketPort(struct qsockaddr *addr)
{
    return ntohs(((struct sockaddr_in *)addr)->sin_port);
}


int
WINS_SetSocketPort(struct qsockaddr *addr, int port)
{
    ((struct sockaddr_in *)addr)->sin_port = htons((unsigned short)port);
    return 0;
}
