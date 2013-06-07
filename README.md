Hello , This is NS2 Project for writin protocol for creating CDS from given network .

We have write DACDSA algorithm for finding the CDS also written the Guha Kullar's algorithm to check 

Remember here i have putted all the files for NS-2.35 make changes in all files then run


```
arun@merom:/home/arun/ns/ns-allinone-2.34/ns-2.34$make clean
arun@merom:/home/arun/ns/ns-allinone-2.34/ns-2.34$ make depend
arun@merom:/home/arun/ns/ns-allinone-2.34/ns-2.34$make
arun@merom:/home/arun/ns/ns-allinone-2.34/ns-2.34$sudo make install
```

For DACDSA algorithm  :
```
$ns at 13.101844 "$routing_(7) CDS \n "
$ns at 131.101844 "$routing_(7) All_Info \n "
```
then run the Test.tcl file for the simulation 
remember here there are different modes of protocl like for finding the CDS by DACDS or by Guha1 or by Guha 2 

go to in Test.tcl and find under this :)
`
\#Modes of operation by Arun 
`
	
```
$ns at 13.101844 "$routing_(7) CDS \n "
```
here you have to initialize $routing_ function then write this line.
Note: here 7 is nothing it's a best node who have maximum degree .
so How to find for your Graph ?

Just run for any value for ex. you have graph scenario 
run 
```
ns Test.tcl > result
```
then in result find "Best Node is"
found result will "Best Node is #some_number"
put that some_number in $ns at 13.101844 "$routing_(some_number) CDS \n "

rerun the TCL file .
after running the simulation and getting result file go to end of result file you will get the result .


For Guha1 algorithm  :
```
$ns at 131.101844 "$routing_(7) Guha1 \n "
$ns at 131.101844 "$routing_(7) All_Info \n "
```

For Guha1 algorithm  :(these are the changes only)
```
$ns at 131.101844 "$routing_(7) Guha2 \n "
$ns at 131.101844 "$routing_(7) All_Info \n "
```











All the best 
