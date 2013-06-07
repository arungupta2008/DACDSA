/*-*-	Mode:C++; c-basic-offset:8; tab-width:8; indent-tabs-mode:t -*- 
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
 * $Header: /cvsroot/nsnam/ns-2/common/mobilenode.cc,v 1.36 2006/02/22 13:21:52 mahrenho Exp $
 *
 * Code in this file will be changed in the near future. From now on it 
 * should be treated as for backward compatibility only, although it is in
 * active use by many other code in ns. - Aug 29, 2000
 */

/* 
 * CMU-Monarch project's Mobility extensions ported by Padma Haldar, 
 * 11/98.
 */


#include <math.h>
#include <stdlib.h>
//Added By Arun for Random Generator 
#include <stdlib.h>
#include <stdio.h>
#include <time.h>



#include "connector.h"
#include "delay.h"
#include "packet.h"
#include "random.h"
#include "trace.h"
#include "address.h"

#include "arp.h"
#include "topography.h"
#include "ll.h"
#include "mac.h"
#include "propagation.h"
#include "mobilenode.h"
#include "phy.h"
#include "wired-phy.h"
#include "god.h"

// XXX Must supply the first parameter in the macro otherwise msvc
// is unhappy. 
static LIST_HEAD(_dummy_MobileNodeList, MobileNode) nodehead = { 0 };

static class MobileNodeClass : public TclClass {
public:
        MobileNodeClass() : TclClass("Node/MobileNode") {}
        TclObject* create(int, const char*const*) {
                return (new MobileNode);
        }
} class_mobilenode;

/*
 *  PositionHandler()
 *
 *  Updates the position of a mobile node every N seconds, where N is
 *  based upon the speed of the mobile and the resolution of the topography.
 *
 */
void
PositionHandler::handle(Event*)
{
	Scheduler& s = Scheduler::instance();

#if 0
	fprintf(stderr, "*** POSITION HANDLER for node %d (time: %f) ***\n",
		node->address(), s.clock());
#endif
	/*
	 * Update current location
	 */
	node->update_position();

	/*
	 * Choose a new random speed and direction
	 */
#ifdef DEBUG
        fprintf(stderr, "%d - %s: calling random_destination()\n",
                node->address_, __PRETTY_FUNCTION__);
#endif
	node->random_destination();

	s.schedule(&node->pos_handle_, &node->pos_intr_,
		   node->position_update_interval_);
}


/* ======================================================================
   Mobile Node
   ====================================================================== */

MobileNode::MobileNode(void) : 
	pos_handle_(this)
{
	X_ = Y_ = Z_ = speed_ = 0.0;
	dX_ = dY_ = dZ_ = 0.0;
	destX_ = destY_ = 0.0;

	random_motion_ = 0;
	base_stn_ = -1;
	T_ = 0;

	log_target_ = 0;
	next_ = 0;
	radius_ = 0;
	//Modified by arun
	y_  = 0  ;
	nl_y = 0;
	P_y_ = 0 ;
	P_node = 0 ;
	change = 0;
	node_status_  = 0;
	parent_id = 1000;
	Pkt_counter = 0 ;
	Blocked_counter = 0;
	prune = 0 ;
	position_update_interval_ = MN_POSITION_UPDATE_INTERVAL;
	position_update_time_ = 0.0;
	

	LIST_INSERT_HEAD(&nodehead, this, link_);	// node list
	LIST_INIT(&ifhead_);				// interface list
	bind("X_", &X_);
	bind("Y_", &Y_);
	bind("Z_", &Z_);
	bind("speed_", &speed_);
	//Arun
	bind("y_", &y_);
	bind("node_status_", &node_status_);
	bind("Child_node", &Child_node);
	//printf("Everything is initialized in Mobile Nodes \n");
	Child_node = 0  ;
	counter = 0 ;
	node_id  = -1000 ;
	neighbor_size = 0 ;
	beacon_counter = 0 ;
	nodes_working_status  = 0;
	nodes_working_status_for = 0 ;
	//route request and reply 
	forward_node = 0 ;
	pkt_id = 0;
	coming_from = 1000 ;
	cost = -1;
	rcv_pkt =  0 ;
	blocked_node = 0 ; 
	//......
	//Linked List Parameters 
	//......................................
	start_p = (node *)malloc(sizeof(node)); 
        temp = start_p;
        temp -> next = NULL;
	//................................
	//Blocked_node_counter = 0 ;
	
	Black_node_counter  = 0 ;
	for(int i = 0 ; i <50;++i)
	{
		Ul[i] = 500;
		Nl[i] = 500;
		Blocked_list[i] = 500;
		Black_node_list[i] = 500;
		// for to store the list of black nodes 
	}
	
}

int
MobileNode::command(int argc, const char*const* argv)
{
	Tcl& tcl = Tcl::instance();
	if(argc == 2) {
		if(strcmp(argv[1], "start") == 0) {
		        start();
			return TCL_OK;
		} else if(strcmp(argv[1], "log-movement") == 0) {
#ifdef DEBUG
                        fprintf(stderr,
                                "%d - %s: calling update_position()\n",
                                address_, __PRETTY_FUNCTION__);
#endif
		        update_position();
		        log_movement();
			return TCL_OK;
		} else if(strcmp(argv[1], "log-energy") == 0) {
			log_energy(1);
			return TCL_OK;
		} else if(strcmp(argv[1], "powersaving") == 0) {
			energy_model()->powersavingflag() = 1;
			energy_model()->start_powersaving();
			return TCL_OK;
		} else if(strcmp(argv[1], "adaptivefidelity") == 0) {
			energy_model()->adaptivefidelity() = 1;
			energy_model()->powersavingflag() = 1;
			energy_model()->start_powersaving();
			return TCL_OK;
		} else if (strcmp(argv[1], "energy") == 0) {
			Tcl& tcl = Tcl::instance();
			tcl.resultf("%f", energy_model()->energy());
			return TCL_OK;
		} else if (strcmp(argv[1], "adjustenergy") == 0) {
			// assume every 10 sec schedule and 1.15 W 
			// idle energy consumption. needs to be
			// parameterized.
			idle_energy_patch(10, 1.15);
			energy_model()->total_sndtime() = 0;
			energy_model()->total_rcvtime() = 0;
			energy_model()->total_sleeptime() = 0;
			return TCL_OK;
		} else if (strcmp(argv[1], "on") == 0) {
			energy_model()->node_on() = true;
			tcl.evalf("%s set netif_(0)", name_);
			const char *str = tcl.result();
			tcl.evalf("%s NodeOn", str);
			God::instance()->ComputeRoute();
			return TCL_OK;
		} else if (strcmp(argv[1], "off") == 0) {
			energy_model()->node_on() = false;
			tcl.evalf("%s set netif_(0)", name_);
			const char *str = tcl.result();
			tcl.evalf("%s NodeOff", str);
			tcl.evalf("%s set ragent_", name_);
			str = tcl.result();
			tcl.evalf("%s reset-state", str);
			God::instance()->ComputeRoute();
		     	return TCL_OK;
		} else if (strcmp(argv[1], "shutdown") == 0) {
			// set node state
			//Phy *p;
			energy_model()->node_on() = false;
			
			//p = ifhead().lh_first;
			//if (p) ((WirelessPhy *)p)->node_off();
			return TCL_OK;
		} else if (strcmp(argv[1], "startup") == 0) {
			energy_model()->node_on() = true;
			return TCL_OK;
		}
	
	} else if(argc == 3) {
		if(strcmp(argv[1], "addif") == 0) {
			WiredPhy* phyp = (WiredPhy*)TclObject::lookup(argv[2]);
			if(phyp == 0)
				return TCL_ERROR;
			phyp->insertnode(&ifhead_);
			phyp->setnode(this);
			return TCL_OK;
		} else if (strcmp(argv[1], "setsleeptime") == 0) {
			energy_model()->afe()->set_sleeptime(atof(argv[2]));
			energy_model()->afe()->set_sleepseed(atof(argv[2]));
			return TCL_OK;
		} else if (strcmp(argv[1], "setenergy") == 0) {
			energy_model()->setenergy(atof(argv[2]));
			return TCL_OK;
		} else if (strcmp(argv[1], "settalive") == 0) {
			energy_model()->max_inroute_time() = atof(argv[2]);
			return TCL_OK;
		} else if (strcmp(argv[1], "maxttl") == 0) {
			energy_model()->maxttl() = atoi(argv[2]);
			return TCL_OK;
		} else if(strcmp(argv[1], "radius") == 0) {
                        radius_ = strtod(argv[2],NULL);
                        return TCL_OK;
                } else if(strcmp(argv[1], "random-motion") == 0) {
			random_motion_ = atoi(argv[2]);
			return TCL_OK;
		} else if(strcmp(argv[1], "addif") == 0) {
			WirelessPhy *n = (WirelessPhy*)
				TclObject::lookup(argv[2]);
			if(n == 0)
				return TCL_ERROR;
			n->insertnode(&ifhead_);
			n->setnode(this);
			return TCL_OK;
		} else if(strcmp(argv[1], "topography") == 0) {
			T_ = (Topography*) TclObject::lookup(argv[2]);
			if (T_ == 0)
				return TCL_ERROR;
			return TCL_OK;
		} else if(strcmp(argv[1], "log-target") == 0) {
			log_target_ = (Trace*) TclObject::lookup(argv[2]);
			if (log_target_ == 0)
				return TCL_ERROR;
			return TCL_OK;
		} else if (strcmp(argv[1],"base-station") == 0) {
			base_stn_ = atoi(argv[2]);
			if(base_stn_ == -1)
				return TCL_ERROR;
			return TCL_OK;
		} 
	} else if (argc == 4) {
		if (strcmp(argv[1], "idleenergy") == 0) {
			idle_energy_patch(atof(argv[2]),atof(argv[3]));
			return TCL_OK;
		}
	} else if (argc == 5) {
		if (strcmp(argv[1], "setdest") == 0) { 
			/* <mobilenode> setdest <X> <Y> <speed> */
#ifdef DEBUG
			fprintf(stderr, "%d - %s: calling set_destination()\n",
				address_, __FUNCTION__);
#endif
  
			if (set_destination(atof(argv[2]), atof(argv[3]), 
					    atof(argv[4])) < 0)
				return TCL_ERROR;
			return TCL_OK;
		}
	}
	return Node::command(argc, argv);
}


/* ======================================================================
   Other class functions
   ====================================================================== */
void
MobileNode::dump(void)
{
	Phy *n;
	fprintf(stdout, "Index: %d\n", address_);
	fprintf(stdout, "Network Interface List\n");
 	for(n = ifhead_.lh_first; n; n = n->nextnode() )
		n->dump();	
	fprintf(stdout, "--------------------------------------------------\n");
}

/* ======================================================================
   Position Functions
   ====================================================================== */
void 
MobileNode::start()
{
	Scheduler& s = Scheduler::instance();

	if(random_motion_ == 0) {
		log_movement();
		return;
	}

	assert(initialized());

	random_position();
#ifdef DEBUG
        fprintf(stderr, "%d - %s: calling random_destination()\n",
                address_, __PRETTY_FUNCTION__);
#endif
	random_destination();
	s.schedule(&pos_handle_, &pos_intr_, position_update_interval_);
}

void 
MobileNode::log_movement()
{
        if (!log_target_) 
		return;

	Scheduler& s = Scheduler::instance();
	sprintf(log_target_->pt_->buffer(),
		"M %.5f %d (%.2f, %.2f, %.2f), (%.2f, %.2f), %.2f",
		s.clock(), address_, X_, Y_, Z_, destX_, destY_, speed_);
	log_target_->pt_->dump();
}


void
MobileNode::log_energy(int flag)
{
	if (!log_target_) 
		return;
	Scheduler &s = Scheduler::instance();
	if (flag) {
		sprintf(log_target_->pt_->buffer(),"N -t %f -n %d -e %f", s.clock(),
			address_, energy_model_->energy()); 
	} else {
		sprintf(log_target_->pt_->buffer(),"N -t %f -n %d -e 0 ", s.clock(),
			address_); 
	}
	log_target_->pt_->dump();
}

//void
//MobileNode::logrttime(double t)
//{
//	last_rt_time_ = (int)t;
//}


void 
MobileNode::set_node_status_rc()
{
	//int i = 23 ;
	//printf("node ind %d\n" , i);
	printf("blocked:%d\n",blocked_node);
	blocked_node = 1;
}
void
MobileNode::bound_position()
{
	double minX;
	double maxX;
	double minY;
	double maxY;
	int recheck = 1;

	assert(T_ != 0);

	minX = T_->lowerX();
	maxX = T_->upperX();
	minY = T_->lowerY();
	maxY = T_->upperY();

	while (recheck) {
		recheck = 0;
		if (X_ < minX) {
			X_ = minX + (minX - X_);
			recheck = 1;
		}
		if (X_ > maxX) {
			X_ = maxX - (X_ - maxX);
			recheck = 1;
		}
		if (Y_ < minY) {
			Y_ = minY + (minY - Y_);
			recheck = 1;
		}
		if (Y_ > maxY) {
			Y_ = maxY- (Y_ - maxY);
			recheck = 1;
		}
		if (recheck) {
			fprintf(stderr, "Adjust position of node %d\n",address_);
		}
	}
}

int
MobileNode::set_destination(double x, double y, double s)
{
	assert(initialized());

	if(x >= T_->upperX() || x <= T_->lowerX())
		return -1;
	if(y >= T_->upperY() || y <= T_->lowerY())
		return -1;
	
	update_position();	// figure out where we are now
	
	destX_ = x;
	destY_ = y;
	speed_ = s;
	
	dX_ = destX_ - X_;
	dY_ = destY_ - Y_;
	dZ_ = 0.0;		// this isn't used, since flying isn't allowed

	double len;
	
	if (destX_ != X_ || destY_ != Y_) {
		// normalize dx, dy to unit len
		len = sqrt( (dX_ * dX_) + (dY_ * dY_) );
		dX_ /= len;
		dY_ /= len;
	}
  
	position_update_time_ = Scheduler::instance().clock();

#ifdef DEBUG
	fprintf(stderr, "%d - %s: calling log_movement()\n", 
		address_, __FUNCTION__);
#endif
	log_movement();

	/* update gridkeeper */
	if (GridKeeper::instance()){
		GridKeeper* gp =  GridKeeper::instance();
		gp-> new_moves(this);
	}                     

	if (namChan_ != 0) {
		
		double v = speed_ * sqrt( (dX_ * dX_) + (dY_ * dY_)); 
		
		sprintf(nwrk_,     
			"n -t %f -s %d -x %f -y %f -U %f -V %f -T %f",
			Scheduler::instance().clock(),
			nodeid_,
			X_, Y_,
			speed_ * dX_, speed_ * dY_,
			( v != 0) ? len / v : 0. );   
		namdump();         
	}
	return 0;
}
//Arun Mobile Node Status =================================================================mycode

void
MobileNode::update_node_status(int i)	
{
	node_status_   = i;
	//return node_status_;
}

void
MobileNode::add_neighbor(int node_id)
{
	
	//printf("y_ == %d\n" , y_);
	Ul[y_]  = node_id;
	Nl[nl_y]  = node_id;
	++y_;
	++nl_y;
	++P_y_;
	++neighbor_size;
	
}
void 
MobileNode::set_P_y_(int size)
{
	P_y_ = size;
}
void 
MobileNode::set_P_node(int set_node_id)
{
	P_node = set_node_id;
}

void 
MobileNode::increament_counter()
{
	++counter;
}

void 
MobileNode::beacon_pkt_have_been_received()
{
	beacon_counter = 1;
}
void
MobileNode::decreament_counter()
{
	--counter;
}

int
MobileNode::get_neighbor_id()
{
	return Nl[nl_y-1];

}

void
MobileNode::set_sending_link_down()
{
	nodes_working_status = 1 ;
}

void 
MobileNode::set_receiving_link_down()
{
	nodes_working_status_for = 1 ;
}

void 
MobileNode::set_working_node_status()
{
	nodes_working_status = 2 ;
}

void
MobileNode::set_parent_id(int node_id)
{
	parent_id = node_id;
	//printf("Working parent_id :: %d\n" , parent_id);
	//printf("Here\n");
}

void
MobileNode::Node_Short()
{
	//printf("I am at Node_Short\n");
	quickSort(Ul,  y_);
}


void 
MobileNode::set_node_id(int node_which_is_going_to_set)
{
	node_id = node_which_is_going_to_set;
}

void
MobileNode::set_neighbor_size(int node_size)
{
	neighbor_size = node_size;
}


void
MobileNode::Ul_list()
{
	//printf("@@@@@@@@@@@@@@@@@\n");
	int i = 0;
	while(Ul[i] != 500)
	{
		//printf("Node is %d\n" , Ul[i] );
		++i;
	}
}

void
MobileNode::Nl_list()
{
	//printf("@@@@@@@@@@@@@@@@@\n");
	int i = 0;
	while(Nl[i] != 500)
	{
		//printf("Node is %d\n" , Nl[i] );
		++i;
	}
}


void 
MobileNode::set_Child_node(int node_id)
{
	Child_node = node_id;
}

void 
MobileNode::set_P_change(int change_val)
{
	change  = change_val;
}


int 
MobileNode::get_Ul_nodes_by_position(int position)
{
	
	return Ul[position];
}

int 
MobileNode::get_Ul_nodes_by_position_nl_node(int position)
{
	return Nl[position];
}


void 
MobileNode::add_node_in_black_node_list(int node)
{
	Black_node_list[Black_node_counter] = node;
	printf("Black Node is ::%d \n",Black_node_list[Black_node_counter]);
	++Black_node_counter;
}

int 
MobileNode::get_black_node_by_position(int position)
{
	return Black_node_list[position];
}
void
MobileNode::remove_Ul_node(int rcv_n)
{
	//printf("Node %d is Going to be Deleted\n ", Ul[rcv_n]);
	while (rcv_n <= y_)
	{
		
		Ul[rcv_n] = Ul[rcv_n+1];
		++rcv_n;
		
	}
	y_ = y_ - 1 ;
	//printf("Node %d at That Place this Doesn't mean that is in first Position\n ", Ul[rcv_n]);
	//printf("Node is Removed");
}

void
MobileNode::add_node_in_y_(int node_to_be_added)
{
	//Ul[] for unknown list 
	// y_ is the counter 
	printf("y_ counter %d\n" , y_);
	printf("Last node in that list Ul[y_] %d\n" , Ul[y_]);
	Ul[y_] = node_to_be_added;
	++y_ ;	
	printf("\nAfter Adding values (MobilenNode) add_node_in_y_  \n\n");
	printf("y_ counter %d\n" , y_);
	printf("Last node in that list Ul[y_] %d\n" , Ul[y_-1]);
		
}
int 
MobileNode::get_Ul_node()
{	
	int val = Random::integer(y_);
	//int val = God::instance()->BestNode();
	//printf("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n\n");
	//printf("This Node is Selected as a Next Black Node %d\n\n" , Ul[val]);
	//printf("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n\n");
	//printf("Random Value  :: %d\n" , val);

	//printf("Value::%d\n" , val);
	
	return Ul[val];
}


void
MobileNode::print_neighbors()
{
	int i = 0 ;
	while(Ul[i] != 500)
	{
		//printf("Node's is %d\n" ,Ul[i] );
		++i;
	}
}

void 
MobileNode::quickSort(int numbers[], int array_size)
{
  q_sort(numbers, 0, array_size - 1);
}
 
 
void 
MobileNode::q_sort(int numbers[], int left, int right)
{
  int pivot, l_hold, r_hold;
 
  l_hold = left;
  r_hold = right;
  pivot = numbers[left];
  while (left < right)
  {
    while ((numbers[right] >= pivot) && (left < right))
      right--;
    if (left != right)
    {
      numbers[left] = numbers[right];
      left++;
    }
    while ((numbers[left] <= pivot) && (left < right))
      left++;
    if (left != right)
    {
      numbers[right] = numbers[left];
      right--;
    }
  }
  numbers[left] = pivot;
  pivot = left;
  left = l_hold;
  right = r_hold;
  if (left < pivot)
    q_sort(numbers, left, pivot-1);
  if (right > pivot)
    q_sort(numbers, pivot+1, right);
}


void 
MobileNode::reduce_this_node_only(int node_id)
{
	int i = 0 ;
	int found  = 0 ;
	while(node_id != Ul[i])
	{
		++i;
		continue;
	}
	if(i <= y_)
	{
		for( i ; i<=y_;++i)
		{
		
			Ul[i] = Ul[i+1];
		}
		y_ = y_ - 1 ;
	}
	//else
		//printf("That Node not Found\n");
	
}

void 
MobileNode::reduce_this_node_only_in_nl_list(int node_id)
{
	int i = 0 ;
	int found  = 0 ;
	while(node_id != Nl[i])
	{
		++i;
		continue;
	}
	if(i <= nl_y)
	{
		for( i ; i<=nl_y;++i)
		{
		
			Nl[i] = Nl[i+1];
		}
		nl_y = nl_y - 1 ;
	}
	//else
	
		//printf("That Node not Found\n");
	
}

//dynamic 


void
MobileNode::set_Blocked_node(int node_id , int counter  )
{
	//printf("We are here !!!!");
	//printf("blocked Counter :: %d" , Blocked_node_counter_val() ); //HEre is the Error i duuno why we are not able to get the Blocked_node_counter value 
	Blocked_list[counter] = node_id;
	++Blocked_counter;
	//printf("\nBlocked Node %d\n" ,Blocked_list[counter] );
	//printf("\nBlocked Node %d\n" ,Blocked_list[Blocked_counter] );
	//printf("Completed");
}

int
MobileNode::get_Blocked_nodes(int pointer)
{
	return Blocked_list[pointer];
}


int
MobileNode::Blocked_counter_s()
{
	//printf("Blocked_counter %d\n",Blocked_counter);
	return Blocked_counter ;
}


void
MobileNode::add_a_node_in_black_node_list(int node_id)
{

	//http://www.cprogramming.com/snippets/source-code/singly-linked-list-insert-remove-add-count   
	//Linked List Data structure :)
	insert(start_p , node_id);

}
// to insert in Linked List 
void 
MobileNode::insert(black_neighbor_list *pointer, int data)
{
	//http://www.thelearningpoint.net/computer-science/data-structures-singly-linked-list-with-c-program-source-code
        /* Iterate through the list till we encounter the last node.*/
        while(pointer->next!=NULL)
        {
                pointer = pointer -> next;
        }
        /* Allocate memory for the new node and put data in it.*/
        pointer->next = (node *)malloc(sizeof(node));
        pointer = pointer->next;
        pointer->black_node = data;
	printf("\n******************************");
	printf("\nnode %d is added in the list\n" , pointer->black_node);
	printf("******************************\n");
	
        pointer->next = NULL;
}

int 
MobileNode::get_start_pointer()
{
	//int pointer = &start_p;	
	//return pointer ; 
}


//=========================================================================================
void 
MobileNode::update_position()
{
	double now = Scheduler::instance().clock();
	double interval = now - position_update_time_;
	double oldX = X_;
	//double oldY = Y_;

	if ((interval == 0.0)&&(position_update_time_!=0))
		return;         // ^^^ for list-based imprvmnt 


	// CHECK, IF THE SPEED IS 0, THEN SKIP, but usually it's not 0
	X_ += dX_ * (speed_ * interval);
	Y_ += dY_ * (speed_ * interval);

	if ((dX_ > 0 && X_ > destX_) || (dX_ < 0 && X_ < destX_))
	  X_ = destX_;		// correct overshoot (slow? XXX)
	if ((dY_ > 0 && Y_ > destY_) || (dY_ < 0 && Y_ < destY_))
	  Y_ = destY_;		// correct overshoot (slow? XXX)
	
	/* list based improvement */
	if(oldX != X_)// || oldY != Y_)
		T_->updateNodesList(this, oldX);//, oldY);
	// COMMENTED BY -VAL- // bound_position();

	// COMMENTED BY -VAL- // Z_ = T_->height(X_, Y_);

#if 0
	fprintf(stderr, "Node: %d, X: %6.2f, Y: %6.2f, Z: %6.2f, time: %f\n",
		address_, X_, Y_, Z_, now);
#endif
	position_update_time_ = now;
}


void
MobileNode::random_position()
{
	if (T_ == 0) {
		fprintf(stderr, "No TOPOLOGY assigned\n");
		exit(1);
	}

	X_ = Random::uniform() * T_->upperX();
	Y_ = Random::uniform() * T_->upperY();
	Z_ = T_->height(X_, Y_);

	position_update_time_ = 0.0;
}

void
MobileNode::random_destination()
{
	if (T_ == 0) {
		fprintf(stderr, "No TOPOLOGY assigned\n");
		exit(1);
	}

	random_speed();
#ifdef DEBUG
        fprintf(stderr, "%d - %s: calling set_destination()\n",
                address_, __FUNCTION__);
#endif
	(void) set_destination(Random::uniform() * T_->upperX(),
                               Random::uniform() * T_->upperY(),
                               speed_);
}

void
MobileNode::random_direction()
{
	/* this code isn't used anymore -dam 1/22/98 */
	double len;

	dX_ = (double) Random::random();
	dY_ = (double) Random::random();

	len = sqrt( (dX_ * dX_) + (dY_ * dY_) );

	dX_ /= len;
	dY_ /= len;
	dZ_ = 0.0;				// we're not flying...

	/*
	 * Determine the sign of each component of the
	 * direction vector.
	 */
	if (X_ > (T_->upperX() - 2*T_->resol())) {
		if (dX_ > 0) 
			dX_ = -dX_;
	} else if (X_ < (T_->lowerX() + 2*T_->resol())) {
		if (dX_ < 0) 
			dX_ = -dX_;
	} else if (Random::uniform() <= 0.5) {
		dX_ = -dX_;
	}

	if (Y_ > (T_->upperY() - 2*T_->resol())) {
		if (dY_ > 0) 
			dY_ = -dY_;
	} else if (Y_ < (T_->lowerY() + 2*T_->resol())) {
		if (dY_ < 0) 
			dY_ = -dY_;
	} else if(Random::uniform() <= 0.5) {
		dY_ = -dY_;
	}
#if 0
	fprintf(stderr, "Location: (%f, %f), Direction: (%f, %f)\n",
		X_, Y_, dX_, dY_);
#endif
}

void
MobileNode::random_speed()
{
	speed_ = Random::uniform() * MAX_SPEED;
}

double
MobileNode::distance(MobileNode *m)
{
	update_position();		// update my position
	m->update_position();		// update m's position

        double Xpos = (X_ - m->X_) * (X_ - m->X_);
        double Ypos = (Y_ - m->Y_) * (Y_ - m->Y_);
	double Zpos = (Z_ - m->Z_) * (Z_ - m->Z_);

        return sqrt(Xpos + Ypos + Zpos);
}

double
MobileNode::propdelay(MobileNode *m)
{
	return distance(m) / SPEED_OF_LIGHT;
}

void 
MobileNode::idle_energy_patch(float /*total*/, float /*P_idle*/)
{
}
