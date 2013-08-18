/*
	Copyright (C) 2013 by Pierre Talbot <ptalbot@mopong.net>
	Part of the Battle for Wesnoth Project http://www.wesnoth.org/

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY.

	See the COPYING file for more details.
*/

#include <boost/lexical_cast.hpp>
#include <boost/current_function.hpp>
#include <boost/assert.hpp>

#include "umcd/protocol/wml/umcd_protocol.hpp"
#include "umcd/special_packet.hpp"
#include "umcd/error.hpp"
#include "umcd/env/server_info.hpp"
#include "umcd/env/protocol_info.hpp"

std::size_t umcd_protocol::REQUEST_HEADER_MAX_SIZE = 8192;

#define FUNCTION_TRACER() UMCD_LOG_IP_FUNCTION_TRACER(socket_)

umcd_protocol::umcd_protocol(io_service_type& io_service, const environment& env)
: environment_(env)
, socket_(io_service)
{
}

wml_reply& umcd_protocol::get_reply()
{
	return reply_;
}

config& umcd_protocol::get_metadata()
{
	return request_.get_metadata();
}

void umcd_protocol::load(const protocol_info& proto_info)
{
	REQUEST_HEADER_MAX_SIZE = proto_info.header_max_size();
}

umcd_protocol::socket_type& umcd_protocol::socket()
{
	return socket_;
}

void umcd_protocol::complete_request(const boost::system::error_code& error, std::size_t)
{
	FUNCTION_TRACER();
	if(error)
	{
		UMCD_LOG_IP(info, socket_) << " -- unable to send data to the client (" << error.message() << "). Connection dropped.";
		// TODO close socket.
	}
}

void umcd_protocol::async_send_reply()
{
	FUNCTION_TRACER();

	boost::asio::async_write(socket_
		, reply_.to_buffers()
		, boost::bind(&umcd_protocol::complete_request, shared_from_this()
			, boost::asio::placeholders::error
			, boost::asio::placeholders::bytes_transferred)
	);
}

void umcd_protocol::async_send_error(const boost::system::error_condition& error)
{
	reply_ = make_error_reply(error.message());
	async_send_reply();
}

void umcd_protocol::async_send_invalid_packet(const std::string &where, const std::exception& e)
{
	UMCD_LOG_IP(error, socket_) << " -- invalid request at " << where << " (" << e.what() << ")";
	async_send_error(make_error_condition(umcd::invalid_packet));
}

void umcd_protocol::async_send_invalid_packet(const std::string &where, const twml_exception& e)
{
	UMCD_LOG_IP(error, socket_) << " -- invalid request at " << where 
										<< " (user message=" << e.user_message << " ; dev message=" << e.dev_message << ")";
	async_send_error(make_error_condition(umcd::invalid_packet));
}

void umcd_protocol::read_request_body(const boost::system::error_code& error, std::size_t)
{
	FUNCTION_TRACER();
	if(!error)
	{
		try
		{
			payload_size_ = ntohl(payload_size_);
			UMCD_LOG_IP(debug, socket_) << " -- Request of size: " << payload_size_;
			if(payload_size_ > REQUEST_HEADER_MAX_SIZE)
			{
				async_send_error(make_error_condition(umcd::request_header_too_large));
			}
			else
			{
				request_body_.resize(payload_size_);
				boost::asio::async_read(socket_, boost::asio::buffer(&request_body_[0], request_body_.size())
					, boost::bind(&umcd_protocol::dispatch_request, shared_from_this()
					, boost::asio::placeholders::error
					, boost::asio::placeholders::bytes_transferred)
				);
			}
		}
		catch(const std::exception& e)
		{
			async_send_invalid_packet(BOOST_CURRENT_FUNCTION, e);
		}
	}
}

void umcd_protocol::dispatch_request(const boost::system::error_code& err, std::size_t)
{
	FUNCTION_TRACER();
	if(!err)
	{
		std::stringstream request_stream(request_body_);      
		try
		{
			// Retrieve request name.
			std::string request_name = peek_request_name(request_stream);
			UMCD_LOG_IP(info, socket_) << " -- request: " << request_name;
			info_ptr request_info = environment_.get_request_info(request_name);
			UMCD_LOG_IP(info, socket_) << " -- request:\n" << request_body_;

			request_ = wml_request();
			// Read into config and validate metadata.
			::read(request_.get_metadata(), request_stream, request_info->validator().get());
			UMCD_LOG_IP(debug, socket_) << " -- request validated.";

			request_info->action()->execute(shared_from_this());
		}
		catch(const std::exception& e)
		{
			async_send_invalid_packet(BOOST_CURRENT_FUNCTION, e);
		}
		catch(const twml_exception& e)
		{
			async_send_invalid_packet(BOOST_CURRENT_FUNCTION, e);
		}
	}
}

void umcd_protocol::handle_request()
{
	FUNCTION_TRACER();
	boost::asio::async_read(socket_, boost::asio::buffer(reinterpret_cast<char*>(&payload_size_), sizeof(payload_size_))
		, boost::bind(&umcd_protocol::read_request_body, shared_from_this()
			, boost::asio::placeholders::error
			, boost::asio::placeholders::bytes_transferred)
	);
}

boost::shared_ptr<umcd_protocol> make_umcd_protocol(umcd_protocol::io_service_type& io_service, const environment& serverinfo)
{
	return boost::make_shared<umcd_protocol>(boost::ref(io_service), boost::cref(serverinfo));
}
