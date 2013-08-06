/**********************************************************************
 * Copyright 2013 Georg Rudoy <0xd34df00d@gmail.com>
 *
 * Boost Software License - Version 1.0 - August 17th, 2003
 *
 * Permission is hereby granted, free of charge, to any person or organization
 * obtaining a copy of the software and accompanying documentation covered by
 * this license (the "Software") to use, reproduce, display, distribute,
 * execute, and transmit the Software, and to prepare derivative works of the
 * Software, and to permit third-parties to whom the Software is furnished to
 * do so, all subject to the following:
 *
 * The copyright notices in the Software and this entire statement, including
 * the above license grant, this restriction and the following disclaimer,
 * must be included in all copies of the Software, in whole or in part, and
 * all derivative works of the Software, unless such copies or derivative
 * works are solely in the form of machine-executable object code generated by
 * a source language processor.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
 * SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
 * FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 **********************************************************************/

#include "clientconnection.h"
#include <string>
#include <iostream>
#include <boost/asio/read_until.hpp>
#include <boost/asio/write.hpp>
#include <boost/lexical_cast.hpp>
#include "packetparser.h"
#include "packetgenerator.h"
#include "operation.h"
#include "dbmanager.h"
#include "dboperator.h"
#include "db.h"

namespace
{
	class MatchPacketEnd
	{
		size_t m_expectedLength;
	public:
		MatchPacketEnd ()
		: m_expectedLength (0)
		{
		}

		template<typename Iterator>
		std::pair<Iterator, bool> operator() (Iterator begin, Iterator end)
		{
			std::string input (begin, end);
			if (!m_expectedLength)
			{
				const std::string lengthMarker ("Length: ");
				const auto pos = input.find (lengthMarker);
				const auto nextLine = input.find ("\n", pos);
				const auto headerEnd = input.find ("\n\n", pos);

				if (pos == std::string::npos ||
						headerEnd == std::string::npos)
					return { begin, false };

				const auto numStart = pos + lengthMarker.size ();
				const auto num = boost::lexical_cast<size_t> (input.substr (numStart, nextLine - numStart));
				m_expectedLength = num;

				begin += headerEnd + 2;
			}

			const auto packetEnd = begin + m_expectedLength;
			const auto hasFullPacket = packetEnd <= end;
			return { hasFullPacket ? packetEnd : begin, hasFullPacket };
		}
	};
}

namespace boost
{
namespace asio
{
	template<>
	struct is_match_condition<MatchPacketEnd> : public true_type {};
}
}

namespace Laretz
{
	namespace asio = boost::asio;

	ClientConnection::ClientConnection (boost::asio::io_service& io, std::shared_ptr<DBManager> dbMgr)
	: m_dbMgr (dbMgr)
	, m_io (io)
	, m_socket (io)
	, m_strand (io)
	{
	}

	asio::ip::tcp::socket& ClientConnection::getSocket ()
	{
		return m_socket;
	}

	void ClientConnection::start ()
	{
		auto shared = shared_from_this ();
		boost::asio::async_read_until (m_socket,
				m_buf, MatchPacketEnd (),
				m_strand.wrap ([shared] (const boost::system::error_code& ec, std::size_t bytes)
						{ shared->handleRead (ec, bytes); }));
	}

	void ClientConnection::handleRead (const boost::system::error_code& ec, size_t bytesRead)
	{
		if (ec)
		{
			if (ec.value () != boost::system::errc::no_such_file_or_directory)
				std::cerr << "error reading " << ec.value () << "; " << ec.message () << std::endl;
			return;
		}

		const std::string data (asio::buffer_cast<const char*> (m_buf.data ()), bytesRead);
		m_buf.consume (bytesRead);
		start ();

		ParseResult result;
		try
		{
			result = Parse (data);
		}
		catch (const std::exception& e)
		{
			writeErrorResponse ("invalid packet format");
			return;
		}

		auto getSafe = [&result] (const std::string& name) -> std::string
		{
			const auto pos = result.fields.find (name);
			return pos == result.fields.end () ? std::string () : pos->second;
		};
		const auto& login = getSafe ("Login");
		const auto& pass = getSafe ("Password");

		DB_ptr db;
		try
		{
			db = m_dbMgr->GetDB ({ login, pass });
		}
		catch (const std::exception& e)
		{
			writeErrorResponse (std::string ("unable to get database: ") + e.what ());
			return;
		}

		try
		{
			PacketGenerator pg { { { "Status", "Success" } } };
			pg [DBOperator { db } (result.operations)];

			auto shared = shared_from_this ();
			boost::asio::async_write (m_socket,
					boost::asio::buffer (pg ()),
					[shared] (const boost::system::error_code&, std::size_t) {});
		}
		catch (const DBOpError& e)
		{
			writeErrorResponse (e.what (), e.getEC ());
		}
		catch (const std::exception& e)
		{
			writeErrorResponse (e.what ());
		}
	}

	void ClientConnection::writeErrorResponse (const std::string& reason, int code)
	{
		std::cerr << "writing invalid " << code << " -> " << reason << std::endl;
		PacketGenerator pg
		{
			{
				{ "Status", "Error" },
				{ "Reason", reason },
				{ "ErrorCode", boost::lexical_cast<std::string> (code) }
			}
		};
		const auto& data = pg ();
		auto shared = shared_from_this ();
		boost::asio::async_write (m_socket,
				boost::asio::buffer (data),
				[shared] (const boost::system::error_code&, std::size_t) {});
	}
}
