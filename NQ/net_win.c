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

#include "net.h"
#include "net_dgrm.h"
#include "net_loop.h"
#include "net_wins.h"

#include "quakedef.h"

net_driver_t net_drivers[] = {
    {
	.name				= "Loopback",
	.initialized			= false,
	.Init				= Loop_Init,
	.Listen				= Loop_Listen,
	.SearchForHosts			= Loop_SearchForHosts,
	.Connect			= Loop_Connect,
	.CheckNewConnections		= Loop_CheckNewConnections,
	.QGetMessage			= Loop_GetMessage,
	.QSendMessage			= Loop_SendMessage,
	.SendUnreliableMessage		= Loop_SendUnreliableMessage,
	.CanSendMessage			= Loop_CanSendMessage,
	.CanSendUnreliableMessage	= Loop_CanSendUnreliableMessage,
	.Close				= Loop_Close,
	.Shutdown			= Loop_Shutdown
    }, {
	.name				= "Datagram",
	.initialized			= false,
	.Init				= Datagram_Init,
	.Listen				= Datagram_Listen,
	.SearchForHosts			= Datagram_SearchForHosts,
	.Connect			= Datagram_Connect,
	.CheckNewConnections		= Datagram_CheckNewConnections,
	.QGetMessage			= Datagram_GetMessage,
	.QSendMessage			= Datagram_SendMessage,
	.SendUnreliableMessage		= Datagram_SendUnreliableMessage,
	.CanSendMessage			= Datagram_CanSendMessage,
	.CanSendUnreliableMessage	= Datagram_CanSendUnreliableMessage,
	.Close				= Datagram_Close,
	.Shutdown			= Datagram_Shutdown
    }
};

int net_numdrivers = 2;

net_landriver_t net_landrivers[] = {
    {
	.name			= "Winsock TCPIP",
	.initialized		= false,
	.controlSock		= 0,
	.Init			= WINS_Init,
	.Shutdown		= WINS_Shutdown,
	.Listen			= WINS_Listen,
	.OpenSocket		= WINS_OpenSocket,
	.CloseSocket		= WINS_CloseSocket,
	.CheckNewConnections	= WINS_CheckNewConnections,
	.Read			= WINS_Read,
	.Write			= WINS_Write,
	.Broadcast		= WINS_Broadcast,
	.GetSocketAddr		= WINS_GetSocketAddr,
	.GetNameFromAddr	= WINS_GetNameFromAddr,
	.GetAddrFromName	= WINS_GetAddrFromName,
	.GetDefaultMTU		= WINS_GetDefaultMTU
    }
};

int net_numlandrivers = 1;
