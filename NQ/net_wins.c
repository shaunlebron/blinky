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

#include <winsock2.h>

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
static netadr_t broadcastaddr;

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
static netadr_t myAddr;
static netadr_t localAddr;
static netadr_t bindAddr;

int winsock_initialized = 0;
WSADATA winsockdata;

static double blocktime;


static void
NetadrToSockadr(const netadr_t *a, struct sockaddr_in *s)
{
    memset(s, 0, sizeof(*s));
    s->sin_family = AF_INET;

    s->sin_addr.s_addr = a->ip.l;
    s->sin_port = a->port;
}

static void
SockadrToNetadr(const struct sockaddr_in *s, netadr_t *a)
{
    a->ip.l = s->sin_addr.s_addr;
    a->port = s->sin_port;
}

static int
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
    netadr_t addr;

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
    myAddr.ip.l = htonl(INADDR_LOOPBACK);
    myAddr.port = htons(DEFAULTnet_hostport);
    err = gethostname(buff, MAXHOSTNAMELEN);
    if (err) {
	Con_Printf("%s: WARNING: gethostname failed.\n", __func__);
    } else {
	buff[MAXHOSTNAMELEN - 1] = 0;
	blocktime = Sys_DoubleTime();
	/* FIXME - WSASetBlockingHook is deprecated in Winsock2 */
	WSASetBlockingHook((FARPROC)BlockingHook);
	local = gethostbyname(buff);
	WSAUnhookBlockingHook();
	if (!local) {
	    Con_Printf("%s: WARNING: gethostbyname timed out.\n", __func__);
	} else if (local->h_addrtype != AF_INET) {
	    Con_Printf("%s: address from gethostbyname not IPv4\n", __func__);
	} else {
	    struct in_addr *inaddr = (struct in_addr *)local->h_addr_list[0];
	    myAddr.ip.l = inaddr->S_un.S_addr;
	}
    }
    Con_Printf("UDP, Local address: %s\n", NET_AdrToString(&myAddr));

    i = COM_CheckParm("-ip");
    if (i && i < com_argc - 1) {
	bindAddr.ip.l = inet_addr(com_argv[i + 1]);
	if (bindAddr.ip.l == INADDR_NONE)
	    Sys_Error("%s: %s is not a valid IP address", __func__,
		      com_argv[i + 1]);
	Con_Printf("Binding to IP Interface Address of %s\n", com_argv[i + 1]);
    } else {
	bindAddr.ip.l = INADDR_NONE;
    }

    i = COM_CheckParm("-localip");
    if (i && i < com_argc - 1) {
	localAddr.ip.l = inet_addr(com_argv[i + 1]);
	if (localAddr.ip.l == INADDR_NONE)
	    Sys_Error("%s: %s is not a valid IP address", __func__,
		      com_argv[i + 1]);
	Con_Printf("Advertising %s as the local IP in response packets\n",
		   com_argv[i + 1]);
    } else {
	localAddr.ip.l = INADDR_NONE;
    }

    net_controlsocket = WINS_OpenSocket(0);
    if (net_controlsocket == -1) {
	Con_Printf("%s: Unable to open control socket\n", __func__);
	if (--winsock_initialized == 0)
	    WSACleanup();
	return -1;
    }

    broadcastaddr.ip.l = INADDR_BROADCAST;
    broadcastaddr.port = htons(net_hostport);

    WINS_GetSocketAddr(net_controlsocket, &addr);
    strcpy(my_tcpip_address, NET_AdrToString(&addr));
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
    netadr_t addr;
    u_long _true = 1;

    if ((newsocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
	return -1;
    if (ioctlsocket(newsocket, FIONBIO, &_true) == -1)
	goto ErrorReturn;

    address.sin_family = AF_INET;
    if (bindAddr.ip.l != INADDR_NONE)
	address.sin_addr.s_addr = bindAddr.ip.l;
    else
	address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons((unsigned short)port);
    if (bind(newsocket, (struct sockaddr *)&address, sizeof(address)) == 0)
	return newsocket;

    SockadrToNetadr(&address, &addr);
    if (tcpipAvailable)
	Sys_Error("Unable to bind to %s", NET_AdrToString(&addr));
    else /* we are still in init phase, no need to error */
	Con_Printf("Unable to bind to %s\n", NET_AdrToString(&addr));

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
WINS_Read(int socket, void *buf, int len, netadr_t *addr)
{
    struct sockaddr_in saddr;
    int addrlen = sizeof(saddr);
    int ret;

    ret = recvfrom(socket, (char *)buf, len, 0, (struct sockaddr *)&saddr,
		   &addrlen);
    SockadrToNetadr(&saddr, addr);
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
WINS_Broadcast(int socket, const void *buf, int len)
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

    return WINS_Write(socket, buf, len, &broadcastaddr);
}


int
WINS_Write(int socket, const void *buf, int len, const netadr_t *addr)
{
    struct sockaddr_in saddr;
    int ret;

    NetadrToSockadr(addr, &saddr);
    ret = sendto(socket, (char *)buf, len, 0, (struct sockaddr *)&saddr,
		 sizeof(saddr));
    if (ret == -1)
	if (WSAGetLastError() == WSAEWOULDBLOCK)
	    return 0;

    return ret;
}


int
WINS_GetSocketAddr(int socket, netadr_t *addr)
{
    struct sockaddr_in saddr;
    int len = sizeof(saddr);

    memset(&saddr, 0, len);
    getsockname(socket, (struct sockaddr *)&saddr, &len);

    /*
     * The returned IP is embedded in our repsonse to a broadcast request for
     * server info from clients. The server admin may wish to advertise a
     * specific IP for various reasons, so allow the "default" address
     * returned by the OS to be overridden.
     */
    if (localAddr.ip.l != INADDR_NONE)
	saddr.sin_addr.s_addr = localAddr.ip.l;
    else {
	struct in_addr a = saddr.sin_addr;
	if (!a.s_addr || a.s_addr == htonl(INADDR_LOOPBACK))
	    saddr.sin_addr.s_addr = myAddr.ip.l;
    }
    SockadrToNetadr(&saddr, addr);

    return 0;
}


int
WINS_GetNameFromAddr(const netadr_t *addr, char *name)
{
    struct hostent *hostentry;

    hostentry = gethostbyaddr((char *)&addr->ip.l, sizeof(addr->ip.l), AF_INET);
    if (hostentry) {
	strncpy(name, (char *)hostentry->h_name, NET_NAMELEN - 1);
	return 0;
    }
    strcpy(name, NET_AdrToString(addr));

    return 0;
}


int
WINS_GetAddrFromName(const char *name, netadr_t *addr)
{
    struct hostent *hostentry;

    if (name[0] >= '0' && name[0] <= '9')
	return NET_PartialIPAddress(name, &myAddr, addr);

    hostentry = gethostbyname(name);
    if (!hostentry)
	return -1;

    addr->ip.l = *(int *)hostentry->h_addr_list[0];
    addr->port = htons(net_hostport);

    return 0;
}


int
WINS_GetDefaultMTU(void)
{
    return 1400;
}
