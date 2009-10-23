/*       +------------------------------------+
 *       | Inspire Internet Relay Chat Daemon |
 *       +------------------------------------+
 *
 *  InspIRCd: (C) 2002-2009 InspIRCd Development Team
 * See: http://wiki.inspircd.org/Credits
 *
 * This program is free but copyrighted software; see
 *            the file COPYING for details.
 *
 * ---------------------------------------------------
 */

#ifndef __TREESOCKET_H__
#define __TREESOCKET_H__

#include "socket.h"
#include "inspircd.h"
#include "xline.h"

#include "utils.h"

/*
 * The server list in InspIRCd is maintained as two structures
 * which hold the data in different ways. Most of the time, we
 * want to very quicky obtain three pieces of information:
 *
 * (1) The information on a server
 * (2) The information on the server we must send data through
 *     to actually REACH the server we're after
 * (3) Potentially, the child/parent objects of this server
 *
 * The InspIRCd spanning protocol provides easy access to these
 * by storing the data firstly in a recursive structure, where
 * each item references its parent item, and a dynamic list
 * of child items, and another structure which stores the items
 * hashed, linearly. This means that if we want to find a server
 * by name quickly, we can look it up in the hash, avoiding
 * any O(n) lookups. If however, during a split or sync, we want
 * to apply an operation to a server, and any of its child objects
 * we can resort to recursion to walk the tree structure.
 * Any socket can have one of five states at any one time.
 *
 * CONNECTING:	indicates an outbound socket which is
 *							waiting to be writeable.
 * WAIT_AUTH_1:	indicates the socket is outbound and
 * 							has successfully connected, but has not
 *							yet sent and received SERVER strings.
 * WAIT_AUTH_2:	indicates that the socket is inbound
 * 							but has not yet sent and received
 *							SERVER strings.
 * CONNECTED:		represents a fully authorized, fully
 *							connected server.
 */
enum ServerState { CONNECTING, WAIT_AUTH_1, WAIT_AUTH_2, CONNECTED };

/** Every SERVER connection inbound or outbound is represented by
 * an object of type TreeSocket.
 * TreeSockets, being inherited from BufferedSocket, can be tied into
 * the core socket engine, and we cn therefore receive activity events
 * for them, just like activex objects on speed. (yes really, that
 * is a technical term!) Each of these which relates to a locally
 * connected server is assocated with it, by hooking it onto a
 * TreeSocket class using its constructor. In this way, we can
 * maintain a list of servers, some of which are directly connected,
 * some of which are not.
 */
class TreeSocket : public BufferedSocket
{
	SpanningTreeUtilities* Utils;		/* Utility class */
	std::string myhost;			/* Canonical hostname */
	ServerState LinkState;			/* Link state */
	std::string InboundServerName;		/* Server name sent to us by other side */
	std::string InboundDescription;		/* Server description (GECOS) sent to us by the other side */
	std::string InboundSID;			/* Server ID sent to us by the other side */
	int num_lost_users;			/* Users lost in split */
	int num_lost_servers;			/* Servers lost in split */
	time_t NextPing;			/* Time when we are due to ping this server */
	bool LastPingWasGood;			/* Responded to last ping we sent? */
	std::string IP;
	std::string ModuleList;			/* Required module list of other server from CAPAB */
	std::string OptModuleList;		/* Optional module list of other server from CAPAB */
	std::map<std::string,std::string> CapKeys;	/* CAPAB keys from other server */
	std::string ourchallenge;		/* Challenge sent for challenge/response */
	std::string theirchallenge;		/* Challenge recv for challenge/response */
	std::string OutboundPass;		/* Outbound password */
	int capab_phase;			/* Have sent CAPAB already */
	bool auth_fingerprint;			/* Did we auth using SSL fingerprint */
	bool auth_challenge;			/* Did we auth using challenge/response */
	int proto_version;			/* Remote protocol version */
 public:
	reference<Autoconnect> myautoconnect;		/* Autoconnect used to cause this connection, if any */
	time_t age;

	/** Because most of the I/O gubbins are encapsulated within
	 * BufferedSocket, we just call the superclass constructor for
	 * most of the action, and append a few of our own values
	 * to it.
	 */
	TreeSocket(SpanningTreeUtilities* Util, const std::string& host, int port, unsigned long maxtime, const std::string &ServerName, const std::string &bindto, Autoconnect* myac, const std::string& Hook);

	/** When a listening socket gives us a new file descriptor,
	 * we must associate it with a socket without creating a new
	 * connection. This constructor is used for this purpose.
	 */
	TreeSocket(SpanningTreeUtilities* Util, int newfd, ListenSocket* via, irc::sockets::sockaddrs* client, irc::sockets::sockaddrs* server);

	/** Get link state
	 */
	ServerState GetLinkState();

	/** Get challenge set in our CAPAB for challenge/response
	 */
	const std::string& GetOurChallenge();

	/** Get challenge set in our CAPAB for challenge/response
	 */
	void SetOurChallenge(const std::string &c);

	/** Get challenge set in their CAPAB for challenge/response
	 */
	const std::string& GetTheirChallenge();

	/** Get challenge set in their CAPAB for challenge/response
	 */
	void SetTheirChallenge(const std::string &c);

	/** Compare two passwords based on authentication scheme
	 */
	bool ComparePass(const Link& link, const std::string &theirs);

	/** Clean up information used only during server negotiation
	 */
	void CleanNegotiationInfo();

	CullResult cull();
	/** Destructor
	 */
	~TreeSocket();

	/** Generate random string used for challenge-response auth
	 */
	std::string RandString(unsigned int length);

	/** Construct a password, optionally hashed with the other side's
	 * challenge string
	 */
	std::string MakePass(const std::string &password, const std::string &challenge);

	/** When an outbound connection finishes connecting, we receive
	 * this event, and must send our SERVER string to the other
	 * side. If the other side is happy, as outlined in the server
	 * to server docs on the inspircd.org site, the other side
	 * will then send back its own server string.
	 */
	virtual void OnConnected();

	/** Handle socket error event
	 */
	virtual void OnError(BufferedSocketError e);

	/** Sends an error to the remote server, and displays it locally to show
	 * that it was sent.
	 */
	void SendError(const std::string &errormessage);

	/** Recursively send the server tree with distances as hops.
	 * This is used during network burst to inform the other server
	 * (and any of ITS servers too) of what servers we know about.
	 * If at any point any of these servers already exist on the other
	 * end, our connection may be terminated. The hopcounts given
	 * by this function are relative, this doesn't matter so long as
	 * they are all >1, as all the remote servers re-calculate them
	 * to be relative too, with themselves as hop 0.
	 */
	void SendServers(TreeServer* Current, TreeServer* s, int hops);

	/** Returns module list as a string, filtered by filter
	 * @param filter a module version bitmask, such as VF_COMMON or VF_OPTCOMMON
	 */
	std::string MyModules(int filter);

	/** Send my capabilities to the remote side
	 */
	void SendCapabilities(int phase);

	/** Add modules to VF_COMMON list for backwards compatability */
	void CompatAddModules(std::vector<std::string>& modlist);

	/* Check a comma seperated list for an item */
	bool HasItem(const std::string &list, const std::string &item);

	/* Isolate and return the elements that are different between two comma seperated lists */
	std::string ListDifference(const std::string &one, const std::string &two);

	bool Capab(const parameterlist &params);

	/** This function forces this server to quit, removing this server
	 * and any users on it (and servers and users below that, etc etc).
	 * It's very slow and pretty clunky, but luckily unless your network
	 * is having a REAL bad hair day, this function shouldnt be called
	 * too many times a month ;-)
	 */
	void SquitServer(std::string &from, TreeServer* Current);

	/** This is a wrapper function for SquitServer above, which
	 * does some validation first and passes on the SQUIT to all
	 * other remaining servers.
	 */
	void Squit(TreeServer* Current, const std::string &reason);

	/** FMODE command - server mode with timestamp checks */
	void ForceMode(User* who, parameterlist &params);

	/** FTOPIC command */
	bool ForceTopic(const std::string &source, parameterlist &params);

	/** FJOIN, similar to TS6 SJOIN, but not quite. */
	void ForceJoin(User* who, parameterlist &params);

	/* Used on nick collision ... XXX ugly function HACK */
	int DoCollision(User *u, time_t remotets, const std::string &remoteident, const std::string &remoteip, const std::string &remoteuid);

	/** UID command */
	bool ParseUID(const std::string &source, parameterlist &params);

	/** Send one or more FJOINs for a channel of users.
	 * If the length of a single line is more than 480-NICKMAX
	 * in length, it is split over multiple lines.
	 */
	void SendFJoins(TreeServer* Current, Channel* c);

	/** Send G, Q, Z and E lines */
	void SendXLines(TreeServer* Current);

	/** Send channel modes and topics */
	void SendChannelModes(TreeServer* Current);

	/** send all users and their oper state/modes */
	void SendUsers(TreeServer* Current);

	/** This function is called when we want to send a netburst to a local
	 * server. There is a set order we must do this, because for example
	 * users require their servers to exist, and channels require their
	 * users to exist. You get the idea.
	 */
	void DoBurst(TreeServer* s);

	/** This function is called when we receive data from a remote
	 * server.
	 */
	void OnDataReady();

	/** Send one or more complete lines down the socket
	 */
	void WriteLine(std::string line);

	/** Handle ERROR command */
	bool Error(parameterlist &params);

	/** remote MOTD. */
	bool Motd(const std::string &prefix, parameterlist &params);

	/** remote ADMIN. */
	bool Admin(const std::string &prefix, parameterlist &params);

	bool Stats(const std::string &prefix, parameterlist &params);

	/** Because the core won't let users or even SERVERS set +o,
	 * we use the OPERTYPE command to do this.
	 */
	bool OperType(const std::string &prefix, parameterlist &params);

	/** Remote AWAY */
	bool Away(const std::string &prefix, parameterlist &params);

	/** Because Andy insists that services-compatible servers must
	 * implement SVSNICK and SVSJOIN, that's exactly what we do :p
	 */
	bool SVSNick(const std::string &prefix, parameterlist &params);

	/** SAVE to resolve nick collisions without killing */
	bool ForceNick(const std::string &prefix, parameterlist &params);

	/** ENCAP command
	 */
	void Encap(User* who, parameterlist &params);

	/** OPERQUIT command
	 */
	bool OperQuit(const std::string &prefix, parameterlist &params);

	/** SVSJOIN
	 */
	bool ServiceJoin(const std::string &prefix, parameterlist &params);

	/** SVSPART
	 */
	bool ServicePart(const std::string &prefix, parameterlist &params);

	/** KILL
	 */
	bool RemoteKill(const std::string &prefix, parameterlist &params);

	/** PONG
	 */
	bool LocalPong(const std::string &prefix, parameterlist &params);

	/** METADATA
	 */
	bool MetaData(const std::string &prefix, parameterlist &params);

	/** VERSION
	 */
	bool ServerVersion(const std::string &prefix, parameterlist &params);

	/** CHGHOST
	 */
	bool ChangeHost(const std::string &prefix, parameterlist &params);

	/** ADDLINE
	 */
	bool AddLine(const std::string &prefix, parameterlist &params);

	/** DELLINE
	 */
	bool DelLine(const std::string &prefix, parameterlist &params);

	/** CHGNAME
	 */
	bool ChangeName(const std::string &prefix, parameterlist &params);

	/** FIDENT */
	bool ChangeIdent(const std::string &prefix, parameterlist &params);

	/** WHOIS
	 */
	bool Whois(const std::string &prefix, parameterlist &params);

	/** PUSH
	 */
	bool Push(const std::string &prefix, parameterlist &params);

	/** TIME
	 */
	bool Time(const std::string &prefix, parameterlist &params);

	/** PING
	 */
	bool LocalPing(const std::string &prefix, parameterlist &params);

	/** Remove all modes from a channel, including statusmodes (+qaovh etc), simplemodes, parameter modes.
	 * This does not update the timestamp of the target channel, this must be done seperately.
	 */
	void RemoveStatus(User* source, parameterlist &params);

	/** <- (remote) <- SERVER
	 */
	bool RemoteServer(const std::string &prefix, parameterlist &params);

	/** (local) -> SERVER
	 */
	bool Outbound_Reply_Server(parameterlist &params);

	/** (local) <- SERVER
	 */
	bool Inbound_Server(parameterlist &params);

	/** Handle IRC line split
	 */
	void Split(const std::string &line, std::string& prefix, std::string& command, parameterlist &params);

	/** Process complete line from buffer
	 */
	void ProcessLine(std::string &line);

	void ProcessConnectedLine(std::string& prefix, std::string& command, parameterlist& params);

	/** Get this server's name
	 */
	virtual std::string GetName();

	/** Handle socket timeout from connect()
	 */
	virtual void OnTimeout();
	/** Handle server quit on close
	 */
	virtual void Close();
};

/* Used to validate the value lengths of multiple parameters for a command */
struct cmd_validation
{
	const char* item;
	size_t param;
	size_t length;
};

/* Used to validate the length values in CAPAB CAPABILITIES */
struct cap_validation
{
	const char* reason;
	const char* key;
	size_t size;
};

#endif

