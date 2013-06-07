
/*
 * dsragent.cc
 * Copyright (C) 2000 by the University of Southern California
 * $Id: dsragent.cc,v 1.38 2009/12/30 22:06:34 tom_henderson Exp $
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 *
 *
 * The copyright of this module includes the following
 * linking-with-specific-other-licenses addition:
 *
 * In addition, as a special exception, the copyright holders of
 * this module give you permission to combine (via static or
 * dynamic linking) this module with free software programs or
 * libraries that are released under the GNU LGPL and with code
 * included in the standard release of ns-2 under the Apache 2.0
 * license or under otherwise-compatible licenses with advertising
 * requirements (or modified versions of such code, with unchanged
 * license).  You may copy and distribute such a system following the
 * terms of the GNU GPL for this module and the licenses of the
 * other code concerned, provided that you include the source code of
 * that other code when and as the GNU GPL requires distribution of
 * source code.
 *
 * Note that people who make modified versions of this module
 * are not obligated to grant this special exception for their
 * modified versions; it is their choice whether to do so.  The GNU
 * General Public License gives permission to release a modified
 * version without this exception; this exception also makes it
 * possible to release a modified version which carries forward this
 * exception.
 *
 */
//
// Other copyrights might apply to parts of this software and are so
// noted when applicable.
//
/* 
   dsragent.cc

   requires a radio model such that sendPacket returns true
   iff the packet is recieved by the destination node.
   
   Ported from CMU/Monarch's code, appropriate copyright applies.  
*/

extern "C" {

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <signal.h>
#include <float.h>
#include <string.h>
}
#include <string.h>
#include <object.h>
#include <agent.h>
#include <trace.h>
#include <packet.h>
#include <scheduler.h>
#include <random.h>

#include <mac.h>
#include <ll.h>
#include <cmu-trace.h>

#include "path.h"
#include "srpacket.h"
#include "routecache.h"
#include "requesttable.h"
#include "dsragent.h"
//Arun
#include "random.h"
#include "rng.h"

/*==============================================================
  Declarations and global defintions
------------------------------------------------------------*/
// #define NEW_IFQ_LOGIC
// #define NEW_REQUEST_LOGIC
#define NEW_SALVAGE_LOGIC

#ifdef NEW_SALVAGE_LOGIC


/*//Arun .........................................................
int hdr_pkt::offset_;

static class B1HeaderClass : public PacketHeaderClass {
public:
	B1HeaderClass() : PacketHeaderClass("PacketHeader/B1",
					     sizeof(hdr_pkt)) {
		offset(&hdr_pkt::offset_);

//#ifdef DSR_CONST_HDR_SZ
//		fprintf(stderr,"WARNING: DSR treating all source route headers\n"
//			"as having length %d. this should be used only to estimate effect\n"
//			"of no longer needing a src rt in each packet\n",SR_HDR_SZ);
//#endif

	}


} class_pkt;
//Arun .........................................................
//*/


/*
 *  Maximum number of times that a packet may be salvaged.
 */
static int dsr_salvage_max_attempts = 15;
/*
 *  Maximum number of Route Requests that can be sent for a salvaged
 *  packets that was originated at another node.
 */
static int dsr_salvage_max_requests = 1;
/*
 *  May an intermediate node send a propagating Route Request for
 *  a salvaged packet that was originated elsewhere.
 */
static bool dsr_salvage_allow_propagating = 0;

#endif

/* couple of flowstate constants... */
static const bool dsragent_enable_flowstate = true;
static const bool dsragent_prefer_default_flow = true;
static const bool dsragent_prefer_shorter_over_default = true;
static const bool dsragent_always_reestablish = true;
static const int min_adv_interval = 5;
static const int default_flow_timeout = 60;
//#define DSRFLOW_VERBOSE

static const int verbose = 0;
static const int verbose_srr = 0;
static const int verbose_ssalv = 1;

DSRAgent_List DSRAgent::agthead = { 0 };

Time arp_timeout = 30.0e-3;	// (sec) arp request timeout
Time rt_rq_period = 0.5;	// (sec) length of one backoff period
Time rt_rq_max_period = 10.0;	// (sec) maximum time between rt reqs
Time send_timeout = SEND_TIMEOUT; // (sec) how long a packet can live in sendbuf

#if 0
/* used in route reply holdoffs, which are currently disabled -dam 5/98 */
Time rt_rep_holdoff_period = 3.0e-3; // secs (about 2*process_time)
// to determine how long to sit on our rt reply we pick a number
// U(O.0,rt_rep_holdoff_period) + (our route length-1)*rt_rep_holdoff
#endif //0

Time grat_hold_down_time = 1.0;	// (sec) min time between grat replies for
				// same route

Time max_err_hold = 1.0;        // (sec) 
// maximum time between when we recv a route error told to us, and when we
// transmit a propagating route request that can carry that data.  used to
// keep us from propagating stale route error data


/*************** selectors ******************/
bool dsragent_snoop_forwarded_errors = true;
// give errors we forward to our cache?
bool dsragent_snoop_source_routes = true;
// should we snoop on any source routes we see?
bool dsragent_reply_only_to_first_rtreq = false;
// should we only respond to the first route request we receive from a host?
bool dsragent_propagate_last_error = true;
// should we take the data from the last route error msg sent to us
// and propagate it around on the next propagating route request we do?
// this is aka grat route error propagation
bool dsragent_send_grat_replies = true;
// should we send gratuitous replies to effect route shortening?
bool dsragent_salvage_with_cache = true;
// should we consult our cache for a route if we get a xmitfailure
// and salvage the packet using the route if possible
bool dsragent_use_tap = true;
// should we listen to a promiscuous tap?
bool dsragent_reply_from_cache_on_propagating = true;
// should we consult the route cache before propagating rt req's and
// answer if possible?
bool dsragent_ring_zero_search = true;
// should we send a non-propagating route request as the first action
// in each route discovery action?

// NOTE: to completely turn off replying from cache, you should
// set both dsragent_ring_zero_search and 
// dsragent_reply_from_cache_on_propagating to false

bool dsragent_dont_salvage_bad_replies = true;
// if we have an xmit failure on a packet, and the packet contains a 
// route reply, should we scan the reply to see if contains the dead link?
// if it does, we won't salvage the packet unless there's something aside
// from a reply in it (in which case we salvage, but cut out the rt reply)
bool dsragent_require_bi_routes = true;
// do we need to have bidirectional source routes? 
// [XXX this flag doesn't control all the behaviors and code that assume
// bidirectional links -dam 5/14/98]

#if 0
bool lsnode_holdoff_rt_reply = true;
// if we have a cached route to reply to route_request with, should we
// hold off and not send it for a while?
bool lsnode_require_use = true;
// do we require ourselves to hear a route requestor use a route
// before we withold our route, or is merely hearing another (better)
// route reply enough?
#endif

/*


Our strategy is as follows:

 - it's only worth discovering bidirectional routes, since all data
 paths will have to have to be bidirectional for 802.11 ACKs to work

 - reply to all route requests for us that we recv (not just the first one)
 but reply to them by reversing the route and unicasting.  don't do
 a route request (since that will end up returning the requestor lots of
 routes that are potentially unidirectional). By reversing the discovered 
 route for the route reply, only routes that are bidirectional will make it
 back the original requestor

 - once a packet goes into the sendbuffer, it can't be piggybacked on a 
 route request.  the code assumes that the only thing that removes
 packets from the send buff is the StickPktIn routine, or the route reply
 arrives routine

*/

/* Callback helpers */
void
XmitFailureCallback(Packet *pkt, void *data)
{
  DSRAgent *agent = (DSRAgent *)data; // cast of trust
  agent->xmitFailed(pkt);
}

void
XmitFlowFailureCallback(Packet *pkt, void *data)
{
  DSRAgent *agent = (DSRAgent *)data;
  agent->xmitFlowFailed(pkt);
}


/*===========================================================================
  SendBuf management and helpers
---------------------------------------------------------------------------*/
void
SendBufferTimer::expire(Event *) 
{ 
	 a_->sendBufferCheck(); 
  	resched(BUFFER_CHECK + BUFFER_CHECK * Random::uniform(1.0));
	
}

/*============================================================================Arun //Mycode !!!
*/
// call coming form start_CDS_timer()
void
CDStimers::expire(Event *) 
{ 
  	nsaddr_t  netid ;
	
	netid = a_->get_net_id();
	//printf("We are at Expire Event ++++++++++++++++++++++++++++++++++++++++++++++ %.5f\n " , Scheduler::instance().clock());
	//a_->trace("We are at Expire Event ++++++++++++++++++++++++++++++++++++++++++++++ %.5f\n " , Scheduler::instance().clock());
	if(God::instance()->get_neighbor_size(netid)> 0)
	{
		God::instance()->get_Single_nodes_Neighbors(netid);
		printf("Due to time out Node is going to create a Black Node %d\n" , netid);
		a_->trace("Due to time out Node is going to create a Black Node %d at time %.5f\n " , netid ,Scheduler::instance().clock() );
		a_->send_B1_pkt(netid) ;
		//a_->send_Beacon_pkt();
		//Random::integer(y_);
	}
}

// call is coming from start_beacon_timer
void
Beacontimers::expire(Event *) 
{ 
  	printf("Timeout is happening \n");
	//nsaddr_t  netid ;
	//netid = a_->get_net_id();
	a_->send_Beacon_pkt();
}


//call is coming form start_delay_timer
void
Delay::expire(Event *) 
{ 
  	printf("Delay Ended \n");
	a_->send_Beacon_pkt();
}


//call is coming from start_child_reply_timer
void
Child_node_reply::expire(Event *) 
{ 
	nsaddr_t  netid ;
	
	netid = a_->get_net_id();
  	printf("Child Reply time is off Ended \n");
	//a_->send_Beacon_pkt();
	if(God::instance()->get_prune_val(netid)  != 1)
	{
		printf("Node %d is going to make itself as a Grey node \n\n" , netid);
		a_->Change_Status(netid , 1);
		
	}
	else
	{
		printf("Node %d have some nodes to cover \n" , netid);	
	}
}


//=================================================================================


int
DSRAgent::get_net_id()
{
	return net_id.getNSAddr_t();
}

void
DSRAgent::neighbors(int i) 
{ 
  int nodes=God::instance()->nodes(); 
  //printf("finding the neighbors of that node really i am here  %d" , i);
  //cout<<"neighbors of node "<<i<<":"<<endl; 
	//int nodes = j; 
	//printf("\n no of nodes %d" , nodes);
	printf("\nneighbors of node %d \n" , i);
	int value = 0;
  for(int k=0;k<nodes;k++) 
  { 
   if(k!=i) 
   { 
	//printf("i am inside this ");
	//cout<<k<<":"<<God::instance()->getdistance(i,k)<<" yes"<<endl; 
	//cout<<k<<":"<<God::instance()->getdistance(i,k)<<" no"<<endl; 
    if(God::instance()->IsNeighbor(i,k))
	{ 
        value =	God::instance()->getdistance(i,k);
     printf("%d : %d  \tYes\n" , k ,value );
	}
    else 
	printf("%d : %d  \tNo\n" , k ,God::instance()->getdistance(i,k));
	//int a = 0; 
      
   } 
  } 
  printf("\n");
} 


void 
DSRAgent::start_CDS_timer(int node_id)
{
	//nsaddr_t  netid ;
	
	//netid = a_-get_net_id();
	int size = God::instance()->get_neighbor_size(node_id);
	int calculated_timeout = ((1/size) + (Random::integer(10)/ 100));
	//RNG *rng;
	//float timeout = .60 + (rng->uniform(10000 , 99999) / 10000000)  + (node_id * 2 * 0.0001) ;
	//timeout.sched(.60 + (Random::integer(99999) / 10000000)  + (node_id * 2 * 0.0001) + ((God::instance()->get_neighbor_size(node_id) * 3.1 * 0.001))));
	//timeout.sched(.60 + (Random::integer(99999) / 10000000) +  ((100 *.01)/(God::instance()->get_neighbor_size(node_id))) + (node_id * 6 * 0.01));
	timeout.sched(.60+calculated_timeout );
}

void 
DSRAgent::start_beacon_timer(int node_id)
{
	int changes = 0 ;
	if(get_node_status(node_id) == 2 && changes == 0)
	{
		printf("Arun %d\n" , node_id);
		int timer_val = ((Random::integer(123)*(node_id*node_id*12)+3) % 10 );
		printf("timer_val :: %d\n" , timer_val);
		beacontimeout.sched(1.0 + (timer_val / 10) +(timer_val / 10) );
		changes = 1;
	}
}

void 
DSRAgent::start_delay_timer()
{
		delaytimeout.resched(1.0 + (Random::integer(10) / 10));
}

void 
DSRAgent::start_child_reply_timer()
{
		nsaddr_t  netid ;
	
		netid = get_net_id();		
		printf("Printf Node id Who starts the timer :%d\n" , netid);
		Child_node_reply_timeout.resched(2.0);
}


/*void 
DSRAgent::start_CDS_timer(int node_id)
{
	
	
	timeout.sched((0.40 + 0.20 +Random::integer(1600) *(Random::integer(1600)  /100000000)));
//	RNG *rng;
//	static int counter = 2 ;
//	printf("Statrfdsfd");
//	timeout.sched(0.40 + 0.20 + (counter*0.01) + (rng->uniform(10000 , 99999) / 10000000) +(node_id * 2 * 0.0001)+ ((God::instance()->get_neighbor_size(node_id) * 3.1 * 0.001)));
//	counter = counter + 2 ;
	//RNG::uniform(1200,1600)
}*/




//================================================================================
void
DSRAgent::dropSendBuff(SRPacket &p)
  // log p as being dropped by the sendbuffer in DSR agent
{
  trace("Ssb %.5f _%s_ dropped %s -> %s", Scheduler::instance().clock(), 
	net_id.dump(), p.src.dump(), p.dest.dump());
  drop(p.pkt, DROP_RTR_QTIMEOUT);
  p.pkt = 0;
  p.route.reset();
}

void
DSRAgent::stickPacketInSendBuffer(SRPacket& p)
{
  Time min = DBL_MAX;
  int min_index = 0;
  int c;

  if (verbose)
    trace("Sdebug %.5f _%s_ stuck into send buff %s -> %s",
	  Scheduler::instance().clock(), 
	  net_id.dump(), p.src.dump(), p.dest.dump());

  for (c = 0 ; c < SEND_BUF_SIZE ; c ++)
    if (send_buf[c].p.pkt == NULL)
      {
	send_buf[c].t = Scheduler::instance().clock();
	send_buf[c].p = p;
	return;
      }
    else if (send_buf[c].t < min)
      {
	min = send_buf[c].t;
	min_index = c;
      }
  
  // kill somebody
  dropSendBuff(send_buf[min_index].p);
  send_buf[min_index].t = Scheduler::instance().clock();
  send_buf[min_index].p = p;
}
//arun




//================================================================================================== coding Done by me 201111
//Arun ................................................calling function.........send
//Arun .........................................................CDS calledd
void
DSRAgent::send_B1_pkt(nsaddr_t starter_node )
{
	//DSRAgent::Pkt_counter = Pkt_counter+ 1;
	//++Pkt_counter; //To Count the Total number of Packets communicated through out the Simulation 
//	printf("I got a call from send_B1_pkt and starter node is ::::%d \n" , starterr_node);
	// nsaddr_t starter_node = starterr_node;
	//hdr_sr *hdr =  hdr_sr::access(pkt);
	//printf("Arun Below\n");      
      // Access the Ping header for the new packet:
    //hdr_pkt* hdr = hdr_pkt::access(pkt);
	//srh->route_error();
     // hdr->type_of_packet = 10;
	nsaddr_t  netid ;// rcv_node;
	//netid = net_id.getNSAddr_t();
	netid = starter_node;
	int node_id = netid;
	// node_id and netid are the same thing but netid and net_id are not the same thing 
	//rcv_node = 3;
	//printf("net_id :%d\n" , net_id);
	//Packet* pkt = allocpkt();
	SRPacket p;
  	//p.src=net_id; //p.src always takes everything in a "ID" special for the SRPackets
	//p.src =  ID((Address::instance().get_nodeaddr(iph->daddr())),::IP);
	//p.src = Address::instance().create_ipaddr(starter_node,RT_PORT);
	//printf("p.src : %d" , p.src);
	p.pkt=allocpkt();
	//God::find_neighbor();
	//Packet* pkt = allocpkt();
	//hdr_sr *hdr =  hdr_sr::access(pkt);

	hdr_sr *srh =  hdr_sr::access(p.pkt);
	hdr_ip *iph =  hdr_ip::access(p.pkt);
	hdr_cmn *cmnh =  hdr_cmn::access(p.pkt);
	
	 cmnh->ptype() == PT_DSR;
	ID mac_id;
	srh->type_of_packet = 0;
	printf("Node %d is Going to add a new Node \n", netid);
	int Child_node = God::instance()->god_select_a_node(netid);
	
	srh->giving_authority_to_node = Child_node;
	God::instance()->set_child_id(netid ,Child_node);
	God::instance()->increament_counter(node_id);
//	printf("type of packet at the time of call::::%d\n\n" ,srh->type_of_packet);
//	printf("Starting Node is : %d:::\n\n\n" ,starter_node  );
	//hdr_ip* iph = hdr_ip::access(pkt);
//	printf("Sequence Number for node %d:::::: %d\n" ,(u_int32_t)hdrip->saddr(), seq);
        srh->parent_id = God::instance()->get_parent_id(node_id);
	//strcpy(srh->msg,"Hi_message_sent");
	// hdr_ip* iph = HDR_IP(pkt);
	//hdr->giving_authority_to_node
//	iph->saddr() = 0;
	//send_B1_pkt(nsaddr_t starter_node )
	//iph->saddr()=Address::instance().create_ipaddr(net_id.getNSAddr_t(),RT_PORT);
	// yes yes 
	// other node 
	iph->saddr()=Address::instance().create_ipaddr(starter_node,RT_PORT);
	p.src =  ID((Address::instance().get_nodeaddr(iph->saddr())),::IP);
	//p.src= iph->saddr();
	//iph->saddr() = starter_node;
	//iph->daddr()=Address::instance().create_ipaddr(rcv_node,RT_PORT);
	iph->daddr() = IP_BROADCAST;
	p.dest = ID((Address::instance().get_nodeaddr(iph->daddr())),::IP);
	//printf("p.dest :::: %d\n" , p.dest);
	//p.dest = Address::instance().create_ipaddr(iph->daddr(),RT_PORT);
	//iph->daddr() = 1;
      	iph->dport() = 255;
	iph->sport() = 255;
//Port Number is too important to understand ... they are defining the  
	iph->ttl()=1;
	 cmnh->next_hop() = IP_BROADCAST; 
	strcpy(srh->msg,"B1_message");	
	cmnh->direction() = hdr_cmn::DOWN; 
	cmnh->size() = size_ + IP_HDR_LEN;

         srh->init();
	Change_Status(netid, 2); //Creating a Black Node 
	trace("%d is created as a Black Node and Parent Node is %d\n\n\n" , node_id ,God::instance()->get_parent_id(node_id) );
	//send_B3_pkt(1);
	printf("Black Node Have Been Created ::%d\n" ,netid );
	
	
	//	printf("B1 Packet Send and starter node is :: %d\n" , iph->saddr());
     // send(pkt, 0);  
//srh->valid() = 0;
//	printf("srh->valid()_sendin_time :: %d\n" , srh->valid() );
	 //Scheduler::instance().schedule(ll, p.pkt, 0.23); 
	//printf("Random::uniform() * net_id.getNSAddr_t() :::: %d\n" , Random::uniform() * net_id.getNSAddr_t());
	//Scheduler::instance().schedule(ll, p.pkt,0.20 );
	if(Child_node == 1000)
	{
		
		printf("Special Case ");
		printf("Node %d is Going to make itself as a Grey Node nothing to cover \n" , netid);
		Change_Status(netid, 1); // Changing Back to Grey Node 
		send_B3_pkt(God::instance()->get_parent_id(node_id));		
	}
	else 
	{
		Scheduler::instance().schedule(ll, p.pkt, 0.20 );
		God::instance()->set_Pkt_counter(node_id);
	}
	
		// handlePktWithoutSR(p, false);
	//printf("\n Packet B1 Broadcasted\n");
	p.pkt=NULL;
      // return TCL_OK, so the calling function knows that
      // the command has been processed

}

//Arun B2 Packet
void
DSRAgent::send_B2_pkt(nsaddr_t receiver_node )
{
//        DSRAgent::Pkt_counter = Pkt_counter+ 1;
	//++Pkt_counter;
	nsaddr_t  netid ;
	netid = net_id.getNSAddr_t();
	SRPacket p;
  	p.src=net_id;
	p.pkt=allocpkt();
	hdr_sr *srh =  hdr_sr::access(p.pkt);
	hdr_ip *iph =  hdr_ip::access(p.pkt);
	hdr_cmn *cmnh =  hdr_cmn::access(p.pkt);
	cmnh->ptype() == PT_DSR;
	//ID mac_id;
	srh->type_of_packet = 1; //This shows this is going to send a B2 Packet 
	srh->rcv_node = receiver_node;
	//srh->giving_authority_to_node = God::instance()->god_select_a_node(netid);	
	srh->parent_id = God::instance()->get_parent_id(netid);
	iph->saddr()=	Address::instance().create_ipaddr(netid,RT_PORT);
	iph->daddr() = IP_BROADCAST;
	p.dest = 	ID((Address::instance().get_nodeaddr(iph->daddr())),::IP);
	iph->dport() = 255;
	iph->sport() = 255;
	iph->ttl()=1;
	cmnh->next_hop() = IP_BROADCAST; 
	strcpy(srh->msg,"B2_message");	
	cmnh->direction() = hdr_cmn::DOWN; 
	cmnh->size() = size_ + IP_HDR_LEN;

         srh->init();
	//Change_Status(netid, 2); //Creating a Black Node 
	//printf("Black Node Have Been Created ::%d\n" ,netid );
	Scheduler::instance().schedule(ll, p.pkt, 0.20 );
	God::instance()->set_Pkt_counter(netid);
	printf("Going to send a B2 Packet by node %d to node %d\n" , netid ,receiver_node );
	trace("Going to send a B2 Packet by node %d to node %d\n" , netid ,receiver_node );
	p.pkt=NULL;
	
	
	
	
	
	 
	
}

void 
DSRAgent::send_B3_pkt(nsaddr_t receiver_node )
{
	// one more thing we are not going to use this variable receiver_node not needed at all .
	//B3 Message is always intended to it's Parent Node Only 	
	//printf("Hello");
	// DSRAgent::Pkt_counter = Pkt_counter+ 1;
	//++Pkt_counter;
	nsaddr_t  netid ;// rcv_node;
	netid = net_id.getNSAddr_t();
	int node_id = netid;
	SRPacket p;
  	p.src=net_id; //p.src always takes everything in a "ID" special for the SRPackets
	p.pkt=allocpkt();
	hdr_sr *srh =  hdr_sr::access(p.pkt);
	hdr_ip *iph =  hdr_ip::access(p.pkt);
	hdr_cmn *cmnh =  hdr_cmn::access(p.pkt);
	
	cmnh->ptype() == PT_DSR;
	srh->type_of_packet = 2;
        srh->parent_id = God::instance()->get_parent_id(netid);
	srh->rcv_node = receiver_node;	
	iph->saddr()=Address::instance().create_ipaddr(netid,RT_PORT);
	iph->daddr() = IP_BROADCAST;
	p.dest = ID((Address::instance().get_nodeaddr(iph->daddr())),::IP);
      	iph->dport() = 255;
	iph->sport() = 255;
	iph->ttl()=1;
	cmnh->next_hop() = IP_BROADCAST; 
	strcpy(srh->msg,"B3_message");	
	cmnh->direction() = hdr_cmn::DOWN; 
	cmnh->size() = size_ + IP_HDR_LEN;
	Change_Status(netid, 1); 
         srh->init();
	Scheduler::instance().schedule(ll, p.pkt,0.20 );
	God::instance()->set_Pkt_counter(node_id);
	p.pkt=NULL;
	printf("Node %d is Going to make it self as a Grey Node and Parent Node is %d\n" , node_id ,srh->parent_id  );
	trace("Node %d is Going to make it self as a Grey Node and Parent Node is %d\n\n" , node_id ,srh->parent_id  );
        // return TCL_OK, so the calling function knows that
        // the command has been processed
	//printf("111111111111111111111111111111111111111111111111111111111111111");
}



void 
DSRAgent::send_Pick_up_pkt(nsaddr_t node_id)
{
	nsaddr_t  netid ;
	netid = net_id.getNSAddr_t();
	SRPacket p;
  	p.src=net_id;
	p.pkt=allocpkt();
	hdr_sr *srh =  hdr_sr::access(p.pkt);
	hdr_ip *iph =  hdr_ip::access(p.pkt);
	hdr_cmn *cmnh =  hdr_cmn::access(p.pkt);
	cmnh->ptype() == PT_DSR;
	//node_id = netid;
	//ID mac_id;
	srh->type_of_packet = 10; //This is showing that this is Sending a Pick_up_node_packet	
	srh->Pick_up_node_size = God::instance()->get_P_y_(node_id);
	srh->Pick_up_node = God::instance()->get_P_node(node_id) ;
	srh->change = God::instance()->get_P_change( node_id);

	int node___ = God::instance()->get_P_node(node_id);
	//printf("srh->Pick_up_node_size %d \n srh->Pick_up_node %d \n \n" ,srh->Pick_up_node_size ,  node___);
//	printf("Sending Node :: %d\n" , node_id);
	iph->saddr()=	Address::instance().create_ipaddr(node_id,RT_PORT);
	iph->daddr() = IP_BROADCAST;
	
	p.dest = ID((Address::instance().get_nodeaddr(iph->daddr())),::IP);
	iph->dport() = 255;
	iph->sport() = 255;
	iph->ttl()=1;
	cmnh->next_hop() = IP_BROADCAST; 
	strcpy(srh->msg,"send_Pick_up_pkt");	
	cmnh->direction() = hdr_cmn::DOWN; 
	cmnh->size() = size_ + IP_HDR_LEN;

         srh->init();
	//Change_Status(netid, 2); //Creating a Black Node 
	//printf("Black Node Have Been Created ::%d\n" ,netid );
	Scheduler::instance().schedule(ll, p.pkt, 0.0 );
	//printf("Going to send a B2 Packet by node %d to node %d\n" , netid ,receiver_node );
	p.pkt=NULL;
}



void 
DSRAgent::send_node_selection_pkt(nsaddr_t starter_node)
{
	nsaddr_t  netid ;
	netid = net_id.getNSAddr_t();
	SRPacket p;
	int node_id = netid;
	node_id = netid;
	God::instance()->set_Pkt_counter(node_id);
  	p.src=net_id;
	p.pkt=allocpkt();
	hdr_sr *srh =  hdr_sr::access(p.pkt);
	hdr_ip *iph =  hdr_ip::access(p.pkt);
	hdr_cmn *cmnh =  hdr_cmn::access(p.pkt);
	cmnh->ptype() == PT_DSR;
	//node_id = netid;
	//ID mac_id;
	srh->type_of_packet = 12; //This is showing that this is Sending a Pick_up_node_packet	
	//int node_siw  = God::instance()->get_node_size(node_id);
	srh->node_size = God::instance()->get_node_size(node_id);
//	srh->newvar =  ;
	//srh->Pick_up_node_size = 10;
	srh->node_id = God::instance()->get_node_id(node_id);
	//srh->change = God::instance()->get_P_change( node_id);

	//int node___ = God::instance()->get_P_node(node_id);
	//printf("srh->Pick_up_node_size %d \n srh->Pick_up_node %d \n \n" ,srh->Pick_up_node_size ,  node___);
//	printf("Sending Node :: %d\n" , node_id);
	iph->saddr()=	Address::instance().create_ipaddr(node_id,RT_PORT);
	iph->daddr() = IP_BROADCAST;
	
	p.dest = ID((Address::instance().get_nodeaddr(iph->daddr())),::IP);
	iph->dport() = 255;
	iph->sport() = 255;
	iph->ttl()=1;
	cmnh->next_hop() = IP_BROADCAST; 
	strcpy(srh->msg,"send_node_selection_packet");	
	cmnh->direction() = hdr_cmn::DOWN; 
	cmnh->size() = size_ + IP_HDR_LEN;
	printf("\n\n******************************************************************\n\n");
	printf("Sending Pick_up_packet by node %d\n" , node_id);
	printf("Type of Packet ::%d\n", srh->type_of_packet);
		//printf("Type of Packet ::%d\n", srh->newvar);
	printf("Pick up node_size of sending node :: %d\n" ,srh->node_size);
	printf("Pickup node_id is ::%d\n" ,srh->node_id);
	printf("\n\n******************************************************************\n\n");
         srh->init();
	//Change_Status(netid, 2); //Creating a Black Node 
	//printf("Black Node Have Been Created ::%d\n" ,netid );
	Scheduler::instance().schedule(ll, p.pkt, 0.0 );
	//printf("Going to send a B2 Packet by node %d to node %d\n" , netid ,receiver_node );
	p.pkt=NULL;
}


void
DSRAgent::send_Beacon_pkt()
{
	nsaddr_t  netid ;
	netid = net_id.getNSAddr_t();
	SRPacket p;
	int node_id = netid;
	p.src=net_id;
	p.pkt=allocpkt();
	hdr_sr *srh =  hdr_sr::access(p.pkt);
	hdr_ip *iph =  hdr_ip::access(p.pkt);
	hdr_cmn *cmnh =  hdr_cmn::access(p.pkt);
	cmnh->ptype() == PT_DSR;
	//Putting Values in PAcket 
	srh->type_of_packet = 13; //This is showing that this is Sending a beacon_packet
	srh->parent_id = God::instance()->get_parent_id(netid);	
	//srh->child_list = God::instance()->get_neighbor_black_node_list(netid);
	God::instance()->get_neighbor_black_node_list(netid);		
	iph->saddr()=	Address::instance().create_ipaddr(node_id,RT_PORT);
	iph->daddr() = IP_BROADCAST;
	p.dest = ID((Address::instance().get_nodeaddr(iph->daddr())),::IP);
	iph->dport() = 255;
	iph->sport() = 255;
	iph->ttl()=1;
	cmnh->next_hop() = IP_BROADCAST; 
	strcpy(srh->msg,"Sending_Beacon_pkt");	
	cmnh->direction() = hdr_cmn::DOWN; 
	cmnh->size() = size_ + IP_HDR_LEN;
	srh->init();
	Scheduler::instance().schedule(ll, p.pkt, 0.0 );
	p.pkt=NULL;
	start_delay_timer();
	printf("Sending Node id ::%d at time %.5f \n\n" , p.src , Scheduler::instance().clock());
}

void
DSRAgent::send_offer_pkt(int offered_node_id)
{
	nsaddr_t  netid ;
	netid = net_id.getNSAddr_t();
	SRPacket p;
	int node_id = netid;
	p.src=net_id;
	p.pkt=allocpkt();
	hdr_sr *srh =  hdr_sr::access(p.pkt);
	hdr_ip *iph =  hdr_ip::access(p.pkt);
	hdr_cmn *cmnh =  hdr_cmn::access(p.pkt);
	cmnh->ptype() == PT_DSR;
	//Putting Values in PAcket 
	srh->type_of_packet = 14; //This is showing that this is Sending a offer_packet
	srh->parent_id = God::instance()->get_parent_id(netid);	
	srh->giving_authority_to_node = offered_node_id ;
	//srh->child_list = God::instance()->get_neighbor_black_node_list(netid);
	//God::instance()->get_neighbor_black_node_list(netid);		
	iph->saddr()=	Address::instance().create_ipaddr(node_id,RT_PORT);
	iph->daddr() = IP_BROADCAST;
	p.dest = ID((Address::instance().get_nodeaddr(iph->daddr())),::IP);
	iph->dport() = 255;
	iph->sport() = 255;
	iph->ttl()=1;
	cmnh->next_hop() = IP_BROADCAST; 
	strcpy(srh->msg,"Sending_offer_pkt");	
	cmnh->direction() = hdr_cmn::DOWN; 
	cmnh->size() = size_ + IP_HDR_LEN;
	srh->init();
	Scheduler::instance().schedule(ll, p.pkt, 0.0 );
	p.pkt=NULL;
	//Change_Status(offered_node_id, 2); //Creating a Black Node 
	//start_delay_timer();
	//printf("Sending Node id ::%d at time %.5f \n\n" , p.src , Scheduler::instance().clock());
}
void
DSRAgent::send_Black_pkt()
{
	// here node_id is the id of that node who have offered to become back ndoe ... at last removed 
	nsaddr_t  netid ;
	netid = net_id.getNSAddr_t();
	SRPacket p;
	int node_id = netid;
	p.src=net_id;
	p.pkt=allocpkt();
	hdr_sr *srh =  hdr_sr::access(p.pkt);
	hdr_ip *iph =  hdr_ip::access(p.pkt);
	hdr_cmn *cmnh =  hdr_cmn::access(p.pkt);
	cmnh->ptype() == PT_DSR;
	//Putting Values in PAcket 
	srh->type_of_packet = 15; //This is showing that this is Sending a Black_pkt
	
	srh->parent_id = God::instance()->get_parent_id(node_id);	
	//srh->child_list = God::instance()->get_neighbor_black_node_list(netid);
	//God::instance()->get_neighbor_black_node_list(netid);		
	iph->saddr()=	Address::instance().create_ipaddr(node_id,RT_PORT);
	iph->daddr() = IP_BROADCAST;
	p.dest = ID((Address::instance().get_nodeaddr(iph->daddr())),::IP);
	iph->dport() = 255;
	iph->sport() = 255;
	iph->ttl()=1;
	cmnh->next_hop() = IP_BROADCAST; 
	strcpy(srh->msg,"Sending_Beacon_pkt");	
	cmnh->direction() = hdr_cmn::DOWN; 
	cmnh->size() = size_ + IP_HDR_LEN;
	srh->init();
	Scheduler::instance().schedule(ll, p.pkt, 0.0 );
	p.pkt=NULL;
	start_delay_timer();
	printf("Sending Node id ::%d at time %.5f \n\n" , p.src , Scheduler::instance().clock());
}
void
DSRAgent::send_reset_packet()
{
	nsaddr_t  netid ;
	netid = net_id.getNSAddr_t();
	SRPacket p;
	int node_id = netid;
	p.src=net_id;
	p.pkt=allocpkt();
	hdr_sr *srh =  hdr_sr::access(p.pkt);
	hdr_ip *iph =  hdr_ip::access(p.pkt);
	hdr_cmn *cmnh =  hdr_cmn::access(p.pkt);
	cmnh->ptype() == PT_DSR;
	//Putting Values in PAcket 
	srh->type_of_packet = 16; //This is showing that this is Sending a Reset_Packet
	God::instance()->god_set_parent_id(node_id,node_id );
	srh->parent_id = God::instance()->get_parent_id(node_id);	
	//srh->child_list = God::instance()->get_neighbor_black_node_list(netid);
	//God::instance()->get_neighbor_black_node_list(netid);		
	iph->saddr()=	Address::instance().create_ipaddr(node_id,RT_PORT);
	iph->daddr() = IP_BROADCAST;
	p.dest = ID((Address::instance().get_nodeaddr(iph->daddr())),::IP);
	iph->dport() = 255;
	iph->sport() = 255;
	iph->ttl()=1;
	cmnh->next_hop() = IP_BROADCAST; 
	strcpy(srh->msg,"Sending_reset_pkt");	
	cmnh->direction() = hdr_cmn::DOWN; 
	cmnh->size() = size_ + IP_HDR_LEN;
	srh->init();
	Scheduler::instance().schedule(ll, p.pkt, 0.0 );
	p.pkt=NULL;
	Change_Status(netid, 0); //Creating a Black Node 
	//start_delay_timer();
	//printf("Sending Node id ::%d at time %.5f \n\n" , p.src , Scheduler::instance().clock());
}

void
DSRAgent::send_route_find_pkt(int responsible_node_for_fixing ,int problem_node )
{
	nsaddr_t  netid ;
	netid = net_id.getNSAddr_t();
	SRPacket p;
	int node_id = netid;
	p.src=net_id;
	p.pkt=allocpkt();
	hdr_sr *srh =  hdr_sr::access(p.pkt);
	hdr_ip *iph =  hdr_ip::access(p.pkt);
	hdr_cmn *cmnh =  hdr_cmn::access(p.pkt);
	cmnh->ptype() == PT_DSR;
	//Putting Values in PAcket 
	srh->type_of_packet = 17; //This is showing that this is Sending a Route_find_pkt
	//God::instance()->god_set_parent_id(node_id,node_id );
	//nsaddr_t problem_node;	
	//nsaddr_t reponsible_node;
	srh->cost = God::instance()->get_cost(netid);	
	//srh->parent_id = God::instance()->get_parent_id(node_id);	
	srh->pkt_id = God::instance()->get_pkt_id(netid);	
	srh->problem_node = problem_node ;
	srh->reponsible_node= responsible_node_for_fixing;
	//srh->child_list = God::instance()->get_neighbor_black_node_list(netid);
	//God::instance()->get_neighbor_black_node_list(netid);		
	iph->saddr()=	Address::instance().create_ipaddr(node_id,RT_PORT);
	iph->daddr() = IP_BROADCAST;
	p.dest = ID((Address::instance().get_nodeaddr(iph->daddr())),::IP);
	iph->dport() = 255;
	iph->sport() = 255;
	iph->ttl()=1;
	cmnh->next_hop() = IP_BROADCAST; 
	strcpy(srh->msg,"Sending_route_find_pkt");	
	cmnh->direction() = hdr_cmn::DOWN; 
	cmnh->size() = size_ + IP_HDR_LEN;
	srh->init();
	Scheduler::instance().schedule(ll, p.pkt, 0.0 );
	p.pkt=NULL;
	//Change_Status(netid, 0); //Creating a Black Node 
	//start_delay_timer();
	//printf("Sending Node id ::%d at time %.5f \n\n" , p.src , Scheduler::instance().clock());
}

void
DSRAgent::send_route_rep_pkt(int responsible_node_for_fixing ,int problem_node  )
{
	nsaddr_t  netid ;
	netid = net_id.getNSAddr_t();
	SRPacket p;
	int node_id = netid;
	p.src=net_id;
	p.pkt=allocpkt();
	hdr_sr *srh =  hdr_sr::access(p.pkt);
	hdr_ip *iph =  hdr_ip::access(p.pkt);
	hdr_cmn *cmnh =  hdr_cmn::access(p.pkt);
	cmnh->ptype() == PT_DSR;
	//Putting Values in PAcket 
	srh->type_of_packet = 18; //This is showing that this is Sending a Route_rep_pkt
	//God::instance()->god_set_parent_id(node_id,node_id );
	//nsaddr_t problem_node;	
	//nsaddr_t reponsible_node;
	srh->cost = God::instance()->get_cost(netid);	
	//srh->parent_id = God::instance()->get_parent_id(node_id);	
	srh->pkt_id = God::instance()->get_pkt_id(netid);	
	srh->problem_node = problem_node ;
	srh->reponsible_node= responsible_node_for_fixing;
	//srh->child_list = God::instance()->get_neighbor_black_node_list(netid);
	//God::instance()->get_neighbor_black_node_list(netid);		
	iph->saddr()=	Address::instance().create_ipaddr(node_id,RT_PORT);
	iph->daddr() = IP_BROADCAST;
	p.dest = ID((Address::instance().get_nodeaddr(iph->daddr())),::IP);
	iph->dport() = 255;
	iph->sport() = 255;
	iph->ttl()=1;
	cmnh->next_hop() = IP_BROADCAST; 
	strcpy(srh->msg,"Sending_route_find_pkt");	
	cmnh->direction() = hdr_cmn::DOWN; 
	cmnh->size() = size_ + IP_HDR_LEN;
	srh->init();
	Scheduler::instance().schedule(ll, p.pkt, 0.0 );
	p.pkt=NULL;
	//Change_Status(netid, 0); //Creating a Black Node 
	//start_delay_timer();
	//printf("Sending Node id ::%d at time %.5f \n\n" , p.src , Scheduler::instance().clock());
}


void 
DSRAgent::send_child_discover_pkt(int node_id)
{


	//nsaddr_t  netid ;// rcv_node;
	SRPacket p;
	p.pkt=allocpkt();

	hdr_sr *srh =  hdr_sr::access(p.pkt);
	hdr_ip *iph =  hdr_ip::access(p.pkt);
	hdr_cmn *cmnh =  hdr_cmn::access(p.pkt);
	
	 cmnh->ptype() == PT_DSR;
	//ID mac_id;
	srh->type_of_packet = 19; // this is child_discover_packet
        srh->parent_id = God::instance()->get_parent_id(node_id);
	iph->saddr()=Address::instance().create_ipaddr(node_id,RT_PORT);
	p.src =  ID((Address::instance().get_nodeaddr(iph->saddr())),::IP);
	//p.src= iph->saddr();
	//iph->saddr() = starter_node;
	//iph->daddr()=Address::instance().create_ipaddr(rcv_node,RT_PORT);
	iph->daddr() = IP_BROADCAST;
	p.dest = ID((Address::instance().get_nodeaddr(iph->daddr())),::IP);
	//printf("p.dest :::: %d\n" , p.dest);
	//p.dest = Address::instance().create_ipaddr(iph->daddr(),RT_PORT);
	//iph->daddr() = 1;
      	iph->dport() = 255;
	iph->sport() = 255;
//Port Number is too important to understand ... they are defining the  
	iph->ttl()=1;
	 cmnh->next_hop() = IP_BROADCAST; 
	strcpy(srh->msg,"Node_sending_child_pkt");	
	cmnh->direction() = hdr_cmn::DOWN; 
	cmnh->size() = size_ + IP_HDR_LEN;
	printf("send_child_discover_pkt set by ndoe %d\n " ,p.src);
	//start from here :) 
         srh->init();
	Scheduler::instance().schedule(ll, p.pkt, 0.0 );
	p.pkt=NULL;
		//start_child_reply_timer();
	
	

}

void 
DSRAgent::send_NAC(int rcv_n)
{
	nsaddr_t  netid ;
	netid = net_id.getNSAddr_t();
	SRPacket p;
	int node_id = netid;
	p.src=net_id;
	p.pkt=allocpkt();
	hdr_sr *srh =  hdr_sr::access(p.pkt);
	hdr_ip *iph =  hdr_ip::access(p.pkt);
	hdr_cmn *cmnh =  hdr_cmn::access(p.pkt);
	cmnh->ptype() == PT_DSR;
	//Putting Values in PAcket 
	srh->type_of_packet = 20; //This is showing that this is Sending a Route_rep_pkt
	//God::instance()->god_set_parent_id(node_id,node_id );
	//nsaddr_t problem_node;	
	//nsaddr_t reponsible_node;
	//srh->cost = God::instance()->get_cost(netid);	
	//srh->parent_id = God::instance()->get_parent_id(node_id);	
	//srh->pkt_id = God::instance()->get_pkt_id(netid);	
	//srh->problem_node = problem_node ;
	//srh->reponsible_node= responsible_node_for_fixing;
	//srh->child_list = God::instance()->get_neighbor_black_node_list(netid);
	//God::instance()->get_neighbor_black_node_list(netid);		
	srh->rcv_node = rcv_n ;
	iph->saddr()=	Address::instance().create_ipaddr(node_id,RT_PORT);
	iph->daddr() = IP_BROADCAST;
	p.dest = ID((Address::instance().get_nodeaddr(iph->daddr())),::IP);
	iph->dport() = 255;
	iph->sport() = 255;
	iph->ttl()=1;
	cmnh->next_hop() = IP_BROADCAST; 
	strcpy(srh->msg,"Sending_NAC");	
	cmnh->direction() = hdr_cmn::DOWN; 
	cmnh->size() = size_ + IP_HDR_LEN;
	srh->init();
	Scheduler::instance().schedule(ll, p.pkt, 0.0 );
	p.pkt=NULL;
	//Change_Status(netid, 0); //Creating a Black Node 
	//start_delay_timer();
	//printf("Sending Node id ::%d at time %.5f \n\n" , p.src , Scheduler::instance().clock());
}


void 
DSRAgent::send_live_pkt(int rcv_n)
{
	nsaddr_t  netid ;
	netid = net_id.getNSAddr_t();
	SRPacket p;
	int node_id = netid;
	p.src=net_id;
	p.pkt=allocpkt();
	hdr_sr *srh =  hdr_sr::access(p.pkt);
	hdr_ip *iph =  hdr_ip::access(p.pkt);
	hdr_cmn *cmnh =  hdr_cmn::access(p.pkt);
	cmnh->ptype() == PT_DSR;
	//Putting Values in PAcket 
	srh->type_of_packet = 21; //This is showing that this is Sending a Route_rep_pkt
	//God::instance()->god_set_parent_id(node_id,node_id );
	//nsaddr_t problem_node;	
	//nsaddr_t reponsible_node;
	//srh->cost = God::instance()->get_cost(netid);	
	//srh->parent_id = God::instance()->get_parent_id(node_id);	
	//srh->pkt_id = God::instance()->get_pkt_id(netid);	
	//srh->problem_node = problem_node ;
	//srh->reponsible_node= responsible_node_for_fixing;
	//srh->child_list = God::instance()->get_neighbor_black_node_list(netid);
	//God::instance()->get_neighbor_black_node_list(netid);		
	srh->rcv_node = rcv_n ;
	iph->saddr()=	Address::instance().create_ipaddr(node_id,RT_PORT);
	iph->daddr() = IP_BROADCAST;
	p.dest = ID((Address::instance().get_nodeaddr(iph->daddr())),::IP);
	iph->dport() = 255;
	iph->sport() = 255;
	iph->ttl()=1;
	cmnh->next_hop() = IP_BROADCAST; 
	strcpy(srh->msg,"Sending_NAC");	
	cmnh->direction() = hdr_cmn::DOWN; 
	cmnh->size() = size_ + IP_HDR_LEN;
	srh->init();
	Scheduler::instance().schedule(ll, p.pkt, 0.0 );
	p.pkt=NULL;
	//Change_Status(netid, 0); //Creating a Black Node 
	//start_delay_timer();
	//printf("Sending Node id ::%d at time %.5f \n\n" , p.src , Scheduler::instance().clock());
}

/*

void 
DSRAgent::send_node_selection_pkt(nsaddr_t node_id)
{
	nsaddr_t  netid ;
	netid = net_id.getNSAddr_t();
	SRPacket p;
	node_id = netid;
	God::instance()->set_Pkt_counter(node_id);
  	p.src=net_id;
	p.pkt=allocpkt();
	hdr_sr *srh =  hdr_sr::access(p.pkt);
	hdr_ip *iph =  hdr_ip::access(p.pkt);
	hdr_cmn *cmnh =  hdr_cmn::access(p.pkt);
	cmnh->ptype() == PT_DSR;
	//node_id = netid;
	//ID mac_id;
	srh->type_of_packet = 12; //This is showing that this is Sending a Pick_up_node_packet	
	//int node_siw  = God::instance()->get_node_size(node_id);
	srh->Pick_up_node_size = God::instance()->get_node_size(node_id);
	srh->Pick_up_node = God::instance()->get_node_id(node_id);
	//srh->change = God::instance()->get_P_change( node_id);

	//int node___ = God::instance()->get_P_node(node_id);
	//printf("srh->Pick_up_node_size %d \n srh->Pick_up_node %d \n \n" ,srh->Pick_up_node_size ,  node___);
//	printf("Sending Node :: %d\n" , node_id);
	iph->saddr()=	Address::instance().create_ipaddr(node_id,RT_PORT);
	iph->daddr() = IP_BROADCAST;
	
	p.dest = ID((Address::instance().get_nodeaddr(iph->daddr())),::IP);
	iph->dport() = 255;
	iph->sport() = 255;
	iph->ttl()=1;
	cmnh->next_hop() = IP_BROADCAST; 
	strcpy(srh->msg,"send_node_selection_packet");	
	cmnh->direction() = hdr_cmn::DOWN; 
	cmnh->size() = size_ + IP_HDR_LEN;
	printf("\n\n******************************************************************\n\n");
	printf("Sending Pick_up_packet by node %d\n" , node_id);
	printf("Type of Packet ::%d\n", srh->type_of_packet);
	printf("Pick up node_size of sending node :: %d\n" ,srh->Pick_up_node_size);
	printf("Pickup node_id is ::%d\n" ,srh->Pick_up_node);
	printf("\n\n******************************************************************\n\n");
         srh->init();
	//Change_Status(netid, 2); //Creating a Black Node 
	//printf("Black Node Have Been Created ::%d\n" ,netid );
	Scheduler::instance().schedule(ll, p.pkt, 0.0 );
	//printf("Going to send a B2 Packet by node %d to node %d\n" , netid ,receiver_node );
	p.pkt=NULL;
}
*/

/*void
DSRAgent::send_B3_pkt(nsaddr_t receiver_node )
{
	// one more thing we are not going to use this variable receiver_node not needed at all .
	//B3 Message is always intended to it's Parent Node Only 
	printf("Not a Problem");
	nsaddr_t  netid ;
	netid = net_id.getNSAddr_t();
	SRPacket p;
	int node_id = netid;
  	p.src=net_id;
	p.pkt=allocpkt();
	hdr_sr *srh =  hdr_sr::access(p.pkt);
	hdr_ip *iph =  hdr_ip::access(p.pkt);
	hdr_cmn *cmnh =  hdr_cmn::access(p.pkt);
	cmnh->ptype() == PT_DSR;
	//ID mac_id;
	srh->type_of_packet = 2; //This shows this is going to send a B3 Packet 
	//srh->rcv_node = God::instance()->get_parent_id(netid);
	srh->rcv_node = receiver_node;
	//srh->giving_authority_to_node = God::instance()->god_select_a_node(netid);	
	srh->parent_id = God::instance()->get_parent_id(netid);
	iph->saddr()=Address::instance().create_ipaddr(netid,RT_PORT);
	iph->daddr() = IP_BROADCAST;
	p.dest = ID((Address::instance().get_nodeaddr(iph->daddr())),::IP);
	iph->dport() = 255;
	iph->sport() = 255;
	iph->ttl()=1;
	cmnh->next_hop() = IP_BROADCAST; 
	strcpy(srh->msg,"B3_message");	
	cmnh->direction() = hdr_cmn::DOWN; 
	cmnh->size() = size_ + IP_HDR_LEN;
	Change_Status(netid, 1); //Creating a Black Node to back to Grey node
         srh->init();
	
	trace("%d is going to created as a Grey node from the Black node at time %s \n\n\n" , node_id , Scheduler::instance().clock());
	//printf("Black Node Have Been Created ::%d\n" ,netid );
	Scheduler::instance().schedule(ll, p.pkt, 0.0 );
	p.pkt=NULL;
	printf("Completed");
}

*/
//sadriwala.hakimuddin   face

void
DSRAgent::send_me(nsaddr_t starter_node )
{
	//SRPacket p;
	
}


void
DSRAgent::Reduce_Nodes(int snd_node_id , int rcv_node_id )
{
	God::instance()->god_Reduce_Nodes(snd_node_id , rcv_node_id );
	God::instance()->god_Ul_list(rcv_node_id);

}




//Arun .........................................................
//
//====================================================================================
void
DSRAgent::sendBufferCheck()
  // see if any packets in send buffer need route requests sent out
  // for them, or need to be expired
{ // this is called about once a second.  run everybody through the
  // get route for pkt routine to see if it's time to do another 
  // route request or what not
  int c;

  for (c  = 0 ; c <SEND_BUF_SIZE ; c++) {
	  if (send_buf[c].p.pkt == NULL)
		  continue;
	  if (Scheduler::instance().clock() - send_buf[c].t > send_timeout) {
		  dropSendBuff(send_buf[c].p);
		  send_buf[c].p.pkt = 0;
		  continue;
	  }
#ifdef DEBUG
	  trace("Sdebug %.5f _%s_ checking for route for dst %s",
		Scheduler::instance().clock(), net_id.dump(), 
		send_buf[c].p.dest.dump());
#endif

	  handlePktWithoutSR(send_buf[c].p, true);
#ifdef DEBUG
	  if (send_buf[c].p.pkt == NULL) 
		  trace("Sdebug %.5f _%s_ sendbuf pkt to %s liberated by handlePktWOSR",
			Scheduler::instance().clock(), net_id.dump(), 
			send_buf[c].p.dest.dump());
#endif
  }
}


/*==============================================================
  Route Request backoff
------------------------------------------------------------*/
static bool
BackOffTest(Entry *e, Time time)
// look at the entry and decide if we can send another route
// request or not.  update entry as well
{
  Time next = ((Time) (0x1 << (e->rt_reqs_outstanding * 2))) * rt_rq_period;

  if (next > rt_rq_max_period)
	  next = rt_rq_max_period;

  if (next + e->last_rt_req > time)
	  return false;

  // don't let rt_reqs_outstanding overflow next on the LogicalShiftsLeft's
  if (e->rt_reqs_outstanding < 15)
	  e->rt_reqs_outstanding++;

  e->last_rt_req = time;

  return true;
}

/*===========================================================================
  DSRAgent OTcl linkage
---------------------------------------------------------------------------*/
static class DSRAgentClass : public TclClass {
public:
  DSRAgentClass() : TclClass("Agent/DSRAgent") {}
  TclObject* create(int, const char*const*) {
    return (new DSRAgent);
  }
} class_DSRAgent;

/*===========================================================================
  DSRAgent methods
---------------------------------------------------------------------------*/
DSRAgent::DSRAgent(): Agent(PT_DSR), request_table(128), route_cache(NULL),
send_buf_timer(this), flow_table(), ars_table() , timeout(this) , beacontimeout(this) , delaytimeout(this) ,Child_node_reply_timeout(this)
{
//here Arun timeout(this) is added by arun for CDS timeout 
// one more timer is added to this on 20th Apr Beaconing time out 
// one more for delay
  int c;
  route_request_num = 1;
  //int Pkt_counter = 0;
//bind("Pkt_counter", &Pkt_counter);
  route_cache = makeRouteCache();

  for (c = 0 ; c < RTREP_HOLDOFF_SIZE ; c++)
	  rtrep_holdoff[c].requested_dest = invalid_addr;
  num_heldoff_rt_replies = 0;

  target_ = 0;
  logtarget = 0;

  grat_hold_victim = 0;
  for (c = 0; c < RTREP_HOLDOFF_SIZE ; c++) {
    grat_hold[c].t = 0;
    grat_hold[c].p.reset();
  }

  //bind("off_SR_", &off_sr_);
  //bind("off_ll_", &off_ll_);
  //bind("off_mac_", &off_mac_);
  //bind("off_ip_", &off_ip_);

  ll = 0;
  ifq = 0;
  mac_ = 0;

  LIST_INSERT_HEAD(&agthead, this, link);
#ifdef DSR_FILTER_TAP
  bzero(tap_uid_cache, sizeof(tap_uid_cache));
#endif
  route_error_held = false;
}

DSRAgent::~DSRAgent()
{
  fprintf(stderr,"DFU: Don't do this! I haven't figured out ~DSRAgent\n");
  exit(-1);
}

void
DSRAgent::Terminate()
{
	int c;
	for (c  = 0 ; c < SEND_BUF_SIZE ; c++) {
		if (send_buf[c].p.pkt) {
			drop(send_buf[c].p.pkt, DROP_END_OF_SIMULATION);
			send_buf[c].p.pkt = 0;
		}
	}
}

void
DSRAgent::testinit()
{
  struct hdr_sr hsr;
  
  if (net_id == ID(1,::IP))
    {
      printf("adding route to 1\n");
      hsr.init();
      hsr.append_addr( 1, NS_AF_INET );
      hsr.append_addr( 2, NS_AF_INET );
      hsr.append_addr( 3, NS_AF_INET );
      hsr.append_addr( 4, NS_AF_INET );
      
      route_cache->addRoute(Path(hsr.addrs(),
				 hsr.num_addrs()), 0.0, ID(1,::IP));
    }
  
  if (net_id == ID(3,::IP))
    {
      printf("adding route to 3\n");
      hsr.init();
      hsr.append_addr( 3, NS_AF_INET );
      hsr.append_addr( 2, NS_AF_INET );
      hsr.append_addr( 1, NS_AF_INET );
      
      route_cache->addRoute(Path(hsr.addrs(),
				 hsr.num_addrs()), 0.0, ID(3,::IP));
    }
}

//arun ..  comm1
int
DSRAgent::command(int argc, const char*const* argv)
{
  TclObject *obj;  

if(argc == 3)
{
if ((strcasecmp(argv[1], "neighbor(0)"  ) == 0))
	{
		int jj = atoi(argv[2]);
		printf("........%d" , jj);
	}
	
}

  if (argc == 2) 
    {   

	
	//char k[20] 
	//char tstring[16];
       // strcpy(k, "neighbor(0)");
	
	if ((strcasecmp(argv[1], "neighbor(0)"  ) == 0) || (strcasecmp(argv[1], "neighbor(1)"  ) == 0) || (strcasecmp(argv[1], "neighbor(2)"  ) == 0) || (strcasecmp(argv[1], "neighbor(3)"  ) == 0) || (strcasecmp(argv[1], "neighbor(4)"  ) == 0) || (strcasecmp(argv[1], "neighbor(5)"  ) == 0) || (strcasecmp(argv[1], "neighbor(6)"  ) == 0) || (strcasecmp(argv[1], "neighbor(7)"  ) == 0) || (strcasecmp(argv[1], "neighbor(8)"  ) == 0) || (strcasecmp(argv[1], "neighbor(9)"  ) == 0) || (strcasecmp(argv[1], "neighbor(10)"  ) == 0) || (strcasecmp(argv[1], "neighbor(11)"  ) == 0) || (strcasecmp(argv[1], "neighbor(12)"  ) == 0) || (strcasecmp(argv[1], "neighbor(13)"  ) == 0) || (strcasecmp(argv[1], "neighbor(14)"  ) == 0) || (strcasecmp(argv[1], "neighbor(15)"  ) == 0) || (strcasecmp(argv[1], "neighbor(16)"  ) == 0) || (strcasecmp(argv[1], "neighbor(17)"  ) == 0) || (strcasecmp(argv[1], "neighbor(18)"  ) == 0) || (strcasecmp(argv[1], "neighbor(19)"  ) == 0) || (strcasecmp(argv[1], "neighbor(20)"  ) == 0))
	{
            printf("Argument 1 : %s \n Argument 2 : %s\n" ,argv[0] , argv[1] );
	  printf("I have Called You Successfully \n");
	   //printf("Argument 1 %d" , argv[0]);	
		const char *string = argv[1];
		//char  string[2] = {argv[1][9] , argv[1][10]};
                //char string[2];
                //string[0] = argv[1][9];
		//string[1] = argv[1][10];
		 //string = argv[1];
  			//string = argv[1][10];	
		//char call = string[9];
                  //string  = argv[1];
                //char *aa = &argv[1] ;
		//string = "arun_gupya12345" ;
         	char mine[2] ;
			mine [0]= argv[1][9];
			mine[1] = argv[1][10];
		char* cc= mine; // It's that simple!
//		const char * destPtr = (const char *)srcPtr;
		//int arunn = &mine;
		//const char *arun = (const char )mine;
		//printf("addresss :::: %d ::::: %d" , &argv , &mine);
		//char arun = mine[1];
                //const char *mine;
		//mine = string;
		//char *mm = &mine;
		int j = 0;

		j  = atoi(argv[1]);  
		//printf("Hello ::::::: %s:::::%s" , string );
               // int j = 0 ;
		//j = j+1;
		//atoi(&string);   (int) string;
		//int j = (int) argv[2];
		//int k = (int) argv[3];
		printf("\n\n=============Called Node : %d " , atoi(argv[1]));
		printf("\n================%d\n",j);
		//printf("\n%d",k);
		//Change_Status(1);
		//neighbors(j); 
		
		
	  return TCL_OK;
	}

	
}
//Arun ...................................................command..........CDSCALL
if(argc == 2)
  {
     if ((strcasecmp(argv[1], "CDS"  ) == 0)) 
	{
	//printf("I got the CDS call Thanx %s\n " ,argv[2]);
	//nsaddr_t starter_node =  atoi(argv[2]);
	 //starter_node =  0;    
	//u_int32_t starter_node =  atoi(argv[2]);  
	//printf("Starting Node is : %d:::%d\n\n\n" ,starter_node  , atoi(argv[2]));
	//printf("Printing The Starter_node: ... %d\n" ,starter_node );
	//send_me(starter_node);
	God::instance()->find_neighbor();
	
	nsaddr_t node_id = God::instance()->pick_up_a_Best_node_centralized();
	printf("Best Node is %d\n\n" , node_id);
	God::instance()->god_set_parent_id(node_id,node_id );
	send_B1_pkt(node_id);
	//print_the_whole_node_status();
		return TCL_OK;
		}
	 if ((strcasecmp(argv[1], "Block"  ) == 0)) 
	{
		printf("Here \n");
		int node_id = net_id.getNSAddr_t();
		printf("Here node id %d\n" , node_id);
		God::instance()->set_node_status_rc(node_id);	
		printf("Node status is ## %d \n" ,God::instance()->get_node_status_rc(node_id)	 );
		return TCL_OK;
		
	}
  }
if(argc == 3)
  {
     if ((strcasecmp(argv[1], "Pick_Up_a_Node"  ) == 0)) 
	{
	//printf("I got the CDS call Thanx %s\n " ,argv[2]);
	//nsaddr_t starter_node =  atoi(argv[2]);
	 //starter_node =  0;    
	//u_int32_t starter_node =  atoi(argv[2]);  
	//printf("Starting Node is : %d:::%d\n\n\n" ,starter_node  , atoi(argv[2]));
	//printf("Printing The Starter_node: ... %d\n" ,starter_node );
	//send_me(starter_node);
	God::instance()->find_neighbor();
//	nsaddr_t starter_node = 
	//Pick_up_a_Node();
	//send_B1_pkt(starter_node);
	
	//print_the_whole_node_status();
	int num_nodes = God::instance()->get_number_of_nodes();	
	printf("Nodes::%d\n\n" , num_nodes);
	//nsaddr_t node_id = Random::integer(num_nodes);
	nsaddr_t node_id = net_id.getNSAddr_t();	
	printf("Randomly selected node is :::: %d\n\n" , node_id);
	//God::instance()->set_P_change(node_id ,10);
	//God::instance()->set_P_node( node_id , node_id);
	printf("Node's %d neighbor size is %d" , node_id , God::instance()->get_node_size(node_id));
	God::instance()->set_node_id( node_id , node_id);
	God::instance()->set_neighbor_size( node_id ,God::instance()->get_neighbor_size(node_id));
	send_node_selection_pkt(node_id);
	printf("%d Selected Node is ::%d\n\n" ,node_id , God::instance()->get_node_id( node_id));
	//print_whole_end_status();
		return TCL_OK;
		}
  }

if(argc == 2)
{
	if ((strcasecmp(argv[1], "All_Info"  ) == 0)) 
	{
		printf("\n\n\nPrint Whole Status \n\n\n");
		print_whole_end_status();
		printf("\n\nBest node is ::%d\n\n" , node_id_Pick_node);
		//printf("Total Number of Packet's Communicated ::: %d" ,Pkt_counter);
		return TCL_OK;
	}
}
if(argc == 2)
{
	if ((strcasecmp(argv[1], "Guha1"  ) == 0)) 
	{
		printf("\n\n\nGoing to run Guha Kullaer's Method \n\n\n");
		God::instance()->Guha_Kular_Algo_1();
		//God::instance()->Guha_Kular_Algo_2();
		//God::instance()->Guha_Kular_Algo_3();
		//printf("Total Number of Packet's Communicated ::: %d" ,Pkt_counter);
		return TCL_OK;
	}
}
if(argc == 3)
{
	if ((strcasecmp(argv[1], "Guha2"  ) == 0)) 
	{
		printf("\n\n\nGoing to run Guha Kullaer's Method \n\n\n");
		//God::instance()->Guha_Kular_Algo_1();
		God::instance()->Guha_Kular_Algo_2();
		//God::instance()->Guha_Kular_Algo_3();
		//printf("Total Number of Packet's Communicated ::: %d" ,Pkt_counter);
		return TCL_OK;
	}
}
if(argc == 3)
{
	if ((strcasecmp(argv[1], "LinkDown"  ) == 0)) 
	{
		//printf("\n\n\n Node is going to down the link between  \n\n\n");
		//printf("Print Argv 2 value: %s \n",argv[2]);
		int blocked_node  = atoi(argv[2]);
		nsaddr_t node_id = net_id.getNSAddr_t();
		//int node = node_id;
		God::instance()->set_sending_link_down(blocked_node);
		God::instance()->set_receiving_link_down(node_id);
		//printf("Print Argv 2 value: %d \n",blocked_node);
		printf("Node which will not receieve the PAcket   : %d from node  %d\n",node_id , blocked_node);
		God::instance()->add_blocked_node(node_id , blocked_node);
		//printf("Done!!");
		return TCL_OK;
	}
}

if(argc == 3)
{
	if ((strcasecmp(argv[1], "Beacon"  ) == 0)) 
	{
		//start_beacon_timer();
		//printf("We are at Call");
		send_Beacon_pkt();
		//beaconing();
		return TCL_OK;
	}
}

if(argc == 2)
{
	if ((strcasecmp(argv[1], "Pruning"  ) == 0)) 
	{
		//start_beacon_timer();
		//printf("We are at Call");
		//send_Beacon_pkt();
		//beaconing();
		printf("we are at Pruning \n\n");
		
		check_leaf_node();
		return TCL_OK;
	}
}

//............................................................................


//Arun

 if (argc == 2) 
    {


      if (strcasecmp(argv[1], "testinit") == 0)
	{
	  testinit();
	  return TCL_OK;
	}
      if (strcasecmp(argv[1], "reset") == 0)
	{
	  Terminate();
	  return Agent::command(argc, argv);
	}
      if (strcasecmp(argv[1], "check-cache") == 0)
	{
	  return route_cache->command(argc, argv);
	}
      if (strcasecmp(argv[1], "startdsr") == 0)
	{
	  if (ID(1,::IP) == net_id) 
	    { // log the configuration parameters of the dsragent
  trace("Sconfig %.5f tap: %s snoop: rts? %s errs? %s",
		    Scheduler::instance().clock(),
		    dsragent_use_tap ? "on" : "off",
		    dsragent_snoop_source_routes ? "on" : "off",
		    dsragent_snoop_forwarded_errors ? "on" : "off");
  trace("Sconfig %.5f salvage: %s !bd replies? %s",
		    Scheduler::instance().clock(),
		    dsragent_salvage_with_cache ? "on" : "off",
		    dsragent_dont_salvage_bad_replies ? "on" : "off");
  trace("Sconfig %.5f grat error: %s grat reply: %s",
	            Scheduler::instance().clock(),
	            dsragent_propagate_last_error ? "on" : "off",
	            dsragent_send_grat_replies ? "on" : "off");
  trace("Sconfig %.5f $reply for props: %s ring 0 search: %s",
	            Scheduler::instance().clock(),
	            dsragent_reply_from_cache_on_propagating ? "on" : "off",
	            dsragent_ring_zero_search ? "on" : "off");
	    }
	  // cheap source of jitter
	  send_buf_timer.sched(BUFFER_CHECK 
			       + BUFFER_CHECK * Random::uniform(1.0));	  
          return route_cache->command(argc,argv);
	}
    }
  else if(argc == 3) 
    {

	

      if (strcasecmp(argv[1], "addr") == 0) 
	{
	  int temp;
	  temp = Address::instance().str2addr(argv[2]);
	 net_id = ID(temp, ::IP);
	 flow_table.setNetAddr(net_id.addr);
	 route_cache->net_id = net_id;
	 return TCL_OK;
	} 
      else if(strcasecmp(argv[1], "mac-addr") == 0) 
	{
	  MAC_id = ID(atoi(argv[2]), ::MAC);
	  route_cache->MAC_id = MAC_id;
	  return TCL_OK;
	}
      else if(strcasecmp(argv[1], "rt_rq_max_period") == 0)
        {
          rt_rq_max_period = strtod(argv[2],NULL);
          return TCL_OK;
        }
      else if(strcasecmp(argv[1], "rt_rq_period") == 0)
        {
          rt_rq_period = strtod(argv[2],NULL);
          return TCL_OK;
        }
      else if(strcasecmp(argv[1], "send_timeout") == 0)
        {
          send_timeout = strtod(argv[2],NULL);
          return TCL_OK;
        }

      
      if( (obj = TclObject::lookup(argv[2])) == 0) 
	{
	  fprintf(stderr, "DSRAgent: %s lookup of %s failed\n", argv[1],
		  argv[2]);
	  return TCL_ERROR;
	}

      if (strcasecmp(argv[1], "log-target") == 0)  {
	      logtarget = (Trace*) obj;
	      return route_cache->command(argc, argv);
      }
      else if (strcasecmp(argv[1], "tracetarget") == 0 )
       	{
	  logtarget = (Trace*) obj;
	  return route_cache->command(argc, argv);
	}
      else if (strcasecmp(argv[1], "install-tap") == 0)  
	{
	  mac_ = (Mac*) obj;
	  mac_->installTap(this);
	  return TCL_OK;
	}
      else if (strcasecmp(argv[1], "node") == 0)
	{
	  node_ = (MobileNode *) obj;
	  return TCL_OK;
	}
      else if (strcasecmp (argv[1], "port-dmux") == 0) 
	{
	  port_dmux_ = (NsObject *) obj;
	  return TCL_OK;
	}
    }
  else if (argc == 4)
    {
      if (strcasecmp(argv[1], "add-ll") == 0) 
	{
	  if( (obj = TclObject::lookup(argv[2])) == 0) {
	    fprintf(stderr, "DSRAgent: %s lookup of %s failed\n", argv[1],
		    argv[2]);
	    return TCL_ERROR;
	  }
	  ll = (NsObject*) obj;
	  if( (obj = TclObject::lookup(argv[3])) == 0) {
	    fprintf(stderr, "DSRAgent: %s lookup of %s failed\n", argv[1],
		    argv[3]);
	    return TCL_ERROR;
	  }
	  ifq = (CMUPriQueue *) obj;
	  return TCL_OK;

	}


    }
  return Agent::command(argc, argv);
}

void
DSRAgent::sendOutBCastPkt(Packet *p)
{
  hdr_cmn *cmh =  hdr_cmn::access(p);
  if(cmh->direction() == hdr_cmn::UP)
    cmh->direction() = hdr_cmn::DOWN;
  // no jitter required
  Scheduler::instance().schedule(ll, p, 0.0);
}


//Arun
void
DSRAgent::Change_Status(int node_id , int status_code)
{
			//printf("Got a Call\n");
			int value = God::instance()->changestatus(node_id , status_code);
			//printf("Change Status at DSRAgent ::%d\n" , value);
			// nodee_ = (MobileNode *) obj;
			//node_->update_node_status(3)	;
}

/*
void
DSRAgent::check_leaf_node()
{
	int num_nodes = God::instance()->get_number_of_nodes();	
	for(int node_id = 0 ; node_id < num_nodes ; ++node_id)
	{
		if(get_node_status(node_id) == 2)
		{
			if(God::instance()->is_that_node_is_the_leave_node_in_back_bone(node_id) == true )
			{
				printf("Node %d is the Leave node in Back_bone\n " , node_id);
				send_child_discover_pkt(node_id);
			
				//start from here 
			}
		}
	}
	
}*/
//updated one love you arun

void
DSRAgent::check_leaf_node()
{
	//int num_nodes = God::instance()->get_number_of_nodes();	
	//for(int node_id = 0 ; node_id < num_nodes ; ++node_id)
	//{
		int node_id  = get_net_id();
		if(get_node_status(node_id) == 2)
		{	
			neighbor_black_node_prepare();
			if(God::instance()->is_that_node_is_the_leave_node_in_back_bone(node_id) == true )
			{
				printf("Node %d is the Leave node in Back_bone\n " , node_id);
				send_child_discover_pkt(node_id);
			
				//start from here 
			}
		}
	//}
	
}
void 
DSRAgent::Pick_up_a_Node()
{
	int num_nodes = God::instance()->get_number_of_nodes();	
	nsaddr_t node_id = Random::integer(num_nodes);
	printf("Randomly selected node is :::: %d" , node_id);
	//God::instance()->set_P_change(node_id ,10);
	//God::instance()->set_P_node( node_id , node_id);
	God::instance()->set_node_id( node_id , node_id);
	God::instance()->set_neighbor_size( node_id ,God::instance()->get_neighbor_size(node_id));
	send_node_selection_pkt(node_id);
	printf("Selected Node is ::%d\n\n" ,God::instance()->get_node_id( node_id));
	//print_whole_end_status();
}


void
DSRAgent::print_the_whole_node_status()
{
	God::instance()->print_the_whole_node_status();	
}

void
DSRAgent::beaconing()
{
	printf("\n\n\n*&^%$#\n\n\n");
	int total_nodes = get_total_nodes();
	printf("nodes total ::%d\n\n" , total_nodes);
	for(int nodes = 0 ; nodes < total_nodes ; ++nodes )
	{
		//printf("QAWSED");
		if(get_node_status(nodes) == 2 )
		{
			start_beacon_timer(nodes);
			break ;
		}
	}
}

int 
DSRAgent::get_total_nodes()
{
	return God::instance()->get_total_nodes();
}

void
DSRAgent::neighbor_black_node_prepare()
{
	int total_size = get_total_nodes();
	for(int node = 0 ; node < total_size ; ++node)
	{
	God::instance()->get_neighbor_black_node_list(node);
	}		
}

int 
DSRAgent::get_node_status(int rcv_n)
{
	return God::instance()->get_node_status_god(rcv_n);
}

void
DSRAgent::print_whole_end_status()
{
	 God::instance()->print_whole_end_status();
}
//void
//DSRAgent::fixing(int responsible_node_for_fixing , int problem_node)
//{
	
//}

//this is for intermediate node and 
void
DSRAgent::fixing(int responsible_node_for_fixing , int problem_node)
{
	// assumption is that problem node is alos a Black node 
	// That node is responsible for the finxing this node(responsible_node_for_fixing ) wants to get connected with that either problem problem_node or their neighbor black node 
	// we should first try to connect with that problem only 
	int node_id = 1000 ; 
	// check's whether they are direclty connected or not
	//find out is  responsible_node_for_fixing is the leave node.
	printf("\nResponsible node %d \nProblem node %d\n" , responsible_node_for_fixing ,problem_node);
	if(God::instance()->is_that_node_is_the_leave_node_in_back_bone(responsible_node_for_fixing) == true)
	{
		printf("Case !! 1.1\n");
		int node = God::instance()->is_that_node_have_any_other_black_node(responsible_node_for_fixing ,problem_node );
		printf("node = %d\n" ,node);
		if(( node == 1000))
		{
			printf("Case !! 1.1.1\n");
			send_reset_packet();
		}
		else 
		{
			printf("Changing The Parent_id of node %d from node %d to node %d \n\n", responsible_node_for_fixing ,problem_node ,node );
		}
	}
	else 
	{
		connect(responsible_node_for_fixing , problem_node);
	}
	/*
	// as philosophically this function is not needed here 
	else if(God::instance()->is_that_node_in_black_node_list(responsible_node_for_fixing , problem_node))
	{
		printf("Case !! 1.2");
		printf("Node %d and %d are Connected directly \n" ,responsible_node_for_fixing , problem_node );
	}
	//check whether is it in 2 hop distance 
	else if(( node_id = God::instance()->if_node_is_in_2_hop(responsible_node_for_fixing ,problem_node)) != 1000)
	{
		printf("Case !! 1.3");
		printf("Node Problem_node %d is connected to node %d via node %d\n" ,problem_node ,responsible_node_for_fixing ,node_id );
		if(get_node_status(node_id) == 1 ) // check whether that node is black node or not ... checks whether that node is grey node
		{
			send_offer_pkt(node_id);//1581
		}
	}
	else if(parent_node(problem_node) != problem_node)
	{	printf("Case !! 1.4");
		printf("Node Problem_node %d is not in 2hop from node %d\nGoing to Try another Method \n" ,problem_node ,responsible_node_for_fixing);
		//here should be not like that 
		connect(responsible_node_for_fixing , parent_node(problem_node));
		//at the end you have to check that node's are connected or not. 
	}
	else 
	{	printf("Case !! 1.5");
		int Black_node_List_size = God::instance()->get_node_list_size(problem_node);
		for(int counter = 0  ;counter < Black_node_List_size ; ++counter )
		{
			connect(responsible_node_for_fixing , God::instance()->get_black_node_by_position(problem_node , counter));				
		}
	}
	*/
}

int 
DSRAgent::parent_node(int node_id)
{
	return God::instance()->get_parent_id(node_id);
}

void
DSRAgent::connect(int responsible_node_for_fixing ,int problem_node )
{	if(responsible_node_for_fixing != problem_node )
	{
		int node_id = 0; // this node_id may be Black_node or the Grey node so we have to check the condition
		if(God::instance()->is_that_node_in_black_node_list(responsible_node_for_fixing , problem_node))
		{
			printf("Case !! 2.1\n");
			printf("Node %d and %d are Connected directly \n" ,responsible_node_for_fixing , problem_node );
		}
		else 
		{
			if(( node_id = God::instance()->if_node_is_in_2_hop(responsible_node_for_fixing ,problem_node)) != 1000)
			{
				printf("Case !! 2.2\n");
				printf("Node Problem_node %d is connected to node %d via node %d\n" ,problem_node ,responsible_node_for_fixing ,node_id );
				if(get_node_status(node_id) == 1 ) // check whether that node is black node or not ... checks whether that node is grey node
				{
					send_offer_pkt(node_id);//1581
				}
				// if that node is grey node
				else if(get_node_status(node_id) == 2)
				{
					//here node_id means    A ==========B(node_id)=============C
					//Going to change the Parent id of node_id
					//Change_Status(node_id, 2);
					God::instance()->god_set_parent_id(responsible_node_for_fixing,node_id );
					printf("Problem Solved\n ");
				
				
				}
			}
			else
			{
				printf("That node %d not in Range (two hop) with node %d\n" ,problem_node,responsible_node_for_fixing);	
				God::instance()->set_cost(responsible_node_for_fixing);
				God::instance()->set_coming_from(responsible_node_for_fixing , responsible_node_for_fixing);			
				God::instance()->set_pkt_id(responsible_node_for_fixing , responsible_node_for_fixing);			
				send_route_find_pkt(responsible_node_for_fixing ,problem_node );
			
				//1698
			}
		}
		
			
		//if(God::instance()->is_that_node_in_black_node_list(responsible_node_for_fixing , problem_node))
		//{
		//	//responsible_node_for_fixing will search in own Black node list to check  weather that problem node exist in his black node list or not 
			//if yes then
		//	printf("Node %d and %d are Connected directly \n" ,responsible_node_for_fixing , problem_node );
		//}
	
		if(( node_id = God::instance()->if_node_is_in_2_hop(responsible_node_for_fixing ,problem_node)) != 1000)
		{
			printf("Node %d and %d are Connected via node %d \n" ,responsible_node_for_fixing ,problem_node ,node_id  );
		}
	}
}


//.............Arun.... receieve function ......rcvv....mila .... 
//Remember always i have added else if condition you never change this node should take only one operation not more than one ohhk
void
DSRAgent::recv(Packet* packet, Handler*)
  /* handle packets with a MAC destination address of this host, or
     the MAC broadcast addr */
{


//printf("\nin rcv function \n");
  hdr_sr *srh =  hdr_sr::access(packet);
  hdr_ip *iph =  hdr_ip::access(packet);
  hdr_cmn *cmh =  hdr_cmn::access(packet);
//.... arun

  //hdr_pkt* pkt = hdr_pkt::access(packet);
  // special process for GAF
  if (cmh->ptype() == PT_GAF) {
    if (iph->daddr() == (int)IP_BROADCAST) { 
      if(cmh->direction() == hdr_cmn::UP)
	cmh->direction() = hdr_cmn::DOWN;
      Scheduler::instance().schedule(ll,packet,0);
      return;
    } else {
      target_->recv(packet, (Handler*)0);
      return;	  
    }
  }
//net_id.getNSAddr_t()
//Things to be noted is that never use p.src and p.dest in a same line to print like 
//printf("at SRPACKET LEVEL Sending Node  ::%d:   Receiving node ::::%d\n" ,p.src , p.dest); this will give you error so please do not follow this way always print in a single value is a single line 
SRPacket p(packet, srh);
p.dest = ID((Address::instance().get_nodeaddr(iph->daddr())),::IP);
p.src = ID((Address::instance().get_nodeaddr(iph->saddr())),::IP);
nsaddr_t  netid ,src_id;
netid = net_id.getNSAddr_t();
src_id = iph->saddr();
int node_id = netid;	
//printf("rcv_id%d\n" , src_id);
//printf("net_id ::: %d\n" , net_id);
//printf("Receiving Node ::%d\n" , p.dest);
//...Arun .....rcv
//printf("yes packet recvied of type %s\n",srh->msg);
/*if(strcmp(srh->msg,"bd_hello")==0)
		{
		printf("\n node-%d-hello recived\n",net_id.getNSAddr_t());
                
                  // recvHello(packet);
		}

if (cmh->ptype() == PT_DSR) {
printf("entered\n");
}*/

//printf("Type of packet ******************************************************** %d\n" , srh->type_of_packet);
//printf("Sending Node  :::%d::Receiving Node ::%d\n" , iph->saddr() , iph->daddr());

//printf("srh-> valid() :: %d\n" ,srh->valid());
	//if (srh->valid() == 0) {
  if (cmh->ptype() == PT_DSR) {
    if (iph->daddr() == (int)IP_BROADCAST) { 
//int receiving_node = ;
// if I am Receiving the B1 message    B1  type_of_packet == 0
	
	if(srh->type_of_packet == 0) {
				//printf("Receivied an B1 Packet \n");
				//printf("Sending Node    ::%d:\n", src_id);
				//printf("Receiving Node  ::%d\n" , netid);
			//int static  se = 0;
			//printf("Static Value  %d\t:at the IP Layer   Sending Node::%d::Receiving Node::%d\n" , ++se , iph->saddr() , iph->daddr());
			//printf("Parent _ ID :: %d\n" , srh->parent_id);
			//printf("srh->msg :: %s\n" , srh->msg);
			//value =God::instance()->getdistance(i,k);
			//refer to line 
			
			//int we = get_node_status(node_id);
			//Case ::: 1   White nodes are receiving the B1 Message 
			
			if((get_node_status(node_id) == 0) && (God::instance()->get_parent_id(netid) == 1000))
			{
//				if((get_node_status(node_id) == 0) && (God::instance()->get_parent_id(netid) == 1000) && (God::instance()->get_neighbor_size(netid)> 0))
				//printf("CASE :: 1.1\n");
				//printf("Node (*&^%$#@*&^%$@ %d" , node_id);
				God::instance()->god_set_parent_id(netid ,src_id );
				//setting the Parent Id
				///here netid == receiving node's id and src_id == sending node's id
				//printf("get_node_status(node_id) ::%d\n" , we);
				Change_Status(netid, 1); //Creating a Grey Node  //Find this function
				Reduce_Nodes(src_id , netid);
				//netid receiving node's id who is receiving B1 message
				//src_id is the Sending node's id who is sending the B1 message 
				//printf("Status is changed to Grey of Node  ::%d\n" , netid);
				//printf("Node's Parent Id ::%d\n",God::instance()->get_parent_id(netid));
				// 0 == white node   ===== used at Change_Status(netid, 1);
				// 1 == Grey Node 
				// 2 == Black Node 
				//Black Node 
				if((srh->giving_authority_to_node == netid) )
				{
					if((God::instance()->get_neighbor_size(netid)> 0))
					{
						//printf("CASE :: 1.1.1\n");
						//printf("We are at this point ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n\n\n\n\n\n\n\n");
						send_B1_pkt(netid);
					}
					if((God::instance()->get_neighbor_size(netid) == 0))
					{
						//printf("CASE :: 1.1.2\n");
						printf("Node was the white Node and it doesn't have any node to cover so going to send a B3 message");
						send_B3_pkt(God::instance()->get_parent_id(netid));
					}
				}
				//Grey Node 
				if((srh->giving_authority_to_node != netid) && (God::instance()->get_neighbor_size(netid)> 0))
				{
				//	printf("CASE :: 1.1.2\n");
					trace("Starting Timer At %.5f by Node %d" , Scheduler::instance().clock() , node_id);
					//printf("Starting Timer At %f by Node %d\n" , Scheduler::instance().clock() , node_id);
					God::instance()->get_Single_nodes_Neighbors(netid);
					start_CDS_timer(node_id);
				}
				
				
			}
			//Case ::: 2 Grey Nodes are Receiving the B1 message if that receiving node is not selected as a Black node 
			else if((get_node_status(node_id) == 1)  && (God::instance()->get_neighbor_size(netid)> 0) && (srh->giving_authority_to_node != netid))
			{
				//printf("CASE :: 1.2\n");
				// understand why i have taken that three conditions to check this thing that /
				//first and third condition is ok but why i have taken the 2nd one because this tell this message is again 
				//from the my parent because parent can select me as a new Black node because it may happen that node whom he have 
				//selected is covered before .got it .
				//so second condition helps us for this thing
				//here we have to add one more condition to check weather that node is selected as a Black node or not
				//at the end i thought that condition 2 should not use .. condition 2 was this 
				//(God::instance()->get_parent_id(netid) == src_id )
				if(God::instance()->get_parent_id(netid) != src_id)
				{
				//printf("CASE :: 1.2.1\n");
				//printf("I am  Grey Node Before i am gonna just reduce the UL list Size\n");
				Reduce_Nodes(src_id , netid);
				//printf("Node's Parent Id ::%d\n",God::instance()->get_parent_id(netid));
				}
			}
			//Case ::: 3 Grey Nodes are Receiving the B1 message if that receiving node is selected as a Black node 
			else if((get_node_status(node_id) == 1) && (srh->giving_authority_to_node == netid)&& (God::instance()->get_neighbor_size(netid)> 0))
			{
				//printf("CASE :: 1.3\n");
				if(God::instance()->get_parent_id(netid) == src_id )
				{
					//printf("CASE :: 1.3.1\n");
					//this is for when a black node is send one more B1 Packet 
					//printf("Special Case When Black node is going to send the B1 message again");	
					send_B1_pkt(netid);
					
				}
				else
     				{
					//printf("CASE :: 1.3.2\n");
					printf("Going to Send a B2 Message to Sender Back by %d\n" , net_id);
					Reduce_Nodes(src_id , netid);
					send_B2_pkt(src_id);//// src_id is the id of Sending Node's id who have send this B1 message 
				}
			}
			//Case 4 Node is Receiving a B1 message and it's Y==0 and Counter value is zero then he should become a Grey node Back
			 else if((God::instance()->get_neighbor_size(netid) ==  0) && (srh->giving_authority_to_node == netid) && (God::instance()->get_counter(netid) ==  0))
			{
				//printf("CASE :: 1.4\n");
				printf("Going To send a B3 Packet from node %d to node %d" , node_id , src_id );
				send_B3_pkt(src_id);	//// src_id is the id of Sending Node's id who have send this PAcket  
								
			}
//========================================  Up to here all is Ok!!!
			//int i = netid;
			//printf("Going to call m Change_Status(i) where i is : %d \n" , i );
			//Change_Status(i);
			//case 5 where 
			else if((get_node_status(node_id) == 2) && (srh->giving_authority_to_node == netid)  )
			{
				//printf("CASE :: 1.5\n");
				God::instance()->reduce_this_node_only(node_id , src_id);
				//here src_id is going to be deleted from node_id UL list 
				//printf("Going to Send a B2 Message to Sender Back by Node %d",net_id);
				//printf("\tto node %d\n" , src_id);
				send_B2_pkt(src_id);	
				//printf("Child Node is ::::%d\n" , God::instance()->get_Child_node(node_id));
				if(God::instance()->get_Child_node(netid) == src_id )
				{
				
					//printf("CASE 1.5.1\n");
					//printf("decreament_counter(node_id) dest: %d \t src%d\n" ,node_id , src_id);
					//God::instance()->decreament_counter(node_id);
					if((God::instance()->get_neighbor_size(netid) ==  0) &&  (God::instance()->get_counter(netid) ==  0))
						{
							//printf("CASE 1.5.1.1\n");
							printf("Going To send a B3 Packet by node %d to node %d\n", node_id , src_id );
							send_B3_pkt(src_id);	
								
						}	
					
				}
				//here is the idea make a Child Node then if Child node 
				//this condition should not come 
				// Arun Add here Check Condition !!! Remember chaech condition is that condition when i check that y>0 or not 
				//// src_id is the id of Sending Node's id who have send this B1 message 
			}
			//printf("\n\n\n");
			}
		// B2 Packet Send by the Grey Node if that node is selected as a Black node 
		//Receiving the B2 Packet 
		else if(srh->type_of_packet == 1) 
		{
				//printf("CASE 2.1\n");
				//printf("Receivied an B2 Packet \n");
				//printf("Sending Node    ::%d:\n", src_id);
				//printf("Receiving Node  ::%d\n" , netid);
			
			if(srh->rcv_node == netid)
			{
				//printf("CASE 2.1.1\n");
				//printf("Receiving Node  ::%d\n" , netid);
				//printf("Sending Node    ::%d:\n", src_id);
				God::instance()->reduce_this_node_only(node_id , src_id);
				//here src_id is going to be deleted from netid list 
				//printf("decreament_counter(node_id) dest: %d \t src%d\n" ,node_id , src_id);
				God::instance()->decreament_counter(node_id);	// this may be the problem !!!
				
				//Arun start from here :)
				if((get_node_status(node_id) == 2))
				{
					//printf("CASE 2.1.1.1\n");
					if((God::instance()->get_neighbor_size(netid) == 0) && (God::instance()->get_counter(netid) ==  0))
					{
						//printf("CASE 2.1.1.1.1\n");
						printf("Node %d is Going to become a Grey Node from Black Node \n", node_id);
						send_B3_pkt(God::instance()->get_parent_id(node_id));
						//send_B3_pkt(5);
						//printf("Completed");
						//B3 Packet is always Send  to his Parent 
					}
					else if((God::instance()->get_neighbor_size(netid) > 0))
					{
						trace("Node %d is going to send the another B1 Packet because that node %d is already Grey Node " , node_id , src_id );
						printf("Node %d is going to send the another B1 Packet because that node %d is already Grey Node \n" , node_id , src_id );
						printf("Childe Counter :: %d for node id %d\n" , God::instance()->get_counter(netid),netid);
						send_B1_pkt(netid);
					}	
				}
			}
			else if(srh->rcv_node != netid)
			{
				God::instance()->reduce_this_node_only(node_id , src_id);
			}	
			//when a Grey node or a White node is Receiving a B2 Packet ,for a grey node if that is intended to that node
			else if((get_node_status(node_id) == 0) ||(get_node_status(node_id) == 1) )
			{
				//printf("CASE 2.2\n");
				God::instance()->reduce_this_node_only(node_id , src_id);
			}
			/*if((srh->rcv_node == netid) && (get_node_status(node_id) == 2))
			{
				if((God::instance()->get_neighbor_size(netid)> 0))
				{
					//God::instance()->decreament_counter(node_id);
					//God::instance()->decreament_counter(node_id);
					send_B1_pkt(netid);
				
				//God::instance()->reduce_this_node_only(node_id);
				//Black node is going to send a B1 message again here 
				}
				//else if this to be implemented 
				

			}
			else if((God::instance()->get_neighbor_size(netid) ==  0) &&  (God::instance()->get_counter(netid) ==  0))
			{
				printf("Going To send a B3 Packet" );
				send_B3_pkt(src_id);	
								
			}*/
			//printf("Here");
		}

		//Receiving the B3 PAcket 
		else if(srh->type_of_packet == 2) 
		{
			//printf("Receivied an B3 Packet \n");
			//printf("Sending Node    ::%d:\n", src_id);
			//printf("Receiving Node  ::%d\n" , netid);
			//God::instance()->reduce_this_node_only(node_id);	
			if((get_node_status(node_id) == 2) && (srh->rcv_node == netid))
			{
				//God::instance()->decreament_counter(node_id);	
				if(God::instance()->get_parent_id(src_id) == netid)
				{
					//printf("Node %d got a B3 Packet by my Child node so i have to stay as a Black Node " , node_id);
				}
				else if(God::instance()->get_neighbor_size(netid) > 0)
				{
					God::instance()->reduce_this_node_only(node_id , src_id);
					//printf("decreament_counter(node_id) dest: %d \t src%d\n" ,node_id , src_id);
					God::instance()->decreament_counter(node_id);
					//printf("CASE 3.1.1.1\n");	
					send_B1_pkt(netid);
				}
				if((God::instance()->get_neighbor_size(netid) ==  0) &&  (God::instance()->get_counter(netid) ==  0))
						{
							//printf("CASE 3.1.1.2\n");
							printf("Childe Counter :: %d for node id %d\n" , God::instance()->get_counter(netid),netid);
							//printf("Going To send a B3 Packet" );
							send_B3_pkt(src_id);	
								
						}
			}
		}
		//Received an BestNodeSelection Method Message 
		else if(srh->type_of_packet == 10)
		{
			//if(srh->change != God::instance()->get_P_change(node_id))
			//{
				
			//}
			if( srh->Pick_up_node_size > God::instance()->get_P_y_(node_id))
			{
				God::instance()->set_P_y_(node_id ,srh->Pick_up_node_size );
				God::instance()->set_P_node(node_id ,God::instance()->get_P_node(src_id) );
				nsaddr_t add = God::instance()->get_P_y_(node_id);
				//printf(" case 1\nNode %d is selected and with Size %d at node %d\n" ,God::instance()->get_P_node(node_id), add  , node_id );
				send_Pick_up_pkt(netid);
				
				//node_id_Pick_node = netid;
			}
			else if(srh->Pick_up_node_size < God::instance()->get_P_y_(node_id))
			{
				God::instance()->set_P_node(node_id ,node_id );	
				//nsaddr_t add = God::instance()->get_P_y_(node_id);
				//printf("Node %d is selected and with Size %d\n" ,God::instance()->get_P_node(node_id), add  );
				send_Pick_up_pkt(netid);
				
			}
			else if((srh->Pick_up_node_size == God::instance()->get_P_y_(node_id)) && (srh->Pick_up_node != God::instance()->get_P_node(node_id) ) && (src_id != God::instance()->get_P_node(node_id)))
			{
				if(srh->Pick_up_node >= netid)
				{
					God::instance()->set_P_node(node_id ,srh->Pick_up_node );
					nsaddr_t add = God::instance()->get_P_y_(node_id);
					//printf("case 2\nNode %d is selected and with Size %d at node %d\n" ,God::instance()->get_P_node(node_id), add , node_id );
					send_Pick_up_pkt(netid);
				}
				else 
				{
					God::instance()->set_P_node(node_id ,node_id );
					nsaddr_t add = God::instance()->get_P_y_(node_id);
					//printf("case 3\nNode %d is selected and with Size %d at node %d\n" ,God::instance()->get_P_node(node_id), add , node_id );
					send_Pick_up_pkt(netid);
				}
			}
		}
		else if(srh->type_of_packet == 12)
		{
			int change = 0 ;
			printf("********************************\n\n");
			printf("Receiving node's id %d\n",node_id);
			printf("Sending Node id ::%d\n" , src_id);
			printf("Received node_size ::%d\n" , srh->node_size);
			printf("Received node_id ::%d\n" , srh->node_id);
			printf("Receiving node's node_size ::%d\n" ,God::instance()->get_node_size(node_id) );
			printf("Receiving node's node_id ::%d\n" ,God::instance()->get_node_id(node_id));
			printf("\n\n********************************\n\n");
			if(srh->node_size > God::instance()->get_node_size(node_id))
			{
				printf("Receiving node's id %d\n",node_id);
				printf("Sending Node id is %d\n", src_id);
				change = 1;
				God::instance()->set_node_id(node_id ,srh->node_id);
				God::instance()->set_neighbor_size( node_id,srh->node_size );
				node_id_Pick_node  = srh->node_id;
			}
			else if(srh->node_size < God::instance()->get_node_size(node_id))
			{
				if(God::instance()->get_node_id(node_id) == (-1000))
				{
					printf("Receiving node's id %d\n",node_id);
					printf("Sending Node id is in ** condition %d\n", src_id);
					change = 1;
					God::instance()->set_node_id(node_id ,node_id);
					//God::instance()->set_neighbor_size( node_id,);
					node_id_Pick_node  = node_id;
				}
			}
			else if(srh->node_size == God::instance()->get_node_size(node_id))
			{
				if(srh->node_id > God::instance()->get_node_id(node_id))
				{
					printf("Receiving node's id in condition2___ %d\n",node_id);
					printf("Sending Node id is %d\n", src_id);
					God::instance()->set_node_id(node_id ,srh->node_id);
					change = 1;
					node_id_Pick_node  = srh->node_id;
				}
			}
			if(change ==1)
			{
				send_node_selection_pkt(node_id);
				change = 0 ;
			}
		}
		//QWE
		else if(srh->type_of_packet == 13)
		{
			//Receiving a Beacon(alive) Packet 
			if(get_node_status(node_id) == 2 && God::instance()->is_beacon_pkt_received_before(node_id) == 0)
			{
				//printf("Reveiving a Beacon Packet \n\n");
				//printf("Receiving node's id %d\n",node_id);
				//trace("S$hit %.5f _%s_ %s -> %s %s",
	      			//Scheduler::instance().clock(), net_id.dump(),
	      			//p.src.dump(), p.dest.dump(), p.route.dump());
				//printf("Sending Node id ::%d at time %.5f\n\n" , src_id , Scheduler::instance().clock());
				send_Beacon_pkt();
				God::instance()->beacon_pkt_have_been_received(node_id);
			}
			//else if(get_node_status(node_id) == 2)
			//{
				//printf("Reveiving a Beacon Packet \n\n");
				//printf("Receiving node's id %d\n",node_id);
				//printf("Sending Node id ::%d at time %.5f\n\n" , src_id , Scheduler::instance().clock());
			//}
			else if(God::instance()->is_sending_node_is_blocked(node_id ,src_id ))
			{
				//1913 if true means that node is blocked 
				printf("Node %d is blocked to node %d means node is not receving the packets from node %d\n\n" , src_id , node_id , src_id);
				fixing(node_id , src_id);
			}
			
		}
		else if(srh->type_of_packet == 14)
		{
			//2051
			if(srh->giving_authority_to_node == netid)
			{
				God::instance()->god_set_parent_id(netid ,src_id );// setting parent id 
				Change_Status(netid, 2); //Creating a Black Node 
				printf("Node %d is going to be created as a black node \n " , netid);
				send_Black_pkt();
			}

		}
		else if(srh->type_of_packet == 15)
		{
			//2062
			if(get_node_status(netid) == 2)
			{
				printf("Node %d will think about the pruning \n" , netid);
			}
		}
		else if(srh->type_of_packet == 16)
		{
			// if you are receiving this packet means that your neighbor black node made himself as a grey node and sent this packet 
			// so all receiving node whos parent_id == src_id will make itself as a white node and ask other's to cover them
			printf("Sending Id %d\t Receiving ID %d\n" , src_id , node_id);
			int link1 = God::instance()->get_receiving_link_down(src_id); //i think it's not needed at all and this is incorrect too so don't use that 
			int link2 = God::instance()->get_sending_link_down_status(node_id);
			if(God::instance()->get_parent_id(netid) == src_id)
			{
				printf("Received a Packet which says that Node id Leave node and going to make itslef as a Grey node src_id %d ::: netid :%d \n\n" , src_id ,node_id);
				send_reset_packet();
			}
			//else if(God::instance()->get_parent_id(netid) != src_id)
			//{
				
			//}
			
			else if((!(( link1 == 1 )&& (link2 == 1 ))) && (God::instance()->get_parent_id(netid) != src_id))
			{
				//printf("Link is Broken Between node %d and %d" ,node_id , src_id );
				//going to increse the y_ value and foing to add that node in that list 
				God::instance()->add_node_in_y_(node_id , src_id);
				// Startfromhere :)
				// 31 should not able to start the timer
				// but 34 should able to start the timer  
				
			}
			
		}
		// this is a Pruning Process 
		else if(srh->type_of_packet == 17)
		{
			//2222
			//node_id for receiving node 
			// src_id for sending in 
			//if(God::instance()->get_cost(node_id) < srh->cost )
			// check whether they are  black or not 
			if(God::instance()->get_coming_from(node_id) == 1000)
			{
				//God::instance()->set_cost(node_id)
				God::instance()->set_coming_from(node_id , src_id);
				int n_d = 0 ;
				if(( n_d = God::instance()->if_node_is_in_2_hop(node_id , srh->problem_node)) != 1000 )
				{
					printf("Case !! 2.2\n");
					// here we have to think :)
					printf("Node Problem_node %d is connected to node %d via node %d\n" ,srh->problem_node ,node_id  ,n_d );
					God::instance()->set_cost(node_id);
					God::instance()->set_pkt_id(node_id , srh->pkt_id);
					God::instance()->set_cost(node_id);
					God::instance()->set_forward_node(node_id , n_d);
					send_route_rep_pkt(srh->reponsible_node ,srh->problem_node );
					
				}
				else
				{
					God::instance()->set_cost(node_id);
					//God::instance()->set_coming_from(node_id ,srh_id);			
					God::instance()->set_pkt_id(node_id , srh->pkt_id);
					send_route_find_pkt(srh->reponsible_node ,srh->problem_node );
					// sending the route find packet 
					//srh->problem_node = problem_node ;
					//srh->reponsible_node= responsible_node_for_fixing;
				}
				
			}
		}
		else if(srh->type_of_packet == 18)
		{		
			if(srh->reponsible_node != node_id)
			{
				God::instance()->set_super_cost(node_id , srh->cost);
				God::instance()->set_forward_node(node_id , src_id);
				send_route_rep_pkt(srh->reponsible_node ,srh->problem_node );				
			}
			else if(srh->reponsible_node == node_id)
			{
				if(God::instance()->get_cost(node_id) < srh->cost) 
				{
					printf("Previous Route is better ");
				}
				else if(God::instance()->get_cost(node_id) > srh->cost)
				{
					printf("New Route is better ");
					God::instance()->set_forward_node(node_id , src_id);
				}
			}
		}
		else if(srh->type_of_packet == 19)
		{
			int node = 0 ;
			if(get_node_status(node_id) == 1)
			{
				
				printf("Node %d is Receiving send_child_discover_pkt by node id %d \n" , node_id , src_id);
				if(God::instance()->get_parent_id(node_id) == src_id)
				{
					printf("Receivingnode id Node Id :: %d Node \n" , node_id);
					
					node = God::instance()->is_that_node_have_any_other_black_node(node_id , src_id);
					printf("!!!Node :::: %d\n" , node);
					if(node == 1000)
					{
						//means node does not have any other black node in his neighbor
					
						send_NAC(src_id);
					}
					else 
					{
						God::instance()->god_set_parent_id(node_id,node );					
					}
					send_live_pkt(src_id);
				}	
			}
		}
		else if(srh->type_of_packet == 20)
		{
			if(srh->rcv_node == node_id)
			{
				God::instance()->set_prune_val(node_id); /// setting NAC value at Receiver 
			}
		}
		else if(srh->type_of_packet == 21)
		{
			if(srh->rcv_node == node_id)
			{
				if(God::instance()->get_rcv_pkt(node_id) == 0)
				{
					God::instance()->set_rcv_pkt(node_id);
					start_child_reply_timer();
				}
			}
		}
			
			
		
		
//Increamenting a Counter 
		else if(God::instance()->get_parent_id(netid) == src_id )
			{
				God::instance()->increament_counter(node_id);	
			}
	}

		}
//}

	if((srh->type_of_packet == 0) || (srh->type_of_packet == 1))
	{	
		//printf("Case :: 0.1\n");
		God::instance()->reduce_this_node_only(node_id , src_id);
	}



//Arun ==============================================================================================



  assert(cmh->size() >= 0);

 // SRPacket p(packet, srh);
  //p.dest = ID(iph->dst(),::IP);
  //p.src = ID(iph->src(),::IP);
//  p.dest = ID((Address::instance().get_nodeaddr(iph->daddr())),::IP);
 // p.src = ID((Address::instance().get_nodeaddr(iph->saddr())),::IP);

  assert(logtarget != 0);

  if (srh->valid() != 1) {
    unsigned int dst = cmh->next_hop();
    if (dst == IP_BROADCAST) {
      // extensions for mobileIP --Padma, 04/99.
      // Brdcast pkt - treat differently
      if (p.src == net_id)
	// I have originated this pkt
	sendOutBCastPkt(packet);
      else 
	//hand it over to port-dmux
	port_dmux_->recv(packet, (Handler*)0);
      
    } else {
      // this must be an outgoing packet, it doesn't have a SR header on it
      
      srh->init();		 // give packet an SR header now
      cmh->size() += IP_HDR_LEN; // add on IP header size
      if (verbose)
	trace("S %.9f _%s_ originating %s -> %s",
	      Scheduler::instance().clock(), net_id.dump(), p.src.dump(), 
	      p.dest.dump());
      handlePktWithoutSR(p, false);
      goto done;
    }
  }
  else if (srh->valid() == 1) 
    {
      if (p.dest == net_id || p.dest == IP_broadcast)
	{ // this packet is intended for us
		//printf("we are here too\n");
	  handlePacketReceipt(p);
	  goto done;
	}
      
      // should we check to see if it's an error packet we're handling
      // and if so call processBrokenRouteError to snoop
      if (dsragent_snoop_forwarded_errors && srh->route_error())
	{
	  processBrokenRouteError(p);
	}

      if (srh->route_request())
	{ // propagate a route_request that's not for us
	  handleRouteRequest(p);
	}
      else
	{ // we're not the intended final recpt, but we're a hop
	  handleForwarding(p);
	}
    }
  else {
    // some invalid pkt has reached here
    fprintf(stderr,"dsragent: Error-received Invalid pkt!\n");
    Packet::free(p.pkt);
    p.pkt =0; // drop silently
  }

 done:
  assert(p.pkt == 0);
  
  p.pkt = 0;
  return;
}


/*===========================================================================
  handlers for each class of packet
---------------------------------------------------------------------------*/
void
DSRAgent::handlePktWithoutSR(SRPacket& p, bool retry)
  /* obtain a source route to p's destination and send it off.
     this should be a retry if the packet is already in the sendbuffer */
{
  assert(HDR_SR (p.pkt)->valid());

  if (p.dest == net_id)
    { // it doesn't need a source route, 'cause it's for us
      handlePacketReceipt(p);
      return;
    }

  // Extensions for wired cum wireless simulation mode
  //if pkt dst outside my subnet, route to base_stn

  ID dest;
  if (diff_subnet(p.dest,net_id)) {
  dest = ID(node_->base_stn(),::IP);
  p.dest = dest;
  }

  if (route_cache->findRoute(p.dest, p.route, 1))
    { // we've got a route...
      if (verbose)
	trace("S$hit %.5f _%s_ %s -> %s %s",
	      Scheduler::instance().clock(), net_id.dump(),
	      p.src.dump(), p.dest.dump(), p.route.dump());      
      sendOutPacketWithRoute(p, true);
      return;
    } // end if we have a route
  else
    { // we don't have a route...
      if (verbose) 
	trace("S$miss %.5f _%s_ %s -> %s", 
	      Scheduler::instance().clock(), net_id.dump(), 
	      net_id.dump(), p.dest.dump());

      getRouteForPacket(p, retry);
      return;
    } // end of we don't have a route
}

void
DSRAgent::handlePacketReceipt(SRPacket& p)
  /* Handle a packet destined to us */
{
  hdr_cmn *cmh =  hdr_cmn::access(p.pkt);
  hdr_sr *srh =  hdr_sr::access(p.pkt);

  if (srh->route_reply())
    { // we got a route_reply piggybacked on a route_request
      // accept the new source route before we do anything else
      // (we'll send off any packet's we have queued and waiting)
      acceptRouteReply(p);
    }
  
  if (srh->route_request())
    {
      if (dsragent_reply_only_to_first_rtreq  && ignoreRouteRequestp(p)) 
	{ //we only respond to the first route request
	  // we receive from a host 
	  Packet::free(p.pkt);     // drop silently
	  p.pkt = 0;
	  return;
	}
      else
	{ // we're going to process this request now, so record the req_num
	  request_table.insert(p.src, p.src, srh->rtreq_seq());
	  returnSrcRouteToRequestor(p);
	}
    }

  if (srh->route_error())
    { // register the dead route      
      processBrokenRouteError(p);
    }

  if (srh->flow_unknown())
    processUnknownFlowError(p, false);

  if (srh->flow_default_unknown())
    processUnknownFlowError(p, true);

  /* give the data in the packet to our higher layer (our port dmuxer, most 
   likely) */
  //handPktToDmux(p);
  assert(p.dest == net_id || p.dest == MAC_id);
  
#if 0
  if (iph->dport() == 255) {
    int mask = Address::instance().portmask();
    int shift = Address::instance().portshift();  
    iph->daddr() = ((iph->dport() & mask) << shift) | ((~(mask) << shift) & iph->dst());
  }
#endif
  
  cmh->size() -= srh->size();	// cut off the SR header 4/7/99 -dam
  srh->valid() = 0;
  cmh->size() -= IP_HDR_LEN;    // cut off IP header size 4/7/99 -dam
  target_->recv(p.pkt, (Handler*)0);
  p.pkt = 0;

}


void
DSRAgent::handleDefaultForwarding(SRPacket &p) {
  hdr_ip *iph = hdr_ip::access(p.pkt);
  u_int16_t flowid;
  int       flowidx;

  if (!flow_table.defaultFlow(p.src.addr, p.dest.addr, flowid)) {
    sendUnknownFlow(p, true);
    assert(p.pkt == 0);
    return;
  }

  if ((flowidx = flow_table.find(p.src.addr, p.dest.addr, flowid)) == -1) {
    sendUnknownFlow(p, false, flowid);
    assert(p.pkt == 0);
    return;
  }

  if (iph->ttl() != flow_table[flowidx].expectedTTL) {
    sendUnknownFlow(p, true);
    assert(p.pkt == 0);
    return;
  }

  // XXX should also check prevhop

  handleFlowForwarding(p, flowidx);
}

void
DSRAgent::handleFlowForwarding(SRPacket &p, int flowidx) {
  hdr_sr *srh = hdr_sr::access(p.pkt);
  hdr_ip *iph = hdr_ip::access(p.pkt);
  hdr_cmn *cmnh =  hdr_cmn::access(p.pkt);
  int amt;

  assert(flowidx >= 0);
  assert(!srh->num_addrs());

  cmnh->next_hop() = flow_table[flowidx].nextHop;
  cmnh->addr_type() = ::IP;

  cmnh->xmit_failure_ = XmitFlowFailureCallback;
  cmnh->xmit_failure_data_ = (void *) this;

  // make sure we aren't cycling packets
  //assert(p.pkt->incoming == 0); // this is an outgoing packet
  assert(cmnh->direction() == hdr_cmn::UP);

  if (!iph->ttl()--) {
    drop(p.pkt, DROP_RTR_TTL);
    p.pkt = 0;
    return;
  }

  trace("SFf %.9f _%s_ %d [%s -> %s] %d to %d", 
	Scheduler::instance().clock(), net_id.dump(), cmnh->uid(),
	p.src.dump(), p.dest.dump(), flow_table[flowidx].flowId,
	flow_table[flowidx].nextHop);

  // XXX ych 5/8/01 ARS also should check previous hop
  if (!srh->salvaged() && 
      (amt = ars_table.findAndClear(cmnh->uid(), flow_table[flowidx].flowId)) &&
      p.route.index() - amt > 0) {
    trace("SFARS %.9f _%s_ %d [%s -> %s] %d %d", 
	  Scheduler::instance().clock(), net_id.dump(), cmnh->uid(),
	  p.src.dump(), p.dest.dump(), flow_table[flowidx].flowId, amt);

    // stamp a route in the packet...
    p.route = flow_table[flowidx].sourceRoute;
    p.route.index() -= amt;
    sendRouteShortening(p, p.route.index(), 
			flow_table[flowidx].sourceRoute.index());
  }

  if (dsragent_always_reestablish) {
    // XXX this is an utter hack. the flow_table needs to remember the original
    // timeout value specified, as well as the original time to timeout. No
    // establishment packets are allowed after the original time. Must make sure
    // flowids assigned do not overlap. ych 5/8/01
    flow_table[flowidx].timeout = Scheduler::instance().clock() + 
				  default_flow_timeout;
  }
  // set the direction pkt to be down
  cmnh->direction() = hdr_cmn::DOWN;
  Scheduler::instance().schedule(ll, p.pkt, 0);
  p.pkt = 0;
}

void
DSRAgent::handleFlowForwarding(SRPacket &p) {
  hdr_sr *srh = hdr_sr::access(p.pkt);
  hdr_ip *iph = hdr_ip::access(p.pkt);
  int flowidx = flow_table.find(p.src.addr, p.dest.addr, srh->flow_id());

  assert(srh->flow_header());

  if (srh->num_addrs()) {
    assert(srh->flow_timeout());

    if (flowidx == -1) {
      flow_table.cleanup();
      flowidx = flow_table.createEntry(p.src.addr, p.dest.addr, srh->flow_id());

      assert(flowidx != -1);

      flow_table[flowidx].timeout = Scheduler::instance().clock() + 
				    srh->flow_timeout_time();
      flow_table[flowidx].hopCount = srh->hopCount();
      flow_table[flowidx].expectedTTL = iph->ttl();
      flow_table[flowidx].sourceRoute = p.route;
      flow_table[flowidx].nextHop = srh->get_next_addr();
      assert(srh->hopCount() == srh->cur_addr());
      assert(srh->get_next_type() == ::IP);
      assert(flow_table[flowidx].sourceRoute[flow_table[flowidx].hopCount] == 
	     net_id);

      flow_table[flowidx].count = 0;            // shouldn't be used
      flow_table[flowidx].allowDefault = false; // shouldn't be used
    }

    assert(flowidx != -1);
    //assert(flow_table[flowidx].hopCount == srh->hopCount());
    
    srh->hopCount()++;
    return;
  }

  if (flowidx == -1) {
    // return an error
    sendUnknownFlow(p, false, srh->flow_id());
    assert(p.pkt == 0);
    return;
  }

  //assert(flow_table[flowidx].hopCount == srh->hopCount());

  srh->hopCount()++;

  // forward the packet
  handleFlowForwarding(p, flowidx);
}

void
DSRAgent::handleForwarding(SRPacket &p)
  /* forward packet on to next host in source route,
   snooping as appropriate */
{
  hdr_sr *srh =  hdr_sr::access(p.pkt);
  hdr_ip *iph = hdr_ip::access(p.pkt);
  hdr_cmn *ch =  hdr_cmn::access(p.pkt);
  bool flowOnly = !srh->num_addrs();

  if (srh->flow_header())
    handleFlowForwarding(p);
  else if (!srh->num_addrs())
    handleDefaultForwarding(p);

  if (flowOnly)
    return;

  assert(p.pkt); // make sure flow state didn't eat the pkt

  // first make sure we are the ``current'' host along the source route.
  // if we're not, the previous node set up the source route incorrectly.
  assert(p.route[p.route.index()] == net_id
	 || p.route[p.route.index()] == MAC_id);

  if (p.route.index() >= p.route.length())
    {
      fprintf(stderr,"dfu: ran off the end of a source route\n");
      trace("SDFU:  ran off the end of a source route\n");
      drop(p.pkt, DROP_RTR_ROUTE_LOOP);
      p.pkt = 0;
      // maybe we should send this packet back as an error...
      return;
    }

  // if there's a source route, maybe we should snoop it too
  if (dsragent_snoop_source_routes)
    route_cache->noticeRouteUsed(p.route, Scheduler::instance().clock(), 
				 net_id);

  // sendOutPacketWithRoute will add in the size of the src hdr, so
  // we have to subtract it out here
  ch->size() -= srh->size();

  // we need to manually decr this, since nothing else does.
  if (!iph->ttl()--) {
    drop(p.pkt, DROP_RTR_TTL);
    p.pkt = 0;
    return;
  }

  // now forward the packet...
  sendOutPacketWithRoute(p, false);
}

void
DSRAgent::handleRouteRequest(SRPacket &p)
  /* process a route request that isn't targeted at us */
{
  hdr_sr *srh =  hdr_sr::access(p.pkt);
  assert (srh->route_request());

#ifdef notdef
  {
          int src = mac_->hdr_src(HDR_MAC(p.pkt));

          if(mac_->is_neighbor(src) == 0) {
                  Packet::free(p.pkt);
                  p.pkt = 0;
                  return;
          }
  }
#endif

  if (ignoreRouteRequestp(p)) 
    {
      if (verbose_srr) 
        trace("SRR %.5f _%s_ dropped %s #%d (ignored)",
              Scheduler::instance().clock(), net_id.dump(), p.src.dump(),
              srh->rtreq_seq());
      Packet::free(p.pkt);  // pkt is a route request we've already processed
      p.pkt = 0;
      return; // drop silently
    }

  // we're going to process this request now, so record the req_num
  request_table.insert(p.src, p.src, srh->rtreq_seq());

  /*  - if it's a Ring 0 search, check the rt$ for a reply and use it if
     possible.  There's not much point in doing Ring 0 search if you're 
     not going to check the cache.  See the comment about turning off all
     reply from cache behavior near the definition of d_r_f_c_o_p (if your 
     workload had really good spatial locality, it might still make 
     sense 'cause your target is probably sitting next to you)
      - if reply from cache is on, check the cache and reply if possible
      - otherwise, just propagate if possible. */
  if ((srh->max_propagation() == 0 || dsragent_reply_from_cache_on_propagating)
      && replyFromRouteCache(p))
	  return;			// all done

#ifdef NEW_REQUEST_LOGIC
  /*
   * If we are congested, don't forward or answer the Route Reply
   */
  if(ifq->prq_length() > 10) {
	  trace("SRR %.9f _%s_ discarding %s #%d (ifq length %d)",
		Scheduler::instance().clock(),
		net_id.dump(),
		p.src.dump(),
		srh->rtreq_seq(),
		ifq->prq_length());
	  Packet::free(p.pkt);
	  p.pkt = 0;
	  return;
  }

  /*
   *  If "free air time" < 15%, don't forward or answer the Route Reply
   */
  {
	  double atime = mac_->air_time_free(10);

	  if(atime > 0.0 && atime < 0.15) {
		  trace("SRR %.9f _%s_ discarding %s #%d (free air time %f)",
			Scheduler::instance().clock(),
			net_id.dump(),
			p.src.dump(),
			srh->rtreq_seq(),
			atime);
		  Packet::free(p.pkt);
		  p.pkt = 0;
		  return;
	  }
  }  
#endif /* NEW_REQUEST_LOGIC */

  // does the orginator want us to propagate?
  if (p.route.length() > srh->max_propagation())
    { 	// no propagation
      if (verbose_srr) 
        trace("SRR %.5f _%s_ dropped %s #%d (prop limit exceeded)",
              Scheduler::instance().clock(), net_id.dump(), p.src.dump(),
              srh->rtreq_seq());
      Packet::free(p.pkt); // pkt isn't for us, and isn't data carrying
      p.pkt = 0;
      return;		
    }

  // can we propagate?
  if (p.route.full())
    { 	// no propagation
      trace("SRR %.5f _%s_ dropped %s #%d (SR full)",
            Scheduler::instance().clock(), net_id.dump(), p.src.dump(),
	    srh->rtreq_seq());
      /* pkt is a rt req, even if data carrying, we don't want to log 
	 the drop using drop() since many nodes could be dropping the 
	 packet in this fashion */
      Packet::free(p.pkt);
      p.pkt = 0;
      return;		
    }

  // add ourselves to the source route
  p.route.appendToPath(net_id);

  if (verbose_srr)
    trace("SRR %.5f _%s_ rebroadcast %s #%d ->%s %s",
          Scheduler::instance().clock(), net_id.dump(), p.src.dump(),
          srh->rtreq_seq(), p.dest.dump(), p.route.dump());

  sendOutPacketWithRoute(p, false);
  return;      
}

/*===========================================================================
  Helpers
---------------------------------------------------------------------------*/
bool
DSRAgent::ignoreRouteRequestp(SRPacket &p)
// should we ignore this route request?
{
  hdr_sr *srh =  hdr_sr::access(p.pkt);

  if (request_table.get(p.src) >= srh->rtreq_seq())
    { // we've already processed a copy of this reqest so
      // we should drop the request silently
      return true;
    }
  if (p.route.member(net_id,MAC_id))
    { // we're already on the route, drop silently
      return true;
    }

  if (p.route.full())
    { // there won't be room for us to put our address into
      // the route
      // so drop silently - sigh, so close, and yet so far...
      // Note that since we don't record the req_id of this message yet,
      // we'll process the request if it gets to us on a shorter path
      return true;
    }
  return false;
}


bool
DSRAgent::replyFromRouteCache(SRPacket &p)
  /* - see if can reply to this route request from our cache
     if so, do it and return true, otherwise, return false 
     - frees or hands off p iff returns true */
{
  Path rest_of_route;
  Path complete_route = p.route;

  /* we shouldn't yet be on on the pkt's current source route */
  assert(!p.route.member(net_id, MAC_id));

  // do we have a cached route the target?
  /* XXX what if we have more than 1?  (and one is legal for reply from
     cache and one isn't?) 1/28/97 -dam */
  if (!route_cache->findRoute(p.dest, rest_of_route, 0))
    { // no route => we're done
      return false;
    }

  /* but we should be on on the remainder of the route (and should be at
   the start of the route */
  assert(rest_of_route[0] == net_id || rest_of_route[0] == MAC_id);

  if (rest_of_route.length() + p.route.length() > MAX_SR_LEN)
    return false; // too long to work with...

  // add our suggested completion to the route so far
  complete_route.appendPath(rest_of_route);

  // call compressPath to remove any double backs
  ::compressPath(complete_route);

  if (!complete_route.member(net_id, MAC_id))
    { // we're not on the suggested route, so we can't return it
      return false;
    }

  // if there is any other information piggybacked into the
  // route request pkt, we need to forward it on to the dst
  hdr_cmn *cmh =  hdr_cmn::access(p.pkt);
  hdr_sr *srh =  hdr_sr::access(p.pkt);
  int request_seqnum = srh->rtreq_seq();
  
  if (PT_DSR != cmh->ptype()	// there's data
      || srh->route_reply()
      || (srh->route_error() && 
	  srh->down_links()[srh->num_route_errors()-1].tell_addr 
	  != GRAT_ROUTE_ERROR))
    { // must forward the packet on
      SRPacket p_copy = p;
      p.pkt = 0;
      srh->route_request() = 0;

      p_copy.route = complete_route;
      p_copy.route.setIterator(p.route.length());

      assert(p.route[p.route.index()] == net_id);
      
      if (verbose) trace("Sdebug %.9f _%s_ splitting %s to %s",
                         Scheduler::instance().clock(), net_id.dump(),
                         p.route.dump(), p_copy.route.dump());

      sendOutPacketWithRoute(p_copy,false);
    }
  else 
    {
      Packet::free(p.pkt);  // free the rcvd rt req before making rt reply
      p.pkt = 0;
    }

  // make up and send out a route reply
  p.route.appendToPath(net_id);
  p.route.reverseInPlace();
  route_cache->addRoute(p.route, Scheduler::instance().clock(), net_id);
  p.dest = p.src;
  p.src = net_id;
  p.pkt = allocpkt();

  hdr_ip *iph =  hdr_ip::access(p.pkt);
  iph->saddr() = Address::instance().create_ipaddr(p.src.addr, RT_PORT);
  iph->sport() = RT_PORT;
  iph->daddr() = Address::instance().create_ipaddr(p.dest.addr, RT_PORT);
  iph->dport() = RT_PORT;
  iph->ttl() = 255;

  srh = hdr_sr::access(p.pkt);
  srh->init();
  for (int i = 0 ; i < complete_route.length() ; i++)
    complete_route[i].fillSRAddr(srh->reply_addrs()[i]);
  srh->route_reply_len() = complete_route.length();
  srh->route_reply() = 1;

  // propagate the request sequence number in the reply for analysis purposes
  srh->rtreq_seq() = request_seqnum;

  hdr_cmn *cmnh =  hdr_cmn::access(p.pkt);
  cmnh->ptype() = PT_DSR;
  cmnh->size() = IP_HDR_LEN;

  if (verbose_srr)
    trace("SRR %.9f _%s_ cache-reply-sent %s -> %s #%d (len %d) %s",
	  Scheduler::instance().clock(), net_id.dump(),
	  p.src.dump(), p.dest.dump(), request_seqnum, complete_route.length(),
	  complete_route.dump());
  sendOutPacketWithRoute(p, true);
  return true;
}


void
DSRAgent::sendOutPacketWithRoute(SRPacket& p, bool fresh, Time delay)
     // take packet and send it out, packet must a have a route in it
     // return value is not very meaningful
     // if fresh is true then reset the path before using it, if fresh
     //  is false then our caller wants us use a path with the index
     //  set as it currently is
{
if(God::instance()->get_node_status_rc(net_id.getNSAddr_t()) != 1)
{
//printf("Node is node is sendiing the packet !! %d!!\n" ,net_id.getNSAddr_t() );
  hdr_sr *srh =  hdr_sr::access(p.pkt);
  hdr_cmn *cmnh = hdr_cmn::access(p.pkt);

  assert(srh->valid());
  assert(cmnh->size() > 0);

  ID dest;
  if (diff_subnet(p.dest,net_id)) {
  dest = ID(node_->base_stn(),::IP);
  p.dest = dest;
  }

  if (p.dest == net_id)
    { // it doesn't need to go on the wire, 'cause it's for us
      recv(p.pkt, (Handler *) 0);
      p.pkt = 0;
      return;
    }

  if (fresh)
    {
      p.route.resetIterator();
      if (verbose && !srh->route_request())
	{
	  trace("SO %.9f _%s_ originating %s %s", 
		Scheduler::instance().clock(), 
		net_id.dump(), packet_info.name(cmnh->ptype()), p.route.dump());
	}
    }

  p.route.fillSR(srh);

  // set direction of pkt to DOWN , i.e downward
  cmnh->direction() = hdr_cmn::DOWN;

  // let's see if we can snag this packet for flow state... ych 5/2/01
  if (dsragent_enable_flowstate &&
      p.src == net_id && !srh->route_request() && !srh->cur_addr() &&
      // can't yet decode flow errors and route errors/replies together
      // so don't tempt the system... ych 5/7/01
      !srh->route_error() && !srh->route_reply()) {
    hdr_ip *iph =  hdr_ip::access(p.pkt);
    int flowidx;
    u_int16_t flowid, default_flowid;
    double now = Scheduler::instance().clock();

    // hmmm, let's see if we can save us some overhead...
    if (dsragent_prefer_default_flow &&
	flow_table.defaultFlow(p.src.addr, p.dest.addr, flowid) &&
	-1 != (flowidx = flow_table.find(p.src.addr, p.dest.addr, flowid)) &&
	flow_table[flowidx].timeout >= now &&
	(!dsragent_prefer_shorter_over_default || 
	  flow_table[flowidx].sourceRoute.length() <= p.route.length()) &&
	!(p.route == flow_table[flowidx].sourceRoute)) {

      p.route = flow_table[flowidx].sourceRoute;
      p.route.fillSR(srh);
    }

    flowidx = flow_table.find(p.src.addr, p.dest.addr, p.route);

    if (flowidx == -1 || flow_table[flowidx].timeout < now) {
      // I guess we don't know about this flow; allocate it.
      flow_table.cleanup();
      flowid = flow_table.generateNextFlowId(p.dest.addr, true);
      flowidx = flow_table.createEntry(p.src.addr, p.dest.addr, flowid);
      assert(flowidx != -1);

      // fill out the table
      flow_table[flowidx].count = 1;
      flow_table[flowidx].lastAdvRt = Scheduler::instance().clock();
      flow_table[flowidx].timeout = now + default_flow_timeout;
      flow_table[flowidx].hopCount = 0;
      flow_table[flowidx].expectedTTL = iph->ttl();
      flow_table[flowidx].allowDefault = true;
      flow_table[flowidx].sourceRoute = p.route;
      flow_table[flowidx].nextHop = srh->get_next_addr();
      assert(srh->get_next_type() == ::IP);

      // fix up the srh for the timeout
      srh->flow_timeout() = 1;
      srh->flow_timeout_time() = default_flow_timeout;
      srh->cur_addr() = srh->cur_addr() + 1;
    } else if (flow_table[flowidx].count <= END_TO_END_COUNT ||
		flow_table[flowidx].lastAdvRt < 
		   (Scheduler::instance().clock() - min_adv_interval)) {
      // I've got it, but maybe someone else doesn't
      if (flow_table[flowidx].expectedTTL != iph->ttl())
	flow_table[flowidx].allowDefault = false;

      flow_table[flowidx].count++;
      flow_table[flowidx].lastAdvRt = Scheduler::instance().clock();

      srh->flow_timeout() = 1;
      if (dsragent_always_reestablish)
	srh->flow_timeout_time() = default_flow_timeout;
      else
	srh->flow_timeout_time() = (int)(flow_table[flowidx].timeout - now);
      srh->cur_addr() = srh->cur_addr() + 1;
    } else {
      // flow is established end to end
      assert (flow_table[flowidx].sourceRoute == p.route);
      srh->flow_timeout() = srh->cur_addr() = srh->num_addrs() = 0;
    }

    if (dsragent_always_reestablish) {
      // XXX see major problems detailed above (search for dsragent_always_re..)
      flow_table[flowidx].timeout = now + default_flow_timeout;
    }

    cmnh->next_hop() = flow_table[flowidx].nextHop;
    cmnh->addr_type() = ::IP;

    if (flow_table.defaultFlow(p.src.addr, p.dest.addr, default_flowid) &&
	flow_table[flowidx].flowId == default_flowid &&
	!srh->num_addrs() && iph->ttl() == flow_table[flowidx].expectedTTL &&
	flow_table[flowidx].allowDefault) {
      // we can go without anything... woo hoo!
      assert(!srh->flow_header());
    } else {
      srh->flow_header() = 1;
      srh->flow_id() = flow_table[flowidx].flowId;
      srh->hopCount() = 1;
    }

    trace("SF%ss %.9f _%s_ %d [%s -> %s] %d(%d) to %d %s", 
	srh->num_addrs() ? "EST" : "",
	Scheduler::instance().clock(), net_id.dump(), cmnh->uid(),
	p.src.dump(), p.dest.dump(), flow_table[flowidx].flowId,
	srh->flow_header(), flow_table[flowidx].nextHop,
	srh->num_addrs() ? srh->dump() : "");

    cmnh->size() += srh->size();
    cmnh->xmit_failure_ = srh->num_addrs() ? XmitFailureCallback : 
					     XmitFlowFailureCallback;
    cmnh->xmit_failure_data_ = (void *) this;

    assert(!srh->num_addrs() || srh->flow_timeout());
  } else {
    // old non-flowstate stuff...
    assert(p.src != net_id || !srh->flow_header());
    cmnh->size() += srh->size();

    if (srh->route_request())
      { // broadcast forward
        cmnh->xmit_failure_ = 0;
        cmnh->next_hop() = MAC_BROADCAST;
        cmnh->addr_type() = NS_AF_ILINK;
      }
    else
      { // forward according to source route
        cmnh->xmit_failure_ = XmitFailureCallback;
        cmnh->xmit_failure_data_ = (void *) this;

        cmnh->next_hop() = srh->get_next_addr();
        cmnh->addr_type() = srh->get_next_type();
        srh->cur_addr() = srh->cur_addr() + 1;
      } /* route_request() */
  } /* can snag for path state */

  /* put route errors at the head of the ifq somehow? -dam 4/13/98 */

  // make sure we aren't cycling packets
  
#ifdef notdef
  if (ifq->prq_length() > 25)
	  trace("SIFQ %.5f _%s_ len %d",
		Scheduler::instance().clock(),
		net_id.dump(), ifq->prq_length());
#endif
#ifdef NEW_IFQ_LOGIC
  /*
   *  If the interface queue is full, there's no sense in sending
   *  the packet.  Drop it and generate a Route Error?
   */
  /* question for the author: this seems rife with congestion/infinite loop
   * possibilities. you're responding to an ifq full by sending a rt err.
   * sounds like the source quench problem. ych 5/5/01
   */
  if(ifq->prq_isfull(p.pkt)) {
	  xmitFailed(p.pkt, DROP_IFQ_QFULL);
	  p.pkt = 0;
	  return;
  }
#endif /* NEW_IFQ_LOGIC */

  // ych debugging
  assert(!srh->flow_header() || !srh->num_addrs() || srh->flow_timeout());

  // off it goes!
  if (srh->route_request())
    { // route requests need to be jittered a bit
      Scheduler::instance().schedule(ll, p.pkt, 
				     Random::uniform(RREQ_JITTER) + delay);
    }
  else
    { // no jitter required 
      Scheduler::instance().schedule(ll, p.pkt, delay);
    }
  p.pkt = NULL; /* packet sent off */
}
else
  printf("\n\n %d\n",net_id.getNSAddr_t());
}

void
DSRAgent::getRouteForPacket(SRPacket &p, bool retry)
  /* try to obtain a route for packet
     pkt is freed or handed off as needed, unless retry == true
     in which case it is not touched */
{
  // since we'll commonly be only one hop away, we should
  // arp first before route discovery as an optimization...

  Entry *e = request_table.getEntry(p.dest);
  Time time = Scheduler::instance().clock();

#if 0
  /* pre 4/13/98 logic -dam removed b/c it seemed more complicated than
     needed since we're not doing piggybacking and we're returning
     route replies via a reversed route (the copy in this code is
     critical if we need to piggyback route replies on the route request to
     discover the return path) */

  /* make the route request packet */
  SRPacket rrp = p;
  rrp.pkt = p.pkt->copy();
  hdr_sr *srh = hdr_sr::access(rrp.pkt);
  hdr_ip *iph = hdr_ip::access(rrp.pkt);
  hdr_cmn *cmh =  hdr_cmn::access(rrp.pkt);
  //iph->daddr() = p.dest.getNSAddr_t();
  iph->daddr() = Address::instance().create_ipaddr(p.dest.getNSAddr_t(),RT_PORT);
  iph->dport() = RT_PORT;
  //iph->saddr() = net_id.getNSAddr_t();
  iph->saddr() = Address::instance().create_ipaddr(net_id.getNSAddr_t(),RT_PORT);
  iph->sport() = RT_PORT;
  cmnh->ptype() = PT_DSR;
  cmnh->size() = size_;
  cmnh->num_forwards() = 0;
#endif

  /* make the route request packet */
  SRPacket rrp;
  rrp.dest = p.dest;
  rrp.src = net_id;
  rrp.pkt = allocpkt();

  hdr_sr *srh = hdr_sr::access(rrp.pkt); 
  hdr_ip *iph = hdr_ip::access(rrp.pkt);
  hdr_cmn *cmnh =  hdr_cmn::access(rrp.pkt);
  
  iph->daddr() = Address::instance().create_ipaddr(p.dest.getNSAddr_t(),RT_PORT);
  iph->dport() = RT_PORT;
  iph->saddr() = Address::instance().create_ipaddr(net_id.getNSAddr_t(),RT_PORT);
  iph->sport() = RT_PORT;
  cmnh->ptype() = PT_DSR;
  cmnh->size() = size_ + IP_HDR_LEN; // add in IP header
  cmnh->num_forwards() = 0;
  
  srh->init();


  if (BackOffTest(e, time)) {
	  // it's time to start another route request cycle

#ifdef NEW_SALVAGE_LOGIC
	  if(p.src != net_id) {

		  assert(dsr_salvage_max_requests > 0);
		  assert(p.pkt);

		  if(e->rt_reqs_outstanding > dsr_salvage_max_requests) {
			  drop(p.pkt, DROP_RTR_NO_ROUTE);
			  p.pkt = 0;

			  // dump the route request packet we made up
			  Packet::free(rrp.pkt);
			  rrp.pkt = 0;

			  return;
		  }
	  }
#endif /* NEW_SALVAGE_LOGIC */

	  if (dsragent_ring_zero_search) {
		  // do a ring zero search
		  e->last_type = LIMIT0;
		  sendOutRtReq(rrp, 0);
	  } else {
		  // do a propagating route request right now
		  e->last_type = UNLIMIT;
		  sendOutRtReq(rrp, MAX_SR_LEN);
	  }

	  e->last_arp = time;
  }  else if (LIMIT0 == e->last_type &&
#ifdef NEW_SALVAGE_LOGIC
	      (dsr_salvage_allow_propagating || p.src == net_id) &&
#endif
	   (time - e->last_arp) > arp_timeout) {
	  // try propagating rt req since we haven't heard back
	  // from limited one

	  e->last_type = UNLIMIT;
	  sendOutRtReq(rrp, MAX_SR_LEN);
  }
  else {
	  // it's not time to send another route request...
	  if (!retry && verbose_srr)
		  trace("SRR %.5f _%s_ RR-not-sent %s -> %s", 
			Scheduler::instance().clock(), 
			net_id.dump(), rrp.src.dump(), rrp.dest.dump());
	  Packet::free(rrp.pkt); // dump the route request packet we made up
	  rrp.pkt = 0;
  }

  /* for now, no piggybacking at all, queue all pkts */
  if (!retry) {
	  stickPacketInSendBuffer(p);
	  p.pkt = 0; // pkt is handled for now (it's in sendbuffer)
  }

}

void
DSRAgent::sendOutRtReq(SRPacket &p, int max_prop)
  // turn p into a route request and launch it, max_prop of request is
  // set as specified
  // p.pkt is freed or handed off
{
  hdr_sr *srh =  hdr_sr::access(p.pkt);
  assert(srh->valid());

  srh->route_request() = 1;
  srh->rtreq_seq() = route_request_num++;
  srh->max_propagation() = max_prop;
  p.route.reset();
  p.route.appendToPath(net_id);

  if (dsragent_propagate_last_error && route_error_held 
      && Scheduler::instance().clock() - route_error_data_time  < max_err_hold)
    {
      assert(srh->num_route_errors() < MAX_ROUTE_ERRORS);
      srh->route_error() = 1;
      link_down *deadlink = &(srh->down_links()[srh->num_route_errors()]);
      deadlink->addr_type = NS_AF_INET;
      deadlink->from_addr = err_from.getNSAddr_t();
      deadlink->to_addr = err_to.getNSAddr_t();
      deadlink->tell_addr = GRAT_ROUTE_ERROR;
      srh->num_route_errors() += 1;
      /*
       * Make sure that the Route Error gets on a propagating request.
       */
      if(max_prop > 0) route_error_held = false;
    }

  if (verbose_srr)
    trace("SRR %.5f _%s_ new-request %d %s #%d -> %s", 
	  Scheduler::instance().clock(), net_id.dump(), 
	  max_prop, p.src.dump(), srh->rtreq_seq(), p.dest.dump());
  sendOutPacketWithRoute(p, false);
}

void
DSRAgent::returnSrcRouteToRequestor(SRPacket &p)
  // take the route in p, add us to the end of it and return the
  // route to the sender of p
  // doesn't free p.pkt
{
  hdr_sr *old_srh = hdr_sr::access(p.pkt);

  if (p.route.full()) 
    return; // alas, the route would be to long once we add ourselves

  SRPacket p_copy = p;
  p_copy.pkt = allocpkt();
  p_copy.dest = p.src;
  p_copy.src = net_id;

  p_copy.route.appendToPath(net_id);

  hdr_ip *new_iph =  hdr_ip::access(p_copy.pkt);
  //new_iph->daddr() = p_copy.dest.addr;
  new_iph->daddr() = Address::instance().create_ipaddr(p_copy.dest.getNSAddr_t(),RT_PORT);
  new_iph->dport() = RT_PORT;
  //new_iph->saddr() = p_copy.src.addr;
  new_iph->saddr() =
    Address::instance().create_ipaddr(p_copy.src.getNSAddr_t(),RT_PORT); 
  new_iph->sport() = RT_PORT;
  new_iph->ttl() = 255;

  hdr_sr *new_srh =  hdr_sr::access(p_copy.pkt);
  new_srh->init();
  for (int i = 0 ; i < p_copy.route.length() ; i++)
    p_copy.route[i].fillSRAddr(new_srh->reply_addrs()[i]);
  new_srh->route_reply_len() = p_copy.route.length();
  new_srh->route_reply() = 1;

  // propagate the request sequence number in the reply for analysis purposes
  new_srh->rtreq_seq() = old_srh->rtreq_seq();
  
  hdr_cmn *new_cmnh =  hdr_cmn::access(p_copy.pkt);
  new_cmnh->ptype() = PT_DSR;
  new_cmnh->size() = IP_HDR_LEN;

  if (verbose_srr)
    trace("SRR %.9f _%s_ reply-sent %s -> %s #%d (len %d) %s",
	  Scheduler::instance().clock(), net_id.dump(),
	  p_copy.src.dump(), p_copy.dest.dump(), old_srh->rtreq_seq(),
	  p_copy.route.length(), p_copy.route.dump());

  // flip the route around for the return to the requestor, and 
  // cache the route for future use
  p_copy.route.reverseInPlace();
  route_cache->addRoute(p_copy.route, Scheduler::instance().clock(), net_id);

  p_copy.route.resetIterator();
  p_copy.route.fillSR(new_srh);
  new_cmnh->size() += new_srh->size();
  
  /* we now want to jitter when we first originate route replies, since
     they are a transmission we make in response to a broadcast packet 
     -dam 4/23/98
     sendOutPacketWithRoute(p_copy, true); */
  {
	  double d = Random::uniform(RREQ_JITTER);
#if 0
	  fprintf(stderr, "Random Delay: %f\n", d);
#endif
	  Scheduler::instance().schedule(this, p_copy.pkt, d);
  }
}

int
DSRAgent::diff_subnet(ID dest, ID myid) 
{
	int dst = dest.addr;
	int id = myid.addr;
	char* dstnet = Address::instance().get_subnetaddr(dst);
	char * subnet = Address::instance().get_subnetaddr(id);
	if (subnet != NULL) {
		if (dstnet != NULL) {
			if (strcmp(dstnet, subnet) != 0) {
				delete [] dstnet;
				return 1;
			}
			delete [] dstnet;
		}
		delete [] subnet;
	}
	assert(dstnet == NULL);
	return 0;
}


void
DSRAgent::acceptRouteReply(SRPacket &p)
  /* - enter the packet's source route into our cache
     - see if any packets are waiting to be sent out with this source route
     - doesn't free the pkt */
{
  hdr_sr *srh =  hdr_sr::access(p.pkt);
  Path reply_route(srh->reply_addrs(), srh->route_reply_len());

  if (!srh->route_reply())
    { // somethings wrong...
      trace("SDFU non route containing packet given to acceptRouteReply");
      fprintf(stderr,
	      "dfu: non route containing packet given to acceptRouteReply\n");
    }

  bool good_reply = true;  
  //#ifdef USE_GOD_FEEDBACK
  /* check to see if this reply is valid or not using god info */
  int i;
  
  for (i = 0; i < reply_route.length()-1 ; i++) 
    if (God::instance()->hops(reply_route[i].getNSAddr_t(), 
			      reply_route[i+1].getNSAddr_t()) != 1)
      {
	good_reply = false;
	break;
      }
  //#endif //GOD_FEEDBACK

  if (verbose_srr)
    trace("SRR %.9f _%s_ reply-received %d from %s  %s #%d -> %s %s",
	  Scheduler::instance().clock(), net_id.dump(),
	  good_reply ? 1 : 0,
	  p.src.dump(), reply_route[0].dump(), srh->rtreq_seq(),
	  reply_route[reply_route.length()-1].dump(),
	  reply_route.dump());

  // add the new route into our cache
  route_cache->addRoute(reply_route, Scheduler::instance().clock(), p.src);

  // back down the route request counters
  Entry *e = request_table.getEntry(reply_route[reply_route.length()-1]);
  e->rt_reqs_outstanding = 0;
  e->last_rt_req = 0.0;	 

  // see if the addtion of this route allows us to send out
  // any of the packets we have waiting
  Time delay = 0.0;
  ID dest;
  for (int c = 0; c < SEND_BUF_SIZE; c++)
    {
      if (send_buf[c].p.pkt == NULL) continue;

      // check if pkt is destined to outside domain
      if (diff_subnet(send_buf[c].p.dest,net_id)) {
	dest = ID(node_->base_stn(),::IP);
	send_buf[c].p.dest = dest;
      }

      if (route_cache->findRoute(send_buf[c].p.dest, send_buf[c].p.route, 1))
	{ // we have a route!
#ifdef DEBUG
	  struct hdr_cmn *ch = HDR_CMN(send_buf[c].p.pkt);
	  if(ch->size() < 0) {
		drop(send_buf[c].p.pkt, "XXX");
		abort();
	  }
#endif
	  if (verbose)
	    trace("Sdebug %.9f _%s_ liberated from sendbuf %s->%s %s",
		  Scheduler::instance().clock(), net_id.dump(),
		  send_buf[c].p.src.dump(), send_buf[c].p.dest.dump(), 
		  send_buf[c].p.route.dump());
	  /* we need to spread out the rate at which we send packets
	     in to the link layer to give ARP time to complete.  If we
	     dump all the packets in at once, all but the last one will
	     be dropped.  XXX THIS IS A MASSIVE HACK -dam 4/14/98 */
	  sendOutPacketWithRoute(send_buf[c].p, true, delay);
	  delay += arp_timeout;	
	  send_buf[c].p.pkt = NULL;
	}
    }
}

void
DSRAgent::processUnknownFlowError(SRPacket &p, bool asDefault) {
  hdr_sr *srh = hdr_sr::access(p.pkt);
  int flowidx = -1;
  struct flow_error *fe;
  u_int16_t flowid;

  if (asDefault) {
    assert (srh->flow_default_unknown() && srh->num_default_unknown());
    fe = &srh->unknown_defaults()[srh->num_default_unknown()-1];
  } else {
    assert (srh->flow_unknown() && srh->num_flow_unknown());
    fe = &srh->unknown_flows()[srh->num_flow_unknown()-1];
    if (!flow_table.defaultFlow(fe->flow_src, fe->flow_dst, flowid))
      goto skip_proc;
  }

  /* not for us; hope it gets the right place... */
  if (fe->flow_src != (int) net_id.addr)
    return;

  if (-1 != (flowidx = flow_table.find(fe->flow_src, fe->flow_dst, 
				       asDefault ? flowid : fe->flow_id)))
    flow_table[flowidx].count = 0;

skip_proc:
  trace("SFEr %.9f _%s_ from %d re %d : %d [%d]",
	Scheduler::instance().clock(), net_id.dump(), p.src.addr, fe->flow_dst,
	asDefault ? -1 : fe->flow_id, 
	flowidx != -1 ? flow_table[flowidx].count : -1);

  if ((asDefault ? srh->num_default_unknown() : srh->num_flow_unknown()) == 1)
    return;

  SRPacket p_copy = p;
  p_copy.pkt = p.pkt->copy();

  hdr_sr *new_srh = hdr_sr::access(p_copy.pkt);
  hdr_ip *new_iph = hdr_ip::access(p_copy.pkt);
  
  // remove us from the list of errors
  if (asDefault)
    new_srh->num_default_unknown()--;
  else
    new_srh->num_flow_unknown()--;
  
  // send the packet to the person listed in what's now the last entry
  p_copy.dest = ID(fe[-1].flow_src, ::IP);
  p_copy.src = net_id;

  //new_iph->daddr() = p_copy.dest.addr;
  new_iph->daddr() = Address::instance().create_ipaddr(p_copy.dest.getNSAddr_t(),RT_PORT);
  new_iph->dport() = RT_PORT;
  //new_iph->saddr() = p_copy.src.addr;
  new_iph->saddr() = Address::instance().create_ipaddr(p_copy.src.getNSAddr_t(),RT_PORT);
  new_iph->sport() = RT_PORT;
  new_iph->ttl() = 255;

  new_srh->flow_header() = 0;
  new_srh->flow_timeout() = 0;

  // an error packet is a first class citizen, so we'll
  // use handlePktWOSR to obtain a route if needed
  handlePktWithoutSR(p_copy, false);
}

void
DSRAgent::processBrokenRouteError(SRPacket& p)
// take the error packet and proccess our part of it.
// if needed, send the remainder of the errors to the next person
// doesn't free p.pkt
{
  hdr_sr *srh = hdr_sr::access(p.pkt);

  if (!srh->route_error())
    return; // what happened??
  
  /* if we hear A->B is dead, should we also run the link B->A through the
     cache as being dead, since 802.11 requires bidirectional links 
      XXX -dam 4/23/98 */

  // since CPU time is cheaper than network time, we'll process
  // all the dead links in the error packet
  assert(srh->num_route_errors() > 0);
  for (int c = 0 ; c < srh->num_route_errors() ; c++)
    {
      assert(srh->down_links()[c].addr_type == NS_AF_INET);
      route_cache->noticeDeadLink(ID(srh->down_links()[c].from_addr,::IP),
				 ID(srh->down_links()[c].to_addr,::IP),
				 Scheduler::instance().clock());
      flow_table.noticeDeadLink(ID(srh->down_links()[c].from_addr,::IP),
				 ID(srh->down_links()[c].to_addr,::IP));
      // I'll assume everything's of type NS_AF_INET for the printout... XXX
      if (verbose_srr)
        trace("SRR %.9f _%s_ dead-link tell %d  %d -> %d",
              Scheduler::instance().clock(), net_id.dump(),
              srh->down_links()[c].tell_addr,
              srh->down_links()[c].from_addr,
              srh->down_links()[c].to_addr);
    }

  ID who = ID(srh->down_links()[srh->num_route_errors()-1].tell_addr, ::IP);
  if (who != net_id && who != MAC_id)
    { // this error packet wasn't meant for us to deal with
      // since the outer entry doesn't list our name
      return;
    }

  // record this route error data for possible propagation on our next
  // route request
  route_error_held = true;
  err_from = ID(srh->down_links()[srh->num_route_errors()-1].from_addr,::IP);
  err_to = ID(srh->down_links()[srh->num_route_errors()-1].to_addr,::IP);
  route_error_data_time = Scheduler::instance().clock();

  if (1 == srh->num_route_errors())
    { // this error packet has done its job
      // it's either for us, in which case we've done what it sez
      // or it's not for us, in which case we still don't have to forward
      // it to whoever it is for
      return;
    }

  /* make a copy of the packet and send it to the next tell_addr on the
     error list.  the copy is needed in case there is other data in the
     packet (such as nested route errors) that need to be delivered */
  if (verbose) 
    trace("Sdebug %.5f _%s_ unwrapping nested route error",
          Scheduler::instance().clock(), net_id.dump());
  
  SRPacket p_copy = p;
  p_copy.pkt = p.pkt->copy();

  hdr_sr *new_srh = hdr_sr::access(p_copy.pkt);
  hdr_ip *new_iph = hdr_ip::access(p_copy.pkt);
  
  // remove us from the list of errors
  new_srh->num_route_errors() -= 1;
  
  // send the packet to the person listed in what's now the last entry
  p_copy.dest = ID(new_srh->down_links()[new_srh->num_route_errors()-1].tell_addr, ::IP);
  p_copy.src = net_id;

  //new_iph->daddr() = p_copy.dest.addr;
  new_iph->daddr() = Address::instance().create_ipaddr(p_copy.dest.getNSAddr_t(),RT_PORT);
  new_iph->dport() = RT_PORT;
  //new_iph->saddr() = p_copy.src.addr;
  new_iph->saddr() = Address::instance().create_ipaddr(p_copy.src.getNSAddr_t(),RT_PORT);
  new_iph->sport() = RT_PORT;
  new_iph->ttl() = 255;

  new_srh->flow_header() = 0;
  new_srh->flow_timeout() = 0;
      
  // an error packet is a first class citizen, so we'll
  // use handlePktWOSR to obtain a route if needed
  handlePktWithoutSR(p_copy, false);
}

#ifdef DSR_FILTER_TAP
int64_t dsr_tap = 0;
int64_t dsr_tap_skip = 0;
#endif

// Process flow state Automatic Route Shortening
void
DSRAgent::processFlowARS(const Packet *packet) {
  
  hdr_sr *srh = hdr_sr::access(packet);
  hdr_ip *iph = hdr_ip::access(packet);
  hdr_cmn *cmh = hdr_cmn::access(packet);
  //hdr_sr  *srh = (hdr_sr*) ((Packet *)packet)->access(off_sr_);
  //hdr_ip  *iph = (hdr_ip*) ((Packet *)packet)->access(off_ip_);
  //hdr_cmn *cmh =  (hdr_cmn*)((Packet *)packet)->access(off_cmn_);
  u_int16_t flowid;
  int flowidx;
  int shortamt;

  assert(!srh->num_addrs());

  if (srh->flow_header()) {
    flowid = srh->flow_id();

    // do I know about this flow?
    if (-1 == (flowidx = flow_table.find(iph->saddr(), iph->daddr(), flowid)))
      return;

    shortamt = flow_table[flowidx].hopCount - srh->hopCount();
  } else {
    // do I know which flow is default?
    if (!flow_table.defaultFlow(iph->saddr(), iph->daddr(), flowid))
      return;

    // do I know about this flow?
    if (-1 == (flowidx = flow_table.find(iph->saddr(), iph->daddr(), flowid)))
      return;

    shortamt = iph->ttl() - flow_table[flowidx].expectedTTL;
  }

  // transmitter downstream from us
  if (shortamt <= 0)
    return;

  // this is a _MAJOR_ problem!!!
  if (flow_table[flowidx].sourceRoute.length() < shortamt)
    return;

  ars_table.insert(cmh->uid(), flowid, shortamt);
}

void 
DSRAgent::tap(const Packet *packet)
  /* process packets that are promiscously listened to from the MAC layer tap
  *** do not change or free packet *** */
{
  hdr_sr *srh = hdr_sr::access(packet);
  hdr_ip *iph = hdr_ip::access(packet);
  hdr_cmn *cmh =  hdr_cmn::access(packet);
  
  if (!dsragent_use_tap) return;

  if (!srh->valid()) return;	// can't do anything with it

  if (!srh->num_addrs()) {
    processFlowARS(packet);
    return;
  }

  // don't trouble me with packets I'm about to receive anyway
  /* this change added 5/13/98 -dam */
  ID next_hop(srh->addrs()[srh->cur_addr()]);
  if (next_hop == net_id || next_hop == MAC_id) return;

  SRPacket p((Packet *) packet, srh);
  //p.dest = ID(iph->dst(),::IP);
  //p.src = ID(iph->src(),::IP);
  p.dest = ID((Address::instance().get_nodeaddr(iph->daddr())),::IP);
  p.src = ID((Address::instance().get_nodeaddr(iph->saddr())),::IP);

  // don't trouble me with my own packets
  if (p.src == net_id) return; 

#ifdef DSR_FILTER_TAP
  /* 
   * Don't process packets more than once.  In real implementations
   * this can be done with the (IP Source, IP ID) pair, but it is
   * simpler to implement it with the global "uid" in simulation.
   */
  {
          int uid = cmh->uid();
          if(tap_uid_cache[(uid & TAP_BITMASK)] == uid) {
		  dsr_tap_skip++;
                  return;
	  }
	  dsr_tap++;
          tap_uid_cache[(uid & TAP_BITMASK)] = uid;
  }
#endif

  /* snoop on the SR data */
  if (srh->route_error())
    {
      if (verbose)
	trace("Sdebug _%s_ tap saw error %d",  net_id.dump(), cmh->uid());
      processBrokenRouteError(p);
    }

  if (srh->route_reply())
    {
      Path reply_path(srh->reply_addrs(), srh->route_reply_len());
      if(verbose)
	trace("Sdebug _%s_ tap saw route reply %d  %s",
	       net_id.dump(), cmh->uid(), reply_path.dump());
      route_cache->noticeRouteUsed(reply_path, Scheduler::instance().clock(), 
				   p.src);
    }

  /* we can't decide whether we should snoop on the src routes in 
     route requests.  We've seen cases where we hear a route req from a
     node, but can't complete an arp with that node (and so can't actually
     deliver packets through it if called on to do so) -dam 4/16/98 */

  if (srh->route_request()) return; // don't path shorten route requests
  // the logic is wrong for shortening rtreq's anyway, cur_addr always = 0

  if (dsragent_snoop_source_routes)
    {
      if (verbose)
	trace("Sdebug _%s_ tap saw route use %d %s", net_id.dump(), 
	      cmh->uid(), p.route.dump());
      route_cache->noticeRouteUsed(p.route, Scheduler::instance().clock(), 
				   net_id);
    }

  if (PT_DSR == cmh->ptype()) return; //  no route shortening on any
  // DSR packet

  /* I think we ended up sending grat route replies for source routes on 
     route replies for route requests that were answered by someone else's
     cache, resulting in the wrong node receiving the route.  For now, I 
     outlaw it.

     The root of the problem is that when we salvage a pkt from a failed
     link using a route from our cache, we break what had been an invariant
     that the IP src of a packet was also the first machine listed on the
     source route.  Here's the route of the problem that results in the 
     simulator crashing at 8.56135 when 44 recieves a route reply that
     has 24 listed as the first node in the route.

SSendFailure 8.52432 24 [10 |24 46 45 1 40 ]
S$hit 8.52432 salvaging 10 -> 40 with [(24) 44 50 9 40 ]
S$hit 8.52432 salvaging 44 -> 40 with [(24) 44 50 9 40 ]
D 8.52432 [20 42 2e 18 800] 24 DSR 156 -- 10->40 6 [0] [1 9 39] [0 0 0->0]
s 8.52438 [1b 45e 2c 18 0] 24 MAC 20
r 8.52446 [1b 45e 2c 18 0] 44 MAC 20
s 8.52454 [101b 27e 23 1b 0] 27 MAC 20
s 8.52564 [101b 27e 23 1b 0] 27 MAC 20
s 8.52580 [101b 45e 2c 18 0] 24 MAC 20
r 8.52588 [101b 45e 2c 18 0] 44 MAC 20
s 8.52589 [1c 41c 18 0 0] 44 MAC 14
r 8.52595 [1c 41c 18 0 0] 24 MAC 14
s 8.52600 [20 42 2c 18 800] 24 DSR 244 -- 10->40 5 [0] [1 9 39] [0 0 24->46]
r 8.52698 [20 42 2c 18 800] 44 DSR 216 -- 10->40 5 [0] [1 9 39] [0 0 24->46]

s 8.53947 [20 42 2c 18 800] 24 DSR 204 -- 44->40 5 [0] [1 8 39] [0 0 0->0]
r 8.54029 [20 42 2c 18 800] 44 DSR 176 -- 44->40 5 [0] [1 8 39] [0 0 0->0]
Sdebug 50 consider grat arp for [24 (44) 50 9 40 ]
SRR 8.54029 50 gratuitous-reply-sent 50 -> 44 [24 (50) 9 40 ]
SF 8.54029 44 [44 -> 40] via 0x3200 [24 |44 50 9 40 ]
s 8.54030 [1d 0 18 0 0] 44 MAC 14
r 8.54036 [1d 0 18 0 0] 24 MAC 14
s 8.54044 [101b 54f 32 2c 0] 44 MAC 20
r 8.54053 [101b 54f 32 2c 0] 50 MAC 20
s 8.54054 [1c 50d 2c 0 0] 50 MAC 14
r 8.54059 [1c 50d 2c 0 0] 44 MAC 14
s 8.54064 [20 42 32 2c 800] 44 DSR 304 -- 10->40 5 [0] [1 9 39] [0 0 24->46]
r 8.54186 [20 42 32 2c 800] 50 DSR 276 -- 10->40 5 [0] [1 9 39] [0 0 24->46]
SF 8.54186 50 [10 -> 40] via 0x900 [24 44 |50 9 40 ]

s 8.56101 [20 42 2c 18 800] 24 DSR 84 -- 50->44 2 [0] [1 4 40] [0 0 0->0]
r 8.56135 [20 42 2c 18 800] 44 DSR 56 -- 50->44 2 [0] [1 4 40] [0 0 0->0]

*/


  /* check to see if we can shorten the route being used */
  if (p.route[p.route.index()] != net_id
      && p.route[p.route.index()] != MAC_id)
    { // it's not immeadiately to us
      for (int i = p.route.index() + 1; i < p.route.length(); i++)
	if (p.route[i] == net_id || p.route[i] == MAC_id)
	  { // but it'll get here eventually...
	    sendRouteShortening(p, p.route.index(), i);
	  }
    }
}

static GratReplyHoldDown *
FindGratHoldDown(GratReplyHoldDown *hd, int sz, Path& query)
{
  int c;
  for (c = 0; c < sz; c++)
    if (query == hd[c].p) return &hd[c];
  return NULL;
}

void
DSRAgent::sendRouteShortening(SRPacket &p, int heard_at, int xmit_at)
  // p was overheard at heard_at in it's SR, but we aren't supposed to
  // get it till xmit_at, so all the nodes between heard_at and xmit_at
  // can be elided.  Send originator of p a gratuitous route reply to 
  // tell them this.
{
  // this shares code with returnSrcRouteToRequestor - factor them -dam */

  if (!dsragent_send_grat_replies) return;

  if (verbose)
    trace("Sdebug %s consider grat arp for %s", net_id.dump(), p.route.dump());
  GratReplyHoldDown *g = FindGratHoldDown(grat_hold, RTREP_HOLDOFF_SIZE, 
					  p.route);
  if (!g)
    { 
      grat_hold[grat_hold_victim].p = p.route;
      grat_hold_victim = (grat_hold_victim + 1) % RTREP_HOLDOFF_SIZE;
      g = &grat_hold[grat_hold_victim];      
    }
  else if (Scheduler::instance().clock() - g->t < grat_hold_down_time) return;
  g->t = Scheduler::instance().clock();

  SRPacket p_copy = p;
  p_copy.pkt = allocpkt();
  p_copy.dest = p.route[0];   // tell the originator of this long source route
  p_copy.src = net_id;

  // reverse the route to get the packet back
  p_copy.route[p_copy.route.index()] = net_id;
  p_copy.route.reverseInPlace();
  p_copy.route.removeSection(0,p_copy.route.index());

  hdr_ip *new_iph =  hdr_ip::access(p_copy.pkt);
  //new_iph->daddr() = p_copy.dest.addr;
  new_iph->daddr() = Address::instance().create_ipaddr(p_copy.dest.getNSAddr_t(),RT_PORT);
  new_iph->dport() = RT_PORT;
  //new_iph->saddr() = p_copy.src.addr;
  new_iph->saddr() = Address::instance().create_ipaddr(p_copy.src.getNSAddr_t(),RT_PORT);
  new_iph->sport() = RT_PORT;
  new_iph->ttl() = 255;

  // shorten's p's route
  p.route.removeSection(heard_at, xmit_at);
  hdr_sr *new_srh =  hdr_sr::access(p_copy.pkt);
  new_srh->init();
  for (int i = 0 ; i < p.route.length() ; i++)
    p.route[i].fillSRAddr(new_srh->reply_addrs()[i]);
  new_srh->route_reply_len() = p.route.length();
  new_srh->route_reply() = 1;
  // grat replies will have a 0 seq num (it's only for trace analysis anyway)
  new_srh->rtreq_seq() = 0;

  hdr_cmn *new_cmnh =  hdr_cmn::access(p_copy.pkt);
  new_cmnh->ptype() = PT_DSR;
  new_cmnh->size() += IP_HDR_LEN;

  if (verbose_srr)
    trace("SRR %.9f _%s_ gratuitous-reply-sent %s -> %s (len %d) %s",
	  Scheduler::instance().clock(), net_id.dump(),
	  p_copy.src.dump(), p_copy.dest.dump(), p.route.length(), 
	  p.route.dump());

  // cache the route for future use (we learned the route from p)
  route_cache->addRoute(p_copy.route, Scheduler::instance().clock(), p.src);
  sendOutPacketWithRoute(p_copy, true);
}

/*==============================================================
  debug and trace output
------------------------------------------------------------*/
void
DSRAgent::trace(char* fmt, ...)
{
  va_list ap;
  
  if (!logtarget) return;

  va_start(ap, fmt);
  vsprintf(logtarget->pt_->buffer(), fmt, ap);
  logtarget->pt_->dump();
  va_end(ap);
}


/*==============================================================
  Callback for link layer transmission failures
------------------------------------------------------------*/
// XXX Obviously this structure and FilterFailure() is not used anywhere, 
// because off_cmn_ in this structure cannot be populated at all!
// Instead of deleting, I'm simply commenting them out, perhaps they'll be 
// salvaged sometime in the future. - haoboy

//  struct filterfailuredata {
//    nsaddr_t dead_next_hop;
//    int off_cmn_;
//    DSRAgent *agent;
//  };

//  int
//  FilterFailure(Packet *p, void *data)
//  {
//    struct filterfailuredata *ffd = (filterfailuredata *) data;
//    hdr_cmn *cmh = (hdr_cmn*)p->access(ffd->off_cmn_);
//    int remove = cmh->next_hop() == ffd->dead_next_hop;

//    if (remove)
//  	  ffd->agent->undeliverablePkt(p,1);
//    return remove;
//  }

void
DSRAgent::undeliverablePkt(Packet *pkt, int mine)
  /* when we've got a packet we can't deliver, what to do with it? 
     frees or hands off p if mine = 1, doesn't hurt it otherwise */
{
  hdr_sr *srh = hdr_sr::access(pkt);
  hdr_ip *iph = hdr_ip::access(pkt);
  hdr_cmn *cmh;

  SRPacket p(pkt,srh);
  //p.dest = ID(iph->dst(),::IP);
  //p.src = ID(iph->src(),::IP);
  p.dest = ID((Address::instance().get_nodeaddr(iph->daddr())),::IP);
  p.src = ID((Address::instance().get_nodeaddr(iph->saddr())),::IP);
  p.pkt = mine ? pkt : pkt->copy();

  srh = hdr_sr::access(p.pkt);
  iph = hdr_ip::access(p.pkt);
  cmh = hdr_cmn::access(p.pkt);

  // we're about to salvage. flowstate rules say we must strip all flow
  // state info out of this packet. ych 5/5/01
  cmh->size() -= srh->size(); // changes affect size of header...
  srh->flow_timeout() = 0;
  srh->flow_header() = 0;
  cmh->size() += srh->size(); // done fixing flow state headers

  if (ID((Address::instance().get_nodeaddr(iph->saddr())),::IP) == net_id) {
    // it's our packet we couldn't send
    cmh->size() -= srh->size(); // remove size of SR header
    assert(cmh->size() >= 0);
    
    handlePktWithoutSR(p, false);
    
    return;
  }

  /*
   * Am I allowed to salvage?
   */
  if(dsragent_salvage_with_cache == 0) {
	  assert(mine);
	  drop(pkt, DROP_RTR_NO_ROUTE);  
	  return;
  }

#ifdef NEW_SALVAGE_LOGIC
  if(srh->salvaged() >= dsr_salvage_max_attempts) {
	  assert(mine);
	  drop(pkt, DROP_RTR_SALVAGE);
	  return;
  }
#endif /* NEW_SALVAGE_LOGIC */

  // it's a packet we're forwarding for someone, save it if we can...
  Path salvage_route;
      
  if (route_cache->findRoute(p.dest, salvage_route, 0)) {
	  // be nice and send the packet out
#if 0
	  /* we'd like to create a ``proper'' source route with the
	     IP src of the packet as the first node, but we can't actually 
	     just append the salvage route onto the route used so far, 
	     since the append creates routes with loops in them 
	     like  1 2 3 4 3 5 
	     If we were to squish the route to remove the loop, then we'd be
	     removing ourselves from the route, which is verboten.
	     If we did remove ourselves, and our salvage route contained
	     a stale link, we might never hear the route error.
	     -dam 5/13/98

	     Could we perhaps allow SRs with loops in them on the air?
	     Since it's still a finite length SR, the pkt can't loop
	     forever... -dam 8/5/98 */

	  // truncate the route at the bad link and append good bit
	  int our_index = p.route.index();

	  p.route.setLength(our_index);
	  // yes this cuts us off the route,

	  p.route.appendPath(salvage_route);
	  // but we're at the front of s_r
	  p.route.setIterator(our_index);
#else
	  p.route = salvage_route;
	  p.route.resetIterator();
#endif

	  if (dsragent_dont_salvage_bad_replies && srh->route_reply()) {
		  // check to see if we'd be salvaging a packet
		  // with the dead link in it

		  ID to_id(srh->addrs()[srh->cur_addr()+1].addr,
			   (ID_Type) srh->addrs()[srh->cur_addr()].addr_type);
		  bool bad_reply = false;

		  for (int i = 0 ; i < srh->route_reply_len()-1; i++) {

			  if ((net_id == ID(srh->reply_addrs()[i]) &&
			      to_id == ID(srh->reply_addrs()[i+1])) ||
			      (dsragent_require_bi_routes &&
			       to_id == ID(srh->reply_addrs()[i]) &&
			       net_id == ID(srh->reply_addrs()[i+1]))) {
					  
				  bad_reply = true;
				  break;
			  }
		  }
		  if (bad_reply) {
			  // think about killing this packet
			  srh->route_reply() = 0;
			  if (PT_DSR == cmh->ptype() &&
			      ! srh->route_request() &&
			      ! srh->route_error()) {
				  // this packet has no reason to live
				  if (verbose_srr)
					  trace("SRR %.5f _%s_ --- %d dropping bad-reply %s -> %s", 
						Scheduler::instance().clock(), net_id.dump(), 
						cmh->uid(), p.src.dump(), p.dest.dump());
				  if (mine)
					  drop(pkt, DROP_RTR_MAC_CALLBACK);
				  return;
			  }
		  }
	  }

	  if (verbose_ssalv) 
		  trace("Ssalv %.5f _%s_ salvaging %s -> %s --- %d with %s",
			Scheduler::instance().clock(), net_id.dump(),
			p.src.dump(), p.dest.dump(),
			cmh->uid(), p.route.dump());

	  // remove size of SR header, added back in sendOutPacketWithRoute
	  cmh->size() -= srh->size(); 
	  assert(cmh->size() >= 0);
#ifdef NEW_SALVAGE_LOGIC
	  srh->salvaged() += 1;
#endif
	  sendOutPacketWithRoute(p, false);
  }
#ifdef NEW_SALVAGE_LOGIC
  else if(dsr_salvage_max_requests > 0) {
	  /*
	   * Allow the node to perform route discovery for an
	   * intermediate hop.
	   */
	  if (verbose_ssalv) 
		  trace("Ssalv %.5f _%s_ adding to SB --- %d %s -> %s [%d]", 
			Scheduler::instance().clock(), 
			net_id.dump(),
			cmh->uid(),
			p.src.dump(), p.dest.dump(),
			srh->salvaged());
	  stickPacketInSendBuffer(p);
  }
#endif
  else {
	  // we don't have a route, and it's not worth us doing a
	  // route request to try to help the originator out, since
	  // it might be counter productive
	  if (verbose_ssalv) 
		  trace("Ssalv %.5f _%s_ dropping --- %d %s -> %s [%d]", 
			Scheduler::instance().clock(), 
			net_id.dump(), cmh->uid(),
			p.src.dump(), p.dest.dump(),
			srh->salvaged());
	  if (mine)
		  drop(pkt, DROP_RTR_NO_ROUTE);
  }
}

#ifdef USE_GOD_FEEDBACK
static int linkerr_is_wrong = 0;
#endif

void
DSRAgent::sendUnknownFlow(SRPacket &p, bool asDefault, u_int16_t flowid) {
  hdr_sr *srh = hdr_sr::access(p.pkt);
  hdr_ip *iph = hdr_ip::access(p.pkt);
  hdr_cmn *cmh = hdr_cmn::access(p.pkt);
  struct flow_error *fe;

  assert(!srh->num_addrs()); // flow forwarding basis only.
#if 0
  // this doesn't always hold true; if an xmit fails, we'll dump the
  // thing from our flow table, possibly before we even get here (though how
  // we found out, other than from this packet, is anyone's guess, considering
  // that underliverablePkt() should have been called in any other circumstance,
  // so we shouldn't go through the failed stuff.
  assert(p.src != net_id); // how'd it get here if it were?

  // this doesn't always hold true; I may be sending it default, fail,
  // the flow times out, but I still know the flowid (whacked paths through
  // the code, I know... ych 5/7/01
  assert(srh->flow_header() ^ asDefault); // one or the other, not both
#endif

  if (p.src == net_id) {
    Packet::free(p.pkt);
    p.pkt = 0;
    return; // gimme a break, we already know!
  }

  undeliverablePkt(p.pkt, false); // salvage, but don't molest.
 
  /* warp into an error... */
  if (asDefault) {
    if (!srh->flow_default_unknown()) {
      srh->num_default_unknown() = 1;
      srh->flow_default_unknown() = 1;
      fe = srh->unknown_defaults();
    } else if (srh->num_default_unknown() < MAX_ROUTE_ERRORS) {
      fe = srh->unknown_defaults() + srh->num_default_unknown();
      srh->num_default_unknown()++;
    } else {
      trace("SYFU  %.5f _%s_ dumping maximally nested Flow error %d -> %d",
      Scheduler::instance().clock(), net_id.dump(), p.src.addr, p.dest.addr);

      Packet::free(p.pkt);        // no drop needed
      p.pkt = 0;
      return;
    }
  } else {
    if (!srh->flow_unknown()) {
      srh->num_flow_unknown() = 1;
      srh->flow_unknown() = 1;
      fe = srh->unknown_flows();
    } else if (srh->num_default_unknown() < MAX_ROUTE_ERRORS) {
      fe = srh->unknown_flows() + srh->num_flow_unknown();
      srh->num_flow_unknown()++;
    } else {
      trace("SYFU  %.5f _%s_ dumping maximally nested Flow error %d -> %d",
      Scheduler::instance().clock(), net_id.dump(), p.src.addr, p.dest.addr);

      Packet::free(p.pkt);        // no drop needed
      p.pkt = 0;
      return;
    }
  }

  trace("SFErr %.5f _%s_ %d -> %d : %d",
	Scheduler::instance().clock(), net_id.dump(), p.src.addr, p.dest.addr,
	flowid);

  srh->route_reply() = 0;
  srh->route_request() = 0;
  srh->flow_header() = 0;
  srh->flow_timeout() = 0;

  //iph->daddr() = p.src.addr;
  iph->daddr() = Address::instance().create_ipaddr(p.src.getNSAddr_t(),RT_PORT);
  iph->dport() = RT_PORT;
  //iph->saddr() = net_id.addr;
  iph->saddr() = Address::instance().create_ipaddr(net_id.getNSAddr_t(),RT_PORT);
  iph->sport() = RT_PORT;
  iph->ttl() = 255;

  //fe->flow_src = p.src.addr;
  fe->flow_src = p.src.getNSAddr_t();
  //fe->flow_dst = p.dest.addr;
  fe->flow_dst = p.dest.getNSAddr_t();
  fe->flow_id  = flowid;

  //p.src = ID(iph->src(), ::IP);
  //p.dest = ID(iph->dst(), ::IP);
  p.dest = ID((Address::instance().get_nodeaddr(iph->daddr())),::IP);
  p.src = ID((Address::instance().get_nodeaddr(iph->saddr())),::IP);


  cmh->ptype() = PT_DSR;                // cut off data
  cmh->size() = IP_HDR_LEN;
  cmh->num_forwards() = 0;
  // assign this packet a new uid, since we're sending it
  cmh->uid() = uidcnt_++;

  handlePktWithoutSR(p, false);
  assert(p.pkt == 0);
}

void 
DSRAgent::xmitFlowFailed(Packet *pkt, const char* reason)
{
  hdr_sr *srh = hdr_sr::access(pkt);
  hdr_ip *iph = hdr_ip::access(pkt);
  hdr_cmn *cmh = hdr_cmn::access(pkt);
  int flowidx = flow_table.find(iph->saddr(), iph->daddr(), srh->flow_id());
  u_int16_t default_flow;

  assert(!srh->num_addrs());

  if (!srh->flow_header()) {
    if (!flow_table.defaultFlow(iph->saddr(), iph->daddr(), default_flow)) {
      SRPacket p(pkt, srh);
      //p.src = ID(iph->src(), ::IP);
      //p.dest = ID(iph->dst(), ::IP);
      p.dest = ID((Address::instance().get_nodeaddr(iph->daddr())),::IP);
      p.src = ID((Address::instance().get_nodeaddr(iph->saddr())),::IP);


      sendUnknownFlow(p, true);
      return;
    }
    flowidx = flow_table.find(iph->saddr(), iph->daddr(), default_flow);
  }

  if (flowidx == -1 || 
      flow_table[flowidx].timeout < Scheduler::instance().clock()) {
    // blah, the flow has expired, or been forgotten.
    SRPacket p(pkt, srh);
    //p.src = ID(iph->src(), ::IP);
    //p.dest = ID(iph->dst(), ::IP);
    p.dest = ID((Address::instance().get_nodeaddr(iph->daddr())),::IP);
    p.src = ID((Address::instance().get_nodeaddr(iph->saddr())),::IP);


    return;
  }

  cmh->size() -= srh->size(); // gonna change the source route size
  assert(cmh->size() >= 0);
  
  flow_table[flowidx].sourceRoute.fillSR(srh);
  srh->cur_addr() = flow_table[flowidx].hopCount;
  assert(srh->addrs()[srh->cur_addr()].addr == (nsaddr_t) net_id.addr);
  cmh->size() += srh->size();

  // xmitFailed is going to assume this was incr'ed for send
  srh->cur_addr()++;
  xmitFailed(pkt, reason);
}

void 
DSRAgent::xmitFailed(Packet *pkt, const char* reason)
  /* mark our route cache reflect the failure of the link between
     srh[cur_addr] and srh[next_addr], and then create a route err
     message to send to the orginator of the pkt (srh[0])
     p.pkt freed or handed off */
{
  hdr_sr *srh = hdr_sr::access(pkt);
  hdr_ip *iph = hdr_ip::access(pkt);
  hdr_cmn *cmh = hdr_cmn::access(pkt);

  assert(cmh->size() >= 0);

  srh->cur_addr() -= 1;		// correct for inc already done on sending
  
  if (srh->cur_addr() >= srh->num_addrs() - 1)
    {
      trace("SDFU: route error beyond end of source route????");
      fprintf(stderr,"SDFU: route error beyond end of source route????\n");
      Packet::free(pkt);
      return;
    }

  if (srh->route_request())
    {
      trace("SDFU: route error forwarding route request????");
      fprintf(stderr,"SDFU: route error forwarding route request????\n");
      Packet::free(pkt);
      return;
    }


  ID tell_id(srh->addrs()[0].addr,
	     (ID_Type) srh->addrs()[srh->cur_addr()].addr_type);
  ID from_id(srh->addrs()[srh->cur_addr()].addr,
	     (ID_Type) srh->addrs()[srh->cur_addr()].addr_type);
  ID to_id(srh->addrs()[srh->cur_addr()+1].addr,
	     (ID_Type) srh->addrs()[srh->cur_addr()].addr_type);
  assert(from_id == net_id || from_id == MAC_id);

  trace("SSendFailure %.9f _%s_ %d %d %d:%d %d:%d %s->%s %d %d %d %d %s",
	Scheduler::instance().clock(), net_id.dump(), 
	cmh->uid(), cmh->ptype(),
	iph->saddr(), iph->sport(),
	iph->daddr(), iph->dport(),
	from_id.dump(),to_id.dump(),
	God::instance()->hops(from_id.getNSAddr_t(), to_id.getNSAddr_t()),
	God::instance()->hops(iph->saddr(),iph->daddr()),
	God::instance()->hops(from_id.getNSAddr_t(), iph->daddr()),
	srh->num_addrs(), srh->dump());

#ifdef USE_GOD_FEEDBACK
  if (God::instance()->hops(from_id.getNSAddr_t(), to_id.getNSAddr_t()) == 1)
    { /* god thinks this link is still valid */
      linkerr_is_wrong++;
      trace("SxmitFailed %.5f _%s_  %d->%d god okays #%d",
            Scheduler::instance().clock(), net_id.dump(),
            from_id.getNSAddr_t(), to_id.getNSAddr_t(), linkerr_is_wrong);
      fprintf(stderr,
	      "xmitFailed on link %d->%d god okays - ignoring & recycling #%d\n",
	      from_id.getNSAddr_t(), to_id.getNSAddr_t(), linkerr_is_wrong);
      /* put packet back on end of ifq for xmission */
      srh->cur_addr() += 1;	// correct for decrement earlier in proc 
      // make sure we aren't cycling packets
      // also change direction in pkt hdr
      cmh->direction() = hdr_cmn::DOWN;
      ll->recv(pkt, (Handler*) 0);
      return;
    }
#endif

  if(strcmp(reason, "DROP_IFQ_QFULL") != 0) {
	  assert(strcmp(reason, "DROP_RTR_MAC_CALLBACK") == 0);

	  /* kill any routes we have using this link */
	  route_cache->noticeDeadLink(from_id, to_id,
				      Scheduler::instance().clock());
	  flow_table.noticeDeadLink(from_id, to_id);

	  /* give ourselves a chance to save the packet */
	  undeliverablePkt(pkt->copy(), 1);

	  /* now kill all the other packets in the output queue that would
	     use the same next hop.  This is reasonable, since 802.11 has
	     already retried the xmission multiple times => a persistent
	     failure. */

	  /* XXX YCH 5/4/01 shouldn't each of these packets get Route Errors
	   * if one hasn't already been sent? ie if two different routes
	   * are using this link?
	   */
	  {
	    Packet *r, *nr, *queue1 = 0, *queue2 = 0;
	    // pkts to be recycled
	    
	    while((r = ifq->prq_get_nexthop(to_id.getNSAddr_t()))) {
	      r->next_ = queue1;
	      queue1 = r; 
	    }

	    // the packets are now in the reverse order of how they
	    // appeared in the IFQ so reverse them again
	    for(r = queue1; r; r = nr) {
	      nr = r->next_;
	      r->next_ = queue2;
	      queue2 = r;
	    }

	    // now process them in order
	    for(r = queue2; r; r = nr) {
	      nr = r->next_;
	      undeliverablePkt(r, 1);
	    }
	  }
  }
  
  /* warp pkt into a route error message */
  if (tell_id == net_id || tell_id == MAC_id)
    { // no need to send the route error if it's for us
      if (verbose) 
        trace("Sdebug _%s_ not bothering to send route error to ourselves", 
	      tell_id.dump());
      Packet::free(pkt);	// no drop needed
      pkt = 0;
      return;
    }

  if (srh->num_route_errors() >= MAX_ROUTE_ERRORS)
    { // no more room in the error packet to nest an additional error.
      // this pkt's been bouncing around so much, let's just drop and let
      // the originator retry
      // Another possibility is to just strip off the outer error, and
      // launch a Route discovey for the inner error XXX -dam 6/5/98
      trace("SDFU  %.5f _%s_ dumping maximally nested error %s  %d -> %d",
	    Scheduler::instance().clock(), net_id.dump(),
	    tell_id.dump(),
	    from_id.dump(),
	    to_id.dump());
      Packet::free(pkt);	// no drop needed
      pkt = 0;
      return;
    }

  link_down *deadlink = &(srh->down_links()[srh->num_route_errors()]);
  deadlink->addr_type = srh->addrs()[srh->cur_addr()].addr_type;
  deadlink->from_addr = srh->addrs()[srh->cur_addr()].addr;
  deadlink->to_addr = srh->addrs()[srh->cur_addr()+1].addr;
  deadlink->tell_addr = srh->addrs()[0].addr;
  srh->num_route_errors() += 1;

  if (verbose)
    trace("Sdebug %.5f _%s_ sending into dead-link (nest %d) tell %d  %d -> %d",
          Scheduler::instance().clock(), net_id.dump(),
          srh->num_route_errors(),
          deadlink->tell_addr,
          deadlink->from_addr,
          deadlink->to_addr);

  srh->route_error() = 1;
  srh->route_reply() = 0;
  srh->route_request() = 0;
  srh->flow_header() = 0;
  srh->flow_timeout() = 0;

  //iph->daddr() = deadlink->tell_addr;
  iph->daddr() = Address::instance().create_ipaddr(deadlink->tell_addr,RT_PORT);
  iph->dport() = RT_PORT;
  //iph->saddr() = net_id.addr;
  iph->saddr() = Address::instance().create_ipaddr(net_id.addr,RT_PORT);
  iph->sport() = RT_PORT;
  iph->ttl() = 255;

  cmh->ptype() = PT_DSR;		// cut off data
  cmh->size() = IP_HDR_LEN;
  cmh->num_forwards() = 0;
  // assign this packet a new uid, since we're sending it
  cmh->uid() = uidcnt_++;

  SRPacket p(pkt, srh);
  p.route.setLength(p.route.index()+1);
  p.route.reverseInPlace();
  p.dest = tell_id;
  p.src = net_id;

  /* send out the Route Error message */
  sendOutPacketWithRoute(p, true);
}


#if 0

/* this is code that implements Route Reply holdoff to prevent route 
   reply storms.  It's described in the kluwer paper and was used in 
   those simulations, but isn't currently used.  -dam 8/5/98 */

/*==============================================================
  Callback Timers to deal with holding off  route replies

  Basic theory: if we see a node S that has requested a route to D
  send a packet to D via a route of length <= ours then don't send
  our route.  We record that S has used a good route to D by setting
  the best_length to -1, meaning that our route can't possibly do
  S any good (unless S has been lied to, but we probably can't know
  that).
  
  NOTE: there is confusion in this code as to whether the requestor
  and requested_dest ID's are MAC or IP... It doesn't matter for now
  but will later when they are not the same.

------------------------------------------------------------*/
struct RtHoldoffData: public EventData {
  RtHoldoffData(DSRAgent *th, Packet *pa, int ind):t(th), p(pa), index(ind)
  {}
  DSRAgent *t;
  Packet *p;
  int index;
};

void
RouteReplyHoldoffCallback(Node *node, Time time, EventData *data)
// see if the packet inside the data is still in the
// send buffer and expire it if it is
{
  Packet *p = ((RtHoldoffData *)data)->p;
  DSRAgent *t = ((RtHoldoffData *)data)->t;
  int index = ((RtHoldoffData *)data)->index;

  RtRepHoldoff *entry = &(t->rtrep_holdoff[index]);
  assert((entry->requestor == p->dest));

  // if we haven't heard the requestor use a route equal or better
  // than ours then send our reply.
  if ((lsnode_require_use && entry->best_length != -1)
      || (!lsnode_require_use && entry->best_length > entry->our_length))
    { // we send
      world_statistics.sendingSrcRtFromCache(t,time,p);
      t->sendPacket(t,time,p);
    }
  else
    { // dump our packet
      delete p;
    }
  entry->requestor = invalid_addr;
  entry->requested_dest = invalid_addr;
  delete data;
  t->num_heldoff_rt_replies--;
}

void
DSRAgent::scheduleRouteReply(Time t, Packet *new_p)
  // schedule a time to send new_p if we haven't heard a better
  // answer in the mean time.  Do not modify new_p after calling this
{
  for (int c = 0; c < RTREP_HOLDOFF_SIZE; c ++)
    if (rtrep_holdoff[c].requested_dest == invalid_addr) break;
  assert(c < RTREP_HOLDOFF_SIZE);

  Path *our_route = &(new_p->data.getRoute().source_route);
  rtrep_holdoff[c].requested_dest = (*our_route)[our_route->length() - 1];
  rtrep_holdoff[c].requestor = new_p->dest;
  rtrep_holdoff[c].best_length = MAX_ROUTE_LEN + 1;
  rtrep_holdoff[c].our_length = our_route->length();

  Time send_time = t +
    (Time) (our_route->length() - 1) * rt_rep_holdoff_period
    + U(0.0, rt_rep_holdoff_period);
  RegisterCallback(this,&RouteReplyHoldoffCallback, send_time,
		   new RtHoldoffData(this,new_p,c));
  num_heldoff_rt_replies++;
}

void
DSRAgent::snoopForRouteReplies(Time t, Packet *p)
  // see if p is a route reply that we're watching for
  // or if it was sent off using a route reply we're watching for
{
  for (int c = 0 ; c <RTREP_HOLDOFF_SIZE ; c ++)
    {
      RtRepHoldoff *entry = &(rtrep_holdoff[c]);

      // there is no point in doing this first check if we're always
      // going to send our route reply unless we hear the requester use one
      // better or equal to ours
      if (entry->requestor == p->dest
	  && (p->type == ::route_reply || p->data.sourceRoutep()))
	{ // see if this route reply is one we're watching for
	  Path *srcrt = &(p->data.getRoute().source_route);
	  if (!(entry->requested_dest == (*srcrt)[srcrt->length()-1]))
	    continue;		// it's not ours
	  if (entry->best_length > srcrt->length())
	    entry->best_length = srcrt->length();
	} // end if we heard a route reply being sent
      else if (entry->requestor == p->src
	       && entry->requested_dest == p->dest)
	{ // they're using a route  reply! see if ours is better
          if (p->route.length() <= entry->our_length)
            { // Oh no! they've used a better path than ours!
              entry->best_length = -1; //there's no point in replying.
            }
        } // end if they used used route reply
      else
        continue;
    }
}

#endif //0








