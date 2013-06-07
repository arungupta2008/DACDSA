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
 *    notice, this list of conditions and the following disclaim
er in the
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
 * $Header: /cvsroot/nsnam/ns-2/mobile/god.cc,v 1.20 2006/12/27 14:57:23 tom_henderson Exp $
 */

/* Ported from CMU/Monarch's code, nov'98 -Padma.*/

/*
 * god.cc
 *
 * General Operations Director
 *
 * perform operations requiring omnipotence in the simulation
 *
 * NOTE: Tcl node indexs are 0 based, NS C++ node IP addresses (and the
 * node->index() are 1 based.
 *
 */

#include <object.h>
#include <packet.h>
#include <ip.h>
#include <god.h>
#include <sys/param.h>  /* for MIN/MAX */
#include <math.h>

#include "diffusion/hash_table.h"
#include "mobilenode.h"

God* God::instance_;

static class GodClass : public TclClass {
public:
        GodClass() : TclClass("God") {}
        TclObject* create(int, const char*const*) {
                return (new God);
        }
} class_God;


God::God()
{
        min_hops = 0;
        num_nodes = 0;
	printf("God initialized\n");
        data_pkt_size = 64;
	mb_node = 0;
	next_hop = 0;
	prev_time = -1.0;
	num_alive_node = 0;
	num_connect = 0;
	num_recv = 0;
	num_compute = 0;
	num_data_types = 0;
	source_table = 0;
	sink_table = 0;
	num_send = 0;
	active = false;
	allowTostop = false;
}


// Added by Chalermek 12/1/99

int God::NextHop(int from, int to)
{
  if (active == false) {
    perror("God is off.\n");
    exit(-1);
  }

  if (from >= num_nodes) {
    perror("index from higher than the maximum number of nodes.\n");
    return -1;
  }

  if (to >= num_nodes) {
    perror("index to higher than the maximum number of nodes.\n");
    return -1;
  }

  return NEXT_HOP(from,to);
}


void God::ComputeNextHop()
{
  if (active == false) {
    return;
  }

  int from, to, neighbor;

  for (from=0; from<num_nodes; from++) {
    for (to=0; to<num_nodes; to++) {

      NEXT_HOP(from,to) = UNREACHABLE;

      if (from==to) {
	NEXT_HOP(from,to) = from;     // next hop is itself.
      }

      if (MIN_HOPS(from, to) == UNREACHABLE) {
	continue;
      }

      for (neighbor=0; neighbor<num_nodes; neighbor++){
	if ( MIN_HOPS(from, neighbor) != 1) {
	  continue;
	}

	if ( MIN_HOPS(from, to) == (MIN_HOPS(neighbor,to) +1) ) {
	  NEXT_HOP(from, to) = neighbor;
	  break;
	}
      }

    }
  }
}


void God::UpdateNodeStatus()
{
  int i,j;
  int count, cur, sk, srcid, dt;

   for (i=0; i<num_data_types; i++) {
     for (j=0; j<num_nodes; j++) {
       if (SRC_TAB(i,j) != NULL) {
	 node_status[j].is_source_ = true;
       }
     }
   }

   for (i=0; i<num_data_types; i++) {
     for (j=0; j<num_nodes; j++) {
       if (SK_TAB(i,j) > 0) {
	 node_status[j].is_sink_ = true;
       }
     }
   }

   for (dt=0; dt < num_data_types; dt++) {
     for (srcid=0; srcid < num_nodes; srcid++) {
       if (SRC_TAB(dt,srcid) == NULL) 
	 continue;
       for (sk = 0; sk < num_nodes; sk++) {
	 if (SK_TAB(dt, sk) == 0)
	   continue;
	 cur = srcid;
	 count = 0;
	 node_status[cur].is_on_trees_ = true;
	 while (cur != sk) {
	   if (NextHop(cur, sk) == UNREACHABLE)
	     break;

	   assert(NextHop(cur,sk) >= 0 && NextHop(cur, sk) < num_nodes);

	   cur = NextHop(cur, sk);      
	   node_status[cur].is_on_trees_ = true;

	   count ++;
	   assert(count < num_nodes);
	 }
       }
     }
   }

   Dump();
   DumpNodeStatus();
}


void God::DumpNodeStatus()
{
  for (int i=0; i < num_nodes; i++) {
    printf("Node %d status (sink %d, source %d, on_tree %d)\n", i, 
	   node_status[i].is_sink_, node_status[i].is_source_, 
	   node_status[i].is_on_trees_);
  }
}

void God::DumpNumSend()
{
#ifdef DEBUG_OUTPUT
  for (int i=0; i < num_data_types; i++) {
    fprintf(stdout, "God: data type %d distinct events %d\n", i, num_send[i]);
  }
#endif
}


void God::Dump()
{
   int i, j, k, l;

   // Dump min_hops array

   fprintf(stdout,"Dump min_hops\n");
   for(i = 0; i < num_nodes; i++) {
      fprintf(stdout, "%2d) ", i);
      for(j = 0; j < num_nodes; j++)
          fprintf(stdout, "%2d ", min_hops[i * num_nodes + j]);
          fprintf(stdout, "\n");
  }

   // How many times the god compute routes ?

   fprintf(stdout, "God computes routes %d times.\n", num_compute);


   // The following information can be found only when god is active.

   if (active == false) {
     return;
   }

   // Dump next_hop array

   fprintf(stdout, "Dump next_hop\n");
   for (i = 0; i < num_nodes; i++) {
     for (j = 0; j < num_nodes; j++) {
       fprintf(stdout,"NextHop(%d,%d):%d\n",i,j,NEXT_HOP(i,j));
     }
   }


   // What is inside SRC_TAB ?

   fprintf(stdout, "Dump SRC_TAB\n");
   for (i=0; i<num_data_types; i++) {
     fprintf(stdout,"%2d) ",i);
     for (j=0; j<num_nodes; j++) {
       fprintf(stdout,"%2d ", SRC_TAB(i,j) ? 1:0);
     }
     fprintf(stdout,"\n");
   }


   // What is inside OIF_MAP ?

   int *oif_map;

   fprintf(stdout, "Dump OIF_MAP\n");
   for (i=0; i<num_data_types; i++) {
     for (j=0; j<num_nodes; j++) {
       if (SRC_TAB(i,j)!=NULL) {
	 oif_map = SRC_TAB(i,j);
	 fprintf(stdout,"(%2d,%2d)\n",i,j);
	 for (k=0; k<num_nodes; k++) {
	   for (l=0; l<num_nodes; l++) {
	     fprintf(stdout,"%2d ", oif_map[k*num_nodes +l]);
	   }
	   fprintf(stdout,"\n");
	 }
       }
     }
   }



   // What is inside SK_TAB ?

   fprintf(stdout, "Dump SK_TAB\n");
   for (i=0; i<num_data_types; i++) {
     fprintf(stdout,"%2d) ",i);
     for (j=0; j<num_nodes; j++) {
       fprintf(stdout,"%2d ", SK_TAB(i,j));
     }
     fprintf(stdout,"\n");
   }

}


void God::AddSink(int dt, int skid)
{
  if (active == false) {
    return;
  }

  assert(num_data_types > 0);
  assert(num_nodes > 0);
  assert(dt >= 0 && dt < num_data_types);
  assert(skid >= 0 && skid < num_nodes);

  if (SK_TAB(dt,skid) == 1)
     return;

  SK_TAB(dt,skid) = 1;
  Fill_for_Source(dt, skid);
}


void God::AddSource(int dt, int srcid)
{
  if (active == false) {
    return;
  }

  assert(num_data_types > 0);
  assert(num_nodes > 0);
  assert(dt >= 0 && dt < num_data_types);
  assert(srcid >= 0 && srcid < num_nodes);

  if (SRC_TAB(dt,srcid) != 0)
      return;

  SRC_TAB(dt,srcid) = new int[num_nodes * num_nodes];
  bzero((char*) SRC_TAB(dt, srcid), sizeof(int) * num_nodes * num_nodes);
  Fill_for_Sink(dt, srcid);
  //  Dump();
}


void God::Fill_for_Sink(int dt, int srcid)
{
  int sk, cur, count;
  int *oif_map = SRC_TAB(dt, srcid);

  assert(oif_map != NULL);

  for (sk = 0; sk < num_nodes; sk++) {
    if (SK_TAB(dt, sk) == 0)
      continue;
    cur = srcid;
    count = 0;
    while (cur != sk) {
      if (NextHop(cur, sk) == UNREACHABLE)
	break;

      assert(NextHop(cur,sk) >= 0 && NextHop(cur, sk) < num_nodes);

      oif_map[cur*num_nodes + NextHop(cur, sk)] = 1;
      cur = NextHop(cur, sk);      
      count ++;
      assert(count < num_nodes);
    }
  }
}


void God::Fill_for_Source(int dt, int skid)
{
  int src, cur, count;
  int *oif_map;

  for (src = 0; src < num_nodes; src++) {
    if (SRC_TAB(dt, src) == 0)
      continue;
   
    oif_map = SRC_TAB(dt, src);
    cur = src;
    count = 0;
    while (cur != skid) {
      if (NextHop(cur, skid) == UNREACHABLE)
	break;

      assert(NextHop(cur,skid) >= 0 && NextHop(cur, skid) < num_nodes);

      oif_map[cur*num_nodes + NextHop(cur, skid)] = 1;
      cur = NextHop(cur, skid);      
      count ++;
      assert(count < num_nodes);
    }

  }
}


void God::Rewrite_OIF_Map()
{
  for (int dt = 0; dt < num_data_types; dt++) {
    for (int src = 0; src < num_nodes; src++) {
      if (SRC_TAB(dt, src) == NULL)
	continue;

      memset(SRC_TAB(dt,src),'\x00', sizeof(int) * num_nodes * num_nodes);
      Fill_for_Sink(dt, src);
    }
  }
}


int *God::NextOIFs(int dt, int srcid, int curid, int *ret_num_oif) 
{

  if (active == false) {
    perror("God is inactive.\n");
    exit(-1);
  }  

  int *oif_map = SRC_TAB(dt, srcid);
  int count=0;
  int i;

  for (i=0; i<num_nodes; i++) {
    if (oif_map[curid*num_nodes +i] == 1)
      count++;
  }

  *ret_num_oif = count;

  if (count == 0)
    return NULL;

  int *next_oifs = new int[count];
  int j=0;
  
  for (i=0; i<num_nodes; i++) {
    if (oif_map[curid*num_nodes +i] == 1) {
      next_oifs[j] = i;
      j++;    
    }
  }

  return next_oifs;
}



bool God::IsReachable(int i, int j)
{

//  if (MIN_HOPS(i,j) < UNREACHABLE && MIN_HOPS(i,j) >= 0) 
  if (NextHop(i,j) != UNREACHABLE)
     return true;
  else
     return false;
}


bool God::IsNeighbor(int i, int j)
{
	//printf("IsNeighbor(int i, int j)");
  assert(i<num_nodes && j<num_nodes);
  //printf("\nI am at Neighbour\n");
  //printf("i=%d, j=%d\n", i,j);
  if (mb_node[i]->energy_model()->node_on() == false ||
      mb_node[j]->energy_model()->node_on() == false ||
      mb_node[i]->energy_model()->energy() <= 0.0 ||
      mb_node[j]->energy_model()->energy() <= 0.0 ) {
    return false;
  }
//printf("\nI am at Neighbour_second\n");
  vector a(mb_node[i]->X(), mb_node[i]->Y(), mb_node[i]->Z());
  vector b(mb_node[j]->X(), mb_node[j]->Y(), mb_node[j]->Z());
  vector d = a - b;

  if (d.length() < RANGE)
    return true;
  else
    return false;  
}

//IsNeighbor made by Arun Kumar Gupta
bool God::IsNeighborr(int i, int j)
{
	//printf("IsNeighbor(int i, int j)");
  assert(i<num_nodes && j<num_nodes);
  //printf("\nI am at Neighbour\n");
  //printf("i=%d, j=%d\n", i,j);
 // if (mb_node[i]->energy_model()->node_on() == false ||
   //   mb_node[j]->energy_model()->node_on() == false ||
     // mb_node[i]->energy_model()->energy() <= 0.0 ||
      //mb_node[j]->energy_model()->energy() <= 0.0 ) {
    //return false;
  //}
//printf("\nI am at Neighbour_second\n");
  vector a(mb_node[i]->X(), mb_node[i]->Y(), mb_node[i]->Z());
  vector b(mb_node[j]->X(), mb_node[j]->Y(), mb_node[j]->Z());
  vector d = a - b;
  double distance = (((mb_node[i]->X() - mb_node[j]->X())*(mb_node[i]->X() - mb_node[j]->X()))+((mb_node[i]->Y()-mb_node[j]->Y())*(mb_node[i]->Y()-mb_node[j]->Y()))) ;
//getdistance(i,k)
 // if (getdistance(i,j) < RANGE)
  //{
	if (d.length() < RANGE)
	{
		//printf("Reange is %f" ,d.length());
		return true;
		
	}

 // }
	else
	return false;  
}


void God::CountConnect()
{
  int i,j;

  num_connect = 0;

  for (i=0; i<num_nodes; i++) {
    for (j=i+1; j<num_nodes; j++) {
      if (MIN_HOPS(i,j) != UNREACHABLE) {
	num_connect++;
      }
    }
  }
}


void God::CountAliveNode()
{
  int i;

  num_alive_node = 0;

  for (i=0; i<num_nodes; i++) {
    if (mb_node[i]->energy_model()->energy() > 0.0) {
      num_alive_node++;
    }
  }

}


bool God::ExistSource()
{
  int dtype, i;

  for (dtype = 0; dtype < num_data_types; dtype++) {
    for (i=0; i<num_nodes; i++) {
      if (SRC_TAB(dtype, i) != 0)
	return true;
    }
  }

  return false;
}


bool God::ExistSink()
{
  int dtype, i;

  for (dtype = 0; dtype < num_data_types; dtype++) {
    for (i=0; i<num_nodes; i++) {
      if (SK_TAB(dtype, i) != 0)
	return true;
    }
  }

  return false;
}


bool God::IsPartition()
{
  int dtype, i, j, k;
  int *oif_map;

  for (dtype = 0; dtype < num_data_types; dtype ++) {
    for (i = 0; i < num_nodes; i++) {
      if (SRC_TAB(dtype,i) == NULL)
	continue;
      oif_map = SRC_TAB(dtype, i);
      for (j = 0; j < num_nodes; j++) {
	for (k = 0; k < num_nodes; k++) {
	  if (oif_map[j*num_nodes + k] != 0)
	    return false;
	}
      }
    }
  }

  return true;
}


void God::ComputeRoute() 
{
  if (active == false) {
    return;
  }

  floyd_warshall();
  ComputeNextHop();
  Rewrite_OIF_Map();
  CountConnect();
  CountAliveNode();
  prev_time = NOW;
  num_compute++;

  if (allowTostop == false)
    return;

  if ( ExistSink() == true && ExistSource() == true && IsPartition() == true)
    StopSimulation();
}


void God::CountNewData(int *attr)
{
  if (dtab.GetHash(attr) == NULL) {
    num_send[attr[0]]++;
    dtab.PutInHash(attr);
  }
}


void God::IncrRecv()
{
  num_recv++;

  //  printf("God: num_connect %d, num_alive_node %d at recv pkt %d\n",
  // num_connect, num_alive_node, num_recv);
}


void God::StopSimulation() 
{
  Tcl& tcl=Tcl::instance();

  printf("Network parition !! Exiting... at time %f\n", NOW);
  tcl.evalf("[Simulator instance] at %lf \"finish\"", (NOW)+0.000001);
  tcl.evalf("[Simulator instance] at %lf \"[Simulator instance] halt\"", (NOW)+0.000002);
}


// Modified from setdest.cc -- Chalermek 12/1/99

void God::ComputeW()
{
  int i, j;
  int *W = min_hops;

  memset(W, '\xff', sizeof(int) * num_nodes * num_nodes);

  for(i = 0; i < num_nodes; i++) {
     W[i*num_nodes + i] = 0;     
     for(j = i+1; j < num_nodes; j++) {
	W[i*num_nodes + j] = W[j*num_nodes + i] = 
	                     IsNeighbor(i,j) ? 1 : INFINITY;
     }
  }
}

void God::floyd_warshall()
{
  int i, j, k;

  ComputeW();	// the connectivity matrix

  for(i = 0; i < num_nodes; i++) {
     for(j = 0; j < num_nodes; j++) {
	 for(k = 0; k < num_nodes; k++) {
	    MIN_HOPS(j,k) = MIN(MIN_HOPS(j,k), MIN_HOPS(j,i) + MIN_HOPS(i,k));
	 }
     }
  }


#ifdef SANITY_CHECKS

  for(i = 0; i < num_nodes; i++)
     for(j = 0; j < num_nodes; j++) {
	assert(MIN_HOPS(i,j) == MIN_HOPS(j,i));
	assert(MIN_HOPS(i,j) <= INFINITY);
     }
#endif

}

// --------------------------


int
God::hops(int i, int j)
{
        return min_hops[i * num_nodes + j];
}


void
God::stampPacket(Packet *p)
{
        hdr_cmn *ch = HDR_CMN(p);
        struct hdr_ip *ih = HDR_IP(p);
        nsaddr_t src = ih->saddr();
        nsaddr_t dst = ih->daddr();

        assert(min_hops);

        if (!packet_info.data_packet(ch->ptype())) return;

        if (dst > num_nodes || src > num_nodes) return; // broadcast pkt
   
        ch->opt_num_forwards() = min_hops[src * num_nodes + dst];
}


void 
God::recv(Packet *, Handler *)
{
        abort();
}

int
God::load_grid(int x, int y, int size)
{
	maxX =  x;
	maxY =  y;
	gridsize_ = size;
	
	// how many gridx in X direction
	gridX = (int)maxX/size;
	if (gridX * size < maxX) gridX ++;
	
	// how many grid in Y direcion
	gridY = (int)maxY/size;
	if (gridY * size < maxY) gridY ++;

	printf("Grid info:%d %d %d (%d %d)\n",maxX,maxY,gridsize_,
	       gridX, gridY);

	return 0;
}
//=================================================================mycode====== Coded By Arun 
// return the grid that I am in
// start from left bottom corner, 
// from left to right, 0, 1, ...
//By Arun ...
double 
God::getdistance(int i,int k) 
{ 
//printf("I am At God Distance getdistance(int i,int k)");

double x1=mb_node[i]->X(); 
printf("\nx%d :%f\t" ,i,x1);
double y1=mb_node[i]->Y(); 
printf("\ny%d :%f\t" ,i,y1);
//double z1=mb_node[i]->Z(); 
//printf("\nz%d :%f\t" ,i,z1);
double x2=mb_node[k]->X(); 
printf("\nx%d :%f\t" ,k,x2);
double y2=mb_node[k]->Y(); 
printf("\ny%d :%f\t" ,k,y2);
double distance; 
printf("#######");
distance=sqrt(((x1-x2)*(x1-x2))+((y1-y2)*(y1-y2))); 

return distance; 

} 
//Change Node status ... arun
int  
God::changestatus(nsaddr_t node_iid , int status_code)
{
	
	//int aa;
	//mb_node1 = new MobileNode*[num_nodes];
	//printf("We are at God ::\n");
	//double y2=mb_node[i]->Y(); 
	//printf("y2 ==%f\n" ,y2 );
	 //aa = mb_node[i]->CDS_node_status();
	//int q = 3;
	//mb_node[i]->CDS_node_status() = q;
	
	//aa =101;
	//mb_node[i]->CDS_node_status() = 1;
	//printf("I am here at changestatus(int i)\n");
	int node_id = node_iid;
	//printf("CDS_Node_Status at God before:: %d \n" ,mb_node[node_id]->get_node_status());
	mb_node[node_id]->update_node_status(status_code);
	//printf("CDS_Node_Status at God after update :: %d \n" ,mb_node[node_id]->get_node_status());
	return mb_node[node_id]->get_node_status() ;
	
} 


int 
God::get_node_status_god(int rcv_n)
{
	return mb_node[rcv_n]->get_node_status();
}

int 
God::get_parent_id(int rcv_n)
{
	return mb_node[rcv_n]->get_parent_id();
}


//yet to think how to return 
void
God::get_neighbor_black_node_list(int node_id)
{
	//int * head ;
	//first Reset the list it's important tooo
	//it's important 
	mb_node[node_id]->reset_black_node_pointer();
	printf("This list is for the node %d\n",node_id);
	for(int position = 0 ; position < num_nodes ; ++position)
	{
		int node = mb_node[node_id]->get_Ul_nodes_by_position_nl_node(position);
		if( node != 500)
		{
			if(mb_node[node]->get_node_status() == 2)
			{
				//printf("Black Nodes is %d\n",node);
				mb_node[node_id]->add_node_in_black_node_list(node);
			}
		}
		/*if(mb_node[node_id]->get_Ul_nodes_by_position(position) != 500)
		{
			if((mb_node[mb_node[node_id]->get_Ul_nodes_by_position(position)]->get_node_status()) == 1);
			{
				printf("Black Nodes is %d\n", mb_node[node_id]->get_Ul_nodes_by_position(position));
				//mb_node[node_id]->add_a_node_in_black_node_list(mb_node[node_id]->get_Ul_nodes_by_position(position));
			}
		}*/
	}
//return mb_node[node_id]->get_start_pointer();

// here may happen that it's in int and that pointer size may exceed , please have a look first .
}

int
God::get_node_list_size(int node_id)
{
	return mb_node[node_id]->get_black_node_list_counter();
}
int 
God::get_black_node_by_position(int node_id, int position)
{
	return mb_node[node_id]->get_black_node_by_position(position);
}


int 
God::is_beacon_pkt_received_before(int node_id)
{
	return mb_node[node_id]->is_beacon_pkt_received_before();
}
void
God::beacon_pkt_have_been_received(int node_id)
{
	mb_node[node_id]->beacon_pkt_have_been_received();
}


void
God::reduce_this_node_only(int node_id , int node_which_is_going_to_be_deleted)
{
	mb_node[node_id]->reduce_this_node_only(node_which_is_going_to_be_deleted);
	//printf("Completed Reducing the Single Node \n");
}

void
God::increament_counter(int node_id)
{
	mb_node[node_id]->increament_counter();
}

void
God::decreament_counter(int node_id)
{
	mb_node[node_id]->decreament_counter();
	//printf("Decreamenting A counter is also completed on node %d\n" , node_id);
}
int
God::get_counter(int node_id)
{
	return mb_node[node_id]->get_counter();
}

int
God::get_number_of_nodes()
{
	return num_nodes;
}

void
God::set_rcv_pkt(int node_id)
{
 mb_node[node_id]->set_rcv_pkt()  ;
}

int
God::get_rcv_pkt(int node_id)
{
return mb_node[node_id]->get_rcv_pkt() ; 
}



void 
God::set_prune_val(int node_id )
{
	mb_node[node_id]->set_prune_val()  ;

}
int
God::get_prune_val(int node_id) 
{
	return mb_node[node_id]->get_prune_val() ; 
}



void
God::print_the_whole_node_status()
{
	for(int current_node = 0 ; current_node < num_nodes ; ++current_node)
	{
		//printf("Left Node at End  ..........^^^^^^^^^^^^^^^^^^^^^^^^^^");
		mb_node[current_node]->Ul_list();
	//	printf("\nNeighbor Size ::::%d\n" , mb_node[current_node]->get_neighbor_size());
		//printf("\nCounter Size :::%d\n" ,mb_node[current_node]->get_counter());
		
	}
}






/*int
God::BestNode(int node_id)
{
	int node_size = mb_node[node_id]->get_neighbor_size();
	printf("Node_size ::: %d\n" , node_size);
	int myn = 0;
	int get = 0;
	int def_get = 0;
	for(int pos = 0 ; pos<=node_size ; ++pos)
	{
		myn = mb_node[node_id]->get_Ul_nodes_by_position(pos);
		//printf("RRRRRRRRRRRR  :::::::::: %d" ,myn);
		if(myn != 500)
		{
		get = mb_node[myn]->get_neighbor_size(); 
		}
		
	}
	return myn;
}*/

int
God::BestNode(int node_id)
{
	int neighbor_size = mb_node[node_id]->get_neighbor_size();
	//printf("neighbor_size ::: %d\n\n" , neighbor_size);
	int mynode =0;
	//printf("!");
	int bestnode_ = 0; 
	//printf("!");
	int  y_y = 0 ; 
	//printf("!");
	int besty = 0;
	//printf("!");
	int position  ;
	//printf("!");
	//printf("vgsdf");
	//printf("Selecting a Best Node for Node %d\n" , node_id);
	for(position = 0 ; position< neighbor_size ; ++position )
	{
		//printf("Entered");
		mynode = mb_node[node_id]->get_Ul_nodes_by_position(position);
		//printf("Error is not here");
		if(mynode != 500)
		{
		y_y = mb_node[mynode]->get_neighbor_size(); 
		//printf("neighbor Node id : %d and Neighor's Neighbor Size : %d\n" , mynode , y_y);
		}
		//printf("Node %d's neighbor size is %d \n" ,mynode , y_y );
		if(y_y >= besty)
		{
			besty = y_y;
			bestnode_ = mynode;
			//How a Best node is Selected if Node's Neighbor List is same then Who's node id is higher will be selected as a Best node 
		}
	}
	if(neighbor_size == 0 )
	{
		//printf("No Node to Cover to node by node %d\n\n" , node_id);
		return 1000;
	}
	else 
	{
		//printf("Best Node is selected by node %d and best node is %d\n\n" , node_id , bestnode_);
		return bestnode_;
	}
}
bool
God::is_that_node_in_black_node_list(int responsible_node_for_fixing , int problem_node)
{
	//responsible_node_for_fixing will search in own Black node list to check  weather that problem node exist in his black node list or not
	
	int limit = mb_node[responsible_node_for_fixing]->get_black_node_list_counter();
	int link_status1 = mb_node[responsible_node_for_fixing]->get_receiving_link_down_status();
	int link_status2 = mb_node[problem_node]->get_sending_link_down_status();
	for(int counter = 0 ; counter < limit ; ++counter)
	{
		if((!((link_status1 == 1) && (link_status2 == 1) )))
		{
			if(mb_node[responsible_node_for_fixing]->get_black_node_by_position(counter) == problem_node )
			{
				return true;
			}
		}
	} 
return false ;
}

bool
God::is_that_node_is_the_leave_node_in_back_bone(int responsible_node_for_fixing)
{
	// See this is not working 
	printf("Case ::(GOD) is_that_node_is_the_leave_node_in_back_bone \n");
	//printf("is_that_node_is_the_leave_node_in_back_bone :: %d\n" ,responsible_node_for_fixing );
	int size = mb_node[responsible_node_for_fixing]->get_black_node_list_counter();
	//printf("Node's Black Node Size %d\n\n",size);
	int node = 0 ;
	for(int position = 0 ; position < size ; ++position)
	{
		if((mb_node[responsible_node_for_fixing]->get_black_node_by_position(position)) != 500)
		{
			node = mb_node[responsible_node_for_fixing]->get_black_node_by_position(position);
			printf("Node :::: %d\n\n" , node);
			if(mb_node[node]->get_parent_id() == responsible_node_for_fixing )
			{
				printf("Node have some node's to cover \n\n");
				return false ;
			}		
		}
	}
return true ;
}
int 
God::is_that_node_have_any_other_black_node(int responsible_node_for_fixing ,int problem_node )
{
	// startfromhere
	int node = 0 ;
	int size = mb_node[responsible_node_for_fixing]->get_black_node_list_counter();
	for(int position = 0  ; position < size ; ++position)
	{
		if((mb_node[responsible_node_for_fixing]->get_black_node_by_position(position)) != 500)
		{
			node = mb_node[responsible_node_for_fixing]->get_black_node_by_position(position);
			
			if(( node != problem_node)  && (node != responsible_node_for_fixing ))
			{
				//printf("!!node : %d\n" ,node);
				return mb_node[responsible_node_for_fixing]->get_black_node_by_position(position);
			}			
		}
	}
//printf("!!node : %d\n" ,node);
return 1000;	
}

int
God::if_node_is_in_2_hop(int responsible_node_for_fixing ,int problem_node)
{
	int best_node = 1000;
	if(responsible_node_for_fixing != problem_node )
	{
	int size = mb_node[responsible_node_for_fixing]->get_neighbor_size_Nl_node();
	int node = 0 ;
	int neighbor_size = 0 ;
	int status = 0;
	
	int cost = 0 ;
	int link_status1 = mb_node[responsible_node_for_fixing]->get_receiving_link_down_status();
	int link_status2 = 0;
	int node1 =0 ;
	for(int position = 0 ; position < size; ++position )
	{
		node = mb_node[responsible_node_for_fixing]->get_Ul_nodes_by_position_nl_node(position);
		neighbor_size = mb_node[node]->get_neighbor_size_Nl_node();
		status = mb_node[node]->get_node_status();
		
		link_status2 = mb_node[node]->get_sending_link_down_status();
		if((!((link_status1 == 1) && (link_status2 == 1) )))
		{
			
			for(int position1 = 0 ; position1 < neighbor_size ; ++position1)
			{
		
				node1 = mb_node[node]->get_Ul_nodes_by_position_nl_node(position1);
				if(responsible_node_for_fixing != node1)
				{	//""Node 8 and 8 are Connected via node 3""  above condition prevents from this 
					if(node1 == problem_node)
					{
						if(cost < status)
						{
							cost = status ;
							best_node = node;
						}
						break;	
					}
		
				}
			}
		}
		//mb_node[mb_node[responsible_node_for_fixing]->get_Ul_nodes_by_position_nl_node(position)]->get_Ul_nodes_by_position_nl_node(position);
		if (cost == 2)
		{
			break ;
		}
	}
	
	/*if(best_node != 1000)
	{
		printf("Node is %d\n" , best_node);
	}*/
	if(responsible_node_for_fixing != problem_node)
		printf("That \"problem Node\" %d is not in 2 hop Distance with node %d \n" , problem_node ,responsible_node_for_fixing);
	}
return best_node; 
}

int
God::get_sending_link_down_status(int node_id) 
{
	return  mb_node[node_id]->get_sending_link_down_status();
}
int
God::get_receiving_link_down(int node_id) 
{
	return  mb_node[node_id]->get_receiving_link_down_status();
}
void 
God::set_sending_link_down(int node_id)
{
	mb_node[node_id]->set_sending_link_down();
}
void 
God::set_receiving_link_down(int node_id)
{
	mb_node[node_id]->set_receiving_link_down();
}

void 
God::set_working_node_status_down(int node_id)
{
	mb_node[node_id]->set_working_node_status();
}
int 
God::get_working_node_status(int node_id)
{
	return  mb_node[node_id]->get_sending_link_down_status();
}
int 
God::get_forward_node(int node_id) 
{
	return mb_node[node_id]->get_forward_node() ;
}
int
God::get_pkt_id(int node_id) 
{
	return  mb_node[node_id]->get_pkt_id();
}

int 
God::get_coming_from(int node_id) 
{
	return mb_node[node_id]->get_coming_from();
}
void 
God::set_forward_node(int node_id , int forward_id)
{
	 mb_node[node_id]->set_forward_node(forward_id);
}

void
God::set_pkt_id(int node_id , int pkt_id) 
{
	mb_node[node_id]->set_pkt_id(pkt_id);
}
void
God::set_coming_from(int node_id ,int coming_id)
{
	mb_node[node_id]->set_coming_from(coming_id);
}


int 
God::get_cost(int node_id)
{
	return mb_node[node_id]->get_cost();
}
void
God::set_cost(int node_id)
{
	mb_node[node_id]->set_cost();
}

void 
God::set_super_cost(int node_id , int  cost)
{
	mb_node[node_id]->set_super_cost(cost);
}


//arun
void 
God::find_neighbor()
{			int avg_node_degree = 0 ;
			//printf("AT find Neighbour");
			int nodes=God::instance()->nodes();
			int max_node_degree = 0 ;
			int min_node_degree = 10000 ;
			int node_degree_returned = 0; 
			int node_degree[nodes];
			for(int i = 0 ; i <nodes ; ++i )
			{
				node_degree_returned = node_neighbors_initially(i);
				printf("\nNode %d's Node Degree is ::%d\n" , i , node_degree_returned);
				node_degree[i] = node_degree_returned;
				if(node_degree_returned >max_node_degree )
				{
					max_node_degree = node_degree_returned;
				}
				else if(node_degree_returned < min_node_degree)
				{
					min_node_degree = node_degree_returned;
				}
				//printf("#########################################################");
				avg_node_degree = node_degree_returned + avg_node_degree ;
				// here returns the node degree of node i :) arun
				//printf("Done\n");
				//mb_node[i]->Node_Short(); //these two are the working functions..... this is to short the node list in the array where nodes ae stored
				//mb_node[i]->Ul_list(); //this is for to print the list ...
			}
			quickSort(node_degree,nodes);
			int avg_node_degree_final = (avg_node_degree / nodes);
			int inter = 0;
			float deviation = 0 ;
			for(int i = 0 ; i <nodes ; ++i )
			{
				inter  =  inter +(node_degree[i] - avg_node_degree_final )*(node_degree[i] - avg_node_degree_final );
			}
			deviation = sqrt((inter /nodes));
			for(int i = 0 ; i <nodes ; ++i )
			{
				printf("%d\t" , node_degree[i]);
			}
			printf("\n\nMAx node Degree :: %d \n\n",max_node_degree );
			printf("\n\nmin node Degree :: %d \n\n",min_node_degree );
			printf("\n\nDeviation is :: %f \n\n" , deviation);
			printf("\n\nTotal Node Degree Avg %d\n\n" , avg_node_degree_final);

}



void 
God::quickSort(int numbers[], int array_size)
{
  q_sort(numbers, 0, array_size - 1);
}
 
 
void 
God::q_sort(int numbers[], int left, int right)
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





int
God::get_P_y_(int node_id)
{
	return mb_node[node_id]->get_P_y_();
}

void
God::set_P_y_(int node_id , int size )
{
	mb_node[node_id]->set_P_y_(size);
}

int
God::get_P_node(int node_id)
{
	return mb_node[node_id]->get_P_node();
}

void 
God::set_node_id(int node_id , int node_which_is_going_to_set)
{
	mb_node[node_id]->set_node_id(node_which_is_going_to_set);
}

void 
God::set_neighbor_size( int node_id ,int node_size )
{
	mb_node[node_id]->set_neighbor_size(node_size);
}

int
God::get_node_id(int node_id)
{
	return mb_node[node_id]->get_node_id();
}

int
God::get_node_size(int node_id)
{
	return mb_node[node_id]->get_node_size();
}

void
God::set_node_status_rc(int node_id)
{
	//printf("Here node id %d\n" , node_id);
	//printf("Here node id %d\n" , num_nodes );
	//mb_node[node_id]->set_Child_node(node_id);
	mb_node[node_id]->set_node_status_rc();
}
int 
God::get_node_status_rc(int node_id)
{
	//printf("Here node id %d\n" , node_id);
	return mb_node[node_id]->get_node_status_rc();
}



int 
God::get_total_nodes()
{
	return num_nodes ;
}
void
God::set_P_node(int node_id , int set_node_id)
{
	mb_node[node_id]->set_P_node(set_node_id);
}

void 
God::set_P_change(int node_id , int change_val)
{
	mb_node[node_id]->set_P_change(change_val);
}
int
God::get_P_change(int node_id)
{
	return mb_node[node_id]->get_P_change();
}


void 
God::check_whether_status_of_leaf_node(int node_id)
{
	// please go to bool is_that_node_is_the_leave_node_in_back_bone(int responsible_node_for_fixing);
}




int 
God::god_select_a_node(int netid)
{
	//printf("By node ::%d\n" , netid);
//	return mb_node[netid]->get_Ul_node();
	//printf("&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&\n");
	return BestNode(netid);
}




void
God::god_Ul_list(int rcv_node_id)
{
	mb_node[rcv_node_id]->Ul_list();
}

void 
God::set_Pkt_counter(int node_id)
{
	mb_node[node_id]->set_Pkt_counter();
}
int
God::get_Pkt_counter(int node_id)
{
	return mb_node[node_id]->get_Pkt_counter();
}


//Arun
int 
God::node_neighbors_initially(int node_id) 
{ 
	int count = 0 ;
  int nodes=God::instance()->nodes(); 
  //printf("finding the neighbors of that node really i am here  %d" , i);
  //cout<<"neighbors of node "<<i<<":"<<endl; 
	//int nodes = j; 
	//printf("\n no of nodes %d" , nodes);
	//printf("\nneighbors of node %d \n" , node_id);
	//printf("nodes %d\n" , nodes);
	//int node_id = node_idd;
	//int value = 0;
  for (int expected_node = 0; expected_node < nodes ; ++expected_node) 
	{ 
	//int expected_node = 5;
	//printf("nodes %d\n" , nodes);
	//printf("expected_node : %d\n" , expected_node);
	//printf("node_id : %d\n" , node_id);
   if(node_id != expected_node) 
  	 { 
	
	//printf("if(k!=node_id)\n ");
	//printf("nodes %d\n" , nodes);
	//printf("i am inside this ");
	//cout<<k<<":"<<God::instance()->getdistance(i,k)<<" yes"<<endl; 
	//cout<<k<<":"<<God::instance()->getdistance(i,k)<<" no"<<endl; 
    if(IsNeighborr(node_id,expected_node))
	{ 
	//printf("\nNode %d is the Neighbor of node %d \n" ,expected_node , node_id );
        //value =	God::instance()->getdistance(i,k);
     //printf("%d : %d  \tYes\n" , k ,value );
	//static count = 0 ;
	mb_node[node_id]->add_neighbor(expected_node);
	++count;
	//printf("Neighbor :: %d\n" ,mb_node[node_id]->get_neighbor_id() );
	// here k means node_id which is going to added in to the neighbor od node_id
	}
	
    //else 
	//printf("%d : %d  \tNo\n" , k ,God::instance()->getdistance(i,k));
	//int a = 0; 
      
   } 
	//else
	//{
	//	printf("If is not working \n");
	//}
  }



	//printf("Neighbor List Size ::%d\n",mb_node[node_id]->get_neighbor_size()); 
  //printf("\n");
return count;
}

//Arun
void 
God::god_set_parent_id(int rcv_id , int snd_id )
{
	//rcv id :: where we are going to change the status of node 
	mb_node[rcv_id]->set_parent_id(snd_id);
}

void 
God::set_child_id(int node_id , int Child_id)
{
	mb_node[node_id]->set_Child_node(Child_id);
}

int 
God::get_Child_node(int node_id)
{
	return mb_node[node_id]->get_Child_node();
}
// this is for to add a  node in y_ if you remember y is the critical for our CDS protocol 
void
God::add_node_in_y_(int node_id , int src_id)
{
	printf("Node which is Increasing the y_ value ::%d\n " , node_id);
	mb_node[node_id]->add_node_in_y_(src_id);
}

void 
God::god_Reduce_Nodes(int snd_node_id ,int  rcv_node_id )
{
//printf("Hello\n");
//Remembr Node Size is always starts from the 0 
// this does Reduces the  Ul[Receiving_node_id] - Ul[Sending_Node's_Id]
// not a tough at all but Careful implementation
int rcv_node_size = mb_node[rcv_node_id]->get_neighbor_size();
int snd_node_size = mb_node[snd_node_id]->get_neighbor_size_Nl_node();


//printf("Rcv Node Size :: %d \n Send Node Size %d\n" , rcv_node_size , snd_node_size);
//printf("List Before operation ::::::::::\n");
//printf("Receieving Node Unknown  List \n");
mb_node[rcv_node_id]->Ul_list();
//printf("Sending Node's Neighbor List");
mb_node[snd_node_id]->Nl_list();
//printf("After Operation\n");
int change = 0 , rcv_node , snd_node;
for(int rcv_n = 0 ; rcv_n<=rcv_node_size ;++rcv_n  )
	{
		//printf("Here ^^^\n");
		/*if(change == 1)
		{
			printf("RFTG");
			rcv_n = rcv_n -1;
			change = 0;
		}*/
		//printf("Here $$$\n");
		rcv_node  = mb_node[rcv_node_id]->get_Ul_nodes_by_position(rcv_n);
		//int snd_node ;
		for(int snd_n = 0 ; snd_n<=snd_node_size ; ++snd_n )
			{
				//printf("WEWE\n");
				snd_node = mb_node[snd_node_id]->get_Ul_nodes_by_position_nl_node(snd_n);
				if(rcv_node == snd_node_id )
				{
					mb_node[rcv_node_id]->remove_Ul_node(rcv_n);
					rcv_n = rcv_n - 1;
					break;
				}
				if(rcv_node == snd_node)
				{
					//printf("rcv_node :: %d\nSnd_node :: %d\n" , rcv_node , snd_node);
					if(rcv_node == 500)
					{
						break;}
					mb_node[rcv_node_id]->remove_Ul_node(rcv_n);
					//change = 1;
					rcv_n = rcv_n - 1;
					break;
				}
				
				
			}
	if(rcv_node == 500)
	break;
	
	}
//printf("Updated Receiving Node's neighbor's Size ::%d\n  " , mb_node[rcv_node_id]->get_neighbor_size());
//here RCV_n  is the position of in UL list same for the snd_n
//here rcv_node_size size of the UL actually y_ value
//rcv_node  node in ul

}

int 
God::pick_up_a_Best_node_centralized()
{
	int best_node = 0 ;
	int size = 0 ;
	int bestsize = 0 ;
	for(int node_id = 0  ; node_id <num_nodes; ++node_id)
	{
		size = mb_node[node_id]->get_neighbor_size(); 
		if(size > bestsize)
		{
			bestsize = size;
			best_node = node_id;
		}
		else if((size == bestsize) && (node_id > best_node))
		{
			best_node = node_id;
		}
	}
	return best_node;
}

int 
God::get_neighbor_size(int node_id)
{
	return mb_node[node_id]->get_neighbor_size();
}

void
God::print_whole_end_status()
{
	printf("num_nodes %d\n" , num_nodes);
	//To print the Selected Parent id's 
	for(int i = 0 ; i < num_nodes ; ++i)
	{
		printf("Node %d's Parent Node is %d\n\n" , i ,mb_node[i]->get_parent_id() );
	}
	for(int i = 0 ; i < num_nodes ; ++i)
	{
		if(mb_node[i]->get_node_status() == 2)
		{
			printf("Black Node : %d\n" , i);
		}
	}
	int Black_node_count = 0 ;
	//printf("Nodes Which are Black  Nodes \n");
	//trace("Nodes Which are Black  Nodes \n");
	for(int i = 0 ; i < num_nodes ; ++i)
	{
		if(mb_node[i]->get_node_status() == 2)
		{
			++Black_node_count;
			//printf("Node %d which is black node  \n\n" , i);
		}
		//trace("Node %d which is black node  \n" , i);
	}

	int total_count = 0 ;
	//counts total packet's and all
	for(int i = 0 ; i < num_nodes ; ++i)
	{
		//printf("Node %d have sent number of Packets are ::%d  \n\n" , i , mb_node[i]->get_Pkt_counter());
		//trace("Node %d which is black node  \n" , i);
		total_count = total_count + mb_node[i]->get_Pkt_counter();
		//int i = get_node_id(i) ;
		//printf("%d Selected Node is ::%d\n\n" , i, get_node_id(i));
	}
	
	printf("\n\n******************************************\n\n");
	printf("Total Packet Sendt are:: %d\n\n" ,total_count );
	printf("Total Black Nodes are %d" , Black_node_count );
	printf("\n\n******************************************\n\n");
	
}

void 
God::get_Single_nodes_Neighbors(int node_id)
{
	mb_node[node_id]->print_neighbors();
}


void 
God::Guha_Kular_Algo_3()
{
	find_neighbor();
	int Grey_node_array[1000]; //it contains the list of all black nodes.
	int array_size = 0 ; 
	printf("\n\nGuha's Algorihm 3\n\n");
	int neighbor_size=0;
	while(if_any_white_node_is_white_left())
	{
		Grey_node_array[array_size] = white_or_grey_node_who_is_covering_max_whitenode();
		changestatus(Grey_node_array[array_size],2);
		//printf("Node %d is created as Black node \n\n" , Grey_node_array[array_size]);
		neighbor_size = mb_node[Grey_node_array[array_size]]->get_neighbor_size();
		for(int i = 0 ;i < neighbor_size ;++i)
		{
			if(mb_node[mb_node[Grey_node_array[array_size]]->get_Ul_nodes_by_position_nl_node(i)]->get_node_status() == 0)
					{
						changestatus(mb_node[Grey_node_array[array_size]]->get_Ul_nodes_by_position_nl_node(i),1);
					}
		}
		
		++array_size;
	}
	/*
	int Bl[1000];
	int bl_counter =0 ;
	int i = 0 ;
	int change = 0 ;
	for(int i = 0 ; i<array_size ; ++i)
	{
		neighbor_size = mb_node[Grey_node_array[i]]->get_neighbor_size();
		change  = add_in_bl_list(Bl ,Grey_node_array[i] );
		if(change == 1)
		{
			++bl_counter;
			change = 0 ;
		}
		
		
		for(int j = 0 ; j <neighbor_size ; ++j)
		{
			if(mb_node[mb_node[Grey_node_array[i]]->get_Ul_nodes_by_position_nl_node(j)]->get_node_status() == 2)
			{
				change  = add_in_bl_list(Bl , mb_node[Grey_node_array[i]]->get_Ul_nodes_by_position_nl_node(j));
				if(change = 1)
				{
					++bl_counter;
					change = 0 ;
				}
			}
		}
		/*int node = 0 ;
		int bl_back_counter  = 0 ;
		for(int j = 0 ; j < bl_counter ; ++j)
		{
			node = Bl[bl_back_counter];
			++bl_back_counter;
			neighbor_size = mb_node[node]->get_neighbor_size();
			
				
			
		}*/
}
	
	
		



int 
God::white_or_grey_node_who_is_covering_max_whitenode()
{
	int count = 0 ;	
	int best_count = 0 ;
	int best_node = 0 ;
	int neighbor_size = 0 ;
	for(int i = 0 ; i< num_nodes ; ++i)
	{
		count = 0 ;	
		
			if(mb_node[i]->get_node_status() != 2)
			{
				neighbor_size = mb_node[i]->get_neighbor_size();
				for(int j = 0 ; j<neighbor_size ; ++j )
				{
					
					if(mb_node[mb_node[i]->get_Ul_nodes_by_position_nl_node(j)]->get_node_status() == 0)
					{
						++count;
					}
				}
			}
	
		if(count>=best_count)
		{
			best_count = count;
			best_node = i;
		}	
	}
return best_node;
}


void
God::Guha_Kular_Algo_2()
{
	int count_black_node = 0 ;
	find_neighbor();
	printf("\n\nGuha's Algorihm 2\n\n");
	int Grey_node_array[1000];
	int Grey_node_size[1000];
	int array_size = 0 ; 
	int statrting_node = pick_up_a_Best_node_centralized();
	int i = 0 ;
	int mynode = 0; 
	int neighbor_size = mb_node[statrting_node]->get_neighbor_size();
	changestatus(statrting_node,2) ;
	++count_black_node;
	printf("Node is created as Black Node ::%d\n", statrting_node);
	for(int position = 0 ; position< neighbor_size ; ++position )
	{
		mynode = mb_node[statrting_node]->get_Ul_nodes_by_position(position);
		if(mynode != 500)
		{
			//mb_node[mynode]->get_neighbor_size();
			changestatus(mynode,1) ;
			//mb_node[mynode]->get_neighbor_size()
			Grey_node_array[array_size] = mynode;
			Grey_node_size[array_size] = mb_node[mynode]->get_neighbor_size() ;
			++array_size;
		}
	}
	update_node_array_status(Grey_node_array , Grey_node_size ,  array_size  );
	int count_size = 0;
	int best_single_node = 0 ;
	int best_single_size = 0 ; 
	for(int position = 0 ; position< neighbor_size ; ++position )
	{
		mynode = mb_node[statrting_node]->get_Ul_nodes_by_position(position);
		if((mynode != 500)&&(mb_node[mynode]->get_node_status() == 1))
		{
			count_size = count_all_white_node_for_single_node(mynode);
			//printf("Node %d Have white node Count is::%d \n" , mynode ,count_size );
			
		}
		if(count_size >best_single_size )
		{
			best_single_size = count_size;
			best_single_node = mynode ;
			//printf("So Best node is %d\n\n\n" , best_single_node);
			
		}
	}
	update_node_array_status(Grey_node_array, Grey_node_size , array_size  );
	count_size = 0;
	int best_double_node_1 = 0 ;
	int best_double_node_2 = 0 ;
	int best_double_size = 0 ; 
	int mynode_1 =0 ;
	int mynode_2 = 0 ;
	for(int position = 0 ; position< neighbor_size ; ++position )
	{
		mynode_1 = mb_node[statrting_node]->get_Ul_nodes_by_position(position);
		if((mynode_1 != 500) && ((mb_node[mynode_1]->get_node_status() == 1)))
		{
			int neighbor_size_1 = mb_node[mynode_1]->get_neighbor_size();
			for(int position_2 = 0; position_2 <neighbor_size_1 ; ++position_2 )
			{
				mynode_2 = mb_node[mynode_1]->get_Ul_nodes_by_position(position_2);
				if((mynode_2 != 500)&& ((mb_node[mynode_2]->get_node_status() == 0)))
				{
					count_size = find_the_count(mynode_1 , mynode_2);
					printf("Node1  %d  node2 %d Have white node Count is::%d \n" , mynode_1  ,mynode_2 ,count_size );
				}
				if(count_size >best_double_size )
				{
					best_double_size = count_size ;
					best_double_node_1 = mynode_1;
					best_double_node_2 = mynode_2;
					printf("So Best node is %d and node %d\n\n\n" , best_double_node_1 , best_double_node_2);
				}
			}
			
		}
	}
	if(best_double_size >  best_single_size)
	{
		changestatus(best_double_node_1,2);
		++count_black_node;
		printf("Node is created as Black Node ::%d\n", best_double_node_1);
		changestatus(best_double_node_2,2);
		++count_black_node;
		printf("Node is created as Black Node ::%d\n", best_double_node_2);
		printf("\n\n");
		//Going To Make Node's as a Black  Node's 
		int neighbor_size_1 = mb_node[best_double_node_1]->get_neighbor_size();
		int neighbor_size_2 = mb_node[best_double_node_2]->get_neighbor_size();
		for(int position = 0 ; position< neighbor_size_1 ; ++position )
		{
			mynode_1 = mb_node[best_double_node_1]->get_Ul_nodes_by_position(position);
			if(mynode_1 != 500)
			{
				//int neighbor_size_1 = mb_node[best_double_node_2]->get_neighbor_size();
				for(int position_2 = 0; position_2 <neighbor_size_2 ; ++position_2 )
				{
					mynode_2 = mb_node[best_double_node_2]->get_Ul_nodes_by_position(position_2);
					if(mynode_2 != 500)
					{
						if(mynode_1 == mynode_2)
						{
							changestatus(mynode_1,1);
							Grey_node_array[array_size] = mynode_1;
							Grey_node_size[array_size] = mb_node[mynode_1]->get_neighbor_size();//here the value is not updated so first when we will use update_node_array_status then this node's status will get update 
							++array_size;
						}
						else
						{
							changestatus(mynode_1,1);
							Grey_node_array[array_size] = mynode_1;
							Grey_node_size[array_size] = mb_node[mynode_1]->get_neighbor_size();
							++array_size;
							changestatus(mynode_2,1);	
							Grey_node_array[array_size] = mynode_2;
							Grey_node_size[array_size] = mb_node[mynode_2]->get_neighbor_size();
							++array_size;
						}						
	
					}
					
				}
			
			}
		}
	update_node_array_status( Grey_node_array ,Grey_node_size , array_size  ); //very very important 
	}
	else
	{
		changestatus(best_single_node,2);
		++count_black_node;
		printf("Node is created as Black Node ::%d\n", best_single_node);
		printf("\n\n");
		int neighbor_size = mb_node[best_single_node]->get_neighbor_size();
		for(int i = 0 ; i<neighbor_size ; ++i )
		{
			mynode = mb_node[best_single_node]->get_Ul_nodes_by_position(i);
			if(mynode != 500)
			{
				changestatus(mynode,1);
				Grey_node_array[array_size] = mynode ;
				Grey_node_size[array_size] = mb_node[mynode]->get_neighbor_size();
				++array_size;	
				
			}
		}
	update_node_array_status( Grey_node_array,Grey_node_size , array_size  ); //very very important 	
	}
	while(if_any_white_node_is_white_left())
	{
		//Searching Single best Grey Node 
		count_size = 0;
		best_single_node = 0 ;
		best_single_size = 0 ; 
		//For Double node Parameters		
		best_double_node_1 = 0 ;
		best_double_node_2 = 0 ;
		best_double_size = 0 ; 
		mynode_1 =0 ;
		mynode_2 = 0 ;
		int grey_node = 0 ;
		int white_node = 0;
		for(int position = 0 ; position< array_size ; ++position )
		{
			if((mb_node[Grey_node_array[position]]->get_node_status() == 1))
			{
				count_size = Grey_node_size[position];
				if(count_size > best_single_size)
				{
					best_single_size = count_size;
					best_single_node = Grey_node_array[position];
					printf("Node %d is best node with size %d\n" ,best_single_node,best_single_size );
				}
			}
		}
		
		for(int i = 0 ; i< array_size ; ++i )
		{
			grey_node = Grey_node_array[i];
			neighbor_size = mb_node[grey_node]->get_neighbor_size();
			if((grey_node != 500)&& ((mb_node[grey_node]->get_node_status() == 1)))
			{
				for(int position = 0 ; position< neighbor_size ; ++position )
				{
					white_node = mb_node[grey_node]->get_Ul_nodes_by_position(position);
					if((white_node != 500 )&&(mb_node[white_node]->get_node_status() == 0))
					{
						count_size = find_the_count(grey_node , white_node);
						if(count_size >best_double_size )
						{
							best_double_size = count_size ;
							best_double_node_1 = grey_node;
							best_double_node_2 = white_node;
							//printf("Node1 %d node2 %d is best node with size %d\n" ,best_double_node_1 ,best_double_node_2 ,best_single_size );
						}
					}
				}
			}	
		}
		if(best_double_size >  best_single_size)
		{
			changestatus(best_double_node_1,2);
			++count_black_node;
			printf("Node is created as Black Node ::%d\n", best_double_node_1);
			changestatus(best_double_node_2,2);
			++count_black_node;
			printf("Node is created as Black Node ::%d\n", best_double_node_2);
			printf("\n\n");
			//Going to remove these nodes from the Grey_node_array
			for( i  = 0 ; i<= array_size ; ++i)
			{
				if(Grey_node_array[i] == best_double_node_1)
				{
					//printf("int Break\n");
					break;
				}
			}
			for(i ; i<=array_size ; ++i)
			{
				Grey_node_array[i] = Grey_node_array[i+1];
				Grey_node_size[i] = Grey_node_size[i+1];
			}
			array_size = array_size - 1;
			for( i  = 0 ; i<= array_size ; ++i)
			{
				if(Grey_node_array[i] == best_double_node_2)
				{
					//printf("int Break\n");
					break;
				}
			}
			for(i ; i<=array_size ; ++i)
			{
				Grey_node_array[i] = Grey_node_array[i+1];
				Grey_node_size[i] = Grey_node_size[i+1];
			}
			array_size = array_size - 1;
			//Going To Make Node's as a Black  Node's 
			int neighbor_size_1 = mb_node[best_double_node_1]->get_neighbor_size();
			int neighbor_size_2 = mb_node[best_double_node_2]->get_neighbor_size();
			for(int position = 0 ; position< neighbor_size_1 ; ++position )
			{
				mynode_1 = mb_node[best_double_node_1]->get_Ul_nodes_by_position(position);
				if(mynode_1 != 500)
				{
					//int neighbor_size_1 = mb_node[best_double_node_2]->get_neighbor_size();
					for(int position_2 = 0; position_2 <neighbor_size_2 ; ++position_2 )
					{
						mynode_2 = mb_node[best_double_node_2]->get_Ul_nodes_by_position(position_2);
						if(mynode_2 != 500)
						{
							if(mynode_1 == mynode_2)
							{
								changestatus(mynode_1,1);
								Grey_node_array[array_size] = mynode_1;
								Grey_node_size[array_size] = mb_node[mynode_1]->get_neighbor_size();//here the value is not updated so first when we will use update_node_array_status then this node's status will get update 
								++array_size;
							}
							else
							{
								changestatus(mynode_1,1);
								Grey_node_array[array_size] = mynode_1;
								Grey_node_size[array_size] = mb_node[mynode_1]->get_neighbor_size();
								++array_size;
								changestatus(mynode_2,1);	
								Grey_node_array[array_size] = mynode_2;
								Grey_node_size[array_size] = mb_node[mynode_2]->get_neighbor_size();
								++array_size;
							}						
	
						}
					
					}
			
				}
			}
		update_node_array_status( Grey_node_array ,Grey_node_size , array_size  ); //very very important 
		}
		else
		{
			changestatus(best_single_node,2);
			++count_black_node;
			printf("Node is created as Black Node ::%d\n", best_single_node);
			printf("\n\n");
			//Going to remoe the Node which have became black
			for( i  = 0 ; i<= array_size ; ++i)
			{
				if(Grey_node_array[i] == best_single_node)
				{
					//printf("int Break\n");
					break;
				}
			}
			for(i ; i<=array_size ; ++i)
			{
				Grey_node_array[i] = Grey_node_array[i+1];
				Grey_node_size[i] = Grey_node_size[i+1];
			}
			array_size = array_size - 1;			
			int neighbor_size = mb_node[best_single_node]->get_neighbor_size();
			for(int i = 0 ; i<neighbor_size ; ++i )
			{
				mynode = mb_node[best_single_node]->get_Ul_nodes_by_position(i);
				if(mynode != 500)
				{
					changestatus(mynode,1);
					Grey_node_array[array_size] = mynode ;
					Grey_node_size[array_size] = mb_node[mynode]->get_neighbor_size();
					++array_size;	
				
				}
			}
		update_node_array_status( Grey_node_array ,Grey_node_size , array_size  ); //very very important 	
		}
		
	}
printf("\n\nTotal Black Nodes ::%d\n\n" ,count_black_node);	
}

int
God::find_the_count(int Grey_parent_node , int White_child_node)
{
	int count  = 0 ;
	int node_1 = 0 ;	
	int node_2 = 0 ;
	int count_to_be_reduce = 0 ;
	int change_1 = 0;
	int change_2 = 0;
	count  = mb_node[Grey_parent_node]->get_neighbor_size() + mb_node[White_child_node]->get_neighbor_size() - 2 ;
	int count_1 = mb_node[Grey_parent_node]->get_neighbor_size();
	int count_2 = mb_node[White_child_node]->get_neighbor_size();
	for(int i = 0 ; i<count_1 ; ++i)
	{
		change_1 = 1;
		node_1 =  mb_node[Grey_parent_node]->get_Ul_nodes_by_position_nl_node(i);
		if(node_1 != 500)
		{
			//printf("\tTalking About node %d from node %d\n" ,node_1 ,Grey_parent_node );
			for(int j = 0 ; j<count_2 ; ++j)
			{
				change_2 = 1;
				node_2 = mb_node[White_child_node]->get_Ul_nodes_by_position_nl_node(j);
				if(node_2 != 500)
				{
					//printf("\tTalking About node %d from node %d\n" ,node_2 ,White_child_node );
					if((node_1 == node_2) &&(node_1 != 500))
					{
				
				
							++count_to_be_reduce;
							//printf("Going to Reduce node %d\n" ,node_1 );
							//mb_node[Grey_parent_node]->reduce_this_node_only_in_nl_list(node_1);
							//printf("Node %d is going to be deleted \n",node_1 );
							//mb_node[White_child_node]->reduce_this_node_only_in_nl_list(node_2);
							//printf("Node %d is going to be deleted \n",node_2 );
							change_1 = 0;
							change_2 = 0;
				
					}
				}
			
			}
		}
		//printf("Inner Exit\n");
	if(change_1 == 1)
	{
		if((mb_node[node_1]->get_node_status() == 1) || (mb_node[node_1]->get_node_status() == 2))
				{
					++count_to_be_reduce;
					//mb_node[Grey_parent_node]->reduce_this_node_only_in_nl_list(node_1);
					//printf("Node %d is going to be deleted \n",node_1 );
					//printf("Going to Reduce node %d\n" ,node_1 );
					change_1 = 0;
				}		
	}
	if(change_2 == 1)
	{
		if((mb_node[node_2]->get_node_status() == 1) || (mb_node[node_2]->get_node_status() == 2))
				{
					++count_to_be_reduce;
					//mb_node[White_child_node]->reduce_this_node_only_in_nl_list(node_2);
					//printf("Node %d is going to be deleted \n",node_2 );
					//printf("Going to Reduce node %d\n" ,node_2 );
					change_2 = 0;
				}		
	}
	}
	count = count - count_to_be_reduce;
	//printf("\nExit\n");
return count;
}

void
God::Guha_Kular_Algo_1()
{
	find_neighbor();
	int Grey_node_array[1000];
	int Grey_node_size[1000];
	int array_size = 0 ; 
	int statrting_node = pick_up_a_Best_node_centralized();
	int i = 0 ;
	int mynode = 0; 
	int black_node_count = 0 ;
	int neighbor_size = mb_node[statrting_node]->get_neighbor_size();
	changestatus(statrting_node,2) ;
	++black_node_count;
	//printf("Node is created as Black Node ::%d\n", statrting_node);
	for(int position = 0 ; position< neighbor_size ; ++position )
	{
		mynode = mb_node[statrting_node]->get_Ul_nodes_by_position(position);
		if(mynode != 500)
		{
			//mb_node[mynode]->get_neighbor_size();
			changestatus(mynode,1) ;
			//mb_node[mynode]->get_neighbor_size()
			//Grey_node_array[array_size] = mynode;
			//++array_size;
		}
	}
	int count_size = 0;
	int best_single_node = 0 ;
	int best_size = 0 ; 
	for(int position = 0 ; position< neighbor_size ; ++position )
	{
		mynode = mb_node[statrting_node]->get_Ul_nodes_by_position(position);
		if((mynode != 500)&&(mb_node[mynode]->get_node_status() == 1))
		{
			count_size = count_all_white_node_for_single_node(mynode);
			Grey_node_array[array_size] = mynode;
			Grey_node_size[array_size] = count_size;
			++array_size;
		}
		if(count_size >best_size )
		{
			best_size = count_size;
			best_single_node = mynode ;
			
		}
	}
	update_node_array_status(Grey_node_array, Grey_node_size , array_size  );
	while(if_any_white_node_is_white_left())
	{
		int i = 0 ;
		changestatus(best_single_node,2);
		++black_node_count;
		//printf("Node is Created as the Black Node :::%d\n" , best_single_node);
		for( i  = 0 ; i<= array_size ; ++i)
		{
			if(Grey_node_array[i] == best_single_node)
			{
				//printf("int Break\n");
				break;
			}
		}
		for(i ; i<=array_size ; ++i)
		{
			Grey_node_array[i] = Grey_node_array[i+1];
			Grey_node_size[i] = Grey_node_size[i+1];
		}
		array_size = array_size - 1;
		//printf("QAWS");
		neighbor_size = mb_node[best_single_node]->get_neighbor_size();
		//printf("Neighbor Size :: %d\n" , neighbor_size);
		for(int position = 0 ; position< neighbor_size ; ++position )
		{
			mynode = mb_node[best_single_node]->get_Ul_nodes_by_position(position);
			if(mynode != 500)
			{
				//printf("CASE 1.1");
				//printf("Node is  Selected %d\n", mynode);
				//printf("Node's Status %d\n" , mb_node[mynode]->get_node_status());
				if(mb_node[mynode]->get_node_status() == 0 )
				{
					//printf("@Q@W@W@\n");
					//mb_node[mynode]->get_neighbor_size();
					changestatus(mynode,1) ;
					//mb_node[mynode]->get_neighbor_size()
						Grey_node_array[array_size] = mynode;
						Grey_node_size[array_size] = count_all_white_node_for_single_node(mynode);
						++array_size;
						//printf("Node is added ::%d\n" ,mynode);
					
				}
				//printf("But that node is not selected node %d " , mynode);
			}
		}
		update_node_array_status(Grey_node_array, Grey_node_size , array_size  );
		count_size = 0;
		best_single_node = 0 ;
		best_size = 0 ;
		//going to find the best Node Again
		for(int position = 0 ; position< array_size ; ++position )
		{
			count_size = Grey_node_size[position];
			if(count_size > best_size)
			{
				best_size = count_size;
				best_single_node = Grey_node_array[position];
			}
		}
		
		
	}
	
printf("\n\nTotal Black nodes %d\n\n" , black_node_count);	
}

bool 
God::if_any_white_node_is_white_left()
{
	for(int i = 0 ; i <num_nodes ; ++i)
	{
		if(mb_node[i]->get_node_status() == 0)
		{
			return true ;
		}
	}
return false ;
}

void
God::update_node_array_status(int Grey_node_array[] ,int Grey_node_size[] , int array_size  )
{
	for(int i = 0 ; i<array_size ; ++i)
	{		
		Grey_node_size[i] = count_all_white_node_for_single_node(Grey_node_array[i]);
	}
}

//int 
//God::get_neighbor_size(int node_id)
//{
//	return mb_node[node_id]->get_neighbor_size();
//}

int
God::count_all_white_node_for_single_node(int node_id)
{
	int neighbor_size = mb_node[node_id]->get_neighbor_size();
	int mynode = 0; 
	int count = 0;
	//printf("We are at Node %d\n" , node_id);
	for(int position = 0 ; position< neighbor_size ; ++position )
	{
		mynode = mb_node[node_id]->get_Ul_nodes_by_position(position);
		if(mynode != 500)
		{
			
			if(mb_node[mynode]->get_node_status() == 0 )
			{
				++count;
				//printf("\t Node %d is White node \n" , mynode);
			}
		}		
	}

return count;	
}


//Dynamics

/*void 
God::add_blocked_node(int Receiver_node , int Sending_node)
{
	//printf("Here We are Ok!!");
	printf("Receiver Node %d\n" , Receiver_node);
	//int blocked_counter  = mb_node[Receiver_node]->Blocked_counter_s();
	//int blocked_counter  = mb_node[2]->Blocked_counter_s();
	int blocked_counter  = 0;
	printf("Coubert rmrekgtrklgtlk %d" ,blocked_counter );
	mb_node[Receiver_node]->set_Blocked_node(Sending_node ,blocked_counter );
	printf("\nWe are at end add_blocked_node\n");
}*/

void 
God::add_blocked_node(int Receiver_node, int Sending_node)
{
	//printf("Receiver Node %d\n" ,Receiver_node);
	int blocked_counter  = mb_node[Receiver_node]->Blocked_counter_s();
	//printf("Coubert rmrekgtrklgtlk %d\n\n" ,blocked_counter );
	mb_node[Receiver_node]->set_Blocked_node(Sending_node ,blocked_counter );
}
bool
God::is_sending_node_is_blocked(int Receiver_node, int Sending_node)
{
	int blocked_counter  = mb_node[Receiver_node]->Blocked_counter_s();
	for(int counter = 0 ; counter<blocked_counter ; ++counter)
	{
		if(mb_node[Receiver_node]->get_Blocked_nodes(counter) == Sending_node  )
		return true;
	}
return false ;
}



//====================================================================================================

int
God::getMyGrid(double x, double y)
{
	int xloc, yloc;
	
	if (x > maxX || y >maxY) return(-1);
	
	xloc = (int) x/gridsize_;
	yloc = (int) y/gridsize_;
	
	return(yloc*gridX+xloc);
}




int
God::getMyLeftGrid(double x, double y)
{

	int xloc, yloc;
	
	if (x > maxX || y >maxY) return(-1);
	
	xloc = (int) x/gridsize_;
	yloc = (int) y/gridsize_;

	xloc--;
	// no left grid
	if (xloc < 0) return (-2);
	return(yloc*gridX+xloc);
}

int
God::getMyRightGrid(double x, double y)
{

	int xloc, yloc;
	
	if (x > maxX || y >maxY) return(-1);
	
	xloc = (int) x/gridsize_;
	yloc = (int) y/gridsize_;

	xloc++;
	// no left grid
	if (xloc > gridX) return (-2);
	return(yloc*gridX+xloc);
}

int
God::getMyTopGrid(double x, double y)
{

	int xloc, yloc;
	
	if (x > maxX || y >maxY) return(-1);
	
	xloc = (int) x/gridsize_;
	yloc = (int) y/gridsize_;

	yloc++;
	// no top grid
	if (yloc > gridY) return (-2);
	return(yloc*gridX+xloc);
}

int
God::getMyBottomGrid(double x, double y)
{

	int xloc, yloc;
	
	if (x > maxX || y >maxY) return(-1);
	
	xloc = (int) x/gridsize_;
	yloc = (int) y/gridsize_;

	yloc--;
	// no top grid
	if (yloc < 0 ) return (-2);
	return(yloc*gridX+xloc);
}

int 
God::command(int argc, const char* const* argv)
{
	Tcl& tcl = Tcl::instance(); 
	if ((instance_ == 0) || (instance_ != this))
          	instance_ = this; 

        if (argc == 2) {

	        if(strcmp(argv[1], "update_node_status") == 0) {
		  UpdateNodeStatus();
		  return TCL_OK;
		}

	        if(strcmp(argv[1], "compute_route") == 0) {
		  ComputeRoute();
		  return TCL_OK;
		}

                if(strcmp(argv[1], "dump") == 0) {
		        Dump();
                        return TCL_OK;
                }
		
		if (strcmp(argv[1], "dump_num_send") == 0) {
		  DumpNumSend();
		  return TCL_OK;
		}

		if (strcmp(argv[1], "on") == 0) {
		  active = true;
		  return TCL_OK;
		}

		if (strcmp(argv[1], "off") == 0) {
		  active = false;
		  return TCL_OK;
		}

		if (strcmp(argv[1], "allow_to_stop") == 0) {
		  allowTostop = true;
		  return TCL_OK;
		}

		if (strcmp(argv[1], "not_allow_to_stop") == 0) {
		  allowTostop = false;
		  return TCL_OK;
		}

		/*
                if(strcmp(argv[1], "dump") == 0) {
                        int i, j;

                        for(i = 1; i < num_nodes; i++) {
                                fprintf(stdout, "%2d) ", i);
                                for(j = 1; j < num_nodes; j++)
                                        fprintf(stdout, "%2d ",
                                                min_hops[i * num_nodes + j]);
                                fprintf(stdout, "\n");
                        }
                        return TCL_OK;
                }
		*/

		if(strcmp(argv[1], "num_nodes") == 0) {
			tcl.resultf("%d", nodes());
			return TCL_OK;
		}
        }
        else if(argc == 3) {

	        if (strcasecmp(argv[1], "is_source") == 0) {
		  int node_id = atoi(argv[2]);

		  if (node_status[node_id].is_source_ == true) {
		    tcl.result("1");
		  } else {
		    tcl.result("0");
		  }
		  return TCL_OK;
	        }

	        if (strcasecmp(argv[1], "is_sink") == 0) {
		  int node_id = atoi(argv[2]);

		  if (node_status[node_id].is_sink_ == true) {
		    tcl.result("1");
		  } else {
		    tcl.result("0");
		  }
		  return TCL_OK;
	        }

	        if (strcasecmp(argv[1], "is_on_trees") == 0) {
		  int node_id = atoi(argv[2]);

		  if (node_status[node_id].is_on_trees_ == true) {
		    tcl.result("1");
		  } else {
		    tcl.result("0");
		  }
		  return TCL_OK;
	        }

                if (strcasecmp(argv[1], "num_nodes") == 0) {
                        assert(num_nodes == 0);

                        // index always starts from 0 //mnode
                        num_nodes = atoi(argv[2]);

			assert(num_nodes > 0);
			
			printf("num_nodes is set  %d\n", num_nodes);
			//printf("Arun Here\n");
			//Whe we are Using Wireless simulation is always comes here GOD is always called 
                        min_hops = new int[num_nodes * num_nodes];
			mb_node = new MobileNode*[num_nodes];
			//MobileNode **mb_node;
			node_status = new NodeStatus[num_nodes];
			next_hop = new int[num_nodes * num_nodes];

                        bzero((char*) min_hops,
                              sizeof(int) * num_nodes * num_nodes);
			bzero((char*) mb_node,
			      sizeof(MobileNode*) * num_nodes);
			bzero((char*) next_hop,
			      sizeof(int) * num_nodes * num_nodes);
			//Added by Arun to fill the neighbor List 
			//int nodes=God::instance()->nodes(); 
			//for(int i = 0 ; i <nodes ; ++i )
			//{
			//	node_neighbors_initially(i) ;
			//}
                        instance_ = this;

                        return TCL_OK;
                }

		if (strcasecmp(argv[1], "num_data_types") == 0) {
		  assert(num_data_types == 0);

                  num_data_types = atoi(argv[2]);

		  assert(num_nodes > 0);
		  assert(num_data_types > 0);
			
                  source_table = new int*[num_data_types * num_nodes];
		  sink_table = new int[num_data_types * num_nodes];
		  num_send = new int[num_data_types];

                  bzero((char*) source_table,
                              sizeof(int *) * num_data_types * num_nodes);
                  bzero((char*) sink_table,
                              sizeof(int) * num_data_types * num_nodes);
		  bzero((char*) num_send, sizeof(int) * num_data_types);

		  return TCL_OK;
		}

		if (strcasecmp(argv[1], "new_node") == 0) {
		  assert(num_nodes > 0);
		  MobileNode *obj = (MobileNode *)TclObject::lookup(argv[2]);
		  assert(obj != 0);
		  assert(obj->address() < num_nodes);

		  mb_node[obj->address()] = obj; 
		  return TCL_OK;
		}

		/*
                if (strcasecmp(argv[1], "num_nodes") == 0) {
                        assert(num_nodes == 0);

                        // allow for 0 based to 1 based conversion
                        num_nodes = atoi(argv[2]) + 1;

                        min_hops = new int[num_nodes * num_nodes];
                        bzero((char*) min_hops,
                              sizeof(int) * num_nodes * num_nodes);

                        instance_ = this;

                        return TCL_OK;
                }
		*/

        }
	else if (argc == 4) {

	  if (strcasecmp(argv[1], "is_reachable") == 0) {
	    int n1 = atoi(argv[2]);
	    int n2 = atoi(argv[3]);

	    if (IsReachable(n1,n2) == true) {
	      tcl.result("1");
	    } else {
	      tcl.result("0");
	    }

	    return TCL_OK;
	  }


	  // We can add source from tcl script or call AddSource directly.

	  if (strcasecmp(argv[1], "add_source") == 0) {
	    int dt = atoi(argv[2]);
	    int srcid = atoi(argv[3]);
	    
	    AddSource(dt, srcid);
	    return TCL_OK;
	  }

	  // We can add sink from tcl script or call AddSink directly.

	  if (strcasecmp(argv[1], "add_sink") == 0) {
	    int dt = atoi(argv[2]);
	    int skid = atoi(argv[3]);
	    
	    AddSink(dt, skid);
	    return TCL_OK;
	  }

	}
        else if(argc == 5) {

		/* load for grid-based adaptive fidelity */
		if (strcmp(argv[1], "load_grid") == 0) {
			if(load_grid(atoi(argv[2]), atoi(argv[3]), atoi(argv[4])))
				return TCL_ERROR;
			return TCL_OK;
		}

                if (strcasecmp(argv[1], "set-dist") == 0) {
                        int i = atoi(argv[2]);
                        int j = atoi(argv[3]);
                        int d = atoi(argv[4]);

                        assert(i >= 0 && i < num_nodes);
                        assert(j >= 0 && j < num_nodes);

			if (active == true) {
			  if (NOW > prev_time) {
			    ComputeRoute();
			  }
			}
			else {
			  min_hops[i*num_nodes+j] = d;
			  min_hops[j*num_nodes+i] = d;
			}

			// The scenario file should set the node positions
			// before calling set-dist !!

			assert(min_hops[i * num_nodes + j] == d);
                        assert(min_hops[j * num_nodes + i] == d);
                        return TCL_OK;
                }

		/*
                if (strcasecmp(argv[1], "set-dist") == 0) {
                        int i = atoi(argv[2]);
                        int j = atoi(argv[3]);
                        int d = atoi(argv[4]);

                        assert(i >= 0 && i < num_nodes);
                        assert(j >= 0 && j < num_nodes);

                        min_hops[i * num_nodes + j] = d;
                        min_hops[j * num_nodes + i] = d;
                        return TCL_OK;
                }
		*/

        } 
        return BiConnector::command(argc, argv);
}








