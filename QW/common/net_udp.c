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
#include <netdb.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>

#include "console.h"
#include "net.h"
#include "quakedef.h"
#include "sys.h"

netadr_t net_local_adr;

netadr_t net_from;
sizebuf_t net_message;
int net_socket;

#define	MAX_UDP_PACKET	8192
static byte net_message_buffer[MAX_UDP_PACKET];


static void
NetadrToSockadr(netadr_t *a, struct sockaddr_in *s)
{
    memset(s, 0, sizeof(*s));
    s->sin_family = AF_INET;

    s->sin_addr.s_addr = a->ip.l;
    s->sin_port = a->port;
}

static void
SockadrToNetadr(struct sockaddr_in *s, netadr_t *a)
{
    a->ip.l = s->sin_addr.s_addr;
    a->port = s->sin_port;
}

qboolean
NET_CompareBaseAdr(netadr_t a, netadr_t b)
{
    return a.ip.l == b.ip.l;
}


qboolean
NET_CompareAdr(netadr_t a, netadr_t b)
{
    return a.ip.l == b.ip.l && a.port == b.port;
}

const char *
NET_AdrToString(netadr_t a)
{
    static char s[64];
    const byte *b = a.ip.b;

    sprintf(s, "%i.%i.%i.%i:%i", b[0], b[1], b[2], b[3], ntohs(a.port));

    return s;
}

const char *
NET_BaseAdrToString(netadr_t a)
{
    static char s[64];

    sprintf(s, "%i.%i.%i.%i", a.ip.b[0], a.ip.b[1], a.ip.b[2], a.ip.b[3]);

    return s;
}

/*
 * =============
 * NET_StringToAdr
 *
 * idnewt
 * idnewt:28000
 * 192.246.40.70
 * 192.246.40.70:28000
 * =============
 */
qboolean
NET_StringToAdr(const char *s, netadr_t *a)
{
    struct hostent *h;
    struct sockaddr_in sadr;
    char *colon;
    char copy[128];

    memset(&sadr, 0, sizeof(sadr));
    sadr.sin_family = AF_INET;

    sadr.sin_port = 0;

    /* strip off a trailing :port if present */
    strcpy(copy, s);
    for (colon = copy; *colon; colon++)
	if (*colon == ':') {
	    *colon = 0;
	    sadr.sin_port = htons(atoi(colon + 1));
	}

    if (copy[0] >= '0' && copy[0] <= '9') {
	sadr.sin_addr.s_addr = inet_addr(copy);
    } else {
	h = gethostbyname(copy);
	if (!h)
	    return 0;
	sadr.sin_addr.s_addr = *(in_addr_t *)h->h_addr_list[0];
    }

    SockadrToNetadr(&sadr, a);

    return true;
}


qboolean
NET_GetPacket(void)
{
    int ret;
    struct sockaddr_in from;
    socklen_t fromlen;

    fromlen = sizeof(from);
    ret =
	recvfrom(net_socket, net_message_buffer, sizeof(net_message_buffer),
		 0, (struct sockaddr *)&from, &fromlen);
    if (ret == -1) {
	if (errno == EWOULDBLOCK)
	    return false;
	if (errno == ECONNREFUSED)
	    return false;
	Sys_Printf("%s: %s\n", __func__, strerror(errno));
	return false;
    }

    net_message.cursize = ret;
    SockadrToNetadr(&from, &net_from);

    return ret;
}


void
NET_SendPacket(int length, void *data, netadr_t to)
{
    int ret;
    struct sockaddr_in addr;

    NetadrToSockadr(&to, &addr);

    ret = sendto(net_socket, data, length, 0, (struct sockaddr *)&addr,
		 sizeof(addr));
    if (ret == -1) {
	if (errno == EWOULDBLOCK)
	    return;
	if (errno == ECONNREFUSED)
	    return;
	Sys_Printf("%s: %s\n", __func__, strerror(errno));
    }
}


static int
UDP_OpenSocket(int port)
{
    int newsocket;
    struct sockaddr_in address;
    int _true = 1;
    int i;

    if ((newsocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
	Sys_Error("%s: socket: %s", __func__, strerror(errno));
    if (ioctl(newsocket, FIONBIO, &_true) == -1)
	Sys_Error("%s: ioctl FIONBIO: %s", __func__, strerror(errno));
    address.sin_family = AF_INET;

    /* ZOID -- check for interface binding option */
    i = COM_CheckParm("-ip");
    if (i && i < com_argc - 1) {
	address.sin_addr.s_addr = inet_addr(com_argv[i + 1]);
	Con_Printf("Binding to IP Interface Address of %s\n",
		   inet_ntoa(address.sin_addr));
    } else
	address.sin_addr.s_addr = INADDR_ANY;
    if (port == PORT_ANY)
	address.sin_port = 0;
    else
	address.sin_port = htons((short)port);
    if (bind(newsocket, (struct sockaddr *)&address, sizeof(address)) == -1)
	Sys_Error("%s: bind: %s", __func__, strerror(errno));

    return newsocket;
}

void
NET_GetLocalAddress(void)
{
    char buff[MAXHOSTNAMELEN];
    struct sockaddr_in address;
    socklen_t namelen;

    gethostname(buff, MAXHOSTNAMELEN);
    buff[MAXHOSTNAMELEN - 1] = 0;

    NET_StringToAdr(buff, &net_local_adr);

    namelen = sizeof(address);
    if (getsockname(net_socket, (struct sockaddr *)&address, &namelen) == -1)
	Sys_Error("%s: getsockname: %s", __func__, strerror(errno));
    net_local_adr.port = address.sin_port;

    Con_Printf("IP address %s\n", NET_AdrToString(net_local_adr));
}

/*
 * ====================
 * NET_Init
 * ====================
 */
void
NET_Init(int port)
{
    /*
     * open the single socket to be used for all communications
     */
    net_socket = UDP_OpenSocket(port);

    /*
     * init the message buffer
     */
    net_message.maxsize = sizeof(net_message_buffer);
    net_message.data = net_message_buffer;

    /*
     * determine my name & address
     */
    NET_GetLocalAddress();

    Con_Printf("UDP Initialized\n");
}

/*
 * ====================
 * NET_Shutdown
 * ====================
 */
void
NET_Shutdown(void)
{
    close(net_socket);
}
