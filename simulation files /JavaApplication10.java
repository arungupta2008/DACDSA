/*
 * To change this template, choose Tools | Templates
 * and open the template in the editor.
 */
package javaapplication10;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.DataInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileWriter;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.util.StringTokenizer;

/**
 *
 * @author Akash
 */
public class JavaApplication10 {

 public static void main(String args[])
  {
  try{
  // Open the file that is the first 
  // command line parameter
  FileInputStream fstream = new FileInputStream("test1.txt");
  // Get the object of DataInputStream
  DataInputStream in = new DataInputStream(fstream);
  BufferedReader br = new BufferedReader(new InputStreamReader(in));
  String strLine;
  
  String s ="" ;
  //Read File Line By Line
  while ((strLine = br.readLine()) != null)   {
  // Print the content on the console
  System.out.println (strLine);
  
  /////////   i will put my whole code over here .........
  
   StringTokenizer tok = new StringTokenizer(strLine);
  
   String ss = null ;
   
   /////////while loop
   
    while( tok.hasMoreTokens()){
      
      ss=  tok.nextToken() ;
      
      ////  if it would be ns
      
      
      if(ss.equals("ns") || ss.equals("$ns") || ss.contains("$ns")){
      
      
      
      
      
      
      
      
      
      }else{
      
      
      
      if(ss.length() < 6  ){
      
      /// it would be possible
          
          if(ss.contains("$n")){
          
              ////// change kiya hai.
             int a = (int) ss.charAt(2);
     
             if(a>=48 || a<=57){
            
             String sub =     ss.substring(2, ss.length()) ;
             
             
             ss= ss.replace(ss, "$node_("+sub+")");

            }
          }else{
              
            
             if(ss.charAt(0) == 'n' && ss.charAt(ss.length()-1)!= ']'){

                 
                int a = (int) ss.charAt(1);
      
             if(a>=48 || a<=57){
            
             String sub =     ss.substring(1, ss.length()) ;
             
             
             ss= ss.replace(ss, "$node_("+sub+")");

            }
             }  
          
          
          
          
          }
      
      
      
      }
     }
      
      
      
      s = s +" "+ ss; 
      
      
      
      
      
      
      
      }
     
   
   
   
   
   
   
   
   
   
   
   //////////
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  
  s= s + "\n" ;
  
  
  
  
  
  }
  
  ////////////////  writing into the file ///////
  
 // String output = String.format( s ,System.getProperty("line.separator"));
BufferedWriter oFile = new BufferedWriter(new OutputStreamWriter(
    new FileOutputStream("test.txt"), "UTF-16"));

System.out.println("The line is: "+ s);
oFile.write(s);
oFile.close();
  
  
  
  
  
  
  
  
  
  
  ////////////////////////////
  
  
  
  
  
  
  System.out.println(s);
  
  
  //Close the input stream
  in.close();
    }catch (Exception e){//Catch exception if any
  System.err.println("Error: " + e.getMessage());
  }
  }
}
