# This script is created by NSG2 beta1
# <http://wushoupong.googlepages.com/nsg>

#===================================
#     Simulation parameters setup
#===================================
set val(chan)   Channel/WirelessChannel    ;# channel type
set val(prop)   Propagation/TwoRayGround   ;# radio-propagation model
set val(netif)  Phy/WirelessPhy            ;# network interface type
set val(mac)    Mac/802_11                 ;# MAC type
set val(ifq)    Queue/DropTail/PriQueue    ;# interface queue type
set val(ll)     LL                         ;# link layer type
set val(ant)    Antenna/OmniAntenna        ;# antenna model
set val(ifqlen) 50                         ;# max packet in ifq
set val(nn)     41                         ;# number of mobilenodes
set val(rp)     DSR                       ;# routing protocol
set val(x)      5104                      ;# X dimension of topography
set val(y)      100                      ;# Y dimension of topography
set val(stop)   500.0                         ;# time of simulation end

#===================================
#        Initialization        
#===================================
#Create a ns simulator
set ns [new Simulator]

#Setup topography object
set topo       [new Topography]
$topo load_flatgrid $val(x) $val(y)
create-god $val(nn)

#Open the NS trace file
set tracefile [open out.tr w]
$ns trace-all $tracefile

#Open the NAM trace file
set namfile [open out.nam w]
$ns namtrace-all $namfile
$ns namtrace-all-wireless $namfile $val(x) $val(y)
set chan [new $val(chan)];#Create wireless channel

#===================================
#     Mobile node parameter setup
#===================================
$ns node-config -adhocRouting  $val(rp) \
                -llType        $val(ll) \
                -macType       $val(mac) \
                -ifqType       $val(ifq) \
                -ifqLen        $val(ifqlen) \
                -antType       $val(ant) \
                -propType      $val(prop) \
                -phyType       $val(netif) \
                -channel       $chan \
                -topoInstance  $topo \
                -agentTrace    ON \
                -routerTrace   OFF \
                -macTrace      OFF \
                -movementTrace OFF

#===================================
#        Nodes Definition        
#===================================
#Create 41 nodes
set node_(0) [$ns node]
$node_(0) set X_ 1557
$node_(0) set Y_ 277
$node_(0) set Z_ 0.0
$ns initial_node_pos $node_(0) 20
set node_(1) [$ns node]
$node_(1) set X_ 1916
$node_(1) set Y_ 443
$node_(1) set Z_ 0.0
$ns initial_node_pos $node_(1) 20
set node_(2) [$ns node]
$node_(2) set X_ 1247
$node_(2) set Y_ 523
$node_(2) set Z_ 0.0
$ns initial_node_pos $node_(2) 20
set node_(3) [$ns node]
$node_(3) set X_ 1559
$node_(3) set Y_ 427
$node_(3) set Z_ 0.0
$ns initial_node_pos $node_(3) 20
set node_(4) [$ns node]
$node_(4) set X_ 1058
$node_(4) set Y_ 588
$node_(4) set Z_ 0.0
$ns initial_node_pos $node_(4) 20
set node_(5) [$ns node]
$node_(5) set X_ 1318
$node_(5) set Y_ 736
$node_(5) set Z_ 0.0
$ns initial_node_pos $node_(5) 20
set node_(6) [$ns node]
$node_(6) set X_ 855
$node_(6) set Y_ 720
$node_(6) set Z_ 0.0
$ns initial_node_pos $node_(6) 20
set node_(7) [$ns node]
$node_(7) set X_ 1447
$node_(7) set Y_ 556
$node_(7) set Z_ 0.0
$ns initial_node_pos $node_(7) 20
set node_(8) [$ns node]
$node_(8) set X_ 1685
$node_(8) set Y_ 519
$node_(8) set Z_ 0.0
$ns initial_node_pos $node_(8) 20
set node_(9) [$ns node]
$node_(9) set X_ 1164
$node_(9) set Y_ 688
$node_(9) set Z_ 0.0
$ns initial_node_pos $node_(9) 20
set node_(10) [$ns node]
$node_(10) set X_ 1249
$node_(10) set Y_ 132
$node_(10) set Z_ 0.0
$ns initial_node_pos $node_(10) 20
set node_(11) [$ns node]
$node_(11) set X_ 1559
$node_(11) set Y_ 711
$node_(11) set Z_ 0.0
$ns initial_node_pos $node_(11) 20
set node_(12) [$ns node]
$node_(12) set X_ 1020
$node_(12) set Y_ 817
$node_(12) set Z_ 0.0
$ns initial_node_pos $node_(12) 20
set node_(13) [$ns node]
$node_(13) set X_ 1450
$node_(13) set Y_ 804
$node_(13) set Z_ 0.0
$ns initial_node_pos $node_(13) 20
set node_(14) [$ns node]
$node_(14) set X_ 1383
$node_(14) set Y_ 352
$node_(14) set Z_ 0.0
$ns initial_node_pos $node_(14) 20
set node_(15) [$ns node]
$node_(15) set X_ 1481
$node_(15) set Y_ 157
$node_(15) set Z_ 0.0
$ns initial_node_pos $node_(15) 20
set node_(16) [$ns node]
$node_(16) set X_ 1243
$node_(16) set Y_ 837
$node_(16) set Z_ 0.0
$ns initial_node_pos $node_(16) 20
set node_(17) [$ns node]
$node_(17) set X_ 1159
$node_(17) set Y_ 359
$node_(17) set Z_ 0.0
$ns initial_node_pos $node_(17) 20
set node_(18) [$ns node]
$node_(18) set X_ 1987
$node_(18) set Y_ 578
$node_(18) set Z_ 0.0
$ns initial_node_pos $node_(18) 20
set node_(19) [$ns node]
$node_(19) set X_ 1777
$node_(19) set Y_ 688
$node_(19) set Z_ 0.0
$ns initial_node_pos $node_(19) 20
set node_(20) [$ns node]
$node_(20) set X_ 2150
$node_(20) set Y_ 723
$node_(20) set Z_ 0.0
$ns initial_node_pos $node_(20) 20
set node_(21) [$ns node]
$node_(21) set X_ 958
$node_(21) set Y_ 398
$node_(21) set Z_ 0.0
$ns initial_node_pos $node_(21) 20
set node_(22) [$ns node]
$node_(22) set X_ 1695
$node_(22) set Y_ 261
$node_(22) set Z_ 0.0
$ns initial_node_pos $node_(22) 20
set node_(23) [$ns node]
$node_(23) set X_ 1658
$node_(23) set Y_ 829
$node_(23) set Z_ 0.0
$ns initial_node_pos $node_(23) 20
set node_(24) [$ns node]
$node_(24) set X_ 2058
$node_(24) set Y_ 375
$node_(24) set Z_ 0.0
$ns initial_node_pos $node_(24) 20
set node_(25) [$ns node]
$node_(25) set X_ 716
$node_(25) set Y_ 594
$node_(25) set Z_ 0.0
$ns initial_node_pos $node_(25) 20
set node_(26) [$ns node]
$node_(26) set X_ 870
$node_(26) set Y_ 542
$node_(26) set Z_ 0.0
$ns initial_node_pos $node_(26) 20
set node_(27) [$ns node]
$node_(27) set X_ 1822
$node_(27) set Y_ 329
$node_(27) set Z_ 0.0
$ns initial_node_pos $node_(27) 20
set node_(28) [$ns node]
$node_(28) set X_ 1452
$node_(28) set Y_ 1025
$node_(28) set Z_ 0.0
$ns initial_node_pos $node_(28) 20
set node_(29) [$ns node]
$node_(29) set X_ 863
$node_(29) set Y_ 887
$node_(29) set Z_ 0.0
$ns initial_node_pos $node_(29) 20
set node_(30) [$ns node]
$node_(30) set X_ 1338
$node_(30) set Y_ 599
$node_(30) set Z_ 0.0
$ns initial_node_pos $node_(30) 20
set node_(31) [$ns node]
$node_(31) set X_ 2221
$node_(31) set Y_ 295
$node_(31) set Z_ 0.0
$ns initial_node_pos $node_(31) 20
set node_(32) [$ns node]
$node_(32) set X_ 1717
$node_(32) set Y_ 120
$node_(32) set Z_ 0.0
$ns initial_node_pos $node_(32) 20
set node_(33) [$ns node]
$node_(33) set X_ 1854
$node_(33) set Y_ 37
$node_(33) set Z_ 0.0
$ns initial_node_pos $node_(33) 20
set node_(34) [$ns node]
$node_(34) set X_ 2044
$node_(34) set Y_ 31
$node_(34) set Z_ 0.0
$ns initial_node_pos $node_(34) 20
set node_(35) [$ns node]
$node_(35) set X_ 2171
$node_(35) set Y_ 89
$node_(35) set Z_ 0.0
$ns initial_node_pos $node_(35) 20
set node_(36) [$ns node]
$node_(36) set X_ 2402
$node_(36) set Y_ 216
$node_(36) set Z_ 0.0
$ns initial_node_pos $node_(36) 20
set node_(37) [$ns node]
$node_(37) set X_ 848
$node_(37) set Y_ 195
$node_(37) set Z_ 0.0
$ns initial_node_pos $node_(37) 20
set node_(38) [$ns node]
$node_(38) set X_ 1042
$node_(38) set Y_ 108
$node_(38) set Z_ 0.0
$ns initial_node_pos $node_(38) 20
set node_(39) [$ns node]
$node_(39) set X_ 694
$node_(39) set Y_ 383
$node_(39) set Z_ 0.0
$ns initial_node_pos $node_(39) 20
set node_(40) [$ns node]
$node_(40) set X_ 535
$node_(40) set Y_ 528
$node_(40) set Z_ 0.0
$ns initial_node_pos $node_(40) 20

#===================================
#        Agents Definition        
#===================================
#Modes of operation by Arun 
#===================================
#        Applications Definition        
#===================================
#$ns at 1.101844 "$routing_(30) Pick_Up_a_Node 30__node \n "
#$ns at 13.101844 "$routing_(10) CDS 30__node \n "
#$ns at 100.0 "$routing_(10) all 30__node \n "
#$ns at 153.101844 "$routing_(4) Guha 4__node \n "

set god_ [create-god $val(nn)]
for { set i 0 } { $i < $val(nn) } { incr i } {
    #set node_($i) [$ns node]
     $node_($i) random-motion 0 ;# disable random motion
     $god_ new_node $node_($i)
	$node_($i) color green
	$node_($i) shape circle
}

for {set i 0} {$i < $val(nn) } {incr i} {
  set routing_($i) [$node_($i) set ragent_]
  set temp_($i) [$node_($i) set ragent_]
}

#$ns at 13.101844 "$routing_(14) CDS 14__node \n "
#$ns at 1.101844 "$routing_(39) Pick_Up_a_Node 3__node \n "
$ns at 13.101844 "$routing_(7) CDS \n "
#$ns at 131.101844 "$routing_(7) All_Info \n "
for {set i 0} {$i < $val(nn) } {incr i} { $ns at 50.101844 "$routing_($i) Pruning \n "}
#$ns at 50.101844 "$routing_(7) Pruning \n "
$ns at 131.101844 "$routing_(7) All_Info \n "
#$ns at 100.10101 "$routing_(7) all \n "	
#$ns at 150.0 "routing_(2) LinkDown 7 \n"
#$ns at 13.101844 "$routing_(8) LinkDown 7 \n "
#$ns at 256.101844 "$routing_(7) Beacon 7 \n "
#$ns at 260.101844 "$routing_(8) LinkDown 7 \n "
#$ns at 260.101844 "$routing_(31) LinkDown 24 \n "
# Going to down the link between 31 to 24 
#===================================
#        Termination        
#===================================
#Define a 'finish' procedure
proc finish {} {
    global ns tracefile namfile
    $ns flush-trace
    close $tracefile
    close $namfile
    #exec nam out.nam &
    exit 0
}
#for {set i 0} {$i < $val(nn) } { incr i } {
 #   $ns at $val(stop) "\$n$i reset"
#}

for {set i 0} {$i < $val(nn) } {incr i} {
    $ns at $val(stop).0 "$node_($i) reset";}
$ns at $val(stop) "$ns nam-end-wireless $val(stop)"
$ns at $val(stop) "finish"
$ns at $val(stop) "puts \"done\" ; $ns halt"
$ns run

