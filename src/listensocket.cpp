/*       +------------------------------------+
 *       | Inspire Internet Relay Chat Daemon |
 *       +------------------------------------+
 *
 *  InspIRCd: (C) 2002-2011 InspIRCd Development Team
 * See: http://wiki.inspircd.org/Credits
 *
 * This program is free but copyrighted software; see
 *            the file COPYING for details.
 *
 * ---------------------------------------------------
 */

#include "inspircd.h"
#include "inspsocket.h"

ListenSocket::ListenSocket(ConfigTag* tag, const irc::sockets::sockaddrs& bind_to)
	: bind_tag(tag)
{
	bind_addr = bind_to.addr();
	bind_port = bind_to.port();
	bind_desc = bind_to.str();

	fd = socket(bind_to.sa.sa_family, SOCK_STREAM, 0);

	if (this->fd == -1)
		return;

#ifdef IPV6_V6ONLY
	/* This OS supports IPv6 sockets that can also listen for IPv4
	 * connections. If our address is "*" or empty, enable both v4 and v6 to
	 * allow for simpler configuration on dual-stack hosts. Otherwise, if it
	 * is "::" or an IPv6 address, disable support so that an IPv4 bind will
	 * work on the port (by us or another application).
	 */
	if (bind_to.sa.sa_family == AF_INET6)
	{
		std::string addr = tag->getString("address");
		int enable = (addr.empty() || addr == "*") ? 0 : 1;
		setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &enable, sizeof(enable));
		// errors ignored intentionally
	}
#endif

	ServerInstance->SE->SetReuse(fd);
	int rv = ServerInstance->SE->Bind(this->fd, bind_to);
	if (rv >= 0)
		rv = ServerInstance->SE->Listen(this->fd, ServerInstance->Config->MaxConn);

	if (rv < 0)
	{
		int errstore = errno;
		ServerInstance->SE->Shutdown(this, 2);
		ServerInstance->SE->Close(this);
		this->fd = -1;
		errno = errstore;
	}
	else
	{
		ServerInstance->SE->NonBlocking(this->fd);
		ServerInstance->SE->AddFd(this, FD_WANT_POLL_READ | FD_WANT_NO_WRITE);
	}
}

ListenSocket::~ListenSocket()
{
	if (this->GetFd() > -1)
	{
		ServerInstance->SE->DelFd(this);
		ServerInstance->Logs->Log("SOCKET", DEBUG,"Shut down listener on fd %d", this->fd);
		if (ServerInstance->SE->Shutdown(this, 2) || ServerInstance->SE->Close(this))
			ServerInstance->Logs->Log("SOCKET", DEBUG,"Failed to cancel listener: %s", strerror(errno));
		this->fd = -1;
	}
}

/* Just seperated into another func for tidiness really.. */
void ListenSocket::AcceptInternal()
{
	irc::sockets::sockaddrs client;
	irc::sockets::sockaddrs server;

	socklen_t length = sizeof(client);
	int incomingSockfd = ServerInstance->SE->Accept(this, &client.sa, &length);

	ServerInstance->Logs->Log("SOCKET",DEBUG,"HandleEvent for Listensocket %s nfd=%d", bind_desc.c_str(), incomingSockfd);
	if (incomingSockfd < 0)
	{
		ServerInstance->stats->statsRefused++;
		return;
	}

	socklen_t sz = sizeof(server);
	if (getsockname(incomingSockfd, &server.sa, &sz))
	{
		ServerInstance->Logs->Log("SOCKET", DEBUG, "Can't get peername: %s", strerror(errno));
		irc::sockets::aptosa(bind_addr, bind_port, server);
	}

	/*
	 * XXX -
	 * this is done as a safety check to keep the file descriptors within range of fd_ref_table.
	 * its a pretty big but for the moment valid assumption:
	 * file descriptors are handed out starting at 0, and are recycled as theyre freed.
	 * therefore if there is ever an fd over 65535, 65536 clients must be connected to the
	 * irc server at once (or the irc server otherwise initiating this many connections, files etc)
	 * which for the time being is a physical impossibility (even the largest networks dont have more
	 * than about 10,000 users on ONE server!)
	 */
	if (incomingSockfd >= ServerInstance->SE->GetMaxFds())
	{
		ServerInstance->Logs->Log("SOCKET", DEBUG, "Server is full");
		ServerInstance->SE->Shutdown(incomingSockfd, 2);
		ServerInstance->SE->Close(incomingSockfd);
		ServerInstance->stats->statsRefused++;
		return;
	}

	if (client.sa.sa_family == AF_INET6)
	{
		/*
		 * This case is the be all and end all patch to catch and nuke 4in6
		 * instead of special-casing shit all over the place and wreaking merry
		 * havoc with crap, instead, we just recreate sockaddr and strip ::ffff: prefix
		 * if it's a 4in6 IP.
		 *
		 * This is, of course, much improved over the older way of handling this
		 * (pretend it doesn't exist + hack around it -- yes, both were done!)
		 *
		 * Big, big thanks to danieldg for his work on this.
		 * -- w00t
		 */
		static const unsigned char prefix4in6[12] = { 0,0,0,0, 0,0,0,0, 0,0,0xFF,0xFF };
		if (!memcmp(prefix4in6, &client.in6.sin6_addr, 12))
		{
			// recreate as a sockaddr_in using the IPv4 IP
			uint16_t sport = client.in6.sin6_port;
			uint32_t addr = *reinterpret_cast<uint32_t*>(client.in6.sin6_addr.s6_addr + 12);
			client.in4.sin_family = AF_INET;
			client.in4.sin_port = sport;
			client.in4.sin_addr.s_addr = addr;

			sport = server.in6.sin6_port;
			addr = *reinterpret_cast<uint32_t*>(server.in6.sin6_addr.s6_addr + 12);
			server.in4.sin_family = AF_INET;
			server.in4.sin_port = sport;
			server.in4.sin_addr.s_addr = addr;
		}
	}

	ServerInstance->SE->NonBlocking(incomingSockfd);

	StreamSocket* sock = NULL;

	DO_EACH_HOOK(OnAcceptConnection, sock, (incomingSockfd, this, &client, &server))
	{
		if (sock)
			break;
	}
	WHILE_EACH_HOOK(OnAcceptConnection);

	if (!sock)
	{
		std::string type = bind_tag->getString("type", "clients");
		if (type == "clients")
		{
			try
			{
				LocalUser* New = new LocalUser(incomingSockfd, &client, &server);
				ServerInstance->Users->AddUser(New, this);
				if (!New->quitting)
					sock = New->eh;
			}
			catch (...)
			{
				ServerInstance->SNO->WriteToSnoMask('a', "WARNING *** Duplicate UUID allocated!");
			}
		}
	}
	if (sock)
	{
		std::string ssl = bind_tag->getString("ssl", "plaintext");
		if (ssl != "plaintext")
		{
			IOHookProvider* prov = static_cast<IOHookProvider*>(ServerInstance->Modules->FindService(SERVICE_IOHOOK, ssl));
			if (!prov)
			{
				sock->SetError("Could not find IOHook " + ssl);
				ServerInstance->Logs->Log("SOCKET",DEFAULT,"Refusing connection on %s due to missing hook %s",
					bind_desc.c_str(), ssl.c_str());
				ServerInstance->stats->statsRefused++;
				return;
			}
			prov->OnServerConnection(sock, this);
		}
		ServerInstance->stats->statsAccept++;
	}
	else
	{
		ServerInstance->stats->statsRefused++;
		ServerInstance->Logs->Log("SOCKET",DEFAULT,"Refusing connection on %s", bind_desc.c_str());
		ServerInstance->SE->Close(incomingSockfd);
	}
}

void ListenSocket::HandleEvent(EventType e, int err)
{
	switch (e)
	{
		case EVENT_ERROR:
			ServerInstance->Logs->Log("SOCKET",DEFAULT,"ListenSocket::HandleEvent() received a socket engine error event! well shit! '%s'", strerror(err));
			break;
		case EVENT_WRITE:
			ServerInstance->Logs->Log("SOCKET",DEBUG,"*** BUG *** ListenSocket::HandleEvent() got a WRITE event!!!");
			break;
		case EVENT_READ:
			this->AcceptInternal();
			break;
	}
}
