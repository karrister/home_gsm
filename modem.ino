

#include <LiquidCrystal.h>

#define FALSE                          0
#define TRUE                           1
#define PROBABLY_TRUE                  2

#define ERROR                          -1
#define OK                             1
 
#define LED_PIN_NUM                    13
#define GSM_ENABLE                     2
#define LM35_PIN                                 A0    // select the input pin for the potentiometer
#define RESOLUTION_MVOLTS                        4.9
#define LM35_CELCIUS_RESOLUTION_MVOLTS           10

#define SCROLL_SPEED                             800
#define LCD_FLICKER_DELAY                        250
#define MAX_OK_WAIT_TIME_SECS                    25       
#define STATUS_CHECK_FREQ_SECS                   2
#define EVENTS_ON_SCREEN_TIMEOUT_SECS            8
#define TIME_WHEN_POLLING_USED_PB_ENTRIES        20

/*Max and Min ASCII characters, to not show any bullshit*/
#define ASCII_TABLE_MIN                32
#define ASCII_TABLE_MAX                126

/*These two are used at the end of a line/command */
#define ASCII_CR                       13
#define ASCII_LF                       10
#define EOL_PUTTY                      ASCII_CR
#define EOL_SAGEM                      ASCII_LF
#define MAX_DELAY_FOR_LF               500 //milliseconds

#define DATE_SEPARATOR_CHAR            0x2E

#define UART_BUFF_LENGTH               128
#define EVENTS_BUFF_LENGTH             32 //probably too small
#define SMS_PHONE_NUM_MAX_LEN          16            
#define SMS_TIME_MAX_LEN               5
#define SMS_DATE_MAX_LEN               8
#define SMS_LENGTH                     160
#define PRINT_BUFF_LEN                 32
#define MAX_AT_CMD_LEN                 64

/*AT command strings*/
#define GET_OPERATOR_NAME              "AT+COPS?"
#define ENABLE_CRING                   "AT+CRC=1"
#define ENABLE_CLIP                    "AT+CLIP=1"
#define READ_SMS                       "AT+CMGR=" //leave the offset open to be decided when the event gets to us
#define SEND_SMS                       "AT+CMGS=" 
#define GET_PB_ENTRY                   "AT+CPBR="
#define GET_PB_ENTRY_COUNT             "AT+CPBS?"

/*AT command defines for SW internal communication*/
#define AT_AT                          1
#define CMGL                           31
#define CRING                          32
#define CLIP                           33
#define CMTI_SMS_EVENT                 34
#define CMGR                           35
#define CMGR_USER_CMD_RECV             36
#define CPBR                           37
#define CPBS                           38
#define AT_OK                          50
#define NO_CARRIER                     51
#define USER_CMD_BITMASK               0x40
#define USER_CMD_GET_TEMPERATURE       (USER_CMD_BITMASK | 0x01)
#define USER_CMD_LIST_UNAUTH_CALLS     (USER_CMD_BITMASK | 0x02)

char inByte = 0;         // incoming serial byte
char text[] = "256"; //for debug

char uartbuff[UART_BUFF_LENGTH];
short ubuff_offset = 0;

char eventsbuff[EVENTS_BUFF_LENGTH];

//This is for the confusion that I am not sure if the module will always end communication
//with both CR and LF. Thus we wait for some time after CR, and then just plain declare the
//current communication over and ready for parsing.
int linefeed_delay_millis = 0;

typedef struct {
    char number[SMS_PHONE_NUM_MAX_LEN+1];
    char time[SMS_TIME_MAX_LEN+1];
    char date[SMS_DATE_MAX_LEN+1];
} TsmsHeaders;

typedef struct {
  char memory_offset[3]; //save the offset number as you receive it in ascii - you only gonna send this information anyways
  //char memory_block[6]; //this could be used if I'd wanna save SMSes somewhere else than the SIM
} TlastSmsEvent;

TsmsHeaders t_smsheaders = {0};
TlastSmsEvent t_last_sms_event = {0};

char smsBuff[SMS_LENGTH+1];
char num_of_smses_unread = 0;
char offset_of_unread_smses[10] = {0}; //saving the offsets in the SIM memory

char misc_print_buffer[PRINT_BUFF_LEN];

char newline_status = FALSE;

int event_on_lcd_start_secs = 0;

short secs_at_start;

short multi_command_count = 0;

float temperature = 0; //No averaging whatsoever, I love my LM35, it should be just fine

int last_user_cmd = 0;

unsigned short pb_entry_counter = 0; //counter for going through the phone book for searching if current call is authorized
unsigned short pb_mc_entry_counter = 0; //counter for going through the phone book for missed calls. another variable cause wouldn't want a situation where someone is calling while someone just smsed to get the missed calls...
unsigned short pb_entries = 0; //amount of phone book entries
unsigned short pb_mc_entries = 0; //amount of phone book entries
char b_pb_entries_retrieved = FALSE; //retrieve the PB entries only at bootup

//Instead, this struct should be a linked list of phone numbers. But because of lack of time, I just made a static array of possible 5 numbers (so that struct would fit 64 bytes)
typedef struct {
  short number_count;
  char number1[11];
  char number2[11];
  char number3[11];
  char number4[11];
  char number5[11];
} TlastUnauthNumbers;

TlastUnauthNumbers t_last_unauth_numbers = {0};

enum t_authorization_sm {
  IDLE,
  SEEKING_AUTHORIZATION,
  AUTHORIZATION_FAILED,
  AUTHORIZATION_FOUND,
  AUTH_SM_DISABLED
};
enum t_authorization_sm auth_sm_state = IDLE;
enum t_authorization_sm auth_sms_sm_state = IDLE;

enum t_gen_sm {
  GEN_IDLE,
  USER_CMD_LIST_UNAUTH,
  USER_CMD_LIST_UNAUTH_FINISHED
};
enum t_gen_sm gen_sm_state = GEN_IDLE;

char cmd_cb_number[SMS_PHONE_NUM_MAX_LEN+1];


LiquidCrystal lcd(13, 12, 11, 10, 9, 8);

/***********************************************************/

/* Just read the temperature to the global variable, with no averaging or history */
void read_temperature()
{
  temperature = ( RESOLUTION_MVOLTS * analogRead(LM35_PIN) / LM35_CELCIUS_RESOLUTION_MVOLTS );   
}


void switchModule(){
  digitalWrite(GSM_ENABLE,HIGH);
  delay(2000);
  digitalWrite(GSM_ENABLE,LOW);
}

void get_number_of_pb_entries()
{
   Serial.print("AT+CPBS=");
   Serial.write(0x22);   // send the " char
   Serial.print("SM");
   Serial.write(0x22);   // send the " char
   Serial.println();
   delay(1500);
   
    Serial.println(GET_PB_ENTRY_COUNT);
    delay(300);
    
    char my_temp_buff[5] = {0};
    short x,offset, comma_found;
    comma_found = 0;
    offset=0;
    for(x=0; x<15; x++) //for loop just to make sure we catch the UART
    {
      while(Serial.available() > 0)
      {
         inByte = Serial.read();

         if(inByte == 0x2C && comma_found == 0)
         {
            comma_found++;
         }
         else if(comma_found == 1 && inByte != 0x2C)
         {
           my_temp_buff[offset++] = inByte;
         }
         else if(inByte == 0x2C && comma_found > 0)
         {
           comma_found++;
           break; 
         }
      }
    }
    
     /*We'll also have to convert the now ascii string number into a short number. We are using the fact that the ascii numbers are 0x30 to 0x39,
       thus we can just mask out the MSB half of the byte and we'll get the wanted number straight. We are also using the temp buffer that has
       the saved values from the chip also as a temporary variable when doing these logical operations.*/
     if(offset == 1)
     {
       pb_entries = my_temp_buff[0] & 0x0F;
     }
     else if(offset == 2)
     {
       pb_entries = 10 * (my_temp_buff[0] & 0x0F) + (my_temp_buff[1] & 0x0F);
     }
     else if(offset == 3)
     {
       pb_entries = 100 * (my_temp_buff[0] & 0x0F) + 10 * (my_temp_buff[1] & 0x0F) + (my_temp_buff[2] & 0x0F);
     }
     /*
      gsm_print("CPBS at boot!",0);
      gsm_print("",1);
      lcd.setCursor(0, 1);
      lcd.print(pb_entries); 
      lcd.setCursor(3, 1);
      lcd.print(offset); 
      my_temp_buff[3] = '\0';
      lcd.setCursor(6, 1);
      lcd.print(my_temp_buff); */
      delay(2000);
}

void setup()
{
  memset(smsBuff, 0, SMS_LENGTH+1);
  
  // set up the LCD's number of columns and rows:
  lcd.begin(16, 2);
  // Print a message to the LCD.
  lcd.print("Testing GSM");
  
  delay(3500);
  
  lcd.clear();
  
  lcd.setCursor(0, 0);
  lcd.print("Status:");
  // start serial port at 9600 bps and wait for port to open:
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for Leonardo only
  }
 
 
  switchModule();
  lcd.setCursor(0, 1);
  lcd.print("Turned on...");
  
  delay(3000);
  
  //turn off serial loopback
  Serial.println("ATE"); 
  delay(500);
  
  //Send AT to check module OK
  Serial.println("AT");
  lcd.setCursor(0, 1);
  lcd.print("Verifying...");
  lcd.setCursor(0, 1);
  
  
  char status_buff[5];
  short secs_at_last_status_check;
  
  /*wait for OK from chip*/
wait_for_ok:
  secs_at_last_status_check = millis() / 1000;
  secs_at_start = millis() / 1000;
  memset(status_buff, 0, 5);
  
  do
  {
    if (Serial.available() > 0)
    {
      inByte = Serial.read();
      
      //debugRx(inByte);
      
      if(inByte == 'O')
      {
          status_buff[0] = inByte;
      }
      else if(inByte == 'K' && status_buff[0] == 'O')
      {
          status_buff[1] = inByte;
      }
      else if(inByte == ASCII_CR && status_buff[0] == 'O' && status_buff[1] == 'K')
      {
          break; //this should be enough to know OK received!
      }
      else
      {
          memset(status_buff, 0, 5); 
      } 
    }
    
    if(millis() / 1000 - secs_at_last_status_check >= STATUS_CHECK_FREQ_SECS)
    {
       //Flash screen to let user know
       notification_to_user();
       Serial.println("AT");
    }
    
  } while(millis()/ 1000 - secs_at_start <= MAX_OK_WAIT_TIME_SECS);
  
  if(millis()/ 1000 - secs_at_start >= MAX_OK_WAIT_TIME_SECS)
  {
       gsm_print("SEVERE ERROR:", 0);
       gsm_print("no OK received!", 1); //no OK received!!
  
       notification_to_user();
       delay(10000); 
       notification_to_user();  
       goto wait_for_ok;
  }
  else //good to go, do config!!!
  {
     gsm_print("chip OK", 1);
     
     /*If some config is needed, can be done here...*/
     //notification_to_user();
     //Serial.println(ENABLE_CRING);
     delay(2000); 
     //Serial.println(ENABLE_CLIP);
     //delay(2000);
  }
  
  //lcd.setCursor(0, 0); //debug
}

void loop()
{
  
  // move all this logic to a separate function, that has two modes - either run once and return (for putting here), or
  // for putting anywhere else in the code, where you want to read uart (make sure you have a timeout)
  if (Serial.available() > 0)
  {
    inByte = Serial.read();
    //forward to debug output
    //debugRx(inByte);
    
    uartbuff[ubuff_offset] = inByte;
    if(inByte == EOL_SAGEM &&//EOL_PUTTY)//EOL_SAGEM
       is_multiline_command() == FALSE)
    {
       ubuff_offset = 0;
       newline_status = TRUE;
    }
    else
    {
      ubuff_offset++;
    }
  }
  
  if(newline_status == TRUE)
  {
     /*for debug*/
     //gsm_print("parsing",1);
     //delay(400);
    
     do_action(parse_serial());
     newline_status = FALSE; 
     memset(uartbuff, 0, UART_BUFF_LENGTH);
  }
  else if(gen_sm_state != GEN_IDLE)
  {
     update_gen_sm();
  }
    
  //poll GPIO here. buttons for SMS etc...
    
  update_screen();
}

int is_multiline_command(void)
{
  if(uartbuff[1] == 'C' &&
     uartbuff[2] == 'M' &&
     uartbuff[3] == 'G' &&
     uartbuff[4] == 'R' &&
     multi_command_count == 0
     )
  {
     multi_command_count++; 
     
     return TRUE;
  }
  /*
  else if(uartbuff[1] == 'C' && //this condition is when we wanna know how many entries there are in the PB of the SIM and the additional empty line and OK messes it up
     uartbuff[2] == 'P' &&
     uartbuff[3] == 'B' &&
     uartbuff[4] == 'S' &&
     multi_command_count == 0
     )
  {
     multi_command_count++; 
     
     return TRUE;
  }*/
  else
  {
     return FALSE; 
  }
}

/* update general SM state when there's no relevant UART activity and take the necessary actions */
void update_gen_sm(void)
{
  if(gen_sm_state == USER_CMD_LIST_UNAUTH)
  {
     ;
  }
  if(gen_sm_state == USER_CMD_LIST_UNAUTH_FINISHED)
  {
     Serial.print(SEND_SMS);
     Serial.write(0x22);   // send the " char
     Serial.print(cmd_cb_number); //Sending back to the sender
     Serial.write(0x22); // send the " char
     Serial.println();
     delay(1500);
     Serial.print("Thank You for Your enquiry. The amount of unauthorized calls is ");
     Serial.print(t_last_unauth_numbers.number_count);
     Serial.print(".");
     short x;
     for(x=0; x<=t_last_unauth_numbers.number_count; x++)
     {
       switch(x)
       {
        case 1:
          Serial.print(" List: ");
          Serial.print(t_last_unauth_numbers.number1);
          Serial.print(", ");
          break; 
        case 2:
          Serial.print(t_last_unauth_numbers.number2);
          Serial.print(", ");
          break; 
        case 3:
          Serial.print(t_last_unauth_numbers.number3);
          Serial.print(", ");
          break; 
        case 4:
          Serial.print(t_last_unauth_numbers.number4);
          Serial.print(", ");
          break; 
        case 5:
          Serial.print(t_last_unauth_numbers.number5);
          Serial.print(".");
          break; 
        default:
          break;
       } 
     }
     Serial.print(" BR, Karri.");
     delay(500);
     Serial.write(0x1A); // end of message command 1A (hex) 
     
     pb_mc_entry_counter = 0;
     gen_sm_state = GEN_IDLE;
  }
}

/* Status "bar", RSSI etc */
void update_screen()
{
  if( (millis() / 1000) - secs_at_start > TIME_WHEN_POLLING_USED_PB_ENTRIES && b_pb_entries_retrieved == FALSE)
  {
    get_number_of_pb_entries(); 
    b_pb_entries_retrieved = TRUE;
  }
  else if( (millis() / 1000) - event_on_lcd_start_secs > EVENTS_ON_SCREEN_TIMEOUT_SECS)
  {
      /*Check the operator status much rarely*/
      if( (millis() / 1000) - event_on_lcd_start_secs > EVENTS_ON_SCREEN_TIMEOUT_SECS)//TODO - get operator name in setup()
      {
        memset(eventsbuff, 0, EVENTS_BUFF_LENGTH);
        Serial.println(GET_OPERATOR_NAME);
      }
      
      //lcd.clear();
      //gsm_print("uart attempt",1);
      delay(150);
      
      short x,sep_count,offset;
      sep_count=0;
      offset=0;
      for(x=0; x<15; x++) //for loop just to make sure we catch the UART
      {
        while(Serial.available() > 0)
        {
           inByte = Serial.read();
           
           //gsm_print("cops read",1);
           //delay(500);
           
           if(/*inByte != 0x2C &&*/ sep_count < 2)
           {
              if(inByte == 0x2C)
              {
                //gsm_print("separator",1);
                //delay(500);
                sep_count++; 
              }
              continue;
           }
           else if( (inByte == 0x22 && sep_count > 2) ||
				   offset > 15 /* We want maximum the length of the first row */
				  )
           {
             eventsbuff[offset] = '\0';
             x = 15; //exit for-loop
             break; 
           }
           else if(inByte == 0x22)
           {
              sep_count++; //don't wanna use space for another variable, reuse this one since no more incoming for this command
              continue; 
           }
           else //finally the operator name
           {
             eventsbuff[offset++] = inByte;
           }
        }
      }
      
      gsm_print(eventsbuff, 0); //Print operator name on the first row
      gsm_print("",1); //for now empty second line
      
      //move the command to here and move parsing where everything is parsed. also add a command for RSSI
      
      event_on_lcd_start_secs = millis() / 1000;
  }
}

void debugRx(char inByte)
{
    //the following prints out the num of the byte!
    //sprintf(text, "%d ", inByte);
    //lcd.print(text);
    if(inByte >= ASCII_TABLE_MIN && inByte <= ASCII_TABLE_MAX)
    {
      lcd.print(inByte);  
    }
}

void do_action(int opcode)
{
  short temp = event_on_lcd_start_secs;
  event_on_lcd_start_secs = millis() / 1000;
  
  if(multi_command_count > 0)
  {
    multi_command_count = 0; //zero out the multi-command counter! 
  }
  
  switch(opcode)
  {
     case AT_OK:
       //we could do something here if wanted
       break;
       
     case CRING:
       gsm_print("!INCOMING CALL!",0);
       break;
       
     case CLIP:
       gsm_print(misc_print_buffer,1); //use scrolling print!!
       
       switch(auth_sm_state)
       {
         case IDLE:
         case SEEKING_AUTHORIZATION: //fall through
           auth_sm_state = SEEKING_AUTHORIZATION;
           //Serial.println(GET_PB_ENTRY_COUNT);
           pb_entry_counter++;
           Serial.print(GET_PB_ENTRY);
           Serial.println(pb_entry_counter); 
           break;
         case AUTHORIZATION_FOUND:
           gsm_print("!AUTHORIZED!",0);
           break;
         case AUTHORIZATION_FAILED:
           gsm_print("!UNAUTHORIZED!",0);
           break;
         case AUTH_SM_DISABLED: //fall through
         default:
           notification_to_user();
           break;
       }
       break;
       
     case CPBS:
       if(auth_sm_state == SEEKING_AUTHORIZATION) //start the witch hunt only if that was the original purpose of calling AT+CPBS
       {
         pb_entry_counter++;
         Serial.print(GET_PB_ENTRY);
         Serial.println(pb_entry_counter); 
       }
       else if(gen_sm_state == USER_CMD_LIST_UNAUTH)
       {
         //gsm_print("KARRI1",0);
         delay(500);
         pb_mc_entry_counter++;
         Serial.print(GET_PB_ENTRY);
         Serial.println(pb_mc_entry_counter);
       }
       //otherwise we could also do something fun here
       break;
     case CPBR:
       if(gen_sm_state == USER_CMD_LIST_UNAUTH)
       {
         //gsm_print("KARRI3",0);
         delay(500);
         pb_mc_entry_counter++;
         Serial.print(GET_PB_ENTRY);
         Serial.println(pb_mc_entry_counter);
         delay(350);
       }
       else
       {
         if(auth_sm_state == SEEKING_AUTHORIZATION)
         {
            pb_entry_counter++;
            Serial.print(GET_PB_ENTRY);
            Serial.println(pb_entry_counter);
         }
         else if(auth_sm_state == AUTHORIZATION_FOUND)
         {
            gsm_print("!AUTHORIZED!",0);
            pb_entry_counter = 0;
            auth_sm_state = IDLE;
         }
         else if(auth_sm_state == AUTHORIZATION_FAILED)
         {
            gsm_print("!UNAUTHORIZED!",0);
            pb_entry_counter = 0;
            auth_sm_state = IDLE;
         }
       }
       break;
       
     case NO_CARRIER:
       pb_entry_counter = 0;
       auth_sm_state = IDLE; //reset the authorization state machine & PB counter, if not reset already
       gsm_print("Call HUNG UP",0);
       notification_to_user();
       break;
       
     case CMTI_SMS_EVENT:
       gsm_print("Incoming SMS:",0);
       notification_to_user();
       Serial.print(READ_SMS);
       if(t_last_sms_event.memory_offset[1] >= 0x30 && t_last_sms_event.memory_offset[1] <= 0x39)//if offset is two digits
       {
          Serial.print(t_last_sms_event.memory_offset[0]);
          Serial.println(t_last_sms_event.memory_offset[1]);
       }
       else
       {
          Serial.println(t_last_sms_event.memory_offset[0]);
       }
       break;
       
     case CMGR:
       gsm_print("",0);
       lcd.setCursor(0, 0);
       lcd.print(t_smsheaders.number); 
       delay(2000);
       gsm_print("",1);
       lcd.setCursor(0, 1);
       lcd.print(t_smsheaders.date); 
       lcd.setCursor(9, 1);
       lcd.print(t_smsheaders.time); 
       delay(3500);
       gsm_print_scrolling(smsBuff,0);
       break;
     
     case CMGR_USER_CMD_RECV:
       gsm_print("COMMAND RECEIVED",1);
       notification_to_user();
       gsm_print("",0);
       lcd.setCursor(0, 0);
       lcd.print(t_smsheaders.number); 
       delay(2000);
       handle_user_commands();
       break;
       
     default:
       //do nothing
       event_on_lcd_start_secs = temp;
       break;
  }
}

void handle_user_commands()
{
   switch(last_user_cmd)
   {
     case USER_CMD_GET_TEMPERATURE:
       read_temperature();
       Serial.print(SEND_SMS);
       Serial.write(0x22);   // send the " char
       Serial.print(t_smsheaders.number); //Sending back to the sender
       Serial.write(0x22); // send the " char
       Serial.println();
       delay(1500);
       Serial.print("Thank You for Your enquiry. The temperature is ");
       Serial.print(temperature);
       Serial.print(" Celcius. Regards, Karri.");
       delay(500);
       Serial.write(0x1A); // end of message command 1A (hex)
       break;
       
     case USER_CMD_LIST_UNAUTH_CALLS:
       gen_sm_state = USER_CMD_LIST_UNAUTH;
       auth_sms_sm_state = SEEKING_AUTHORIZATION;
       Serial.print("AT+CPBS=");
       Serial.write(0x22);   // send the " char
       Serial.print("MC");
       Serial.write(0x22);   // send the " char
       Serial.println();
       delay(1500);
       Serial.println(GET_PB_ENTRY_COUNT);
       delay(350);
       break;
     
     default:
       break;
   }
}

int parse_serial(void)
{
  int local_u_offset = 0;
  int ret = ERROR;

  if(uartbuff[local_u_offset] == 0x2B) //+
  {
    /*for debug*/
    //gsm_print("in CMD",1);
    //delay(500);
     local_u_offset++;
     ret = parse_in_command(&local_u_offset);
  }
  else //for now else, take a look at the AT commands!!!
  {
    /*for debug*/
    //gsm_print("misc CMD",1);
    //delay(500);
     ret = parse_misc(&local_u_offset);
  }
  
  return ret;
}


int parse_in_command(int *offset)
{
  int ret = ERROR;
  
  if(uartbuff[*offset]   == 'C' &&
     uartbuff[*offset+1] == 'R' &&
     uartbuff[*offset+2] == 'I' &&
     uartbuff[*offset+3] == 'N' &&
     uartbuff[*offset+4] == 'G'
     )
     {
       *offset += 5;
       ret = CRING;
     }
     
  if(uartbuff[*offset]   == 'C' &&
     uartbuff[*offset+1] == 'L' &&
     uartbuff[*offset+2] == 'I' &&
     uartbuff[*offset+3] == 'P'
     )
     {
       *offset += 4;
       
       short x, separator_count = 0;
       for(x=0; uartbuff[*offset]!=ASCII_CR && x < UART_BUFF_LENGTH; x++)
       {
         if(uartbuff[*offset] == 0x22/* " */ && separator_count == 0) //caller number
         {
            (*offset)++; //let's not save the first quota mark
            short my_offset = 0;
            do
            {
               misc_print_buffer[my_offset++] = uartbuff[(*offset)++];
            } while(uartbuff[*offset] != 0x22);
            
            misc_print_buffer[my_offset] = '\0';
            separator_count++;
			
	    break; //for now, only take phone number for events
         }
         
         (*offset)++;
       }
       
       ret = CLIP;
     }
     
  if(uartbuff[*offset]   == 'C' &&
     uartbuff[*offset+1] == 'P' &&
     uartbuff[*offset+2] == 'B' &&
     uartbuff[*offset+3] == 'R'
     )
     {
       *offset += 4;
       
                //gsm_print("KARRI2a",0);
         delay(500);
       
       short x, separator_count = 0;
       for(x=0; uartbuff[*offset]!=ASCII_CR && x < UART_BUFF_LENGTH; x++)
       {
         if(uartbuff[*offset] == 0x22/* " */ && separator_count == 0) //phonebook entry phone number
         {
            (*offset)++; //let's not save the first quota mark
            short my_offset = 0;
            do
            {
               t_smsheaders.number[my_offset++] = uartbuff[(*offset)++]; /* Using the number field in the SMS headers, no need to create a new variable in the memory... */
            } while(uartbuff[*offset] != 0x22);
            
            t_smsheaders.number[my_offset] = '\0';
            separator_count++;
            if(gen_sm_state != USER_CMD_LIST_UNAUTH)
            {
	      break; //for now, only take phone number(for auth/unauth feature)
            }
         }
         if(uartbuff[*offset] == 0x22/* " */ && separator_count == 1)
         {
            //If the next character is also a quotation mark etc, then we know it doesn't exist in the PB and thus UNAUTHORIZED!
            if(uartbuff[++(*offset)] == 0x22)
            {
               t_last_unauth_numbers.number_count++;
               save_number_to_unauth_list(t_smsheaders.number);
            }
         }
         
         (*offset)++;
       }
       
       if (!memcmp(t_smsheaders.number, misc_print_buffer, 10)) /* Using the number field in the SMS headers for PB entry, no need to create a new variable in the memory... incoming call number is in misc_print_buffer*/
       {
          if(auth_sm_state == SEEKING_AUTHORIZATION)
          {
            auth_sm_state = AUTHORIZATION_FOUND;
          }
       }
       else //memcmp > 0
       {
          ;
       }
       
       if(pb_entry_counter >= pb_entries)
       {
          if(auth_sm_state == SEEKING_AUTHORIZATION)
          {
            auth_sm_state = AUTHORIZATION_FAILED;
          }
       }
       if(pb_mc_entry_counter >= pb_mc_entries)
       {
                  //gsm_print("KARRI2b",0);
         delay(500);
          gen_sm_state = USER_CMD_LIST_UNAUTH_FINISHED;
       }
       else
       {
                   //gsm_print("KARRI2c",0);
         delay(500); 
       }
 /*      
                  gsm_print("parsing CPBR!",0);
                  gsm_print("",1);
        lcd.setCursor(0, 1);
        lcd.print(pb_entry_counter); 
        lcd.setCursor(5, 1);
                lcd.print(pb_entries); 
           //delay(2000);
 */  
       ret = CPBR;
     }

  if(uartbuff[*offset]   == 'C' &&
     uartbuff[*offset+1] == 'P' &&
     uartbuff[*offset+2] == 'B' &&
     uartbuff[*offset+3] == 'S'
     )
     {
       *offset += 4;
       
       unsigned short my_temp_pb_count = 0;
       char my_temp_buff[5] = {0};
       short x, separator_count = 0;
       short my_offset = 0;
       for(x=0; uartbuff[*offset]!=ASCII_CR && x < UART_BUFF_LENGTH; x++)
       {
         if(uartbuff[*offset] == 0x2C/* , */ && separator_count == 0) //we will totally dismiss the name of the phonebook, we'll just assume it's SM
         {
            (*offset)++; //let's not save the comma
            do
            {
               
               my_temp_buff[my_offset++] = uartbuff[(*offset)++];
            } while(uartbuff[*offset] != 0x2c && my_offset < 4); //max digits is 3, for the number of slots taken in SIM memory
            
            separator_count++;
			
	    break; //not interested in the max number of memory slots for now
         }
         
         (*offset)++;
       }
       
       /*We'll also have to convert the now ascii string number into a short number. We are using the fact that the ascii numbers are 0x30 to 0x39,
         thus we can just mask out the MSB half of the byte and we'll get the wanted number straight. We are also using the temp buffer that has
         the saved values from the chip also as a temporary variable when doing these logical operations.*/
       if(my_offset == 1)
       {
         my_temp_pb_count = my_temp_buff[0] & 0x0F;
       }
       else if(my_offset == 2)
       {
         my_temp_pb_count = 10 * (my_temp_buff[0] & 0x0F) + (my_temp_buff[1] & 0x0F);
       }
       else if(my_offset == 3)
       {
         my_temp_pb_count = 100 * (my_temp_buff[0] & 0x0F) + 10 * (my_temp_buff[1] & 0x0F) + (my_temp_buff[2] & 0x0F);
       }
       
       /*
        gsm_print("parsing CPBR!",0);
        gsm_print("",1);
        lcd.setCursor(0, 1);
        lcd.print(my_temp_pb_count); 
        my_temp_buff[3] = '\0';
        lcd.setCursor(4, 1);
        lcd.print(my_temp_pb_count); 
        delay(2000);
        */
        
       if(gen_sm_state == USER_CMD_LIST_UNAUTH)
       {
          pb_mc_entries = my_temp_pb_count; //Missed calls count
       }
       else
       {
          pb_entries = my_temp_pb_count; //Normal SIM phone book entry count
       }
       
       ret = CPBS;
     }


  if(uartbuff[*offset]   == 'C' &&
     uartbuff[*offset+1] == 'M' &&
     uartbuff[*offset+2] == 'T' &&
     uartbuff[*offset+3] == 'I'
     )
     {
       *offset += 4;
       
       char my_temp_array[3] = {0};
      
       short x;
       for(x=0; uartbuff[*offset]!=ASCII_CR && x < UART_BUFF_LENGTH; x++)
       {
         if(uartbuff[*offset] == 0x2C/* , */) //offset in SIM sms memory
         {
            (*offset)++; //let's not save the comma
            short my_offset = 0;
            do
            {
               my_temp_array[my_offset++] = uartbuff[(*offset)++];
            } while(uartbuff[*offset] != ASCII_CR && my_offset < 2);
			
	    break; //for now, only take the offset to the SMS in the SIM memory
         }
         
         (*offset)++;
       }
       

       t_last_sms_event.memory_offset[0] = my_temp_array[0]; //at least one digit should always exist
       
       if(my_temp_array[1] >= 0x30 && my_temp_array[1] <= 0x39)
       {
          t_last_sms_event.memory_offset[1] = my_temp_array[1];
       }
       
       ret = CMTI_SMS_EVENT;    
       
     }
     
  /*an SMS fetched from the SIM*/
  if(uartbuff[*offset]   == 'C' &&
     uartbuff[*offset+1] == 'M' &&
     uartbuff[*offset+2] == 'G' &&
     uartbuff[*offset+3] == 'R'
     )
     {
       *offset += 4;
       
       short x, pbuf_len, separator_count = 0;
       for(x=0; uartbuff[*offset]!=ASCII_CR && x < UART_BUFF_LENGTH; x++)
       {
         if(separator_count == 0 && uartbuff[*offset] != 0x2C)
         {
            (*offset)++;
         }
         else if(uartbuff[*offset] == 0x2C/* , */ && separator_count == 0) //caller number
         {
            (*offset)++; //let's not save the comma...
            (*offset)++; //...and the first quota mark...
            short my_offset = 0;
            do
            {
               t_smsheaders.number[my_offset++] = uartbuff[(*offset)++];
            } while(uartbuff[*offset] != 0x22);
            
            t_smsheaders.number[my_offset] = '\0';
            separator_count++;
			
	    (*offset)++; //...nor the ending quota mark
         }
         else if(uartbuff[*offset] == 0x2C/* , */ && separator_count == 1) //name, let's not save it for now...
         {
            (*offset)++; //let's not save the comma...
            (*offset)++; //...and the first quota mark...
            //short my_offset = 0;
            do
            {
               (*offset)++;//t_smsheaders.number[my_offset++] = uartbuff[(*offset)++];
            } while(uartbuff[*offset] != 0x22);
            
            //t_smsheaders.number[my_offset] = '\0';
            separator_count++;
			
	    (*offset)++; //...nor the ending quota mark
         }
         else if(uartbuff[*offset] == 0x2C/* , */ && separator_count == 2) //date
         {
            (*offset)++; //let's not save the comma...
            (*offset)++; //...and the first quota mark...
            short my_offset = 0;//SMS_DATE_MAX_LEN - 1;
            do
            {
               t_smsheaders.date[my_offset++] = uartbuff[(*offset)++]; //I don't like the american date, want the european one!
            } while(uartbuff[*offset] != 0x2C); //this time the ending separator is only a comma, not quotation
            
            t_smsheaders.date[my_offset] = '\0';
            separator_count++;
         }
         else if(uartbuff[*offset] == 0x2C/* , */ && separator_count == 3) //time
         {
            (*offset)++; //let's not save the comma...
            short my_offset = 0;
            do
            {
               t_smsheaders.time[my_offset++] = uartbuff[(*offset)++]; //take only hours & mins
            } while(my_offset < SMS_TIME_MAX_LEN);
            
            t_smsheaders.time[my_offset] = '\0';
            separator_count++;
         }
         else if(separator_count == 4)
         {
            *offset += 8; //scroll until the message starts
            break;
         }
       }
       
       //Increment the offset once more, when using the SAGEM GSM module (on Putty only CR is sent, SAGEM sends CR & LF)
       if(uartbuff[*offset] == ASCII_LF)
       {
          (*offset)++;
       }
	   
       /* Determine the length of the string. Just to be sure it's not infinite loop, " < 255" */
       for(pbuf_len=0; *((uartbuff+(*offset))+pbuf_len) != ASCII_CR && pbuf_len < 255; pbuf_len++);
       
       memcpy(smsBuff, (uartbuff+(*offset)), pbuf_len);
       
       smsBuff[pbuf_len] = '\0';
/*
       //debug
       gsm_print("sms text len:",0);
       gsm_print("",1);
       lcd.setCursor(0, 1);
       lcd.print(pbuf_len);   
       delay(3500); 
*/   
       ret = parse_sms_user_commands();
       
       if(ret && USER_CMD_BITMASK)
       {
          memcpy(cmd_cb_number, t_smsheaders.number, SMS_PHONE_NUM_MAX_LEN);
          last_user_cmd = ret; //Saving the user command
          ret = CMGR_USER_CMD_RECV;
       }
       else // a normal SMS
       {
          ret = CMGR;
       }
     }
     
  return ret;
}   

//Let's call it as stupid as this for now...
int parse_misc(int *offset)
{
  int ret = ERROR;
  
  if(uartbuff[*offset]   == 'O' &&
     uartbuff[*offset+1] == 'K' &&
     uartbuff[*offset+2] == ASCII_CR
     )
     {
       ret = AT_OK;
     }
  
  //NO CARRIER
  if(uartbuff[*offset]   == 'N' &&
     uartbuff[*offset+1] == 'O' &&
     uartbuff[*offset+3] == 'C' &&
     uartbuff[*offset+4] == 'A' &&
     uartbuff[*offset+5] == 'R' &&
     uartbuff[*offset+6] == 'R' &&
     uartbuff[*offset+7] == 'I' &&
     uartbuff[*offset+8] == 'E' &&
     uartbuff[*offset+9] == 'R'
     )
     {
        ret = NO_CARRIER;
     }
     
  // simple RING
  if(uartbuff[*offset]   == 'R' &&
     uartbuff[*offset+1] == 'I' &&
     uartbuff[*offset+2] == 'N' &&
     uartbuff[*offset+3] == 'G'
     )
     {
       ret = CRING;
     }
  
  return ret;
}

short parse_sms_user_commands(void)
{
  int ret = FALSE;
  
  if(smsBuff[0] == 'G' &&
     smsBuff[1] == 'E' &&
     smsBuff[2] == 'T' &&
     smsBuff[4] == 'T' &&
     smsBuff[5] == 'E' &&
     smsBuff[6] == 'M' &&
     smsBuff[7] == 'P' &&
     smsBuff[8] == 'E' &&
     smsBuff[9] == 'R' &&
     smsBuff[10] == 'A' &&
     smsBuff[11] == 'T' &&
     smsBuff[12] == 'U' &&
     smsBuff[13] == 'R' &&
     smsBuff[14] == 'E'
     )
     {
       ret = USER_CMD_GET_TEMPERATURE;
     }
     
  if(smsBuff[0] == 'L' &&
     smsBuff[1] == 'I' &&
     smsBuff[2] == 'S' &&
     smsBuff[3] == 'T' &&
     smsBuff[5] == 'U' &&
     smsBuff[6] == 'N' &&
     smsBuff[7] == 'A' &&
     smsBuff[8] == 'U' &&
     smsBuff[9] == 'T' &&
     smsBuff[10] == 'H'
     )
     {
       ret = USER_CMD_LIST_UNAUTH_CALLS;
     }
     
     return ret;
}

void save_number_to_unauth_list(char *number)
{
  switch(t_last_unauth_numbers.number_count)
  {
    case 1:
      memcpy(t_last_unauth_numbers.number1, number, 10);
      break; 
    case 2:
      memcpy(t_last_unauth_numbers.number2, number, 10);
      break; 
    case 3:
      memcpy(t_last_unauth_numbers.number3, number, 10);
      break; 
    case 4:
      memcpy(t_last_unauth_numbers.number4, number, 10);
      break; 
    case 5:
      memcpy(t_last_unauth_numbers.number5, number, 10);
      break; 
    default:
      break;
  }
}

void gsm_print(char *text, short row)
{
   switch(row)
   {
      case 0:
        lcd.setCursor(0, 0);
        lcd.print("                "); //clear the row first
        lcd.setCursor(0, 0);
        lcd.print(text); 
        break;
      case 1:
        lcd.setCursor(0, 1);
        lcd.print("                "); //clear the row first
        lcd.setCursor(0, 1);
        lcd.print(text); 
   }
}

void gsm_print_scrolling(char *text, short row)
{
   char local_print_buffer[17] = {0};
   short pbuf_len, local_pbuf_offset = 0;
   local_print_buffer[16] = '\0';
   
   /* Determine the length of the string. Just to be sure it's not infinite loop, " < 255" */
   for(pbuf_len=0; *(text+pbuf_len) != '\0' && pbuf_len < 255; pbuf_len++);
/*   
   //debug
   gsm_print("text len:",0);
   gsm_print("",1);
   lcd.setCursor(0, 1);
   lcd.print(pbuf_len);   
   delay(5000); 
   
   Serial.println(text);
   delay(250);
   Serial.println(uartbuff);
*/
   for(local_pbuf_offset=0; local_pbuf_offset <= pbuf_len - 16; local_pbuf_offset++)
   {
     memcpy(local_print_buffer, (text+local_pbuf_offset), 16);       
     gsm_print(local_print_buffer, row);
     delay(SCROLL_SPEED);
   }   
   delay(5000);
}




//Notification for SMS, RING, whatever
void notification_to_user(void)
{
  int x;
  
  for(x=0; x<8; x++)
  {
    lcd.noDisplay();
    delay(LCD_FLICKER_DELAY/5);
    lcd.display();
    delay(LCD_FLICKER_DELAY/5);
  }
  
  for(x=0; x<2; x++)
  {
    lcd.noDisplay();
    delay(LCD_FLICKER_DELAY);
    lcd.display();
    delay(LCD_FLICKER_DELAY);
  }

  for(x=0; x<8; x++)
  {
    lcd.noDisplay();
    delay(LCD_FLICKER_DELAY/5);
    lcd.display();
    delay(LCD_FLICKER_DELAY/5);
  }
}


