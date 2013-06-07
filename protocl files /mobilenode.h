/* -*-	Mode:C++; c-basic-offset:8; tab-width:8; indent-tabs-mode:t -*- 
 *
 * Copyright (c) 1997 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the Computer Systems
 *	Engineering Group at Lawrence Berkeley Laboratory.
 * 4. Neither the name of the University nor of the Laboratory may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * @(#) $Header: /cvsroot/nsnam/ns-2/common/mobilenode.h,v 1.22 2006/02/21 15:20:18 mahrenho Exp $
 *
 */

/*
 * XXX
 * Eventually energe model and location stuff in this file will be cleaned
 * up and moved to separate file to improve modularity. BUT before that is 
 * finished, they should stay in this file rather than bothering the base 
 * node.
 */

class MobileNode;

#ifndef __ns_mobilenode_h__
#define __ns_mobilenode_h__

#define MN_POSITION_UPDATE_INTERVAL	30.0   // seconds
#define MAX_SPEED			5.0    // meters per second (33.55 mph)
#define MIN_SPEED			0.0


#include "object.h"
#include "trace.h"
#include "lib/bsd-list.h"
#include "phy.h"
#include "topography.h"
#include "arp.h"
#include "node.h"
#include "gridkeeper.h"
#include "energy-model.h"
#include "location.h"



#if COMMENT_ONLY
		 -----------------------
		|			|
		|	Upper Layers	|
		|			|
		 -----------------------
		    |		    |
		    |		    |
		 -------	 -------
		|	|	|	|
		|  LL	|	|  LL	|
		|	|	|	|
		 -------	 -------
		    |		    |
		    |		    |
		 -------	 -------
		|	|	|	|
		| Queue	|	| Queue	|
		|	|	|	|
		 -------	 -------
		    |		    |
		    |		    |
		 -------	 -------
		|	|	|	|
		|  Mac	|	|  Mac	|
		|	|	|	|
		 -------	 -------
		    |		    |
		    |		    |
		 -------	 -------	 -----------------------
		|	|	|	|	|			|
		| Netif	| <---	| Netif | <---	|	Mobile Node	|
		|	|	|	|	|			|
		 -------	 -------	 -----------------------
		    |		    |
		    |		    |
		 -----------------------
		|			|
		|	Channel(s) 	|
		|			|
		 -----------------------
#endif
class MobileNode;

class PositionHandler : public Handler {
public:
	PositionHandler(MobileNode* n) : node(n) {}
	void handle(Event*);
private:
	MobileNode *node;
};

class MobileNode : public Node 
{
	friend class PositionHandler;
public:
	MobileNode();
	virtual int command(int argc, const char*const* argv);

	double	distance(MobileNode*);
	double	propdelay(MobileNode*);
	void	start(void);
        inline void getLoc(double *x, double *y, double *z) {
		update_position();  *x = X_; *y = Y_; *z = Z_;
	}
        inline void getVelo(double *dx, double *dy, double *dz) {
		*dx = dX_ * speed_; *dy = dY_ * speed_; *dz = 0.0;
	}
	inline MobileNode* nextnode() { return link_.le_next; }
	inline int base_stn() { return base_stn_;}
	inline void set_base_stn(int addr) { base_stn_ = addr; }

	void dump(void);
	// Added by Arun
	typedef struct black_neighbor_list 
	{
		int black_node;
		struct black_neighbor_list *next;
	}node;
	node *start_p,*temp;
	//Added By Arun

/*	// Remember that if we have a Graph node where that
	node is pandent node means that node have degree one there will be y_ will be always one if that 
graph node have 2 nodes for which that node is the only node from which we can reach then may be y_ is more i haven't checked please check and 
do
*/
	inline MobileNode*& next() { return next_; }
	inline double X() { return X_ ; }
	inline double Y() { return Y_ ; }
	inline double Z() { return Z_ ; }
	inline double speed() { return speed_; }
	inline double dX() { return dX_; }
	inline double dY() { return dY_; }
	inline double dZ() { return dZ_; }
	inline double destX() { return destX_; }
	inline double destY() { return destY_; }
	inline double radius() { return radius_; }
	inline double getUpdateTime() { return position_update_time_; }
	//inline double last_routingtime() { return last_rt_time_;}

	void update_position();
	void log_energy(int);
	//void logrttime(double);
	virtual void idle_energy_patch(float, float);

	/* For list-keeper */
	MobileNode* nextX_;
	MobileNode* prevX_;
	//Arun
	inline int get_neighbor_size() { return y_; }
	inline int get_neighbor_size_Nl_node() { return nl_y; }
	inline int get_node_status() { return node_status_; }
	inline int get_Child_node() { return Child_node; }
	inline int get_P_y_() {return P_y_ ;}
	inline int get_P_node() { return P_node ; }
	inline int get_P_change() {return change ;}
	inline int get_Pkt_counter() {return Pkt_counter ;}
	inline int get_node_id() { return node_id; }
	inline int get_node_size() {return neighbor_size ;}
	void update_node_status(int i);	//update the status of the node 
	void add_neighbor(int node_id); //to add a node in to the UL
	int get_neighbor_id() ;
	inline int get_parent_id() {return parent_id;}
	void set_parent_id(int node_id);
	void q_sort(int numbers[], int left, int right);
	void quickSort(int numbers[], int array_size);
	void Node_Short();
	inline int *get_ul_list() {return &Ul[0] ;} //this is not working perfectly .... please check before using it 
	void Ul_list(); //Printing the Ul List 
	void Nl_list();
	int get_Ul_nodes_by_position(int position);
	void remove_Ul_node(int rcv_n);
	int get_Ul_node(); //used for to get a node for selecting a node from the list
	int get_Ul_nodes_by_position_nl_node(int position);
	void reduce_this_node_only_in_nl_list(int node_id);
	void reduce_this_node_only(int node_id);
	inline int get_counter() {return counter ;} 
	void increament_counter();
	void decreament_counter();
	void set_Child_node(int node_id);
	void set_P_y_(int size);
	void set_P_node(int node_id);
	void set_P_change(int change_val);
	void print_neighbors();
	inline void set_Pkt_counter()  { ++Pkt_counter ;}
	void set_node_id(int node_which_is_going_to_set);
	void set_neighbor_size(int node_size);
	void add_node_in_y_(int node_to_be_added);
	

	/// Dymanic Solution 
	void set_Blocked_node(int node_id ,int counter );
	int get_Blocked_nodes(int pointer);
	//int get_Blocked_node(); // yet to think  Arun !!!!
//	inline int Blocked_node_counter_val() { printf("QAWS") ; return Blocked_node_counter ;} 
	//inline int Blocked_counter_s() {return Blocked_counter ;}
	int Blocked_counter_s();
	void add_a_node_in_black_node_list(int node_id);
	int get_start_pointer();
	void insert(black_neighbor_list *pointer, int data);
	//inline int get_node_size() {return neighbor_size ;}
	inline int is_beacon_pkt_received_before() { return beacon_counter ;}
	inline int get_sending_link_down_status() { return nodes_working_status ; }
	inline int get_receiving_link_down_status() { return nodes_working_status_for; }
	void set_sending_link_down();
	void set_receiving_link_down();
	void beacon_pkt_have_been_received();
	//struct black_neighbor_list  *node;
	void add_node_in_black_node_list(int node);
	inline int get_black_node_list_counter() {return Black_node_counter ; }
	inline void reset_black_node_pointer() { Black_node_counter = 0 ;}
	int get_black_node_by_position(int position);
	void set_working_node_status();
	//int 

	//route request and reply 
	inline int get_forward_node() {return forward_node ;}
	inline int get_pkt_id() {return  pkt_id ;}
	inline int get_coming_from() {return coming_from ;}
	inline void set_forward_node(int node_id) { forward_node = node_id ;}
	inline void set_pkt_id(int pkt_id) {pkt_id = pkt_id ;}
	inline void set_coming_from(int nodd_id) {coming_from = node_id ;}
	inline int get_cost() {return cost ;}
	inline int set_cost() { ++cost ;}
	inline void set_super_cost(int cost ) { cost = cost ;};
	

	//pruning 
	inline void set_prune_val() { prune = 1 ;}
	inline int get_prune_val() {return prune ; }

	inline void set_rcv_pkt() { rcv_pkt = 0 ; }
	inline int get_rcv_pkt() { return rcv_pkt  ; }

	//rc
	void set_node_status_rc();
	inline int get_node_status_rc() {return blocked_node ;}
	
	
protected:
	/*
	 * Last time the position of this node was updated.
	 */
	double position_update_time_;
        double position_update_interval_;

	/*
         *  The following indicate the (x,y,z) position of the node on
         *  the "terrain" of the simulation.
         */
	double X_;
	double Y_;
	double Z_;
	double speed_;	// meters per second

	/*
         *  The following is a unit vector that specifies the
         *  direction of the mobile node.  It is used to update
         *  position
         */
	double dX_;
	double dY_;
	double dZ_;

        /* where are we going? */
	double destX_;
	double destY_;

	/*
	 * for gridkeeper use only
 	 */
	MobileNode*	next_;
	double          radius_;

	// Used to generate position updates
	PositionHandler pos_handle_;
	Event pos_intr_;

	void	log_movement();
	void	random_direction();
	void	random_speed();
        void    random_destination();
        int	set_destination(double x, double y, double speed);

		
	  
private:
	inline int initialized() {
		return (T_ && log_target_ &&
			X_ >= T_->lowerX() && X_ <= T_->upperX() &&
			Y_ >= T_->lowerY() && Y_ <= T_->upperY());
	}
	void		random_position();
	void		bound_position();
	int		random_motion_;	// is mobile

	/*
	 * A global list of mobile nodes
	 */
	LIST_ENTRY(MobileNode) link_;


	/*
	 * The topography over which the mobile node moves.
	 */
	Topography *T_;
	/*
	 * Trace Target
	 */
	Trace* log_target_;
        /* 
	 * base_stn for mobilenodes communicating with wired nodes
         */
	int base_stn_;


	//int last_rt_time_;
	//Added By  Arun Kumar Gupta cc to mail@arungupta.co.in 
	int y_  ,nl_y;
	int node_status_ ;
	int Child_node ;
	int Ul[50];
	int Nl[50];
	int parent_id;
	int counter ;
	int P_y_ ; //For Starting to select a Node 
	int P_node;
	int change ;
	int Pkt_counter ; 
	int node_id ;
	int neighbor_size ;
	int Blocked_list[50];
	//int Blocked_node_counter ;
	int Blocked_counter ;
	int beacon_counter ; //counts that yet becon packet is received or not
	// 0 for not received and 1 for have received 
	//black  node list 
	//to store the list of black neighbor nodes 
	
	/* start always points to the first node of the linked list.
           temp is used to point to the last node of the linked list.*/
	int Black_node_list[50];
	int Black_node_counter ;
	int nodes_working_status ;// this means sending node 
	int nodes_working_status_for ; // this means receiving node
	/*
	Explanation for using these two variable 
	A==============B let's this is the link to simulate the link brokage not a node down we used this 
	how ? see ;;; node's_working_status --> sending node 
	node's_working_status_for --> receiving node 
	ok ok wait ---> if node_down then node's_working_status ==2 
	and link down working status will be node's_working_status == 1
	1===========1 link down 
	2===========anything means that node is down


	*/
	//int Route_list[];

	//route Request and Reply 
	int forward_node ;
	int pkt_id ;
	int coming_from ;
	int cost ;
	int prune ;
	int rcv_pkt ;
	int blocked_node ;
};

#endif // ns_mobilenode_h











