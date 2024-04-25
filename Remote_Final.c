#include <EFM8LB1.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define SYSCLK 72000000
#define BAUDRATE 115200L
#define SARCLK 18000000L
#define TIMER_5_FREQ 2048 //should be set 2048L later
#define TIMER_2_FREQ 1000L//For a 1ms tick

#define LCD_RS P1_7
// #define LCD_RW Px_x // Not used in this code.  Connect to GND
#define LCD_E  P2_5
#define LCD_D4 P1_3
#define LCD_D5 P1_2
#define LCD_D6 P1_1
#define LCD_D7 P1_0
#define CHARS_PER_LINE 16

#define BUTTON1 (!P3_7)
#define BUTTON2 (!P3_2)
#define BUTTON3 (!P3_0)

idata char buff[30];
volatile unsigned int TickCount = 0;
volatile unsigned int Sound_out = 1000;
volatile unsigned int state = 0;

char _c51_external_startup (void)
{
	// Disable Watchdog with key sequence
	SFRPAGE = 0x00;
	WDTCN = 0xDE; //First key
	WDTCN = 0xAD; //Second key
  
	VDM0CN=0x80;       // enable VDD monitor
	RSTSRC=0x02|0x04;  // Enable reset on missing clock detector and VDD

	#if (SYSCLK == 48000000L)	
		SFRPAGE = 0x10;
		PFE0CN  = 0x10; // SYSCLK < 50 MHz.
		SFRPAGE = 0x00;
	#elif (SYSCLK == 72000000L)
		SFRPAGE = 0x10;
		PFE0CN  = 0x20; // SYSCLK < 75 MHz.
		SFRPAGE = 0x00;
	#endif
	
	#if (SYSCLK == 12250000L)
		CLKSEL = 0x10;
		CLKSEL = 0x10;
		while ((CLKSEL & 0x80) == 0);
	#elif (SYSCLK == 24500000L)
		CLKSEL = 0x00;
		CLKSEL = 0x00;
		while ((CLKSEL & 0x80) == 0);
	#elif (SYSCLK == 48000000L)	
		// Before setting clock to 48 MHz, must transition to 24.5 MHz first
		CLKSEL = 0x00;
		CLKSEL = 0x00;
		while ((CLKSEL & 0x80) == 0);
		CLKSEL = 0x07;
		CLKSEL = 0x07;
		while ((CLKSEL & 0x80) == 0);
	#elif (SYSCLK == 72000000L)
		// Before setting clock to 72 MHz, must transition to 24.5 MHz first
		CLKSEL = 0x00;
		CLKSEL = 0x00;
		while ((CLKSEL & 0x80) == 0);
		CLKSEL = 0x03;
		CLKSEL = 0x03;
		while ((CLKSEL & 0x80) == 0);
	#else
		#error SYSCLK must be either 12250000L, 24500000L, 48000000L, or 72000000L
	#endif
	
	P0MDOUT |= 0x11; // Enable UART0 TX (P0.4) and UART1 TX (P0.0) as push-pull outputs
	P2MDOUT |= 0x01; // P2.0 in push-pull mode
	P2MDOUT|=0b_0000_0010;
	XBR0     = 0x01; // Enable UART0 on P0.4(TX) and P0.5(RX)                     
	XBR1     = 0X00;
	XBR2     = 0x41; // Enable crossbar and uart 1

	// Configure Uart 0
	#if (((SYSCLK/BAUDRATE)/(2L*12L))>0xFFL)
		#error Timer 0 reload value is incorrect because (SYSCLK/BAUDRATE)/(2L*12L) > 0xFF
	#endif
	SCON0 = 0x10;
	TH1 = 0x100-((SYSCLK/BAUDRATE)/(2L*12L));
	TL1 = TH1;      // Init Timer1
	TMOD &= ~0xf0;  // TMOD: timer 1 in 8-bit auto-reload
	TMOD |=  0x20;                       
	TR1 = 1; // START Timer1
	TI = 1;  // Indicate TX0 ready
	
	//Initialize timer 2 for periodic interrupts
	TMR2CN0=0x00;   // Stop Timer2; Clear TF2;
	CKCON0|=0b_0001_0000; // Timer 2 uses the system clock
	TMR2RL=(0x10000L-(SYSCLK/(2*TIMER_2_FREQ))); // Initialize reload value
	TMR2=0xffff;   // Set to reload immediately
	ET2=1;         // Enable Timer2 interrupts
	TR2=1;         // Start Timer2 (TMR2CN is bit addressable) 
	
	// Initialize timer 5 for periodic interrupts
	SFRPAGE=0x10;
	TMR5CN0=0x00;   // Stop Timer5; Clear TF5; WARNING: lives in SFR page 0x10
	CKCON1|=0b_0000_0100; // Timer 5 uses the system clock
	TMR5RL=(0x10000L-(SYSCLK/(2*TIMER_5_FREQ))); // Initialize reload value
	TMR5=0xffff;   // Set to reload immediately
	EIE2|=0b_0000_1000; // Enable Timer5 interrupts
	TR5=1;         // Start Timer5 (TMR5CN0 is bit addressable)
	SFRPAGE=0x00;

	EA=1;  // Enable global interrupts
	
	return 0;
}

void Timer2_ISR (void) interrupt INTERRUPT_TIMER2
{   
	SFRPAGE=0x0;
	TF2H = 0; // Clear Timer2 interrupt flag
	TickCount++;
	if(TickCount>=Sound_out){
	SFRPAGE = 0x10;
	TR5 ^= 1;
	TickCount=0;
	SFRPAGE = 0x00;
	}	
}

void Timer5_ISR (void) interrupt INTERRUPT_TIMER5
{
	SFRPAGE=0x10;
	TF5H = 0; // Clear Timer5 interrupt flag
	P2_1=!P2_1;
	SFRPAGE=0x00;
}


void InitADC (void)
{
	SFRPAGE = 0x00;
	ADEN=0; // Disable ADC
	
	ADC0CN1=
		(0x2 << 6) | // 0x0: 10-bit, 0x1: 12-bit, 0x2: 14-bit
        (0x0 << 3) | // 0x0: No shift. 0x1: Shift right 1 bit. 0x2: Shift right 2 bits. 0x3: Shift right 3 bits.		
		(0x0 << 0) ; // Accumulate n conversions: 0x0: 1, 0x1:4, 0x2:8, 0x3:16, 0x4:32
	
	ADC0CF0=
	    ((SYSCLK/SARCLK) << 3) | // SAR Clock Divider. Max is 18MHz. Fsarclk = (Fadcclk) / (ADSC + 1)
		(0x0 << 2); // 0:SYSCLK ADCCLK = SYSCLK. 1:HFOSC0 ADCCLK = HFOSC0.
	
	ADC0CF1=
		(0 << 7)   | // 0: Disable low power mode. 1: Enable low power mode.
		(0x1E << 0); // Conversion Tracking Time. Tadtk = ADTK / (Fsarclk)
	
	ADC0CN0 =
		(0x0 << 7) | // ADEN. 0: Disable ADC0. 1: Enable ADC0.
		(0x0 << 6) | // IPOEN. 0: Keep ADC powered on when ADEN is 1. 1: Power down when ADC is idle.
		(0x0 << 5) | // ADINT. Set by hardware upon completion of a data conversion. Must be cleared by firmware.
		(0x0 << 4) | // ADBUSY. Writing 1 to this bit initiates an ADC conversion when ADCM = 000. This bit should not be polled to indicate when a conversion is complete. Instead, the ADINT bit should be used when polling for conversion completion.
		(0x0 << 3) | // ADWINT. Set by hardware when the contents of ADC0H:ADC0L fall within the window specified by ADC0GTH:ADC0GTL and ADC0LTH:ADC0LTL. Can trigger an interrupt. Must be cleared by firmware.
		(0x0 << 2) | // ADGN (Gain Control). 0x0: PGA gain=1. 0x1: PGA gain=0.75. 0x2: PGA gain=0.5. 0x3: PGA gain=0.25.
		(0x0 << 0) ; // TEMPE. 0: Disable the Temperature Sensor. 1: Enable the Temperature Sensor.

	ADC0CF2= 
		(0x0 << 7) | // GNDSL. 0: reference is the GND pin. 1: reference is the AGND pin.
		(0x1 << 5) | // REFSL. 0x0: VREF pin (external or on-chip). 0x1: VDD pin. 0x2: 1.8V. 0x3: internal voltage reference.
		(0x1F << 0); // ADPWR. Power Up Delay Time. Tpwrtime = ((4 * (ADPWR + 1)) + 2) / (Fadcclk)
	
	ADC0CN2 =
		(0x0 << 7) | // PACEN. 0x0: The ADC accumulator is over-written.  0x1: The ADC accumulator adds to results.
		(0x0 << 0) ; // ADCM. 0x0: ADBUSY, 0x1: TIMER0, 0x2: TIMER2, 0x3: TIMER3, 0x4: CNVSTR, 0x5: CEX5, 0x6: TIMER4, 0x7: TIMER5, 0x8: CLU0, 0x9: CLU1, 0xA: CLU2, 0xB: CLU3

	ADEN=1; // Enable ADC
}
void InitPinADC (unsigned char portno, unsigned char pin_num)
{
	unsigned char mask;
	
	mask=1<<pin_num;

	SFRPAGE = 0x20;
	switch (portno)
	{
		case 0:
			P0MDIN &= (~mask); // Set pin as analog input
			P0SKIP |= mask; // Skip Crossbar decoding for this pin
		break;
		case 1:
			P1MDIN &= (~mask); // Set pin as analog input
			P1SKIP |= mask; // Skip Crossbar decoding for this pin
		break;
		case 2:
			P2MDIN &= (~mask); // Set pin as analog input
			P2SKIP |= mask; // Skip Crossbar decoding for this pin
		break;
		default:
		break;
	}
	SFRPAGE = 0x00;
}

unsigned int ADC_at_Pin(unsigned char pin)
{
	ADC0MX = pin;   // Select input from pin
	ADINT = 0;
	ADBUSY = 1;     // Convert voltage at the pin
	while (!ADINT); // Wait for conversion to complete
	return (ADC0);
}
#define VDD 3.3 // The measured value of VDD in volts
float Volts_at_Pin(unsigned char pin)
{
	 return ((ADC_at_Pin(pin)*VDD)/16383.0);
}


void UART1_Init (unsigned long baudrate)
{
    SFRPAGE = 0x20;
	SMOD1 = 0x0C; // no parity, 8 data bits, 1 stop bit
	SCON1 = 0x10;
	SBCON1 =0x00;   // disable baud rate generator
	SBRL1 = 0x10000L-((SYSCLK/baudrate)/(12L*2L));
	TI1 = 1; // indicate ready for TX
	SBCON1 |= 0x40;   // enable baud rate generator
	SFRPAGE = 0x00;
}

// Uses Timer3 to delay <us> micro-seconds. 
void Timer3us(unsigned char us)
{
	unsigned char i;               // usec counter
	
	// The input for Timer 3 is selected as SYSCLK by setting T3ML (bit 6) of CKCON0:
	CKCON0|=0b_0100_0000;
	
	TMR3RL = (-(SYSCLK)/1000000L); // Set Timer3 to overflow in 1us.
	TMR3 = TMR3RL;                 // Initialize Timer3 for first overflow
	
	TMR3CN0 = 0x04;                 // Sart Timer3 and clear overflow flag
	for (i = 0; i < us; i++)       // Count <us> overflows
	{
		while (!(TMR3CN0 & 0x80));  // Wait for overflow
		TMR3CN0 &= ~(0x80);         // Clear overflow indicator
	}
	TMR3CN0 = 0 ;                   // Stop Timer3 and clear overflow flag
}

void waitms (unsigned int ms)
{
	unsigned int j;
	unsigned char k;
	for(j=0; j<ms; j++)
		for (k=0; k<4; k++) Timer3us(250);
}

//LCD
void LCD_pulse (void)
{
	LCD_E=1;
	Timer3us(40);
	LCD_E=0;
}

void LCD_byte (unsigned char x)
{
	// The accumulator in the C8051Fxxx is bit addressable!
	ACC=x; //Send high nible
	LCD_D7=ACC_7;
	LCD_D6=ACC_6;
	LCD_D5=ACC_5;
	LCD_D4=ACC_4;
	LCD_pulse();
	Timer3us(40);
	ACC=x; //Send low nible
	LCD_D7=ACC_3;
	LCD_D6=ACC_2;
	LCD_D5=ACC_1;
	LCD_D4=ACC_0;
	LCD_pulse();
}

void WriteData (unsigned char x)
{
	LCD_RS=1;
	LCD_byte(x);
	waitms(2);
}

void WriteCommand (unsigned char x)
{
	LCD_RS=0;
	LCD_byte(x);
	waitms(5);
}

void LCD_4BIT (void)
{
	LCD_E=0; // Resting state of LCD's enable is zero
	// LCD_RW=0; // We are only writing to the LCD in this program
	waitms(20);
	// First make sure the LCD is in 8-bit mode and then change to 4-bit mode
	WriteCommand(0x33);
	WriteCommand(0x33);
	WriteCommand(0x32); // Change to 4-bit mode

	// Configure the LCD
	WriteCommand(0x28);
	WriteCommand(0x0c);
	WriteCommand(0x01); // Clear screen command (takes some time)
	waitms(20); // Wait for clear screen command to finsih.
}

void LCDprint(char * string, unsigned char line, bit clear)
{
    int j;

    WriteCommand(line==2?0xc0:0x80);
    waitms(5);
    for(j=0; string[j]!=0; j++) WriteData(string[j]);// Write the message
    if(clear) for(; j<CHARS_PER_LINE; j++) WriteData(' '); // Clear the rest of the line
}


void putchar1 (char c) 
{
    SFRPAGE = 0x20;
	while (!TI1);
	TI1=0;
	SBUF1 = c;
	SFRPAGE = 0x00;
}

void sendstr1 (char * s)
{ 
	while(*s)
	{
		putchar1(*s);
		s++;	
	}
}

char getchar1 (void)
{
	char c;
    SFRPAGE = 0x20;
	while (!RI1);
	RI1=0;
	// Clear Overrun and Parity error flags 
	SCON1&=0b_0011_1111;
	c = SBUF1;
	SFRPAGE = 0x00;
	return (c);
}

char getchar1_with_timeout (void)
{
	char c;
	unsigned int timeout;
    SFRPAGE = 0x20;
    timeout=0;
	while (!RI1)
	{
		SFRPAGE = 0x00;
		Timer3us(20);
		SFRPAGE = 0x20;
		timeout++;
		if(timeout==25000)
		{
			SFRPAGE = 0x00;
			return ('\n'); // Timeout after half second
		}
	}
	RI1=0;
	// Clear Overrun and Parity error flags 
	SCON1&=0b_0011_1111;
	c = SBUF1;
	SFRPAGE = 0x00;
	return (c);
}

void getstr1 (char * s)
{
	char c;
	
	while(1)
	{
		c=getchar1_with_timeout();
		if(c=='\n')
		{
			*s=0;
			return;
		}
		*s=c;
		s++;
	}
}

// RXU1 returns '1' if there is a byte available in the receive buffer of UART1
bit RXU1 (void)
{
	bit mybit;
    SFRPAGE = 0x20;
	mybit=RI1;
	SFRPAGE = 0x00;
	return mybit;
}

void waitms_or_RI1 (unsigned int ms)
{
	unsigned int j;
	unsigned char k;
	for(j=0; j<ms; j++)
	{
		for (k=0; k<4; k++)
		{
			if(RXU1()) return;
			Timer3us(250);
		}
	}
}

void SendATCommand (char * s)
{
	printf("Command: %s", s);
	P2_0=0; // 'set' pin to 0 is 'AT' mode.
	waitms(5);
	sendstr1(s);
	getstr1(buff);
	waitms(10);
	P2_0=1; // 'set' pin to 1 is normal operation mode.
	printf("Response: %s\r\n", buff);
}
     

int pwm_txd(int y, int x, unsigned int time_sec)
{
    unsigned char i;
    float voltage;
    int v;
    
    for(i = 0; i < time_sec*10; i++){ 
            
        sprintf(buff, "%d,%d\n\r", x,y);
        sendstr1(buff);
        printf(buff);
        
        waitms_or_RI1(100);
       
        voltage = Volts_at_Pin(QFP32_MUX_P2_4);
        v = (voltage == 0.00) ? 0 : 1;
        if(v == 0)
        {
            return 1; 
        }
        
    }
    
    return 0; // Completed the loop without button press
}
unsigned char unlockSequence[]= {1, 3, 2, 1};
unsigned char currentStep;
    
void mode0(void)
{	
	int one_count = 0;
	currentStep = 0;
	waitms(500);
	waitms(500);
	waitms(500);
	LCDprint("Enter password", 1,1);
	//LCD_4BIT();
	   while(currentStep < 4) {
        if (BUTTON1 && unlockSequence[currentStep] == 1) {
        //LCDprint("1", 1,1, 1);
            while(BUTTON1); // Wait for button release
            currentStep++;
            one_count++;
            if(one_count == 1){
            	WriteCommand(0x01);
            	LCDprint("1",1,1);
            }
            else if(one_count == 2)
            {
            	WriteCommand(0x01);
            	LCDprint("1321", 1, 1);
            }
        } else if (BUTTON3 && unlockSequence[currentStep] == 3) {
        WriteCommand(0x01);
        LCDprint("13", 1, 1);
            while(BUTTON3); // Wait for button release
            currentStep++;
        } else if (BUTTON2 && unlockSequence[currentStep] == 2) {
        WriteCommand(0x01);
        LCDprint("132", 1, 1);
            while(BUTTON2); // Wait for button release
            currentStep++;
        } 
        else if (currentStep > 0 && (BUTTON1 || BUTTON2 || BUTTON3)) {
            // If any button is pressed out of sequence, reset the step counter
            LCDprint("     Error!    ", 1, 1);
            waitms(2500);
            mode0();
            //currentStep = 0;
        }
        waitms(100);
     
    
    }   

	waitms(2000);
	LCDprint("Starting...",1,1);
	waitms(2000);
	LCDprint("           ",1,1);
}

void mode1(void)
{
	
	char a='0';
	int sum=0; 
	float freq_index=0.0;
	float v[3];
	float x,y;
	int xx,yy,dig_v;
	
	LCDprint("Mode 1", 1, 1);
	
	while(1)
	{	int i;
		sum=0;
		freq_index=0.0;
		v[0] = Volts_at_Pin(QFP32_MUX_P2_2);
		v[1] = Volts_at_Pin(QFP32_MUX_P2_3);
		
		y=9/3.3*v[0];
		x=9/3.3*v[1];
		
		yy = (int)y;
		xx = (int)x;
			
		sprintf(buff, "%d,%d\n\r", xx,yy);
		printf("x:%d,y:%d\n",xx,yy);
		sendstr1(buff);
		//LCDprint(buff, 2, 5, 1);
		waitms_or_RI1(100);
		
		if(RXU1())
        {
            
            a = getchar1_with_timeout();
            for(i=0;i<20;i++){
            sum+=a-'0';
            }
            freq_index= sum/20;
            printf("%f\r\n",freq_index);
           
            if(freq_index>=0&&freq_index<1)
            { 
            	Sound_out = 2000;
            	LCDprint("WEAK", 2, 1);
            	
            }
            else if(freq_index>=1&&freq_index<=2){
              
              Sound_out = 1000;
              LCDprint("Medium", 2, 1);
            }
            else if(freq_index>2&&freq_index<3){
            
            Sound_out = 400;
            LCDprint("Metal Detected", 2, 1);
            
            }
            else {
              
              Sound_out = 200;
              LCDprint("Metal Detected", 2, 1);
            }
           
            waitms(10);
        }
        
        v[2] = Volts_at_Pin(QFP32_MUX_P2_4);
		dig_v = (v[2] == 0.00) ? 0 : 1;
        if(dig_v == 0)
        {
        	break;
        }        
        
    }
}

void mode2(void)
{   
    int i;
    LCDprint("Mode 2", 1, 1);  
    while(1)
    {   
    	i = pwm_txd(4, 9, 6);
    	if(i)
    	{
    		break;
    	}
    	/*
    	i = pwm_txd(4, 1, 2);
    	if(i)
    	{
    		break;
    	}
    	i = pwm_txd(9, 4, 6);
    	if(i)
    	{
    		break;
    	}
    	i = pwm_txd(4, 1, 3);
    	if(i)
    	{
    		break;
    	}
    	i = pwm_txd(9, 4, 6);
    	if(i)
    	{
    		break;
    	}
    	i = pwm_txd(4, 1, 3);
    	if(i)
    	{
    		break;
    	}
    	i = pwm_txd(9, 4, 6);
    	if(i)
    	{
    		break;
    	}*/
    	
    	
    }
}


void mode3(void)
{
	int i;
	LCDprint("Mode 3", 1, 1);
	while(1)
	{	
		i = pwm_txd(4, 0 , 6);
    	if(i)
    	{
    		break;
    	}
    	/*i = pwm_txd(9, 4, 6);
    	if(i)
    	{
    		break;
    	}
    	i = pwm_txd(4, 7, 2);
    	if(i)
    	{
    		break;
    	}
    	i = pwm_txd(9, 4, 6);
    	if(i)
    	{
    		break;
    	}
    	i = pwm_txd(4, 7, 2);
    	if(i)
    	{
    		break;
    	}
    	i = pwm_txd(9, 4, 6);
    	if(i)
    	{
    		break;
    	}*/
			
	}
}


void main (void)
{		
    char a='0';
	int sum=0; 
	float freq_index=0.0;
	//float v[3];
	//float x,y;
	//int xx,yy,dig_v;
	float voltage;
	int v1;
	waitms(500);
	printf("\r\nJDY-40 test\r\n");
	UART1_Init(9600);
	
	InitPinADC(2, 2); // Configure P2.2 as analog input
	InitPinADC(2, 3); // Configure P2.3 as analog input
	InitPinADC(2, 4); // Configure P2.4 as analog input
    InitADC();
    LCD_4BIT();
    
	// To configure the device (shown here using default values).
	// For some changes to take effect, the JDY-40 needs to be power cycled.
	// Communication can only happen between devices with the
	// same RFID and DVID in the same channel.
	
	//SendATCommand("AT+BAUD4\r\n");
	//SendATCommand("AT+RFID8899\r\n");
	//SendATCommand("AT+DVID1122\r\n"); // Default device ID.
	//SendATCommand("AT+RFC001\r\n");
	//SendATCommand("AT+POWE9\r\n");
	//SendATCommand("AT+CLSSA0\r\n");
	
	// We should select an unique device ID.  The device ID can be a hex
	// number from 0x0000 to 0xFFFF.  In this case is set to 0xABBA
	SendATCommand("AT+DVID1314\r\n");  

	// To check configuration
	SendATCommand("AT+VER\r\n");
	SendATCommand("AT+BAUD\r\n");
	SendATCommand("AT+RFID1314\r\n");
	SendATCommand("AT+DVID1314\r\n");
	SendATCommand("AT+RFC007\r\n");
	SendATCommand("AT+POWE\r\n");
	SendATCommand("AT+CLSS\r\n");
	
	//printf("\r\Press and hold the BOOT button to transmit.\r\n");
	LCDprint("Mode 1", 1, 1);

	
	
	mode0();
	
	 while (1) {
        voltage = Volts_at_Pin(QFP32_MUX_P2_4);
        v1 = (voltage == 0.00) ? 0 : 1;
        
        if (v1 == 0) {
        	waitms(300) ;
        	if (v1 == 0) {
            state = (state + 1) % 3; 
            }
        }
        

        switch (state) {
            case 0:
                mode1();
                break;
            case 1:
                mode2();
                break;
            case 2:
                mode3();
                break;
            
            default:
                continue; 
        }

	}
}