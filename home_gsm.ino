
#include <TM1637Display.h>
#include <LWiFi.h>
#include <LWiFiClient.h>

enum status
{
	STATUS_GOOD = 0,
	STATUS_ERR = 1,
	STATUS_IDLE = 2,
	STATUS_WAIT1 = 3,
	STATUS_WAIT2 = 4,
	STATUS_WAIT3 = 5,
	STATUS_WAIT4 = 6,
	STATUS_CONN = 7,
};

#define LETTER_C      (SEG_A | SEG_D | SEG_E | SEG_F)
#define LETTER_G      (SEG_A | SEG_C | SEG_D | SEG_E | SEG_F)
#define LETTER_O      (SEG_C | SEG_D | SEG_E | SEG_G)
#define LETTER_D      (SEG_B | SEG_C | SEG_D | SEG_E | SEG_G)
#define LETTER_E      (SEG_A | SEG_D | SEG_E | SEG_F | SEG_G)
#define LETTER_N      (SEG_A | SEG_B | SEG_C | SEG_E | SEG_F)
#define LETTER_R      (SEG_A | SEG_E | SEG_F)
#define LETTER_DASH   (SEG_G)

#define CLK_PIN 19
#define DIO_PIN 18
TM1637Display screen(CLK_PIN, DIO_PIN);

#define WIFI_AP "PositiivisestiKorsolainen"
#define WIFI_PASSWORD "jannelahti47"
#define WIFI_AUTH LWIFI_WPA  // choose from LWIFI_OPEN, LWIFI_WPA, or LWIFI_WEP.
#define SITE_URL "www.yourwebsite.com"

#define SMS_NUMBER	"0587155100"
#define REBOOT_DONE_SMS "Init of Linkit completed succesfully."


void print_status(uint8_t status)
{
	uint8_t display_val[4] = {0,0,0,0};

	/////////
	//Display
	/////////
	switch(status)
	{
		case STATUS_GOOD:
			display_val[0] = LETTER_G;
			display_val[1] = LETTER_O;
			display_val[2] = LETTER_O;
			display_val[3] = LETTER_D;
			break;
		case STATUS_CONN:
			display_val[0] = LETTER_C;
			display_val[1] = LETTER_O;
			display_val[2] = LETTER_N;
			display_val[3] = LETTER_N;
			break;
		case STATUS_WAIT4:
			display_val[3] = LETTER_DASH;
			/* fall-through */
		case STATUS_WAIT3:
			display_val[2] = LETTER_DASH;
			/* fall-through */
		case STATUS_WAIT2:
			display_val[1] = LETTER_DASH;
			/* fall-through */
		case STATUS_WAIT1:
			display_val[0] = LETTER_DASH;
			break;
		case STATUS_ERR:
		default:
			display_val[0] = LETTER_E;
			display_val[1] = LETTER_R;
			display_val[2] = LETTER_R;
			display_val[3] = 0;  
	}

	screen.setSegments(display_val);
	  
	delay(1000);
}


void main()
{
	int send_status = 0;
	
	pinMode(13, OUTPUT);
	//Blink LED
	digitalWrite(13, HIGH);
	delay(75);
	digitalWrite(13, LOW);
	delay(75);
	digitalWrite(13, HIGH);
	delay(75);
	digitalWrite(13, LOW);
	delay(75);

	screen.setBrightness(7);

	print_status(STATUS_WAIT1);

	//Connect to wifi
	LWiFi.begin();
  
	while (0 == LWiFi.connect(WIFI_AP, LWiFiLoginInfo(WIFI_AUTH, WIFI_PASSWORD)))
	{
		digitalWrite(13, HIGH);
		delay(1000);
		digitalWrite(13, LOW);
	}
	
	print_status(STATUS_WAIT2);

#if 0	
	//Send update on reboot over SMS
	while (0 == LSMS.ready()) //wait until ready
	{
		digitalWrite(13, HIGH);
		delay(500);
		digitalWrite(13, LOW);
		delay(500);
	}
#endif
	
	print_status(STATUS_WAIT3);
	
	
#if 0
	int x;
	for (x = 0; x < 20; x++)
	{
		LSMS.beginSMS("0587155100");//(SMS_NUMBER);
		LSMS.print("test sms linkitone");//(REBOOT_DONE_SMS);
		if (0 == LSMS.endSMS())
		{
			digitalWrite(13, HIGH);
			delay(500);
			digitalWrite(13, LOW);
			delay(500);
		}
		else
		{
			break;
		}
	}
#endif
	
	print_status(STATUS_WAIT4);
	delay(800);
	
	print_status(STATUS_CONN);
	delay(1000);

	//////////////////////////////
	//Main loop
	while(1)
	{
		print_status(STATUS_GOOD);

		/////////////////////////////
		//Alive blink LED
		digitalWrite(13, HIGH);
		delay(1000);
		digitalWrite(13, LOW);
		delay(1000);
	}
}


