/*

Copyright (c) 2015, Arvid Norberg
All rights reserved.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "simulator/simulator.hpp"
#include <boost/make_shared.hpp>
#include <boost/tuple/tuple.hpp>

using namespace sim::asio;

namespace sim
{
	simulation::simulation(configuration& config)
		: m_config(config)
		, m_internal_ios(*this)
		, m_stopped(false)
	{
		m_config.build(*this);
	}

	std::size_t simulation::run()
	{
		boost::system::error_code ec;
		return run(ec);
	}

	std::size_t simulation::run(boost::system::error_code& ec)
	{
		std::size_t ret = 0;
		std::size_t last_executed = 0;
		do {

			m_service.reset();
			last_executed = m_service.poll(ec);
			ret += last_executed;

			chrono::high_resolution_clock::time_point now
				= chrono::high_resolution_clock::now();

			if (!m_timer_queue.empty()) {
				asio::high_resolution_timer* next_timer = *m_timer_queue.begin();
				chrono::high_resolution_clock::fast_forward(next_timer->expires_at() - now);

				now = chrono::high_resolution_clock::now();

				while (!m_timer_queue.empty()
					&& (*m_timer_queue.begin())->expires_at() <= now) {

					next_timer = *m_timer_queue.begin();
					m_timer_queue.erase(m_timer_queue.begin());
					next_timer->fire(boost::system::error_code());
					++last_executed;
					++ret;
				}
			}

//			fprintf(stderr, "run: last_executed: %d stopped: %d timer-queue: %d\n"
//				, int(last_executed), m_stopped, int(m_timer_queue.size()));
		} while (last_executed > 0 && !m_stopped);

//		fprintf(stderr, "exiting simulation::run(): last_executed: %d stopped: %d timer-queue: %d ret: %d\n"
//			, int(last_executed), m_stopped, int(m_timer_queue.size()), int(ret));
		return ret;
	}

	void simulation::stop() { m_stopped = true; }
	bool simulation::stopped() const { return m_stopped; }
	void simulation::reset() { m_stopped = false; }

	void simulation::add_timer(asio::high_resolution_timer* t)
	{
		if (t->expires_at() == sim::chrono::high_resolution_clock::now())
		{
			fprintf(stderr, "WARNING: timer scheduled for current time!\n");
		}
		m_timer_queue.insert(t);
	}

	void simulation::remove_timer(asio::high_resolution_timer* t)
	{
		assert(!m_timer_queue.empty());
		timer_queue_t::iterator begin;
		timer_queue_t::iterator end;
		boost::tuples::tie(begin, end) = m_timer_queue.equal_range(t);
		assert(begin != end);
		begin = std::find(begin, end, t);
		assert(begin != end);
		m_timer_queue.erase(begin);
	}

	ip::tcp::endpoint simulation::bind_socket(ip::tcp::socket* socket
		, ip::tcp::endpoint ep, boost::system::error_code& ec)
	{
		assert(ep.address() != boost::asio::ip::address());

		if (ep.port() < 1024 && ep.port() > 0)
		{
			// emulate process not running as root
			ec = boost::asio::error::access_denied;
			return ip::tcp::endpoint();
		}

		if (ep.port() == 0)
		{
			// if the socket is being bound to port 0, it means the system picks a
			// free port.
			ep.port(2000);
			listen_socket_iter_t i = m_listen_sockets.lower_bound(ep);
			while (i != m_listen_sockets.end() && i->first == ep)
			{
				ep.port(ep.port() + 1);
				if (ep.port() > 65530)
				{
					ec = boost::asio::error::address_in_use;
					return ip::tcp::endpoint();
				}
				i = m_listen_sockets.lower_bound(ep);
			}
		}

		listen_socket_iter_t i = m_listen_sockets.lower_bound(ep);
		if (i != m_listen_sockets.end() && i->first == ep)
		{
			ec = boost::asio::error::address_in_use;
			return ip::tcp::endpoint();
		}

		m_listen_sockets.insert(i, std::make_pair(ep, socket));
		ec.clear();
		return ep;
	}

	void simulation::unbind_socket(ip::tcp::socket* socket
		, ip::tcp::endpoint ep)
	{
		listen_socket_iter_t i = m_listen_sockets.find(ep);
		if (i == m_listen_sockets.end() || i->second != socket) return;
		m_listen_sockets.erase(i);
	}

	ip::udp::endpoint simulation::bind_udp_socket(ip::udp::socket* socket
		, ip::udp::endpoint ep, boost::system::error_code& ec)
	{
		assert(ep.address() != boost::asio::ip::address());

		if (ep.port() < 1024 && ep.port() > 0)
		{
			// emulate process not running as root
			ec = boost::asio::error::access_denied;
			return ip::udp::endpoint();
		}

		if (ep.port() == 0)
		{
			// if the socket is being bound to port 0, it means the system picks a
			// free port.
			ep.port(2000);
			udp_socket_iter_t i = m_udp_sockets.lower_bound(ep);
			while (i != m_udp_sockets.end() && i->first == ep)
			{
				ep.port(ep.port() + 1);
				if (ep.port() > 65530)
				{
					ec = boost::asio::error::address_in_use;
					return ip::udp::endpoint();
				}
				i = m_udp_sockets.lower_bound(ep);
			}
		}

		udp_socket_iter_t i = m_udp_sockets.lower_bound(ep);
		if (i != m_udp_sockets.end() && i->first == ep)
		{
			ec = boost::asio::error::address_in_use;
			return ip::udp::endpoint();
		}

		m_udp_sockets.insert(i, std::make_pair(ep, socket));
		ec.clear();
		return ep;
	}

	void simulation::unbind_udp_socket(ip::udp::socket* socket
		, ip::udp::endpoint ep)
	{
		udp_socket_iter_t i = m_udp_sockets.find(ep);
		if (i == m_udp_sockets.end() || i->second != socket) return;
		m_udp_sockets.erase(i);
	}

	std::shared_ptr<aux::channel> simulation::internal_connect(
		asio::ip::tcp::socket* s
		, ip::tcp::endpoint const& target, boost::system::error_code& ec)
	{
		// find remote socket
		listen_sockets_t::iterator i = m_listen_sockets.find(target);
		if (i == m_listen_sockets.end())
		{
			ec = boost::system::error_code(error::connection_refused);
			return std::shared_ptr<aux::channel>();
		}

		// make sure it's a listening socket
		ip::tcp::socket* remote = i->second;
		if (!remote->internal_is_listening())
		{
			ec = boost::system::error_code(error::connection_refused);
			return std::shared_ptr<aux::channel>();
		}

		// create a channel
		std::shared_ptr<aux::channel> c = std::make_shared<aux::channel>();

		asio::ip::tcp::endpoint from = s->local_endpoint(ec);

		route network_route = m_config.channel_route(from.address()
			, target.address());
		c->hops[0] = remote->get_outgoing_route() + network_route + s->get_incoming_route();
		c->hops[1] = s->get_outgoing_route() + network_route + remote->get_incoming_route();

		c->ep[0] = s->local_endpoint(ec);
		c->ep[1] = remote->local_endpoint(ec);

		aux::packet p;
		p.type = aux::packet::syn;
		p.overhead = 28;
		*p.from = asio::ip::udp::endpoint(from.address(), from.port());
		p.channel = c;
		if (ec) return std::shared_ptr<aux::channel>();

		p.hops = c->hops[1];

		forward_packet(std::move(p));

		return c;
	}

	route simulation::find_udp_socket(asio::ip::udp::socket const& socket
		, ip::udp::endpoint const& ep)
	{
		udp_socket_iter_t i = m_udp_sockets.find(ep);
		if (i == m_udp_sockets.end())
			return route();

		ip::udp::endpoint src = socket.local_endpoint();
		route network_route = m_config.channel_route(src.address(), ep.address());

		// ask the socket for its incoming route
		network_route.append(i->second->get_incoming_route());

		return network_route;
	}

	void simulation::add_io_service(asio::io_service* ios)
	{
		bool added = m_nodes.insert(ios).second;
		(void)added;
		assert(added);
	}

	void simulation::remove_io_service(asio::io_service* ios)
	{
		auto it = m_nodes.find(ios);
		assert(it != m_nodes.end());
		m_nodes.erase(it);
	}

	std::vector<io_service*> simulation::get_all_io_services() const
	{
		std::vector<io_service*> ret;
		ret.reserve(m_nodes.size());
		std::remove_copy_if(
			m_nodes.begin(), m_nodes.end(), std::back_inserter(ret)
			, [](io_service* ios) { return ios->get_ips().empty(); });
		return ret;
	}

}

