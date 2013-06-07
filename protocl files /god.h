
/* -*-	Mode:C++; c-basic-offset:8; tab-width:8; indent-tabs-mode:t -*- */
/*
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
 */
/* Ported from CMU/Monarch's code, nov'98 -Padma.*/
/* -*- c++ -*-
   god.h

   General Operations Director

   perform operations requiring omnipotence in the simulation
   */

#ifndef __god_h
#define __god_h

#include <stdarg.h>
#include "bi-connector.h"
#include "object.h"
#include "packet.h"
#include "trace.h"

#include "node.h"
#include "diffusion/hash_table.h"


// Added by Chalermek  12/1/99

#define MIN_HOPS(i,j)    min_hops[i*num_nodes+j]
#define NEXT_HOP(i,j)    next_hop[i*num_nodes+j]
#define SRC_TAB(i,j)     source_table[i*num_nodes+j]
#define SK_TAB(i,j)      sink_table[i*num_nodes+j]
#define	UNREACHABLE	 0x00ffffff
#define RANGE            250.0                 // trasmitter range in meters


class NodeStatus {
public:
  bool is_source_;
  bool is_sink_;
  bool is_on_trees_;

  NodeStatus() { is_source_ = is_sink_ = is_on_trees_ = false; }
};


// Cut and Paste from setdest.h   -- Chalermek 12/1/99

class vector {
public:
	vector(double x = 0.0, double y = 0.0, double z = 0.0) {
		X = x; Y = y; Z = z;
	}
	double length() {
		return sqrt(X*X + Y*Y + Z*Z);
	}

	inline void operator=(const vector a) {
		X = a.X;
		Y = a.Y;
		Z = a.Z;
	}
	inline void operator+=(const vector a) {
		X += a.X;
		Y += a.Y;
		Z += a.Z;
	}
	inline int operator==(const vector a) {
		return (X == a.X && Y == a.Y && Z == a.Z);
	}
	inline int operator!=(const vector a) {
		return (X != a.X || Y != a.Y || Z != a.Z);
	}
	inline vector operator-(const vector a) {
		return vector(X-a.X, Y-a.Y, Z-a.Z);
	}
	friend inline vector operator*(const double a, const vector b) {
		return vector(a*b.X, a*b.Y, a*b.Z);
	}
	friend inline vector operator/(const vector a, const double b) {
		return vector(a.X/b, a.Y/b, a.Z/b);
	}

	double X;
	double Y;
	double Z;
};

// ------------------------


class God : public BiConnector {
public:
        God();

        int             command(int argc, const char* const* argv);

        void            recv(Packet *p, Handler *h);
        void            stampPacket(Packet *p);

        int initialized() {
                return num_nodes && min_hops && uptarget_;
        }

        int             hops(int i, int j);
        static God*     instance() { assert(instance_); return instance_; }
	int nodes() { return num_nodes; }

        inline void getGrid(double *x, double *y, double *z) {
		*x = maxX; *y = maxY; *z = gridsize_;
	}


  // Added by Chalermek 12/1/99

        int  data_pkt_size;        // in bytes. 
        int  num_alive_node;
        int  num_connect;
        int  num_recv;
        int  num_compute;          // number of route-computation times
        double prev_time;          // the previous time it computes the route
        int  num_data_types;      
        int  **source_table;
        int  *sink_table;
        int  *num_send;            // for each data type
        Data_Hash_Table dtab;

        void DumpNodeStatus();
        void DumpNumSend();
        void CountNewData(int *attr);
        void IncrRecv();
        bool ExistSource();
        bool ExistSink();
        bool IsPartition();
        void StopSimulation();
        void CountConnect();
        void CountAliveNode();
        void ComputeRoute();      
        int  NextHop(int from, int to);
        void ComputeNextHop();     // Look at min_hops to fill in next_hop
        void Dump();               // Dump all internal data
        bool IsReachable(int i, int j);  // Is node i reachable to node j ?
        bool IsNeighbor(int i, int j);   // Is node i a neighbor of node j ?
        void ComputeW();           // Initialize the connectivity metrix
        void floyd_warshall();     // Calculate the shortest path

        void AddSink(int dt, int skid);
        void AddSource(int dt, int srcid);
        void Fill_for_Sink(int dt, int srcid);
        void Fill_for_Source(int dt, int skid);
        void Rewrite_OIF_Map();
        void UpdateNodeStatus();
        
        // Return number of next oifs in ret_num_oif.
        // Return array of next oifs as return value of the function.

        int *NextOIFs(int dt, int srcid, int curid, int *ret_num_oif);
  
        // serve for GAF algorithm
  
        int load_grid(int,int,int);
	// finding distance by Arun
	//arun
	double getdistance(int i,int k) ;
	int changestatus(nsaddr_t node_id , int status_code);
	int get_node_status_god(int rcv_n);
	int node_neighbors_initially(int node_id) ;
	void find_neighbor();
	bool IsNeighborr(int i, int j);
	void god_set_parent_id(int rcv_id , int snd_id );
	//rcv id :: where we are going to change the status of node 
	int get_parent_id(int rcv_n);
        void god_Reduce_Nodes(int snd_node_id ,int  rcv_node_id );
	void god_Ul_list(int rcv_node_id);
	int god_select_a_node(int netid);
	int get_neighbor_size(int node_id) ;// to retrieve the y value 
	void reduce_this_node_only(int node_id , int node_which_is_going_to_be_deleted);
	void increament_counter(int node_id);
	void decreament_counter(int node_id);
	int  BestNode(int node_id);
	int get_counter(int node_id);//void decreament_counter(int node_id); // void increament_counter(int node_id); // all are connected 
	void print_the_whole_node_status();
	void set_child_id(int node_id , int Child_id);
	int get_Child_node(int node_id);
	int get_number_of_nodes();
	int get_P_y_(int node_id);
	void set_P_y_(int node_id , int size );
	void set_P_change(int node_id , int change_val);
	int get_P_change(int node_id);
	int get_P_node(int node_id);
	void set_P_node(int node_id , int set_node_id);
	int  get_node_y_(int node_id);
	void print_whole_end_status();
	void get_Single_nodes_Neighbors(int node_id);
	int pick_up_a_Best_node_centralized();
	void set_Pkt_counter(int node_id);
	int get_Pkt_counter(int node_id);
	//int get_neighbor_size_neighbor(int node_id);
	void set_node_id(int node_id , int node_which_is_going_to_set);
	void set_neighbor_size( int node_id ,int node_size );
	int get_node_id(int node_id);
	int get_node_size(int node_id);
	void quickSort(int numbers[], int array_size);
	void q_sort(int numbers[], int left, int right);
	int get_total_nodes();
	void add_node_in_y_(int node_id , int src_id);
	//void check_whether_status_of_leaf_node(int node_id);


	//Testing Algoritms
	void Guha_Kular_Algo_1();
	int count_all_white_node_for_single_node(int node_id);
	bool if_any_white_node_is_white_left();
	void update_node_array_status(int Grey_node_array[] , int Grey_node_size[] , int array_size  );
	void Guha_Kular_Algo_2();
	int find_the_count(int Grey_parent_node , int White_child_node);
	int white_or_grey_node_who_is_covering_max_whitenode();
	void Guha_Kular_Algo_3();

	// Dynamic Graph
	void add_blocked_node(int Receiver_node , int Sending_node);
	//here Receiver_node is the id of node who will node receive the packet's send by node Sending_node
	//  Seding_node -----------will_not_receieve---------------> Receiving_node
	void get_neighbor_black_node_list(int node_id);
	int is_beacon_pkt_received_before(int node_id);
	void beacon_pkt_have_been_received(int node_id);
	bool is_sending_node_is_blocked(int Receiver_node, int Sending_node);
	int get_black_node_by_position(int node_id, int position);
	int get_node_list_size(int node_id);
	bool is_that_node_in_black_node_list(int responsible_node_for_fixing , int problem_node);
	int if_node_is_in_2_hop(int responsible_node_for_fixing ,int problem_node);
	int get_sending_link_down_status(int node_id) ;
	//int get_receiving_link_down(int node_id) ;
	//void set_sending_link_down(int node_id);
	void set_receiving_link_down(int node_id);
	void set_working_node_status_down(int node_id);
	int get_working_node_status(int node_id) ;
	void set_sending_link_down(int node_id);
	int get_receiving_link_down(int node_id);
	bool is_that_node_is_the_leave_node_in_back_bone(int responsible_node_for_fixing);
	int is_that_node_have_any_other_black_node(int responsible_node_for_fixing ,int problem_node );




	//prune 
		
	void set_prune_val(int node_id ); //{ mb_node[node_id]->set_prune_val()  ;}
	int get_prune_val(int node_id)  ; //{return mb_node[node_id]->get_prune_val() ; }
	void check_whether_status_of_leaf_node(int node_id);

	void set_rcv_pkt(int node_id);// { mb_node[node_id]->set_rcv_pkt()  ;}
	int get_rcv_pkt(int node_id);//{return mb_node[node_id]->get_rcv_pkt() ; }


	//rc
	void set_node_status_rc(int node_id);
	int get_node_status_rc(int node_id) ;











	int get_forward_node(int node_id) ;
	int get_pkt_id(int node_id) ;
	int  get_coming_from(int node_id) ;	
	void set_forward_node(int node_id , int forward_id);
	void set_pkt_id(int node_id , int pkt_id) ;
	void set_coming_from(int nodd_id ,int coming_id);
	int get_cost(int node_id);
	void set_cost(int node_id);
	void set_super_cost(int node_id , int  cost);

        int getMyGrid(double x, double y);
        int getMyLeftGrid(double x, double y);
        int getMyRightGrid(double x, double y);
        int getMyTopGrid(double x, double y);
        int getMyBottomGrid(double x, double y);

	

	
        
        inline int getMyGridSize() {
		return gridsize_;
	}

  // -----------------------


private:
        int num_nodes;
        int* min_hops;   // square array of num_nodesXnum_nodes
                         // min_hops[i * num_nodes + j] giving 
			 // minhops between i and j
        static God*     instance_;


        // Added by Chalermek    12/1/99

        bool active;
        bool allowTostop;
        MobileNode **mb_node; // mb_node[i] giving pointer to object 
                              // mobile node i
        NodeStatus *node_status;
        int *next_hop;        // next_hop[i * num_nodes + j] giving
                              //   the next hop of i where i wants to send
                              //	 a packet to j.

        int maxX;          // keeping grid demension info: max X, max Y and 
        int maxY;          // grid size
        int gridsize_;
        int gridX;
        int gridY;

};

#endif

