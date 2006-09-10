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

#if defined(_WIN32)
#include <windows.h>
#include <winsock2.h>
#else
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include "cmd.h"
#include "console.h"
#include "keys.h"
#include "menu.h"
#include "net.h"
#include "net_dgrm.h"
#include "quakedef.h"
#include "server.h"
#include "screen.h"
#include "sys.h"

/* statistic counters */
static int packetsSent = 0;
static int packetsReSent = 0;
static int packetsReceived = 0;
static int receivedDuplicateCount = 0;
static int shortPacketCount = 0;
static int droppedDatagrams;

static net_driver_t *dgrm_driver;

static struct {
    unsigned int length;
    unsigned int sequence;
    byte data[NET_MAXMESSAGE];
} packetBuffer;

#ifdef DEBUG
static const char *
StrAddr(netadr_t *addr)
{
    static char buf[32];

    snprintf(buf, sizeof(buf), "%d.%d.%d.%d:%d",
	    addr->ip.b[0], addr->ip.b[1], addr->ip.b[2], addr->ip.b[3],
	    ntohs(addr->port));
    return buf;
}
#endif

static netadr_t banAddr = { .ip.l = INADDR_ANY };
static netadr_t banMask = { .ip.l = INADDR_NONE };

void
NET_Ban_f(client_t *client)
{
    char addrStr[32];
    char maskStr[32];

    if (pr_global_struct->deathmatch)
	return;

    switch (Cmd_Argc()) {
    case 1:
	if (banAddr.ip.l != INADDR_ANY) {
	    strcpy(addrStr, NET_AdrToString(&banAddr));
	    strcpy(maskStr, NET_AdrToString(&banMask));
	    SV_ClientPrintf(client, "Banning %s [%s]\n", addrStr, maskStr);
	} else {
	    SV_ClientPrintf(client, "Banning not active\n");
	}
	break;

    case 2:
	if (strcasecmp(Cmd_Argv(1), "off") == 0)
	    banAddr.ip.l = INADDR_ANY;
	else
	    banAddr.ip.l = inet_addr(Cmd_Argv(1));
	banMask.ip.l = INADDR_NONE;
	break;

    case 3:
	banAddr.ip.l = inet_addr(Cmd_Argv(1));
	banMask.ip.l = inet_addr(Cmd_Argv(2));
	break;

    default:
	SV_ClientPrintf(client, "BAN ip_address [mask]\n");
	break;
    }
}

static int
SendPacket(qsocket_t *sock)
{
    unsigned int packetLen;
    unsigned int dataLen;
    unsigned int eom;

    if (sock->sendMessageLength <= sock->mtu) {
	dataLen = sock->sendMessageLength;
	eom = NETFLAG_EOM;
    } else {
	dataLen = sock->mtu;
	eom = 0;
    }
    packetLen = NET_HEADERSIZE + dataLen;

    packetBuffer.length = BigLong(packetLen | (NETFLAG_DATA | eom));
    packetBuffer.sequence = BigLong(sock->sendSequence++);
    memcpy(packetBuffer.data, sock->sendMessage, dataLen);

    if (sock->landriver->Write(sock->socket, &packetBuffer, packetLen,
			       &sock->addr) == -1)
	return -1;

    sock->lastSendTime = net_time;
    packetsSent++;

    return 1;
}

int
Datagram_SendMessage(qsocket_t *sock, const sizebuf_t *data)
{
#ifdef DEBUG
    if (data->cursize == 0)
	Sys_Error("%s: zero length message", __func__);

    if (data->cursize > NET_MAXMESSAGE)
	Sys_Error("%s: message too big %u", __func__, data->cursize);

    if (sock->canSend == false)
	Sys_Error("%s: called with canSend == false", __func__);
#endif

    memcpy(sock->sendMessage, data->data, data->cursize);
    sock->sendMessageLength = data->cursize;
    sock->canSend = false;

    return SendPacket(sock);
}

static int
SendMessageNext(qsocket_t *sock)
{
    sock->sendNext = false;

    return SendPacket(sock);
}

static int
ReSendMessage(qsocket_t *sock)
{
    sock->sendNext = false;

    return SendPacket(sock);
}


qboolean
Datagram_CanSendMessage(qsocket_t *sock)
{
    if (sock->sendNext)
	SendMessageNext(sock);

    return sock->canSend;
}


qboolean
Datagram_CanSendUnreliableMessage(qsocket_t *sock)
{
    return true;
}


int
Datagram_SendUnreliableMessage(qsocket_t *sock, const sizebuf_t *data)
{
    int packetLen;

#ifdef DEBUG
    if (data->cursize == 0)
	Sys_Error("%s: zero length message", __func__);

    if (data->cursize > sock->mtu)
	Sys_Error("%s: message too big %u", __func__, data->cursize);
#endif

    packetLen = NET_HEADERSIZE + data->cursize;

    packetBuffer.length = BigLong(packetLen | NETFLAG_UNRELIABLE);
    packetBuffer.sequence = BigLong(sock->unreliableSendSequence++);
    memcpy(packetBuffer.data, data->data, data->cursize);

    if (sock->landriver->Write(sock->socket, &packetBuffer, packetLen,
			       &sock->addr) == -1)
	return -1;

    packetsSent++;
    return 1;
}


int
Datagram_GetMessage(qsocket_t *sock)
{
    unsigned int length;
    unsigned int flags;
    int ret = 0;
    netadr_t readaddr;
    unsigned int sequence;
    unsigned int count;

    if (!sock->canSend)
	if ((net_time - sock->lastSendTime) > 1.0)
	    ReSendMessage(sock);

    while (1) {
	length = sock->landriver->Read(sock->socket, &packetBuffer,
				       NET_MESSAGESIZE, &readaddr);
#if 0
	/* for testing packet loss effects */
	if ((rand() & 255) > 220)
	    continue;
#endif
	if (length == 0)
	    break;

	if (length == -1) {
	    Con_Printf("Read error\n");
	    return -1;
	}

	if (NET_AddrCompare(&readaddr, &sock->addr) != 0) {
#ifdef DEBUG
	    Con_DPrintf("Forged packet received\n");
	    Con_DPrintf("Expected: %s\n", StrAddr(&sock->addr));
	    Con_DPrintf("Received: %s\n", StrAddr(&readaddr));
#endif
	    continue;
	}

	if (length < NET_HEADERSIZE) {
	    shortPacketCount++;
	    continue;
	}

	length = BigLong(packetBuffer.length);
	flags = length & (~NETFLAG_LENGTH_MASK);
	length &= NETFLAG_LENGTH_MASK;

	if (flags & NETFLAG_CTL)
	    continue;

	sequence = BigLong(packetBuffer.sequence);
	packetsReceived++;

	if (flags & NETFLAG_UNRELIABLE) {
	    if (sequence < sock->unreliableReceiveSequence) {
		Con_DPrintf("Got a stale datagram\n");
		ret = 0;
		break;
	    }
	    if (sequence != sock->unreliableReceiveSequence) {
		count = sequence - sock->unreliableReceiveSequence;
		droppedDatagrams += count;
		Con_DPrintf("Dropped %u datagram(s)\n", count);
	    }
	    sock->unreliableReceiveSequence = sequence + 1;

	    length -= NET_HEADERSIZE;

	    SZ_Clear(&net_message);
	    SZ_Write(&net_message, packetBuffer.data, length);

	    ret = 2;
	    break;
	}

	if (flags & NETFLAG_ACK) {
	    if (sequence != (sock->sendSequence - 1)) {
		Con_DPrintf("Stale ACK received\n");
		continue;
	    }
	    if (sequence == sock->ackSequence) {
		sock->ackSequence++;
		if (sock->ackSequence != sock->sendSequence)
		    Con_DPrintf("ack sequencing error\n");
	    } else {
		Con_DPrintf("Duplicate ACK received\n");
		continue;
	    }
	    sock->sendMessageLength -= sock->mtu;
	    if (sock->sendMessageLength > 0) {
		memmove(sock->sendMessage, sock->sendMessage + sock->mtu,
			sock->sendMessageLength);
		sock->sendNext = true;
	    } else {
		sock->sendMessageLength = 0;
		sock->canSend = true;
	    }
	    continue;
	}

	if (flags & NETFLAG_DATA) {
	    packetBuffer.length = BigLong(NET_HEADERSIZE | NETFLAG_ACK);
	    packetBuffer.sequence = BigLong(sequence);
	    sock->landriver->Write(sock->socket, &packetBuffer, NET_HEADERSIZE,
				   &readaddr);

	    if (sequence != sock->receiveSequence) {
		receivedDuplicateCount++;
		continue;
	    }
	    sock->receiveSequence++;

	    length -= NET_HEADERSIZE;

	    if (flags & NETFLAG_EOM) {
		SZ_Clear(&net_message);
		SZ_Write(&net_message, sock->receiveMessage,
			 sock->receiveMessageLength);
		SZ_Write(&net_message, packetBuffer.data, length);
		sock->receiveMessageLength = 0;

		ret = 1;
		break;
	    }

	    memcpy(sock->receiveMessage + sock->receiveMessageLength,
		   packetBuffer.data, length);
	    sock->receiveMessageLength += length;
	    continue;
	}
    }

    if (sock->sendNext)
	SendMessageNext(sock);

    return ret;
}

static void
PrintStats(qsocket_t *s)
{
    Con_Printf("canSend = %4u   \n", s->canSend);
    Con_Printf("sendSeq = %4u   ", s->sendSequence);
    Con_Printf("recvSeq = %4u   \n", s->receiveSequence);
    Con_Printf("\n");
}

void
NET_Stats_f(void)
{
    qsocket_t *s;

    if (Cmd_Argc() == 1) {
	Con_Printf("unreliable messages sent   = %i\n",
		   unreliableMessagesSent);
	Con_Printf("unreliable messages recv   = %i\n",
		   unreliableMessagesReceived);
	Con_Printf("reliable messages sent     = %i\n", messagesSent);
	Con_Printf("reliable messages received = %i\n", messagesReceived);
	Con_Printf("packetsSent                = %i\n", packetsSent);
	Con_Printf("packetsReSent              = %i\n", packetsReSent);
	Con_Printf("packetsReceived            = %i\n", packetsReceived);
	Con_Printf("receivedDuplicateCount     = %i\n",
		   receivedDuplicateCount);
	Con_Printf("shortPacketCount           = %i\n", shortPacketCount);
	Con_Printf("droppedDatagrams           = %i\n", droppedDatagrams);
    } else if (strcmp(Cmd_Argv(1), "*") == 0) {
	for (s = net_activeSockets; s; s = s->next)
	    PrintStats(s);
	for (s = net_freeSockets; s; s = s->next)
	    PrintStats(s);
    } else {
	for (s = net_activeSockets; s; s = s->next)
	    if (strcasecmp(Cmd_Argv(1), s->address) == 0)
		break;
	if (s == NULL)
	    for (s = net_freeSockets; s; s = s->next)
		if (strcasecmp(Cmd_Argv(1), s->address) == 0)
		    break;
	if (s == NULL)
	    return;
	PrintStats(s);
    }
}


struct test_poll_state {
    qboolean inProgress;
    int pollCount;
    int socket;
    net_landriver_t *driver;
    PollProcedure *procedure;
};


static void
Test_Poll(struct test_poll_state *state)
{
    netadr_t clientaddr;
    int control;
    int len;
    char *name;
    char *address;
    int colors;
    int frags;
    int connectTime;
    byte playerNumber;

    while (1) {
	len = state->driver->Read(state->socket, net_message.data,
				  net_message.maxsize, &clientaddr);
	if (len < sizeof(int))
	    break;

	net_message.cursize = len;

	MSG_BeginReading();
	control = MSG_ReadControlHeader();
	if (control == -1)
	    break;
	if ((control & (~NETFLAG_LENGTH_MASK)) != NETFLAG_CTL)
	    break;
	if ((control & NETFLAG_LENGTH_MASK) != len)
	    break;

	if (MSG_ReadByte() != CCREP_PLAYER_INFO)
	    Sys_Error("Unexpected repsonse to Player Info request");

	playerNumber = MSG_ReadByte();
	name = MSG_ReadString();
	colors = MSG_ReadLong();
	frags = MSG_ReadLong();
	connectTime = MSG_ReadLong();
	address = MSG_ReadString();

	Con_Printf("%s (%d)\n  frags:%3i  colors:%u %u  time:%u\n  %s\n",
		   name, (int)playerNumber, frags, colors >> 4, colors & 0x0f,
		   connectTime / 60, address);
    }

    state->pollCount--;
    if (state->pollCount) {
	SchedulePollProcedure(state->procedure, 0.1);
    } else {
	state->driver->CloseSocket(state->socket);
	state->inProgress = false;
    }
}


static void
Test_f(void)
{
    const char *host;
    int i, n;
    int max = MAX_SCOREBOARD;
    netadr_t sendaddr;
    net_landriver_t *driver = NULL;

    static struct test_poll_state state = {
	.inProgress	= false,
	.pollCount	= 0,
	.socket		= 0,
	.driver		= NULL,
	.procedure	= NULL
    };
    static PollProcedure poll_procedure = {
	.next		= NULL,
	.nextTime	= 0.0,
	.procedure	= Test_Poll,
	.arg		= &state
    };

    if (state.inProgress)
	return;

    host = Cmd_Argv(1);

    if (host && hostCacheCount) {
	for (n = 0; n < hostCacheCount; n++)
	    if (strcasecmp(host, hostcache[n].name) == 0) {
		if (hostcache[n].driver != dgrm_driver)
		    continue;
		driver = hostcache[n].ldriver;
		max = hostcache[n].maxusers;
		sendaddr = hostcache[n].addr;
		break;
	    }
	if (driver)
	    goto JustDoIt;
    }

    for (i = 0; i < net_numlandrivers; i++) {
	driver = &net_landrivers[i];
	if (!driver->initialized)
	    continue;

	/* see if we can resolve the host name */
	if (driver->GetAddrFromName(host, &sendaddr) != -1)
	    break;
    }
    if (!driver)
	return;

  JustDoIt:
    state.socket = driver->OpenSocket(0);
    if (state.socket == -1)
	return;

    state.inProgress = true;
    state.pollCount = 20;
    state.driver = driver;
    state.procedure = &poll_procedure;

    for (n = 0; n < max; n++) {
	SZ_Clear(&net_message);
	// save space for the header, filled in later
	MSG_WriteLong(&net_message, 0);
	MSG_WriteByte(&net_message, CCREQ_PLAYER_INFO);
	MSG_WriteByte(&net_message, n);
	MSG_WriteControlHeader(&net_message);
	driver->Write(state.socket, net_message.data, net_message.cursize,
		      &sendaddr);
    }
    SZ_Clear(&net_message);
    SchedulePollProcedure(&poll_procedure, 0.1);
}


static void
Test2_Poll(struct test_poll_state *state)
{
    netadr_t clientaddr;
    int control;
    int len;
    char *name;
    char *value;

    len = state->driver->Read(state->socket, net_message.data,
			      net_message.maxsize, &clientaddr);
    if (len < sizeof(int))
	goto Reschedule;

    net_message.cursize = len;

    MSG_BeginReading();
    control = MSG_ReadControlHeader();
    if (control == -1)
	goto Error;
    if ((control & (~NETFLAG_LENGTH_MASK)) != NETFLAG_CTL)
	goto Error;
    if ((control & NETFLAG_LENGTH_MASK) != len)
	goto Error;

    if (MSG_ReadByte() != CCREP_RULE_INFO)
	goto Error;

    name = MSG_ReadString();
    if (!name[0])
	goto Done;
    value = MSG_ReadString();

    Con_Printf("%-16.16s  %-16.16s\n", name, value);

    SZ_Clear(&net_message);
    // save space for the header, filled in later
    MSG_WriteLong(&net_message, 0);
    MSG_WriteByte(&net_message, CCREQ_RULE_INFO);
    MSG_WriteString(&net_message, name);
    MSG_WriteControlHeader(&net_message);
    state->driver->Write(state->socket, net_message.data, net_message.cursize,
			 &clientaddr);
    SZ_Clear(&net_message);

  Reschedule:
    SchedulePollProcedure(state->procedure, 0.05);
    return;

  Error:
    Con_Printf("Unexpected repsonse to Rule Info request\n");
  Done:
    state->driver->CloseSocket(state->socket);
    state->inProgress = false;
    return;
}

static void
Test2_f(void)
{
    const char *host;
    int i, n;
    netadr_t sendaddr;

    static struct test_poll_state state = {
	.inProgress	= false,
	.pollCount	= 0,
	.socket		= 0,
	.driver		= NULL,
	.procedure	= NULL
    };
    static PollProcedure poll_procedure = {
	.next		= NULL,
	.nextTime	= 0.0,
	.procedure	= Test2_Poll,
	.arg		= &state
    };

    if (state.inProgress)
	return;

    host = Cmd_Argv(1);

    if (host && hostCacheCount) {
	for (n = 0; n < hostCacheCount; n++)
	    if (strcasecmp(host, hostcache[n].name) == 0) {
		if (hostcache[n].driver != dgrm_driver)
		    continue;
		state.driver = hostcache[n].ldriver;
		sendaddr = hostcache[n].addr;
		break;
	    }
	if (state.driver)
	    goto JustDoIt;
    }

    for (i = 0; i < net_numlandrivers; i++) {
	if (!net_landrivers[i].initialized)
	    continue;
	// see if we can resolve the host name
	if (net_landrivers[i].GetAddrFromName(host, &sendaddr) != -1) {
	    state.driver = &net_landrivers[i];
	    break;
	}
    }
    if (!state.driver)
	return;

  JustDoIt:
    state.socket = state.driver->OpenSocket(0);
    if (state.socket == -1)
	return;

    state.inProgress = true;
    state.procedure = &poll_procedure;

    SZ_Clear(&net_message);
    // save space for the header, filled in later
    MSG_WriteLong(&net_message, 0);
    MSG_WriteByte(&net_message, CCREQ_RULE_INFO);
    MSG_WriteString(&net_message, "");
    MSG_WriteControlHeader(&net_message);
    state.driver->Write(state.socket, net_message.data, net_message.cursize,
			&sendaddr);
    SZ_Clear(&net_message);
    SchedulePollProcedure(&poll_procedure, 0.05);
}


int
Datagram_Init(void)
{
    int i, csock, num_inited;

    dgrm_driver = net_driver;
    Cmd_AddCommand("net_stats", NET_Stats_f);

    if (COM_CheckParm("-nolan"))
	return -1;

    num_inited = 0;
    for (i = 0; i < net_numlandrivers; i++) {
	csock = net_landrivers[i].Init();
	if (csock == -1)
	    continue;
	net_landrivers[i].initialized = true;
	net_landrivers[i].controlSock = csock;
	num_inited++;
    }

    if (num_inited == 0)
	return -1;

    Cmd_AddCommand("ban", NULL);
    Cmd_AddCommand("test", Test_f);
    Cmd_AddCommand("test2", Test2_f);

    return 0;
}


void
Datagram_Shutdown(void)
{
    int i;

//
// shutdown the lan drivers
//
    for (i = 0; i < net_numlandrivers; i++) {
	if (net_landrivers[i].initialized) {
	    net_landrivers[i].Shutdown();
	    net_landrivers[i].initialized = false;
	}
    }
}


void
Datagram_Close(qsocket_t *sock)
{
    sock->landriver->CloseSocket(sock->socket);
}


void
Datagram_Listen(qboolean state)
{
    int i;

    for (i = 0; i < net_numlandrivers; i++)
	if (net_landrivers[i].initialized)
	    net_landrivers[i].Listen(state);
}


static qsocket_t *
_Datagram_CheckNewConnections(net_landriver_t *driver)
{
    netadr_t clientaddr;
    netadr_t newaddr;
    netadr_t testAddr;

    int newsock;
    int acceptsock;
    qsocket_t *sock;
    qsocket_t *s;
    int len;
    int command;
    int control;
    int ret;

    acceptsock = driver->CheckNewConnections();
    if (acceptsock == -1)
	return NULL;

    SZ_Clear(&net_message);

    len = driver->Read(acceptsock, net_message.data, net_message.maxsize,
		       &clientaddr);
    if (len < sizeof(int))
	return NULL;
    net_message.cursize = len;

    MSG_BeginReading();
    control = MSG_ReadControlHeader();
    if (control == -1)
	return NULL;
    if ((control & (~NETFLAG_LENGTH_MASK)) != NETFLAG_CTL)
	return NULL;
    if ((control & NETFLAG_LENGTH_MASK) != len)
	return NULL;

    command = MSG_ReadByte();
    if (command == CCREQ_SERVER_INFO) {
	if (strcmp(MSG_ReadString(), "QUAKE") != 0)
	    return NULL;

	SZ_Clear(&net_message);
	// save space for the header, filled in later
	MSG_WriteLong(&net_message, 0);
	MSG_WriteByte(&net_message, CCREP_SERVER_INFO);
	driver->GetSocketAddr(acceptsock, &newaddr);
	MSG_WriteString(&net_message, NET_AdrToString(&newaddr));
	MSG_WriteString(&net_message, hostname.string);
	MSG_WriteString(&net_message, sv.name);
	MSG_WriteByte(&net_message, net_activeconnections);
	MSG_WriteByte(&net_message, svs.maxclients);
	MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
	MSG_WriteControlHeader(&net_message);
	driver->Write(acceptsock, net_message.data, net_message.cursize,
		      &clientaddr);
	SZ_Clear(&net_message);
	return NULL;
    }

    if (command == CCREQ_PLAYER_INFO) {
	int playerNumber;
	int activeNumber;
	int clientNumber;
	client_t *client;

	playerNumber = MSG_ReadByte();
	activeNumber = -1;
	for (clientNumber = 0, client = svs.clients;
	     clientNumber < svs.maxclients; clientNumber++, client++) {
	    if (client->active) {
		activeNumber++;
		if (activeNumber == playerNumber)
		    break;
	    }
	}
	if (clientNumber == svs.maxclients)
	    return NULL;

	SZ_Clear(&net_message);
	// save space for the header, filled in later
	MSG_WriteLong(&net_message, 0);
	MSG_WriteByte(&net_message, CCREP_PLAYER_INFO);
	MSG_WriteByte(&net_message, playerNumber);
	MSG_WriteString(&net_message, client->name);
	MSG_WriteLong(&net_message, client->colors);
	MSG_WriteLong(&net_message, (int)client->edict->v.frags);
	MSG_WriteLong(&net_message,
		      (int)(net_time - client->netconnection->connecttime));
	MSG_WriteString(&net_message, client->netconnection->address);
	MSG_WriteControlHeader(&net_message);
	driver->Write(acceptsock, net_message.data, net_message.cursize,
		      &clientaddr);
	SZ_Clear(&net_message);

	return NULL;
    }

    if (command == CCREQ_RULE_INFO) {
	char *prevCvarName;
	cvar_t *var;

	// find the search start location
	prevCvarName = MSG_ReadString();
	var = Cvar_NextServerVar(prevCvarName);
	if (!var)
	    return NULL;

	// send the response

	SZ_Clear(&net_message);
	// save space for the header, filled in later
	MSG_WriteLong(&net_message, 0);
	MSG_WriteByte(&net_message, CCREP_RULE_INFO);
	if (var) {
	    MSG_WriteString(&net_message, var->name);
	    MSG_WriteString(&net_message, var->string);
	}
	MSG_WriteControlHeader(&net_message);
	driver->Write(acceptsock, net_message.data, net_message.cursize,
		      &clientaddr);
	SZ_Clear(&net_message);

	return NULL;
    }

    if (command != CCREQ_CONNECT)
	return NULL;

    if (strcmp(MSG_ReadString(), "QUAKE") != 0)
	return NULL;

    if (MSG_ReadByte() != NET_PROTOCOL_VERSION) {
	SZ_Clear(&net_message);
	// save space for the header, filled in later
	MSG_WriteLong(&net_message, 0);
	MSG_WriteByte(&net_message, CCREP_REJECT);
	MSG_WriteString(&net_message, "Incompatible version.\n");
	MSG_WriteControlHeader(&net_message);
	driver->Write(acceptsock, net_message.data, net_message.cursize,
		      &clientaddr);
	SZ_Clear(&net_message);
	return NULL;
    }

    // check for a ban
    testAddr.ip.l = clientaddr.ip.l;
    if ((testAddr.ip.l & banMask.ip.l) == banAddr.ip.l) {
	SZ_Clear(&net_message);
	// save space for the header, filled in later
	MSG_WriteLong(&net_message, 0);
	MSG_WriteByte(&net_message, CCREP_REJECT);
	MSG_WriteString(&net_message, "You have been banned.\n");
	MSG_WriteControlHeader(&net_message);
	driver->Write(acceptsock, net_message.data, net_message.cursize,
		      &clientaddr);
	SZ_Clear(&net_message);
	return NULL;
    }

    // see if this guy is already connected
    for (s = net_activeSockets; s; s = s->next) {
	if (s->driver != net_driver)
	    continue;
	ret = NET_AddrCompare(&clientaddr, &s->addr);
	if (ret >= 0) {
	    // is this a duplicate connection reqeust?
	    if (ret == 0 && net_time - s->connecttime < 2.0) {
		// yes, so send a duplicate reply
		SZ_Clear(&net_message);
		// save space for the header, filled in later
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREP_ACCEPT);
		driver->GetSocketAddr(s->socket, &newaddr);
		MSG_WriteLong(&net_message, NET_GetSocketPort(&newaddr));
		MSG_WriteControlHeader(&net_message);
		driver->Write(acceptsock, net_message.data,
			      net_message.cursize, &clientaddr);
		SZ_Clear(&net_message);
		return NULL;
	    }
	    /*
	     * it's somebody coming back in from a crash/disconnect
	     * so close the old qsocket and let their retry get them back in
	     */
	    NET_Close(s);
	    return NULL;
	}
    }

    // allocate a QSocket
    sock = NET_NewQSocket();
    if (sock == NULL) {
	// no room; try to let him know
	SZ_Clear(&net_message);
	// save space for the header, filled in later
	MSG_WriteLong(&net_message, 0);
	MSG_WriteByte(&net_message, CCREP_REJECT);
	MSG_WriteString(&net_message, "Server is full.\n");
	MSG_WriteControlHeader(&net_message);
	driver->Write(acceptsock, net_message.data, net_message.cursize,
		      &clientaddr);
	SZ_Clear(&net_message);
	return NULL;
    }
    // allocate a network socket
    newsock = driver->OpenSocket(0);
    if (newsock == -1) {
	NET_FreeQSocket(sock);
	return NULL;
    }

    // everything is allocated, just fill in the details
    sock->socket = newsock;
    sock->landriver = driver;
    sock->addr = clientaddr;
    strcpy(sock->address, NET_AdrToString(&clientaddr));
    sock->mtu = driver->GetDefaultMTU() - NET_HEADERSIZE;

    // send him back the info about the server connection he has been allocated
    SZ_Clear(&net_message);
    // save space for the header, filled in later
    MSG_WriteLong(&net_message, 0);
    MSG_WriteByte(&net_message, CCREP_ACCEPT);
    driver->GetSocketAddr(newsock, &newaddr);
    MSG_WriteLong(&net_message, NET_GetSocketPort(&newaddr));
    MSG_WriteControlHeader(&net_message);
    driver->Write(acceptsock, net_message.data, net_message.cursize,
		  &clientaddr);
    SZ_Clear(&net_message);

    return sock;
}

qsocket_t *
Datagram_CheckNewConnections(void)
{
    unsigned i;
    net_landriver_t *driver;
    qsocket_t *ret = NULL;

    for (i = 0; i < net_numlandrivers; i++) {
	driver = &net_landrivers[i];
	if (driver->initialized)
	    if ((ret = _Datagram_CheckNewConnections(driver)) != NULL)
		break;
    }

    return ret;
}


static void
_Datagram_SearchForHosts(qboolean xmit, net_landriver_t *driver)
{
    int ret;
    int i, len, hostnum;
    netadr_t readaddr;
    netadr_t myaddr;
    int control;
    hostcache_t *host;

    driver->GetSocketAddr(driver->controlSock, &myaddr);
    if (xmit) {
	SZ_Clear(&net_message);
	// save space for the header, filled in later
	MSG_WriteLong(&net_message, 0);
	MSG_WriteByte(&net_message, CCREQ_SERVER_INFO);
	MSG_WriteString(&net_message, "QUAKE");
	MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
	MSG_WriteControlHeader(&net_message);
	driver->Broadcast(driver->controlSock, net_message.data,
			  net_message.cursize);
	SZ_Clear(&net_message);
    }

    while ((ret = driver->Read(driver->controlSock, net_message.data,
			       net_message.maxsize, &readaddr)) > 0) {
	if (ret < sizeof(int))
	    continue;
	net_message.cursize = ret;

	// don't answer our own query
	if (NET_AddrCompare(&readaddr, &myaddr) >= 0)
	    continue;

	// is the cache full?
	if (hostCacheCount == HOSTCACHESIZE)
	    continue;

	MSG_BeginReading();
	control = MSG_ReadControlHeader();
	if (control == -1)
	    continue;
	if ((control & (~NETFLAG_LENGTH_MASK)) != NETFLAG_CTL)
	    continue;
	if ((control & NETFLAG_LENGTH_MASK) != ret)
	    continue;

	if (MSG_ReadByte() != CCREP_SERVER_INFO)
	    continue;

	driver->GetAddrFromName(MSG_ReadString(), &readaddr);
	// search the cache for this server
	for (i = 0, host = hostcache; i < hostCacheCount; i++, host++)
	    if (NET_AddrCompare(&readaddr, &host->addr) == 0)
		break;
	hostnum = i;

	// is it already there?
	if (hostnum < hostCacheCount)
	    continue;

	// add it
	hostCacheCount++;

	snprintf(host->name, sizeof(host->name), "%s", MSG_ReadString());
	snprintf(host->map, sizeof(host->map), "%s", MSG_ReadString());
	host->users = MSG_ReadByte();
	host->maxusers = MSG_ReadByte();
	if (MSG_ReadByte() != NET_PROTOCOL_VERSION) {
	    strcpy(host->cname, host->name);
	    host->cname[14] = 0;
	    strcpy(host->name, "*");
	    strcat(host->name, host->cname);
	}
	host->addr = readaddr;
	host->driver = net_driver;
	host->ldriver = driver;
	strcpy(host->cname, NET_AdrToString(&readaddr));

	/*
	 * check for a name conflict (FIXME - gross!)
	 */
	for (i = 0; i < hostCacheCount; i++) {
	    if (i == hostnum)
		continue;
	    if (!strcasecmp(host->name, hostcache[i].name)) {
		const int max = sizeof(host->name);
		len = strlen(host->name);
		/*
		 * If there's room to add an extra character and the current
		 * ending character doesn't look like one we might have
		 * just added, add a zero.
		 *
		 * Otherwise, just increment the final character.
		 */
		if (len < max - 1 &&
		    host->name[len - 1] > '0' + HOSTCACHESIZE) {
		    host->name[len] = '0';
		    host->name[len + 1] = 0;
		} else
		    host->name[len - 1]++;
		i = -1; /* reset the loop counter */
	    }
	}
    }
}

void
Datagram_SearchForHosts(qboolean xmit)
{
    int i;
    net_landriver_t *driver;

    for (i = 0; i < net_numlandrivers; i++) {
	if (hostCacheCount == HOSTCACHESIZE)
	    break;
	driver = &net_landrivers[i];
	if (driver->initialized)
	    _Datagram_SearchForHosts(xmit, driver);
    }
}


static qsocket_t *
_Datagram_Connect(const char *host, net_landriver_t *driver)
{
    netadr_t sendaddr;
    netadr_t readaddr;
    qsocket_t *sock;
    int newsock;
    int ret;
    int reps;
    double start_time;
    int control;
    const char *reason;

    // see if we can resolve the host name
    if (driver->GetAddrFromName(host, &sendaddr) == -1)
	return NULL;

    newsock = driver->OpenSocket(0);
    if (newsock == -1)
	return NULL;

    sock = NET_NewQSocket();
    if (sock == NULL)
	goto ErrorReturn2;

    sock->socket = newsock;
    sock->landriver = driver;
    sock->mtu = driver->GetDefaultMTU() - NET_HEADERSIZE;

    // send the connection request
    Con_Printf("trying...\n");
    SCR_UpdateScreen();
    start_time = net_time;

    for (reps = 0; reps < 3; reps++) {
	SZ_Clear(&net_message);
	// save space for the header, filled in later
	MSG_WriteLong(&net_message, 0);
	MSG_WriteByte(&net_message, CCREQ_CONNECT);
	MSG_WriteString(&net_message, "QUAKE");
	MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
	MSG_WriteControlHeader(&net_message);
	driver->Write(newsock, net_message.data, net_message.cursize,
		     &sendaddr);
	SZ_Clear(&net_message);
	do {
	    ret = driver->Read(newsock, net_message.data, net_message.maxsize,
			       &readaddr);
	    // if we got something, validate it
	    if (ret > 0) {
		// is it from the right place?
		if (NET_AddrCompare(&readaddr, &sendaddr) != 0) {
#ifdef DEBUG
		    Con_Printf("wrong reply address\n");
		    Con_Printf("Expected: %s\n", StrAddr(&sendaddr));
		    Con_Printf("Received: %s\n", StrAddr(&readaddr));
		    SCR_UpdateScreen();
#endif
		    ret = 0;
		    continue;
		}

		if (ret < sizeof(int)) {
		    ret = 0;
		    continue;
		}
		net_message.cursize = ret;

		MSG_BeginReading();
		control = MSG_ReadControlHeader();
		if (control == -1) {
		    ret = 0;
		    continue;
		}
		if ((control & (~NETFLAG_LENGTH_MASK)) != NETFLAG_CTL) {
		    ret = 0;
		    continue;
		}
		if ((control & NETFLAG_LENGTH_MASK) != ret) {
		    ret = 0;
		    continue;
		}
	    }
	} while (ret == 0 && (SetNetTime() - start_time) < 2.5);

	if (ret)
	    break;
	Con_Printf("still trying...\n");
	SCR_UpdateScreen();
	start_time = SetNetTime();
    }

    if (ret == 0) {
	reason = "No Response";
	goto ErrorReturn;
    }

    if (ret == -1) {
	reason = "Network Error";
	goto ErrorReturn;
    }

    ret = MSG_ReadByte();
    if (ret == CCREP_REJECT) {
	reason = MSG_ReadString();
	goto ErrorReturn;
    }

    if (ret == CCREP_ACCEPT) {
	sock->addr = sendaddr;
	NET_SetSocketPort(&sock->addr, MSG_ReadLong());
    } else {
	reason = "Bad Response";
	goto ErrorReturn;
    }

    driver->GetNameFromAddr(&sendaddr, sock->address);

    Con_Printf("Connection accepted\n");
    sock->lastMessageTime = SetNetTime();
    m_return_onerror = false;

    return sock;

  ErrorReturn:
    Con_Printf("%s\n", reason);
    snprintf(m_return_reason, sizeof(m_return_reason), "%s", reason);
    NET_FreeQSocket(sock);
  ErrorReturn2:
    driver->CloseSocket(newsock);
    if (m_return_onerror) {
	key_dest = key_menu;
	m_state = m_return_state;
	m_return_onerror = false;
    }
    return NULL;
}

qsocket_t *
Datagram_Connect(const char *host)
{
    int i;
    qsocket_t *ret = NULL;
    net_landriver_t *driver;

    for (i = 0; i < net_numlandrivers; i++) {
	driver = &net_landrivers[i];
	if (driver->initialized)
	    if ((ret = _Datagram_Connect(host, driver)) != NULL)
		break;
    }
    return ret;
}
