/*
This source file is part of KBEngine
For the latest info, see http://www.kbengine.org/

Copyright (c) 2008-2012 KBEngine.

KBEngine is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

KBEngine is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.
 
You should have received a copy of the GNU Lesser General Public License
along with KBEngine.  If not, see <http://www.gnu.org/licenses/>.
*/


#include "websocket_packet_filter.h"
#include "websocket_protocol.h"

#include "network/bundle.h"
#include "network/channel.h"
#include "network/tcp_packet.h"
#include "network/network_interface.h"
#include "network/packet_receiver.h"

namespace KBEngine { 
namespace Network
{

//-------------------------------------------------------------------------------------
WebSocketPacketFilter::WebSocketPacketFilter(Channel* pChannel):
	web_pFragmentDatasRemain_(2),
	web_fragmentDatasFlag_(FRAGMENT_DATA_BASIC_LENGTH),
	msg_opcode_(0),
	msg_fin_(0),
	msg_masked_(0),
	msg_mask_(0),
	msg_length_field_(0),
	msg_payload_length_(0),
	msg_frameType_(websocket::WebSocketProtocol::ERROR_FRAME),
	pChannel_(pChannel),
	pTCPPacket_(NULL)
{
}

//-------------------------------------------------------------------------------------
WebSocketPacketFilter::~WebSocketPacketFilter()
{
	TCPPacket::ObjPool().reclaimObject(pTCPPacket_);
	pTCPPacket_ = NULL;
}

//-------------------------------------------------------------------------------------
void WebSocketPacketFilter::resetFrame()
{
	msg_opcode_ = 0;
	msg_fin_ = 0;
	msg_masked_ = 0;
	msg_mask_ = 0;
	msg_length_field_ = 0;
	msg_payload_length_ = 0;
}

//-------------------------------------------------------------------------------------
Reason WebSocketPacketFilter::send(Channel * pChannel, PacketSender& sender, Packet * pPacket)
{
	if(pPacket->encrypted())
		return PacketFilter::send(pChannel, sender, pPacket);

	Bundle* pBundle = pPacket->pBundle();
	TCPPacket* pRetTCPPacket = TCPPacket::ObjPool().createObject();
	websocket::WebSocketProtocol::FrameType frameType = websocket::WebSocketProtocol::BINARY_FRAME;

	if(pBundle && pBundle->packets().size() > 1)
	{
		bool isEnd = pBundle->packets().back() == pPacket;
		bool isBegin = pBundle->packets().front() == pPacket;

		if(!isEnd && !isBegin)
		{
			frameType = websocket::WebSocketProtocol::NEXT_FRAME;
		}
		else
		{
			if(!isEnd)
			{
				frameType = websocket::WebSocketProtocol::INCOMPLETE_BINARY_FRAME;
			}
			else
			{
				frameType = websocket::WebSocketProtocol::END_FRAME;
			}
		}
	}

	websocket::WebSocketProtocol::makeFrame(frameType, pPacket, pRetTCPPacket);

	int space = pPacket->length() - pRetTCPPacket->space();
	if(space > 0)
	{
		WARNING_MSG(fmt::format("WebSocketPacketFilter::send: no free space, buffer added:{}, total={}.\n",
			space, pRetTCPPacket->size()));

		pRetTCPPacket->data_resize(pRetTCPPacket->size() + space);
	}

	(*pRetTCPPacket).append(pPacket->data() + pPacket->rpos(), pPacket->length());
	pRetTCPPacket->swap(*(static_cast<KBEngine::MemoryStream*>(pPacket)));
	TCPPacket::ObjPool().reclaimObject(pRetTCPPacket);

	pPacket->encrypted(true);
	return PacketFilter::send(pChannel, sender, pPacket);
}

//-------------------------------------------------------------------------------------
Reason WebSocketPacketFilter::recv(Channel * pChannel, PacketReceiver & receiver, Packet * pPacket)
{
	while(pPacket->length() > 0)
	{
		TCPPacket* pRetTCPPacket = TCPPacket::ObjPool().createObject();

		int remainSize = websocket::WebSocketProtocol::getFrame(pPacket, msg_opcode_, msg_fin_, msg_masked_, 
			msg_mask_, msg_length_field_, msg_payload_length_, msg_frameType_, pRetTCPPacket);
		
		if(websocket::WebSocketProtocol::ERROR_FRAME == msg_frameType_)
		{
			ERROR_MSG(fmt::format("WebSocketPacketReader::processMessages: frame is error! addr={}!\n",
				pChannel_->c_str()));

			this->pChannel_->condemn();
			TCPPacket::ObjPool().reclaimObject(pRetTCPPacket);

			return REASON_WEBSOCKET_ERROR;
		}
		else if(msg_frameType_ == websocket::WebSocketProtocol::TEXT_FRAME || 
				msg_frameType_ == websocket::WebSocketProtocol::INCOMPLETE_TEXT_FRAME ||
				msg_frameType_ == websocket::WebSocketProtocol::PING_FRAME ||
				msg_frameType_ == websocket::WebSocketProtocol::PONG_FRAME)
		{
			ERROR_MSG(fmt::format("WebSocketPacketReader::processMessages: Does not support FRAME_TYPE()! addr={}!\n",
				(int)msg_frameType_, pChannel_->c_str()));

			this->pChannel_->condemn();
			TCPPacket::ObjPool().reclaimObject(pRetTCPPacket);

			return REASON_WEBSOCKET_ERROR;
		}
		else if(msg_frameType_ == websocket::WebSocketProtocol::CLOSE_FRAME)
		{
			TCPPacket::ObjPool().reclaimObject(pRetTCPPacket);
			this->pChannel_->condemn();
			pRetTCPPacket = NULL;

			return REASON_SUCCESS;
		}
		else if(msg_frameType_ == websocket::WebSocketProtocol::INCOMPLETE_FRAME)
		{
			KBE_ASSERT(false);
		}

		KBE_ASSERT(remainSize == 0);
		resetFrame();

		Reason reason = PacketFilter::recv(pChannel, receiver, pRetTCPPacket);
		if(reason != REASON_SUCCESS)
			return reason;

	}

	return REASON_SUCCESS;
}

//-------------------------------------------------------------------------------------
} 
}
