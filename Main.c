/*************************************************************************
 *      Main.c
 *
 *      Ver  	Date    	Name Description
 *        		20061124 	AGY  Created.
 *      	    20130219 	AGY  New version for RaspberryPi. Slight mod needed! 
 *      	    20140214 	AGY  New version adding BeagleBone support 
 * 				    20150212    AGY  Added Byteport support (Curl function to report to Byteport cloud, from iGW)
 * 				    20160725    AGY  Added processor running indication on the LCD, done at Småskär!
 *       W    201612      AGY  New interface to Byteport, now including comands from byteport to Applicatiom
 *
 *************************************************************************/
#include <sys/ioctl.h>
#include <execinfo.h>
#include <linux/ioctl.h>
#include <getopt.h>
#include <signal.h>
#include <sys/poll.h>
#include <stdio.h>
#include <stdlib.h>

#include <fcntl.h>
#include <errno.h>
#include <unistd.h> 
#include <string.h>
#include <pthread.h>
#include <linux/input.h>
#include "sys/stat.h"
#include <malloc.h>
#include <sys/resource.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include <curl/curl.h>

#ifdef RPI_DEFINED
//#include <wiringPi.h>
//#include <wiringSerial.h>
#include "lcd_def.h" 
#include <lcd.h>
#elif BB_DEFINED

#endif

#include "SysDef.h"
#include "Main.h"
 
#include "KeyboardIO.h"
//#include "TimHndlr.h"
#include "OWHndlrOWFSFile.h"
//#include "SocketServer.h"

// Definitions for LCD display
#define	Line1   0
#define	Line2  20
#define	Line3  40
#define Line4  60 

void * BPHandler(struct ProcState_s PState);
void * RdKeyboardKnob(struct ProcState_s PState);
void * RdButton(struct ProcState_s PState);
void * RdKeyboard(struct ProcState_s PState);
void * RdLCDButtons(struct ProcState_s PState);
void * OneWireHandler(struct ProcState_s PState);
void * TimeoutHandler(struct ProcState_s PState);
void * Watchdog(struct ProcState_s PState);
void * SockServer(struct ProcState_s PState);
void   LCDDisplayUpdate(struct ProcState_s *PState);
float  GetWaterLevel(float Level);
float  GetDieselLevel(float Level);
void   InitProc(struct ProcState_s *Pstate);
//void   ByteportReport(struct ProcState_s *Pstate);
void   OpButPressed(struct ProcState_s *PState);
void   RghtButPressed(struct ProcState_s *PState);
void   LftButPressed(struct ProcState_s *PState);
void   BuildBarText(char *Str, float Level, float Resolution);
void   SignalCallbackHandler(int signum);
void   QuitProc(void);
 
// Define global variables
char Once=0;

char    InfoText[300];
char    LCDText[100];// Holds the text currently displayed on LCD, with some extra bytes so we don't overwrite..
char    ProtectionText[100]; // A test to see if problem disappeares
long long TM2Temp, TM1Temp, TM1AD, TM2AD, TMLCD1, TMLCD2;
float		DeltaTime;

char    DbgTest=0;  // Set = 1 if debug needed!!
int     ret;
//struct FilterQueue_s 	FQueue[5]; //NO_OF_ELEM_IN_FILTERQUEU] 

struct  FQ_s  { // used to make a smoother presentation
	float     ADWater;
	float     ADDiesel;
	float     ADBatVoltF;
} FQueue[NO_OF_ELEM_IN_FILTERQUEU] ;
  char                  ProcRunning = '*'; 

int    main(int argc, char *argv[]) {
	union SIGNAL			 		*Msg;
	unsigned char       	Buf[sizeof(union SIGNAL)];
  char                	TimeStamp[100], TimeStampStop[100], FilePath[40];
  unsigned char       	UpdateInterval, Idx;
	enum ProcTypes_e    	ProcessorType;
	char									ByteportText[100];
  int                   fd_Own, fd_ToOwn, fd_Timo, fd_BytePRep, fd_Sens;
 
 // First thing to do! Register signal and signal handler for (error) signals from operating system
  signal(SIGINT, SignalCallbackHandler);  // Ctrl -c catched
  signal(SIGSEGV, SignalCallbackHandler); // Segmentation fault catched
  signal(SIGILL, SignalCallbackHandler);  // Illegal instruction catched
  signal(SIGBUS, SignalCallbackHandler);  // Bus error catched 
  signal(SIGSTKFLT, SignalCallbackHandler); // Stack fault catched 
  signal(SIGXFSZ, SignalCallbackHandler);  // File size exceeded ctached 
  
	//DebugOn = TRUE;   // Start in Debug mode
//ProcState.ModeState      = Water;          // Set initial value, for test purpose. TBD later
  ProcState.ModeState      = MainMode;          // Set initial value, for test purpose. TBD later 
 	ProcState.ServMode       = AnchStop;         	// Set initial value
  ProcState.MinOutTemp     = SENS_DEF_VAL;
  ProcState.MaxOutTemp     = SENS_DEF_VAL;
  ProcState.OutTemp        = SENS_DEF_VAL;
  ProcState.MinSeaTemp     = SENS_DEF_VAL;
  ProcState.MaxSeaTemp     = SENS_DEF_VAL;
  ProcState.SeaTemp        = SENS_DEF_VAL;
  ProcState.MinRefrigTemp  = SENS_DEF_VAL;
  ProcState.MaxRefrigTemp  = SENS_DEF_VAL;
  ProcState.RefrigTemp     = SENS_DEF_VAL;
  ProcState.MinBoxTemp     = SENS_DEF_VAL;
  ProcState.MaxBoxTemp     = SENS_DEF_VAL;
  ProcState.BoxTemp        = SENS_DEF_VAL;
  ProcState.MinWaterTemp   = SENS_DEF_VAL;
  ProcState.MaxWaterTemp   = SENS_DEF_VAL;
  ProcState.WaterTemp      = SENS_DEF_VAL;
  ProcState.WaterLevel     = SENS_DEF_VAL;
  ProcState.HWTemp         = SENS_DEF_VAL;
  ProcState.DieselLevel    = SENS_DEF_VAL;
  ProcState.BatVoltS       = 13.5; //SENS_DEF_VAL; Start value, avoids div by 0!
  ProcState.BatVoltF       = 13.5; //SENS_DEF_VAL; Start value, avoids div by 0!
  ProcState.BatAmpS        = SENS_DEF_VAL;
  ProcState.BatAmpF        = SENS_DEF_VAL;
  ProcState.LCD_Id         = 0;
  ProcState.fd.lcd         = 0;
  ProcState.fd.RD_MainPipe = 0;
  ProcState.fd.WR_MainPipe = 0;
  ProcState.fd.RD_TimoPipe = 0;
  ProcState.fd.WR_TimoPipe = 0;
  ProcState.fd.WR_OWPipe    = 0;
  ProcState.fd.RD_OWPipe    = 0;

  ProcState.fd.WR_kbdButPipe  = 0;
  ProcState.fd.RD_kbdButPipe  = 0;
  ProcState.fd.WR_kbdKnobPipe = 0;
  ProcState.fd.RD_kbdKnobPipe = 0;
  ProcState.fd.RD_BPRepPipe   = 0;
  ProcState.fd.WR_BPRepPipe = 0;  
	ProcState.fd.OutTemp     = 0;
	ProcState.fd.BoxTemp     = 0;
	ProcState.fd.DieselLevel = 0;
	ProcState.fd.WaterLevel  = 0;
	ProcState.fd.RefrigTemp  = 0;
	ProcState.DevLCDDefined  = FALSE;
  ProcState.UpdateInterval = 12;   // Timeout intervall for data & display update  
  ProcState.LCDBlkOnTimer  = LCDBlkOnTimerVal; // Time before turning backlight off   
	

	// Check if 1wire master ID present, set name accordingly. For now use default
  sprintf(ProcState.DeviceName,"%s", "JosefinSim");
// Initiate filter queue
	for (Idx = 0; Idx < NO_OF_ELEM_IN_FILTERQUEU; Idx++) {
		FQueue[Idx].ADDiesel		= SENS_DEF_VAL;
		FQueue[Idx].ADWater 		= SENS_DEF_VAL;
		FQueue[Idx].ADBatVoltF 	= SENS_DEF_VAL;
	} 
  memset(LCDText, ' ', 100); // Clear display buffer
  InitProc(&ProcState); //LOG_MSG("All processes initiated\r\n");sleep(0); 
  
  fd_ToOwn    = ProcState.fd.WR_MainPipe;
  fd_Own      = ProcState.fd.RD_MainPipe;
  fd_BytePRep = ProcState.fd.WR_BPRepPipe;
  fd_Timo     = ProcState.fd.WR_TimoPipe;  
  fd_Sens     = ProcState.fd.WR_OWPipe;
  
 	sprintf(&LCDText[0], " %s started   Ver: %s                       Golding production ", ProcState.DeviceName, __DATE__ );

 // Now we must wait for the LCD to be initiated...
  int idx = 0;
  while (!ProcState.DevLCDDefined) {
    usleep(200000);
//    LOG_MSG(".");
    idx++;
    if (idx >= 1000) {
      LOG_MSG("Err: Not able to start, no LCD found..exit! \r\n"); 
      exit(0);
    }  
  }
  
  if (ProcState.DevLCDDefined) {  // If LCD attached
	LCD1W_WRITE(LCD1, 1, &LCDText[Line1]);
	LCD1W_WRITE(LCD1, 3, &LCDText[Line2]);
	LCD1W_WRITE(LCD1, 2, &LCDText[Line3]);
	LCD1W_WRITE(LCD1, 4, &LCDText[Line4]);	
} else { // report error
  sprintf(InfoText, "Err: LCD not initated fd = %d \n", ProcState.fd.lcd);
  LOG_MSG(InfoText); 
}
	
  REQ_TIMEOUT(fd_Timo, fd_ToOwn, "MainInitTOut", SIGInitMeasTempOut, 3 Sec);  
	REQ_TIMEOUT(fd_Timo, fd_ToOwn, "MainInitTBox", SIGInitMeasTempBox, 8 Sec); 
  REQ_TIMEOUT(fd_Timo, fd_ToOwn, "MainInitTRefrig", SIGInitMeasTempRefrig, 3 Sec); 
  REQ_TIMEOUT(fd_Timo, fd_ToOwn, "MainInitTWater", SIGInitMeasTempWater, 15 Sec);
	REQ_TIMEOUT(fd_Timo, fd_ToOwn, "MainInitTHWater", SIGInitMeasTempHW, 15 Sec);
	REQ_TIMEOUT(fd_Timo, fd_ToOwn, "MainInitTSea", SIGInitMeasTempSea, 20 Sec); 
 // REQ_TIMEOUT(fd_Timo, fd_ToOwn, "MainInitADInt", SIGInitMeasADInt, 2 Sec); 
  REQ_TIMEOUT(fd_Timo, fd_ToOwn, "MainInitADExt", SIGInitMeasADExt, 10 Sec); 
  REQ_TIMEOUT(fd_Timo, fd_ToOwn, "MainInitBlkOn", SIGMinuteTick, 60 Sec); 
  REQ_TIMEOUT(fd_Timo, fd_ToOwn, "MainInitLCDBlink", SIGSecondTick, 1 Sec); 
  REQ_TIMEOUT(fd_Timo, fd_ToOwn, "MainSend BPReport", SIGInitByteportReport, 10 Sec); 

	Msg = (void *) Buf;
  if (fd_BytePRep != 0) {
    Msg->SigNo = SIGByteportInit;
	  sprintf(Msg->ByteportReport.Str, ProcState.DeviceName);			
    SEND(fd_BytePRep, Msg, sizeof(union SIGNAL));
  } else 
    LOG_MSG ("Error: No Byteport handler defined \r\n");
  
	sprintf(InfoText, "%s started Ver:  %s\n", ProcState.DeviceName, __DATE__);
  LOG_MSG(InfoText);
  while (TRUE) {
    WAIT(fd_Own, Buf, sizeof(union SIGNAL));
//if (Msg->SigNo == 10) {DbgTest = 1;}
		fflush(stdout);  // Flush stdout, used if we print to file
		Msg = (void *) Buf;
    if (DbgTest == 1) {printf("2: %d\r\n", Msg->SigNo);usleep(200000);}
 
   switch(Msg->SigNo) {
	   case SIGSecondTick:
		   LCDDisplayUpdate(&ProcState);
		   REQ_TIMEOUT(fd_Timo, fd_ToOwn, "MainLCDBlink", SIGSecondTick, 1 Sec); 
	   break;	
     case SIGMinuteTick:  // Wait until backlight should be turned off
  		 REQ_TIMEOUT(fd_Timo, fd_ToOwn, "MinuteTick", SIGMinuteTick, 60 Sec); 
		 // printf("Tick: %d\r\n", ProcState.LCDBlkOnTimer);
			 if (ProcState.fd.lcd >= 0) {  // First check that we have a LCD attached
					if (ProcState.LCDBlkOnTimer <= 0) 
						Set1WLCDBlkOff(LCD1);  // Turn off backlight on display
					else
						ProcState.LCDBlkOnTimer--;	
				}
      break;
      case SIGInitByteportReport:  // Select below which values to report to Byteport, and how often!      	
        Msg->SigNo = SIGByteportReport;
        sprintf(Msg->ByteportReport.Str, "OutTemp=%-.1f;BoxTemp=%-.1f;RefrigTemp=%-.1f;WaterTemp=%-.1f;WaterLevel=%-.1f;DieselLevel=%-.1f;BatVoltF=%-.1f", ProcState.OutTemp, ProcState.BoxTemp,ProcState.RefrigTemp,ProcState.WaterLevel, ProcState.DieselLevel, ProcState.BatVoltF);			
				//sprintf(Msg->ByteportReport.Str, "OutTemp=%-.1f;BoxTemp=%-.1f;RefrigTemp=%-.1f;WaterTemp=%-.1f;HWaterTemp=%-.1f;SeaTemp=%-.1f;WaterLevel=%-.1f;DieselLevel=%-.1f;BatVoltF=%-.1f", ProcState.OutTemp, ProcState.BoxTemp,ProcState.RefrigTemp, ProcState.WaterTemp, ProcState.HWaterTemp, ProcState.SeaTemp, ProcState.WaterLevel, ProcState.DieselLevel, ProcState.BatVoltF);			
        if (DebugOn == 1) { printf(" %s \r\n", Msg->ByteportReport.Str); }  
        SEND(fd_BytePRep, Msg, sizeof(union SIGNAL));		
        REQ_TIMEOUT(fd_Timo, fd_ToOwn, "MainSend BPReport", SIGInitByteportReport, 60 Sec); // Time between each report to Byteport
      break;
			case SIGInitMeasTempBox:  // Initiate loop to read Temperature sensors
        Msg->SigNo = SIGReadSensorReq;
        Msg->SensorReq.Client_fd = fd_ToOwn;
        Msg->SensorReq.Sensor = BOX_TEMP;
        SEND(fd_Sens, Msg, sizeof(union SIGNAL));
      break;
      case SIGInitMeasTempRefrig:  // Initiate loop to read Temperature sensors
        Msg->SigNo = SIGReadSensorReq;
        Msg->SensorReq.Client_fd = fd_ToOwn;
        Msg->SensorReq.Sensor = REFRIG_TEMP;
        SEND(fd_Sens, Msg, sizeof(union SIGNAL));
      break;
      case SIGInitMeasTempWater:  // Initiate loop to read Temperature sensors
        Msg->SigNo = SIGReadSensorReq;
        Msg->SensorReq.Client_fd = fd_ToOwn;
        Msg->SensorReq.Sensor = WATER_TEMP;
        SEND(fd_Sens, Msg, sizeof(union SIGNAL));
      break;
      case SIGInitMeasTempSea:  // Initiate loop to read Temperature sensors
        Msg->SigNo = SIGReadSensorReq;
        Msg->SensorReq.Client_fd = fd_ToOwn;
        Msg->SensorReq.Sensor = SEA_TEMP;
        SEND(fd_Sens, Msg, sizeof(union SIGNAL));
      break; 
      case SIGInitMeasTempHW:  // Initiate loop to read Temperature sensors
        Msg->SigNo = SIGReadSensorReq;
        Msg->SensorReq.Client_fd = fd_ToOwn;
        Msg->SensorReq.Sensor = HWATER_TEMP;
        SEND(fd_Sens, Msg, sizeof(union SIGNAL));
      break; 
      case SIGInitMeasTempOut:  // Initiate loop to read Temperature sensors
        Msg->SigNo = SIGReadSensorReq;
        Msg->SensorReq.Client_fd = fd_ToOwn;
        Msg->SensorReq.Sensor = OUT_TEMP;
        SEND(fd_Sens, Msg, sizeof(union SIGNAL));
      break;
			case SIGInitMeasADInt:  // Initiate loop to read AD sensors
        Msg->SigNo = SIGReadSensorReq;
        Msg->SensorReq.Client_fd = fd_ToOwn;
        Msg->SensorReq.Sensor = ADINT;
        SEND(fd_Sens, Msg, sizeof(union SIGNAL));
      break;
			case SIGInitMeasADExt:  // Initiate loop to read AD sensors
        Msg->SigNo = SIGReadSensorReq;
        Msg->SensorReq.Client_fd = fd_ToOwn;
        Msg->SensorReq.Sensor = ADEXT;
        SEND(fd_Sens, Msg, sizeof(union SIGNAL));
      break;

      case SIGReadSensorResp:
			  if (DbgTest == 1) {		
					printf(" SenRsp %d: %10.5f sec V1: %f V2: %f  Status: %s \r\n", 
								 Msg->SensorResp.Sensor, Msg->SensorResp.CmdTime,
								 Msg->SensorResp.Val[0], Msg->SensorResp.Val[1],
								 Msg->SensorResp.Status ? "OK" : "ERROR");
				}
        switch (Msg->SensorResp.Sensor) {				
					case OUT_TEMP: 
						ProcState.OutTemp = Msg->SensorResp.Val[0];
						if (Msg->SensorResp.Status) { // Valid data recieved
						  if ((ProcState.MinOutTemp == SENS_DEF_VAL) || (ProcState.OutTemp < ProcState.MinOutTemp))
							  ProcState.MinOutTemp   = Msg->SensorResp.Val[0];
						  if ((ProcState.MaxOutTemp == SENS_DEF_VAL) || (ProcState.OutTemp > ProcState.MaxOutTemp))
							  ProcState.MaxOutTemp   = Msg->SensorResp.Val[0];
						} // No valid data
					  REQ_TIMEOUT(fd_Timo, ProcState.fd.WR_MainPipe, "MainInitTOut", SIGInitMeasTempOut, 30 Sec);
            LCDDisplayUpdate(&ProcState);

					break;
					case BOX_TEMP: 
						ProcState.BoxTemp = Msg->SensorResp.Val[0];  
						if (Msg->SensorResp.Status) { // Valid data recieved
						  if ((ProcState.MinBoxTemp == SENS_DEF_VAL) || (ProcState.BoxTemp < ProcState.MinBoxTemp))
							  ProcState.MinBoxTemp   = Msg->SensorResp.Val[0];
						  if ((ProcState.MaxBoxTemp == SENS_DEF_VAL) || (ProcState.BoxTemp > ProcState.MaxBoxTemp))
							  ProcState.MaxBoxTemp   = Msg->SensorResp.Val[0];
						}
					  REQ_TIMEOUT(fd_Timo, ProcState.fd.WR_MainPipe, "MainInitTBox", SIGInitMeasTempBox, 15 Sec);
            LCDDisplayUpdate(&ProcState);

					break;
					case REFRIG_TEMP:
						ProcState.RefrigTemp = Msg->SensorResp.Val[0]; 
						if (Msg->SensorResp.Status) { // Valid data recieved
						  if ((ProcState.MinRefrigTemp == SENS_DEF_VAL) || (ProcState.RefrigTemp < ProcState.MinRefrigTemp))
							  ProcState.MinRefrigTemp   = Msg->SensorResp.Val[0];
						  if ((ProcState.MaxRefrigTemp == SENS_DEF_VAL) || (ProcState.RefrigTemp > ProcState.MaxRefrigTemp))
							  ProcState.MaxRefrigTemp   = Msg->SensorResp.Val[0];
						}
					  REQ_TIMEOUT(fd_Timo, ProcState.fd.WR_MainPipe, "MainInitTRefrig", SIGInitMeasTempRefrig, 20 Sec);
            LCDDisplayUpdate(&ProcState);

					break;
          case WATER_TEMP: 
            ProcState.WaterTemp = Msg->SensorResp.Val[0];
            if (Msg->SensorResp.Status) { // Valid data recieved
              if ((ProcState.MinWaterTemp == SENS_DEF_VAL) || (ProcState.WaterTemp < ProcState.MinWaterTemp))
                ProcState.MinWaterTemp   = Msg->SensorResp.Val[0];
              if ((ProcState.MaxWaterTemp == SENS_DEF_VAL) || (ProcState.WaterTemp > ProcState.MaxWaterTemp))
                ProcState.MaxWaterTemp   = Msg->SensorResp.Val[0];
            }
					  REQ_TIMEOUT(fd_Timo, ProcState.fd.WR_MainPipe, "MainInitTWater", SIGInitMeasTempWater, 20 Sec);
            LCDDisplayUpdate(&ProcState);
				
					break;	
          case HWATER_TEMP: 
            ProcState.HWTemp = Msg->SensorResp.Val[0];
            if (Msg->SensorResp.Status) { // Valid data recieved
              if ((ProcState.MinHWTemp == SENS_DEF_VAL) || (ProcState.HWTemp < ProcState.MinHWTemp))
                ProcState.MinHWTemp   = Msg->SensorResp.Val[0];
              if ((ProcState.MaxHWTemp == SENS_DEF_VAL) || (ProcState.HWTemp > ProcState.MaxHWTemp))
                ProcState.MaxHWTemp   = Msg->SensorResp.Val[0];
            }
					  REQ_TIMEOUT(fd_Timo, ProcState.fd.WR_MainPipe, "MainInitHW", SIGInitMeasTempHW, 20 Sec);
            LCDDisplayUpdate(&ProcState);

					break;
					case SEA_TEMP:    
  					ProcState.SeaTemp= Msg->SensorResp.Val[0];
						if (Msg->SensorResp.Status) { // Valid data recieved
						  if ((ProcState.MinSeaTemp == SENS_DEF_VAL) || (ProcState.SeaTemp < ProcState.MinSeaTemp))
							  ProcState.MinSeaTemp   = Msg->SensorResp.Val[0];
						  if ((ProcState.MaxSeaTemp == SENS_DEF_VAL) || (ProcState.SeaTemp > ProcState.MaxSeaTemp))
							  ProcState.MaxSeaTemp   = Msg->SensorResp.Val[0];
						} 
					  REQ_TIMEOUT(fd_Timo, ProcState.fd.WR_MainPipe, "MainInitTSea", SIGInitMeasTempSea, 40 Sec);
            LCDDisplayUpdate(&ProcState);
						
					break;
					case ADINT:    
/*						if (Msg->SensorResp.Status) { // Valid data recieved
							ProcState.BatVoltF    = 11* (Msg->SensorResp.Val[0]) + 0.65;  //Correction due to ...
//sprintf(InfoText, "BatF %f AD %6.3f\n", ProcState.BatVoltF, Msg->SensorResp.Val[0]);
//LOG_MSG(InfoText);
					  }		
					  REQ_TIMEOUT(fd_Timo, ProcState.fd.WR_MainPipe, "MainInitADInt", SIGInitMeasADInt, 5 Sec);
//          TIMER_START(TM1AD);
            LCDDisplayUpdate(&ProcState); 
*/				break;
					case ADEXT: 
						if (Msg->SensorResp.Status) { // Valid data recieved
							// Filter data to get better readings with less variation
							if (FQueue[0].ADWater == SENS_DEF_VAL) { // Not initiated yet, do it!
								for (Idx = 0; Idx < NO_OF_ELEM_IN_FILTERQUEU; Idx++) {
									FQueue[Idx].ADDiesel    = Msg->SensorResp.Val[0];  								
									FQueue[Idx].ADWater     = Msg->SensorResp.Val[1];
									FQueue[Idx].ADBatVoltF  = Msg->SensorResp.Val[2];							
								}	// for
							} // if
							// Move all data up 1 position in queue
							for (Idx = (NO_OF_ELEM_IN_FILTERQUEU - 1); Idx > 0; Idx--) {
							//	printf("Idx: %d\r\n", Idx);
								FQueue[Idx].ADDiesel    = FQueue[Idx - 1].ADDiesel;  								
								FQueue[Idx].ADWater     = FQueue[Idx - 1].ADWater;
								FQueue[Idx].ADBatVoltF  = FQueue[Idx - 1].ADBatVoltF;							
							}	// for
							FQueue[0].ADDiesel    = Msg->SensorResp.Val[0];								
							FQueue[0].ADWater     = Msg->SensorResp.Val[1];
							FQueue[0].ADBatVoltF  = Msg->SensorResp.Val[2];						
							Msg->SensorResp.Val[0] = 0; 							
							Msg->SensorResp.Val[1] = 0; 							
							Msg->SensorResp.Val[2] = 0;  							
							
							// Calculate mean value of all elem in filter queue
							for (Idx = 0; Idx < NO_OF_ELEM_IN_FILTERQUEU; Idx++) {
								//printf("ADD %d: %4f ", Idx, FQueue[Idx].ADDiesel);

								Msg->SensorResp.Val[0] += FQueue[Idx].ADDiesel; 							
								Msg->SensorResp.Val[1] += FQueue[Idx].ADWater; 							
								Msg->SensorResp.Val[2] += FQueue[Idx].ADBatVoltF; 		 					
							}	// for
							//printf("\r\n");
							Msg->SensorResp.Val[0] = Msg->SensorResp.Val[0]/(NO_OF_ELEM_IN_FILTERQUEU); 							
							Msg->SensorResp.Val[1] = Msg->SensorResp.Val[1]/(NO_OF_ELEM_IN_FILTERQUEU); 							
							Msg->SensorResp.Val[2] = Msg->SensorResp.Val[2]/(NO_OF_ELEM_IN_FILTERQUEU);							
//printf("Wavg: %4f WLatest: %4f \r\n", Msg->SensorResp.Val[1], FQueue[0].ADWater);
						
						  ProcState.BatVoltF      = (Msg->SensorResp.Val[2] + 0.5);  // Need to 0.5V due to measurement errors..?
			 			  ProcState.ADWaterLevel  = Msg->SensorResp.Val[1];
							ProcState.ADDieselLevel = Msg->SensorResp.Val[0]; 
              
							if ((ProcState.BatVoltF < 10) || (ProcState.BatVoltF > 15)) // Check if reasonable Voltage
								ProcState.BatVoltF = 13; // Set default value
								
									
							// Compensate for battery voltage 
							ProcState.ADWaterLevel  = ProcState.ADWaterLevel * 13 / ProcState.BatVoltF; 
							ProcState.ADDieselLevel = ProcState.ADDieselLevel * 13 / ProcState.BatVoltF;
					    if (DebugOn) // Print adjusted AD reading
								printf(">>>>Converted AD reading[BF %4.2f DL: %4.2f WL: %4.2f BS: %4.2f]\r\n", ProcState.BatVoltF, Msg->SensorResp.Val[0], Msg->SensorResp.Val[1], Msg->SensorResp.Val[2]);									


							// Calculate water and diesel levels
							ProcState.WaterLevel   =  GetWaterLevel(ProcState.ADWaterLevel);
							ProcState.DieselLevel  =  GetDieselLevel(ProcState.ADDieselLevel);
						}   
						if ((ProcState.ModeState == Water) || (ProcState.ModeState == Diesel) ) { // Fast update. Always send to Byteport at same pace!
  						Msg->SigNo = SIGByteportReport;
              sprintf(Msg->ByteportReport.Str, "WaterLevel=%-.1f", ProcState.WaterLevel);			
              if (DebugOn == 1) { LOG_MSG(Msg->ByteportReport.Str); }  
              SEND(fd_BytePRep, Msg, sizeof(union SIGNAL));
 
					     REQ_TIMEOUT(fd_Timo, ProcState.fd.WR_MainPipe, "MainInitADExtFst", SIGInitMeasADExt, 1 Sec);
							// if (DebugOn) printf("Fast update M:%d \r\n",ProcState.ModeState);
						} else { // Slow update
					     REQ_TIMEOUT(fd_Timo, ProcState.fd.WR_MainPipe, "MainInitADExtSlw", SIGInitMeasADExt, 12 Sec);
							// if (DebugOn) printf("Slow update M:%d \r\n",ProcState.ModeState );
					  }
//sprintf(InfoText, "BatF  %7.3f AD: %7.3f \n", ProcState.BatVoltS, Msg->SensorResp.Val[3]);
//LOG_MSG(InfoText);
						// Write to file for Byteport reporting. create file if not opened yet
						//if (ProcState.fd.WaterLevel == 0)  // No file descriptor defined
						
            LCDDisplayUpdate(&ProcState);
					break; // ADExt
					default: 
						CHECK(FALSE, "Undefined sensor...\n");
					break;  	
				}

       LCDDisplayUpdate(&ProcState);
/*     // Removed 20161227. Now we use Byteport cmd reporting by Axel!
			 // Write to file for Byteport reporting. Create file if not opened yet
			 if (DbgTest == 1) {printf("Enter send to Byteport\r\n");usleep(200000);}
			 sprintf(FilePath, "/tmp/ByteportReports/BatVoltF");  // Set filename
			 if((ProcState.fd.BatVoltF = fopen(FilePath, "w+")) == NULL)  {  // Check that file exists
					sprintf(InfoText, "ERROR: %s %d Can not open file %s \n", strerror(errno), errno, FilePath);
					CHECK(FALSE, InfoText);
				} else {		
				  sprintf(ByteportText, "%-.1f", ProcState.BatVoltF);
				  fprintf(ProcState.fd.BatVoltF, ByteportText);
				  fclose(ProcState.fd.BatVoltF);
				}	
				
  		 if (DbgTest == 1) {printf("BatVoltF written\r\n");usleep(200000);}						
			 sprintf(FilePath, "/tmp/ByteportReports/DieselLevel");  // Set filename
			 if((ProcState.fd.DieselLevel = fopen(FilePath, "w+")) == NULL)  {  // Check that file exists
					sprintf(InfoText, "ERROR: %s %d Can not open file %s \n", strerror(errno), errno, FilePath);
					CHECK(FALSE, InfoText);
				} else {					
			  	sprintf(ByteportText, "%-.1f", ProcState.DieselLevel);
			  	fprintf(ProcState.fd.DieselLevel, ByteportText);
				  fclose(ProcState.fd.DieselLevel);
				}
				
				if (DbgTest == 1) {printf("DieselLevel written\r\n");usleep(200000);}						
		    sprintf(FilePath, "/tmp/ByteportReports/WaterLevel");  // Set filename
			  if((ProcState.fd.WaterLevel = fopen(FilePath, "w+")) == NULL)  {  // Check that file exists
					sprintf(InfoText, "ERROR: %s %d Can not open file %s \n", strerror(errno), errno, FilePath);
					CHECK(FALSE, InfoText);
				} else {							
		  		sprintf(ByteportText, "%-.1f", ProcState.WaterLevel);
			  	fprintf(ProcState.fd.WaterLevel, ByteportText);
			  	fclose(ProcState.fd.WaterLevel);
				}
        
				if (DbgTest == 1) {printf("Leaving send to Byteport\r\n");usleep(200000);}		 
        if (Msg->SensorResp.Sensor == WATER_TEMP) {// Just to secure only 1 line when no display present
          LCDDisplayUpdate(&ProcState);
        }  
*/

      break;

// Turn on backlight on Display when a button is pushed.
      case SIGOpButOn:
        if  (DbgTest == 1) {printf("3: %d\r\n", Msg->SigNo);usleep(200000);}
				if (!ProcState.fd.lcd) {  // First check that we have a LCD attached
					if (ProcState.LCDBlkOnTimer <= 0) { // If Display OFF, Set timer and turn on Display-nothing else
						ProcState.LCDBlkOnTimer  = LCDBlkOnTimerVal; // Time before turning backlight off
						Set1WLCDBlkOn(LCD1);  // Turn on backlight on display
					} else { // Execute button pressed
						OpButPressed(&ProcState);
						LCDDisplayUpdate(&ProcState);
					}
				}
	    break;
      case SIGLftButOn:
	//printf("Left button presssed Msg: %s\n");
	 			ProcState.LCDBlkOnTimer  = LCDBlkOnTimerVal; // Time before turning backlight off
				Set1WLCDBlkOn(LCD1);  // Turn on backlight on display
        RghtButPressed(&ProcState);
//        LftButPressed(&ProcState); // Due to problems reading Left/Right. Step always Right!!!
        LCDDisplayUpdate(&ProcState);
      break;
      case SIGRghtButOn:
	//printf("Right button presssed \n");
				ProcState.LCDBlkOnTimer  = LCDBlkOnTimerVal; // Time before turning backlight off
				Set1WLCDBlkOn(LCD1);  // Turn on backlight on display
        RghtButPressed(&ProcState);
        LCDDisplayUpdate(&ProcState);
      break;
			
      case SIGServCmdReq: 
				switch (Msg->ServCmdReq.Cmd) {				
					case Dwn:
						printf("Main: Anchor Down \r\n");
					break;
					case SlwUp:		
						printf("Main: Anchor Slow Up \r\n");
					break;
					case Up:
						printf("Main: Anchor Up \r\n");
					break;
					case AnchStop:	
						printf("Main: Anchor STOP \r\n");
					break;
					case SetTime:
						printf("Main: Set Time \r\n");
					break;
					default:
						sprintf(InfoText, "Illegal server cmd received: %d\n", Msg->ServCmdReq.Cmd);
						CHECK(FALSE, InfoText);
					break; 
				}	// End switch		
			break;
				
      default:
        sprintf(InfoText, "Illegal signal received: %d MsgLen: %d Data: %x %x %x %x\n", Msg->SigNo, sizeof(Msg), Msg->Data[0], Msg->Data[1],Msg->Data[2],Msg->Data[3]);
        CHECK(FALSE, InfoText);
      break;
    } // Switch
	}  // While
}
float  GetWaterLevel(float Level) {
  unsigned char         NotFound, Idx;
  float                 K, m, Amount;

  NotFound = TRUE;
 // CHECK(Level > 0, StrErr_c) // " Error, too low AD value\r\n");
  for ( Idx = 0; NotFound; Idx++) {
    if ( Level > Lvl2Water[Idx].Level) {
//if (DebugOn) printf("Water***: Idx: %d Searched Level: %5.2f TblLevel: %5.2f Water: %5.2f \r\n", Idx, Level, Lvl2Water[Idx].Level, Lvl2Water[Idx].Amount);
      //-----------------2005-02-26 13:01-----------------
      //  Just loop until we pass the searched level
      //  We start at lowest value and search until we pass
      //  Then the searched value in between Idx and Idx-1
      //  --------------------------------------------------
    } else {
//		   if (DebugOn) printf("Water***: Last Idx: %d Searched Level: %5.2f TblLevel: %5.2f Water: %5.2f \r\n", Idx, Level, Lvl2Water[Idx].Level, Lvl2Water[Idx].Amount);
       NotFound = FALSE;
    }  // if else 
  }    // for loop 
  Idx--;      // Decrement counter, otherwise for loop leaves with +1 
  K = (Lvl2Water[Idx].Amount - Lvl2Water[Idx-1].Amount) / (Lvl2Water[Idx].Level - Lvl2Water[Idx-1].Level);  // Slope.. 
  m =  Lvl2Water[Idx].Amount - K * Lvl2Water[Idx].Level;
  Amount  =  K * Level + m;                         // Y = K*X + m 
/*  if ( DebugOn) {                   // Use this to calibrate when water is available...
    sprintf(InfoText, "Idx: %d PrevList Lvl: %f Water: %u \r\n CurrList Lvl: %f Water: %f \r\n Level:        %d Water: %d \r\n", Idx, Lvl2Water[Idx-1].Level, Lvl2Water[Idx-1].Amount, Lvl2Water[Idx].Level, Lvl2Water[Idx].Amount, Level, (short) Amount);
    LOG_MSG(InfoText);  
  }*/
  return Amount;
}
float  GetDieselLevel(float Level) {
  unsigned char         NotFound, Idx;
  float                 K, m, Amount;

  NotFound = TRUE;
 // CHECK(Level > 0, StrErr_c) // " Error, too low AD value\r\n");
  for ( Idx = 0; NotFound; Idx++) {
    if ( Level > Lvl2Diesel[Idx].Level) {
      //-----------------2005-02-26 13:01-----------------
      //  Just loop until we pass the searched level
      //  We start at lowest value and search until we pass
      //  Then the searched value in between Idx and Idx-1
      //  --------------------------------------------------
    } else {
       NotFound = FALSE;
    }  // if else 
  }    // for loop 
  Idx--;      // Decrement counter, otherwise for loop leaves with +1 
  K = (Lvl2Diesel[Idx].Amount - Lvl2Diesel[Idx-1].Amount) / (Lvl2Diesel[Idx].Level - Lvl2Diesel[Idx-1].Level);  // Slope.. 
  m =  Lvl2Diesel[Idx].Amount - K * Lvl2Diesel[Idx].Level;
  Amount  =  K * Level + m;                         // Y = K*X + m 
 /* if (DebugOn) {                   // Use this to calibrate when water is available...
    sprintf(InfoText, "Idx: %d PrevList Lvl: %f Diesel: %f \r\n CurrList Lvl: %f Diesel: %f \r\n Level:        %d Diesel: %d \r\n", Idx, Lvl2Diesel[Idx-1].Level, Lvl2Diesel[Idx-1].Amount, Lvl2Diesel[Idx].Level, Lvl2Diesel[Idx].Amount, Level, (short) Amount);
    LOG_MSG(InfoText);  
  }*/
  return Amount;
}
void   LCDDisplayUpdate(struct ProcState_s *PState) {
	/* 20101129 Note, must fill the text string (20 chars with this display)
	otherwise dummy chars will destroy presentation
	*/
	

	short   Resolution;
  char    Indicator;

//TIMER_START(TMLCD1);
memset(LCDText, ' ', 80); // Clear display buffer
//TIMER_STOP("LCD memset", TMLCD2, TMLCD1);
if  (DbgTest == 1) {printf("DispRoutine entered \r\n");usleep(200000);}  
	switch(PState->ModeState) {
		case MainMode:          //   "12345678911234567892"
		  if  (DbgTest == 1) {printf("MainMode \r\n");usleep(200000);}  
      if (PState->OutTemp == SENS_DEF_VAL)
        sprintf(&LCDText[Line1], "Temperatur     -.-     ");
      else
        sprintf(&LCDText[Line1], "Temperatur    %+5.1f    ", PState->OutTemp);
      if (PState->RefrigTemp == SENS_DEF_VAL)
        sprintf(&LCDText[Line2], " Kyl           --.-    ");
      else
        sprintf(&LCDText[Line2], " Kyl          %+5.1f   ", PState->RefrigTemp);
      if (PState->BoxTemp == SENS_DEF_VAL) 
        sprintf(&LCDText[Line3], " Box           --.-    ");
      else        
        sprintf(&LCDText[Line3], " Box          %+5.1f   ", PState->BoxTemp);
      if (PState->WaterLevel == SENS_DEF_VAL)
        sprintf(&LCDText[Line4], "Vatten      -- [180]   ");
      else if (PState->WaterLevel <= MAX_WATER_LEVEL)
        sprintf(&LCDText[Line4], "Vatten    %3.0f [180]  ", PState->WaterLevel);
      else
        sprintf(&LCDText[Line4], "Vatten         > Max   ");
   break;

    case Temperature:       //   "12345678911234567892"
      if (PState->OutTemp == SENS_DEF_VAL)
        sprintf(&LCDText[Line1], "Temperatur    --.-       ");
      else
        sprintf(&LCDText[Line1], "Temperatur   %+5.1f       ", PState->OutTemp);
      if (PState->SeaTemp == SENS_DEF_VAL)
        sprintf(&LCDText[Line2], " Hav          --.-     ");
      else        
        sprintf(&LCDText[Line2], " Hav        %+5.1f     ", PState->SeaTemp);
      if (PState->HWTemp == SENS_DEF_VAL)
        sprintf(&LCDText[Line3], " Varme        --.-     ");
      else
        sprintf(&LCDText[Line3], " Varme       %+5.1f   ", PState->HWTemp);
      if (PState->WaterTemp == SENS_DEF_VAL)
        sprintf(&LCDText[Line4], " Vatten       --.-    ");
      else        
        sprintf(&LCDText[Line4], " Vatten      %+5.1f   ", PState->WaterTemp);
    break;

    case MinMaxTemp:        //   "12345678911234567892"
      sprintf(&LCDText[Line1],   "Temp    Min     Max  ");
			if (PState->OutTemp == SENS_DEF_VAL)  // No value received...
        sprintf(&LCDText[Line2], " Ute    --.-    --.-    ");
      else
        sprintf(&LCDText[Line2], " Ute   %+5.1f   %+5.1f  ", PState->MinOutTemp, PState->MaxOutTemp);
      if (PState->RefrigTemp == SENS_DEF_VAL)
        sprintf(&LCDText[Line3], " Varme  --.-    --.-     ");
      else
        sprintf(&LCDText[Line3], " Varme %+5.1f   %+5.1f  ", PState->MinHWTemp, PState->MaxHWTemp);
      if (PState->SeaTemp == SENS_DEF_VAL)
        sprintf(&LCDText[Line4], " Vatten --.-    --.-      ");
      else        
        sprintf(&LCDText[Line4], " Vatten%+5.1f   %+5.1f   ", PState->MinWaterTemp, PState->MaxWaterTemp);
			break;

    case Water:             //    "12345678911234567892"
//printf("1 W-Level: %6.void3f \r\n", PState->WaterLevel);
// First write temperature
      if (PState->WaterTemp == SENS_DEF_VAL) {
        sprintf(&LCDText[Line1],  "Vatten        --.-   ");
//printf("Water received\n");
			}
      else      
        sprintf(&LCDText[Line1],  "Vatten      %+5.1f      ", PState->WaterTemp);
// Then write water volume
      if (PState->WaterLevel == SENS_DEF_VAL) {
        sprintf(&LCDText[Line2],  "  --.-  [180] Liter     ");
// Clear screen
        sprintf(&LCDText[Line3],  "                    ");// Clear screen
        sprintf(&LCDText[Line4],  "                    ");// Clear screen  
      }
      else if (PState->WaterLevel <= MAX_WATER_LEVEL) {  
        sprintf(&LCDText[Line2],  "  %-3.0f  [180] Liter    ", PState->WaterLevel);
				Resolution = MAX_WATER_LEVEL/20;  // Max water in liters / nr of chars on display line
// Clear screen
        sprintf(&LCDText[Line3],  "                    ");// Clear screen
        sprintf(&LCDText[Line4],  "                    ");// Clear screen  
        if (DebugOn) {
          BuildBarText(&LCDText[Line3], PState->WaterLevel, Resolution);  
					sprintf(&LCDText[Line4]," AD : %6.3f  [V]       ", PState->ADWaterLevel);
        } else {
          BuildBarText(&LCDText[Line3], PState->WaterLevel, Resolution);  
          BuildBarText(&LCDText[Line4], PState->WaterLevel, Resolution);  
        }
      } else {
          sprintf(&LCDText[Line2],"Error: Water > MAX      ");
          sprintf(&LCDText[Line3],"  %-3.0f  [180] Liter   ", PState->WaterLevel);
					sprintf(&LCDText[Line4]," AD : %6.3f  [V]        ", PState->ADWaterLevel);

//printf("W-Level: %f \r\n", PState->WaterLevel);
//printf("AD-Level: %f \r\n", PState->ADWaterLevel);
      }
    break;

    case Diesel:             //   "12345678911234567892"
      sprintf(&LCDText[Line1],    "Diesel               ");
// Then write diesel volume
      if (PState->DieselLevel == SENS_DEF_VAL) {
         sprintf(&LCDText[Line2], "  --.-  [280] Liter  ");
// Clear screen
        sprintf(&LCDText[Line3],  "                     ");// Clear screen
        sprintf(&LCDText[Line4],  "                     ");// Clear screen  
      }
      else if (PState->DieselLevel <= MAX_DIESEL_LEVEL) {  
        sprintf(&LCDText[Line2],  "  %-3.0f  [280] Liter     ", PState->DieselLevel);
			  Resolution = MAX_DIESEL_LEVEL/20;  // Max diesel in liters / nr of chars on display line
// Clear screen
        sprintf(&LCDText[Line3],  "                     ");// Clear screen
        sprintf(&LCDText[Line4],  "                     ");// Clear screen 
        if (DebugOn) {
          BuildBarText(&LCDText[Line3], PState->DieselLevel, Resolution);
					sprintf(&LCDText[Line4]," AD : %6.3f  [V]        ", PState->ADDieselLevel);
        } else {
          BuildBarText(&LCDText[Line3], PState->DieselLevel, Resolution);
          BuildBarText(&LCDText[Line4], PState->DieselLevel, Resolution);
        }
      } else {
          sprintf(&LCDText[Line2],"Error: Diesel > MAX    ");
          sprintf(&LCDText[Line3],"  %-3.0f  [280] Liter   ", PState->DieselLevel);
					sprintf(&LCDText[Line4]," AD : %6.3f  [V]        ", PState->ADDieselLevel);
      }
    break;
 
    case SysInfo:
      if (DebugOn) { //          "12345678911234567892"
        sprintf(&LCDText[Line1], "SysInfo     Dbg ON  ");
			} else {
        sprintf(&LCDText[Line1], "SysInfo     Dbg OFF ");
			}
			
/*		Removing this, not needed anymore! 20150627	
      if (PState->BatVoltS == SENS_DEF_VAL)
        sprintf(&LCDText[Line2], " Str  --.-- V        ");
      else
        sprintf(&LCDText[Line2], " Str %5.1f V         ", PState->BatVoltS);
*/
      if (PState->BatVoltF == SENS_DEF_VAL)
        sprintf(&LCDText[Line2], " Fbr  --.-- V        ");
      else 
        sprintf(&LCDText[Line2], " Fbr %5.1f V         ", PState->BatVoltF);
      sprintf(&LCDText[Line4], "                        ");
 
			break;

    default:
      sprintf(InfoText, "Undefined state %d\n          ", PState);
      CHECK(FALSE, InfoText);
    break;
  }  // End of switch 
  
// Show that process is running
	if (ProcRunning == '*') {
	  LCDText[19] = '-';
      ProcRunning = '-'; 
    } else {
	  LCDText[19] = '*';
	  ProcRunning = '*';  
	}  
  
#ifdef OWLCD_PRESENT
if (PState->DevLCDDefined) {
	LCD1W_WRITE(LCD1, 1, &LCDText[Line1]);
	LCD1W_WRITE(LCD1, 2, &LCDText[Line2]);
	LCD1W_WRITE(LCD1, 3, &LCDText[Line3]);
	LCD1W_WRITE(LCD1, 4, &LCDText[Line4]);
}
#endif 
 
#ifdef LCD_PRESENT
  //TIMER_START(TMLCD1);

// Fix display problem in R8 system, line 2 & 3 switched, switch back!!!
  char Temp[20];
  memcpy(&Temp, &LCDText[Line2], 20);
  memcpy(&LCDText[Line2], &LCDText[Line3], 20);
  memcpy(&LCDText[Line3], &Temp, 20);
	
//{TIMER_STOP("LCD memcpy", TMLCD2, TMLCD1);}
//sleep(5);
 // LCD_WRITE(PState->fd.lcd, 1, 1, LCDText);	
 int i;
/*
 #ifdef RPI_DEFINED
 lcdPosition (PState->fd.lcd, 0, 0);
 for (i = 0; i < 80; i++) 
   lcdPutchar (PState->fd.lcd, LCDText[i]) ;
#elif BB_DEFINED

#endif
*/
if (ret < 0) printf("LCD Write 2: %d bytes\r\n", ret);

//	if (DebugOn) {
//		printf("%s\n", LCDText); // Print on screen also
//	}
	
#else
  //printf(" %s \n", LCDText);
#endif
	if  (DbgTest == 1) {printf("DispRoutine leaving \r\n");usleep(200000);}  
}  // End of function LCDDisplayUpdate 
void   OpButPressed(struct ProcState_s *PState)    {

  switch(PState->ModeState) {
    case Temperature:
      PState->ModeState = MinMaxTemp;
    break;

    case MinMaxTemp:         // Clear Min & Max vaules
      PState->MinWaterTemp     = PState->WaterTemp;
      PState->MaxWaterTemp     = PState->WaterTemp;
      PState->MinHWTemp        = PState->HWTemp;
      PState->MaxHWTemp        = PState->HWTemp;
      PState->MinRefrigTemp    = PState->RefrigTemp;
      PState->MaxRefrigTemp    = PState->RefrigTemp;
      PState->MinWaterTemp     = PState->WaterTemp;
      PState->MaxWaterTemp     = PState->WaterTemp;
      PState->MinSeaTemp       = PState->SeaTemp;
      PState->MaxSeaTemp       = PState->SeaTemp;
      PState->MinOutTemp       = PState->OutTemp;
      PState->MaxOutTemp       = PState->OutTemp;
      PState->MinBoxTemp       = PState->BoxTemp;
      PState->MaxBoxTemp       = PState->BoxTemp;
    break;

    case SysInfo:
       if (DebugOn == TRUE) {
         DebugOn = FALSE;
         LOG_MSG("Debug OFF\n");
       }
       else {
         DebugOn = TRUE;
         LOG_MSG("Debug ON\n");
       }
    break;

    default: // Test, reset LCD display

    break;
  }
} // OP button pressed 
void   LftButPressed(struct ProcState_s *PState)    {

  switch (PState->ModeState) {
    case SysInfo    : PState->ModeState = Diesel;      break;
    case Diesel     : PState->ModeState = Water;       break;
    case Water      : PState->ModeState = Temperature; break;
    case Temperature: PState->ModeState = MainMode;    break;
    case MainMode   : PState->ModeState = SysInfo;		 break;
    case MinMaxTemp : PState->ModeState = Temperature; break;
 //   case Water      : PState->ModeState = Battery;     break;
    default:
      sprintf(InfoText, "Undefined state %d\n", PState);
      CHECK(FALSE, InfoText);
    break;
  } // switch
}  // MiButPressed 
void   RghtButPressed(struct ProcState_s *PState)     {

  switch (PState->ModeState) {
    case MainMode   : PState->ModeState = Temperature; break;
    case Temperature: PState->ModeState = Water;	     break;
    case Water      : PState->ModeState = Diesel;      break;
    case Diesel     : PState->ModeState = SysInfo;     break;
    case SysInfo    : PState->ModeState = MainMode;		 break;
    case MinMaxTemp : PState->ModeState = Temperature; break;
//    case Battery    : PState->ModeState = Water;       break;
    default:
      sprintf(InfoText, "Undefined state %d\n", PState);
      CHECK(FALSE, InfoText);
    break;
  } // switch 
} // PlButPressed
void   BuildBarText(char * Str, float Level, float Resolution)    {
  char Idx, ScreenPos;

  ScreenPos = Level / Resolution;   
 /* for (Idx = 0; Idx < ScreenPos; Idx++) {
   // Str[Idx] = 0xBC;
    Str[Idx] = 'x';
  }
*/ 
 if (ScreenPos <= 20) 
  // memset(Str, 0xBC, ScreenPos);
   memset(Str, 0x3E, ScreenPos);
 else
   printf("Severe error...\r\n"); 
}  // BuildBarText

void   InitProc(struct ProcState_s *PState) {
  
  int ret, rc;
  enum ProcTypes_e    ProcessorType=NONE; // Set default value and then check
  
#ifdef BF537_DEFINED
  ProcessorType = BF537;
  LOG_MSG("BF537 defined\n");
#elif RPI_DEFINED
  ProcessorType = RPI;
  LOG_MSG("RaspberryPi defined\n");
#elif BB_DEFINED
  ProcessorType = BB;
  LOG_MSG("BeagleBone defined\n");
#elif HOST_DEFINED
  ProcessorType = HOSTENV;
  LOG_MSG("HOST defined\n");
#else
  sprintf(InfoText, "Unknown processor type defined: %d \n", ProcessorType);
  CHECK(FALSE, InfoText);
#endif  

  PState->ProcType = ProcessorType;
  
  remove(KBD_PIPE);
  remove(ONEWIRE_PIPE);	
  remove(MAIN_PIPE);
  remove(TIMO_PIPE);
  remove(BYTEPHNDL_PIPE);
  umask(0);
  //mknod("tmp/ByteportReports/",  S_IFDIR|0666, 0); 
	ret = mknod(KBD_PIPE,  S_IFIFO|0666, 0);   
  if (ret < 0) {
    sprintf(InfoText, "Error: %s, %d, %s \r\n", KBD_PIPE, errno, strerror(errno));
    LOG_MSG(InfoText);     
  }
  ret = mknod(ONEWIRE_PIPE, S_IFIFO|0666, 0); 
    if (ret < 0) {
    sprintf(InfoText, "Error: %s, %d, %s \r\n", ONEWIRE_PIPE, errno, strerror(errno));
    LOG_MSG(InfoText);     
  }
  ret = mknod(BYTEPHNDL_PIPE, S_IFIFO|0666, 0); 
    if (ret < 0) {
    sprintf(InfoText, "Error: %s, %d, %s \r\n", BYTEPHNDL_PIPE, errno, strerror(errno));
    LOG_MSG(InfoText);     
  }
  ret = mknod(MAIN_PIPE, S_IFIFO|0666, 0);   
  if (ret < 0) {
    sprintf(InfoText, "Error: %s, %d, %s \r\n", MAIN_PIPE, errno, strerror(errno));
    LOG_MSG(InfoText);     
  }
  ret = mknod(TIMO_PIPE, S_IFIFO|0666, 0); 
    if (ret < 0) {
    sprintf(InfoText, "Error: %s, %d, %s \r\n", TIMO_PIPE, errno, strerror(errno));
    LOG_MSG(InfoText);     
  }
  // Initiate all pipes for communication
  OPEN_PIPE(PState->fd.RD_BPRepPipe, BYTEPHNDL_PIPE, O_RDONLY|O_NONBLOCK);
  OPEN_PIPE(PState->fd.WR_BPRepPipe, BYTEPHNDL_PIPE, O_WRONLY);
  
  OPEN_PIPE(PState->fd.RD_TimoPipe, TIMO_PIPE, O_RDONLY|O_NONBLOCK);
  OPEN_PIPE(PState->fd.WR_TimoPipe, TIMO_PIPE, O_WRONLY);

  OPEN_PIPE(PState->fd.RD_OWPipe, ONEWIRE_PIPE, O_RDONLY|O_NONBLOCK);  
  OPEN_PIPE(PState->fd.WR_OWPipe, ONEWIRE_PIPE, O_WRONLY);  

  OPEN_PIPE(PState->fd.RD_MainPipe, MAIN_PIPE, O_RDONLY|O_NONBLOCK);
  OPEN_PIPE(PState->fd.WR_MainPipe, MAIN_PIPE, O_WRONLY); 
  
//  sprintf(InfoText, " WR_Main %d RD_Main %d WR Timo % d RD_Timo %d \r\n",
//    PState->fd.WR_MainPipe, PState->fd.RD_MainPipe, PState->fd.WR_TimoPipe, PState->fd.RD_TimoPipe );
//  LOG_MSG(InfoText);
 //  LOG_MSG("Pipes initiated\n\r"); 

  
  // Define which processes to use, several options! You need to include correct file also!

  ret= pthread_create( &PState->Thread.Timeout,  NULL, (void *) TimeoutHandler,  (void *) PState);
  if (ret != 0)  printf("%s %d %s open error %s\n", __FILE__, __LINE__, "Timout thread", strerror(errno)); 
  errno = 0;
//  sprintf(InfoText, "Initiated process Timeouthandler: %x..\n\r", &PState->Thread.Timeout);
//  LOG_MSG(InfoText); sleep(2);

 // Not used as LCD buttons works well 20160212 
 // ret = pthread_create( &PState->Thread.Button,      NULL, (void *) RdButton, (void *) ProcessorType);
 // if (ret != 0) printf("%s %d %s open error %s\n", __FILE__, __LINE__, "Button thread", strerror(errno)); 
 // errno = 0;
 
 // Not used, LCD buttons used instead-perhaps useful when debugging! 
 // ret = pthread_create( &PState->Thread.Kbd,      NULL, (void *) RdKeyboard, (void *) ProcessorType);
 // if (ret != 0) printf("%s %d %s open error %s\n", __FILE__, __LINE__, "Kbd thread", strerror(errno)); 
 // errno = 0;

  ret = pthread_create( &PState->Thread.OneWire,  NULL, (void *) OneWireHandler,  (void *) PState);
  if (ret != 0) printf("%s %d %s open error %s\n", __FILE__, __LINE__, "OneWire thread", strerror(errno)); 
  errno = 0;
//  sprintf(InfoText, "Initiated process: Onewire: %x..\n\r", &PState->Thread.OneWire); 
//  LOG_MSG(InfoText); sleep(8);

  ret = pthread_create( &PState->Thread.WDog,     NULL, (void *) Watchdog,        (void *) PState);
  if (ret != 0)  printf("%s %d %s open error %s\n", __FILE__, __LINE__, "Watchdog thread", strerror(errno)); 
  errno = 0;
//  sprintf(InfoText, "Initiated process: Watchdog: %x..\n\r", &PState->Thread.WDog);
//  LOG_MSG(InfoText); sleep(8);

  //Not used, sockets not used right now. Add if needed
	//ret = pthread_create( &PState->Thread.SockServ,     NULL, (void *) SockServer,        (void *) ProcessorType);
  //if (ret != 0)  printf("%s %d %s open error %s\n", __FILE__, __LINE__, "Socket server", strerror(errno)); 
  //errno = 0;

  ret = pthread_create( &PState->Thread.LCDKbd,      NULL, (void *) RdLCDButtons, (void *) PState);
  if (ret != 0) printf("%s %d %s open error %s\n", __FILE__, __LINE__, "LCD Buttons thread", strerror(errno)); 
  errno = 0;
  //LOG_MSG("Initiate process: LCD & Kbd..\n\r"); sleep(8);

  
  ret = pthread_create( &PState->Thread.ByteportHandler,      NULL, (void *) BPHandler, (void *) PState);
  if (ret != 0) printf("%s %d %s open error %s\n", __FILE__, __LINE__, "Byteport handler thread", strerror(errno)); 
  errno = 0;
  //LOG_MSG("Initiate process: Byteporthandler..\n\r"); 
 
  //sleep(8); // Wait until all threads are ready, i.e have opened all resources
 
  
}
void   SignalCallbackHandler(int SigNum) { // Handle signals (normally errorhandling) from operating system
  void *array[10];
  size_t size;
  char **strings;
  size_t i;

  // Log backtrace of execution to easier find the error
  size = backtrace (array, 10);
  strings = (void *) backtrace_symbols (array, size);

  printf ("Obtained %zd stack frames.\n", size);

  for (i = 0; i < size; i++)
     printf ("%s\n", strings[i]);

  free (strings);

 printf("ERROR: Caught signal %d \r\n", SigNum);

 

 exit(SigNum);
}


void   QuitProc(void) { 

 printf("Ctrl-c received\n");
#ifdef LCD_PRESENT
 close(ProcState.fd.lcd);
#endif 
// close(ProcState.fd.kbdKnob);

 close(ProcState.fd.WR_kbdButPipe);
 close(ProcState.fd.WR_TimoPipe);
 close(ProcState.fd.WR_OWPipe);
 close(ProcState.fd.WR_MainPipe);
 close(ProcState.fd.RD_MainPipe);
 //pthread_cancel(ProcState.Thread.Timeout);
 //pthread_cancel(ProcState.Thread.Kbd);
 //pthread_cancel(ProcState.Thread.OneWire);
 pthread_exit((void *) ProcState.Thread.WDog);
 remove(KBD_PIPE);
 remove(ONEWIRE_PIPE);	
 remove(MAIN_PIPE);
 remove(TIMO_PIPE);
 sleep(2);
 printf("Main Exit\n");

 exit(0);
}
