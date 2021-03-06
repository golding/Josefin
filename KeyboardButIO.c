/*************************************************************************
 *      KeyboardBut.c
 *
 *      Ver  Date       Name Description
 *      W    2009-02-26 AGY  Created.
 *      PA1  2016-03-09 AGY  This version uses IO pins as buttons, requires WiringPi for Raspberry
 *
 *************************************************************************/

#include <sys/ioctl.h> 
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <linux/ioctl.h>
#include <errno.h>
#include <unistd.h> 
#include <string.h>
#include <malloc.h>
#include <termios.h>
#include <dirent.h>
#include <sys/time.h>
#include <linux/input.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/time.h>
#include "OWHndlrOWFSFile.h"
#ifdef RPI_DEFINED
 #include <wiringPi.h>
#elif BB_DEFINED
#include "SimpleGPIO.h"
#endif
#include "SysDef.h"
//#include "KeyboardIO.h"
//#include "TimHndlr.h"
#include "Main.h"

// All these started by Main.c (bottom of the file...)
void * RdButton(enum ProcTypes_e ProcType) { // Not used today as LCD buttons works well!
  // Reads buttons connected to corresponding IO pins
  int 	Idx, ButIdx, fd_I2CKnob, fd_main, fd_timo, fd_OpBut, fd_LftBut, fd_RgtBut, ret;
  char 	data_read[2];
  char 	led[10], OpButton[10], LftButton[10], RgtButton[10];
  char	OpButOn, LftButOn, RgtButOn;
  static unsigned char  Buf[sizeof(union SIGNAL)]; // Static due to must be placed in global memory (not stack) otherwise we get OS dump Misaligned data!!!
  union SIGNAL             *Msg;
  struct  input_event       ie; 
#ifdef RPI_DEFINED
	int 		But_Op    = 8; 			// GPIO 3 header pin 5
	int 		But_Rgt   = 9;    	// SDA0 2 header pin 3

#elif BB_DEFINED
	int 		But_Op    = 67; 			// GPIO 67 header pin 8, P8
	int 		But_Rgt   = 60;     	// GPIO 60 header pin 12, P9
#endif	

  if (ProcType == BF537) {
    strcpy(led, STR_LED_BF537);
    strcpy(OpButton,  STR_OPBUT_BF537);
    strcpy(LftButton, STR_LFTBUT_BF537);
    strcpy(RgtButton, STR_RGTBUT_BF537);
  } 
  else if (ProcType == BF533) {
    strcpy(led, STR_LED_BF533);
    strcpy(OpButton,  STR_OPBUT_BF533);
    strcpy(LftButton, STR_LFTBUT_BF533);
    strcpy(RgtButton, STR_RGTBUT_BF533);
  } else if (ProcType == RPI) {
    
  } else if (ProcType == BB) {
	
  } else if (ProcType == HOSTENV) {
    // Do nothing  printf("Kbd defined HOST\n");
	  }
  else
    CHECK(FALSE, "Unknown processor type\n");

  OPEN_PIPE(fd_main, MAIN_PIPE, O_WRONLY);
  OpButOn = FALSE;
  LftButOn = FALSE;
  RgtButOn = FALSE;
// WiringPi initiated by Main.c
#ifdef RPI_DEFINED
	pinMode(But_Op, INPUT);
	pinMode(But_Rgt, INPUT);
#elif BB_DEFINED
	gpio_export(But_Op);
	gpio_export(But_Rgt);
	gpio_set_dir(But_Op, INPUT_PIN);
  
	gpio_set_dir(But_Rgt, INPUT_PIN);
#endif

  LOG_MSG("Button Started\n");
	Idx = 0;
	ButIdx = 0;
  while(TRUE) {
		//Idx++;
    Msg = (void *) Buf; // Set ptr to receiving buffer
    usleep(20000); // Timeout between each scan of keyboard, to be adjusted
#ifdef RPI_DEFINED
// Check if Right button pressed	
    ret = digitalRead(But_Op);
#elif BB_DEFINED
		gpio_get_value(But_Op, &ret);
#endif
  
		//printf(" OP: %d\r", ret);
		if (ret != 0)  // Button is NOT pressed!!
			OpButOn = FALSE;            // Button not pressed
    else if (OpButOn == FALSE)  { // Button pressed and released since last read
      OpButOn = TRUE;
      Msg->SigNo = SIGOpButOn;// Send signal	
      SEND(fd_main, Msg, sizeof(union SIGNAL));
		printf("Oppressed %d\r\n", ret);
		}
		
// Check if Right button pressed			
#ifdef RPI_DEFINED
    ret = digitalRead(But_Rgt);		
#elif BB_DEFINED	
		gpio_get_value(But_Rgt, &ret);
#endif // RPI_DEFINED

		if (ret != 0)	// Button is NOT pressed!!
			RgtButOn = FALSE;            // Button not pressed
    else if (RgtButOn == FALSE)  { // Button pressed and released since last read
      RgtButOn = TRUE;
      Msg->SigNo = SIGRghtButOn;// Send signal	
      SEND(fd_main, Msg, sizeof(union SIGNAL));
		printf("Rgtpressed %d\r\n", ret);
		}
   } // while
	  exit(0);
}; 
// Reads keyboard input, mainly for debugging
void * RdKeyboard(enum ProcTypes_e ProcType) {
  int 	fd_main, ret;
  static unsigned char  Buf[sizeof(union SIGNAL)]; // Static due to must be placed in global memory (not stack) otherwise we get OS dump Misaligned data!!!
  union SIGNAL             *Msg;

  OPEN_PIPE(fd_main, MAIN_PIPE, O_WRONLY);
  LOG_MSG("Kbd Started\n");
  while(TRUE) {
		//Idx++;
    Msg = (void *) Buf; // Set ptr to receiving buffer
 		switch (getchar()) {
     case 'H':  // Help info
     case 'h':
   		 	printf("(O)peration, (R)ight, (D)ebug On/Off \r\n");   	
        printf("\r\n");
		 break;  

     case 'D':  // Debug On/Off
     case 'd':
   		 if (DebugOn == TRUE) {
         DebugOn = FALSE;
         LOG_MSG("Debug OFF\n");
       }
       else {
         DebugOn = TRUE;
         LOG_MSG("Debug ON\n");
       }  
		 break;  
     case 'T':  // Debug level 2 On/Off
     case 't':
   		 if (DebugOn == 2) {
         DebugOn = FALSE;
         LOG_MSG("Debug OFF level 2\n");
       }
       else {
         DebugOn = 2;
         LOG_MSG("Debug ON level 2\n");
       }  
		 break; 
     
   	 case 'O': // Operation, OP pressed
     case 'o':
   		 Msg->SigNo = SIGOpButOn;// Send signal	
 			 SEND(fd_main, Msg, sizeof(union SIGNAL));
		 break;

		 case 'R': // Right pressed
     case 'r':
    	 Msg->SigNo = SIGRghtButOn;// Send signal	
   		 SEND(fd_main, Msg, sizeof(union SIGNAL));
		  break;
		 }
   } // while
	  exit(0);
}; 
