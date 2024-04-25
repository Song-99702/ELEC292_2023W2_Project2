#include "../Common/Include/stm32l051xx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../Common/Include/serial.h"
#include "UART2.h"

#define SYSCLK 32000000L
#define DEF_F 15000L

volatile int PWM_Counter = 0;
volatile unsigned char pwm1=0, pwm2=0;
volatile int pwmx;
volatile int pwmy;
#define PA8 (GPIOA->IDR & BIT8)
#define PIN_PERIOD (GPIOA->IDR&BIT8)
#define PIN_SONAR (GPIOA->IDR&BIT6)

// LQFP32 pinout
//             ----------
//       VDD -|1       32|- VSS
//      PC14 -|2       31|- BOOT0
//      PC15 -|3       30|- PB7
//      NRST -|4       29|- PB6
//      VDDA -|5       28|- PB5
//       PA0 -|6       27|- PB4
//       PA1 -|7       26|- PB3
//       PA2 -|8       25|- PA15 (Used for RXD of UART2, connects to TXD of JDY40)
//       PA3 -|9       24|- PA14 (Used for TXD of UART2, connects to RXD of JDY40)
//       PA4 -|10      23|- PA13 (Used for SET of JDY40)
//       PA5 -|11      22|- PA12
//       PA6 -|12      21|- PA11
//       PA7 -|13      20|- PA10 (Reserved for RXD of UART1)
//       PB0 -|14      19|- PA9  (Reserved for TXD of UART1)
//       PB1 -|15      18|- PA8  (pushbutton)
//       VSS -|16      17|- VDD
//             ----------

#define F_CPU 32000000L
// Uses SysTick to delay <us> micro-seconds. 
void Delay_us(unsigned char us)
{
	// For SysTick info check the STM32L0xxx Cortex-M0 programming manual page 85.
	SysTick->LOAD = (F_CPU/(1000000L/us)) - 1;  // set reload register, counter rolls over from zero, hence -1
	SysTick->VAL = 0; // load the SysTick counter
	SysTick->CTRL  = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk; // Enable SysTick IRQ and SysTick Timer */
	while((SysTick->CTRL & BIT16)==0); // Bit 16 is the COUNTFLAG.  True when counter rolls over from zero.
	SysTick->CTRL = 0x00; // Disable Systick counter
}

void waitms (unsigned int ms)
{
	unsigned int j;
	unsigned char k;
	for(j=0; j<ms; j++)
		for (k=0; k<4; k++) Delay_us(250);
}

void Hardware_Init(void)
{
	GPIOA->OSPEEDR=0xffffffff; // All pins of port A configured for very high speed! Page 201 of RM0451


	RCC->IOPENR |= (BIT0 | BIT1); // Peripheral clock enable for port A and b
	//RCC->IOPENR |= BIT0; // peripheral clock enable for port A

    GPIOA->MODER = (GPIOA->MODER & ~(BIT27|BIT26)) | BIT26; // Make pin PA13 output (page 200 of RM0451, two bits used to configure: bit0=1, bit1=0))
	GPIOA->ODR |= BIT13; // 'set' pin to 1 is normal operation mode.
    
	GPIOA->MODER = (GPIOA->MODER & ~(BIT6|BIT7)) | BIT6;    // PA3
	GPIOA->OTYPER &= ~BIT3; // Push-pull
    GPIOA->MODER = (GPIOA->MODER & ~(BIT8|BIT9)) | BIT8;    // PA4
	GPIOA->OTYPER &= ~BIT4; // Push-pull
    GPIOA->MODER = (GPIOA->MODER & ~(BIT10|BIT11)) | BIT10; // PA5
	GPIOA->OTYPER &= ~BIT5; // Push-pull
	GPIOA->MODER = (GPIOA->MODER & ~(BIT14|BIT15)) | BIT14; // PA7
	GPIOA->OTYPER &= ~BIT7; // Push-pull
	

	GPIOA->MODER &= ~(BIT16 | BIT17); // Make pin PA8 input
	// Activate pull up for pin PA8:
	GPIOA->PUPDR |= BIT16; 
	GPIOA->PUPDR &= ~(BIT17);
	
	GPIOA->MODER &= ~(BIT12 | BIT13); // Make pin PA6 input
	// Activate pull up for pin PA6:
	GPIOA->PUPDR |= BIT12; 
	GPIOA->PUPDR &= ~(BIT13); 
	
	GPIOA->MODER &= ~(BIT2 | BIT3); // Make pin PA1 input PIN7
	// Activate pull up for pin PA1:
	GPIOA->PUPDR |= BIT2; 
	GPIOA->PUPDR &= ~(BIT3);
}

void pwm_harware(void)
{
	// Set up output pins
    GPIOB->MODER = (GPIOB->MODER & ~(BIT15|BIT14)) | BIT14; // Make pin Pb7 output
    //GPIOB->OTYPER &= ~BIT7; //push_pull-for
    GPIOB->MODER = (GPIOB->MODER & ~(BIT13|BIT12)) | BIT12; // Make pin Pb6 output
    //PIOB->OTYPER &= ~BIT6; //push_pull - back
    
    GPIOB->ODR &= ~BIT6; // Set Pb8 low
    
    GPIOB->MODER = (GPIOB->MODER & ~(BIT11|BIT10)) | BIT10; // Make pin Pb5 output
    //GPIOB->OTYPER &= ~BIT5; //push_pull - for
    GPIOB->MODER = (GPIOB->MODER & ~(BIT9|BIT8)) | BIT8; // Make pin Pb4 output
    //GPIOA->OTYPER &= ~BIT4; //push_pull - back
    
    GPIOB->ODR &= ~BIT4; // Set Pb8 low

	// Set up timer
	RCC->APB1ENR |= BIT0;  // turn on clock for timer2 (UM: page 177)
	TIM2->ARR = F_CPU/DEF_F-1;
	NVIC->ISER[0] |= BIT15; // enable timer 2 interrupts in the NVIC
	TIM2->CR1 |= BIT4;      // Downcounting    
	TIM2->CR1 |= BIT7;      // ARPE enable    
	TIM2->DIER |= BIT0;     // enable update event (reload event) interrupt 
	TIM2->CR1 |= BIT0;      // enable counting    
	
	__enable_irq();
}

void SendATCommand (char * s)
{
	char buff[40];
	printf("Command: %s", s);
	GPIOA->ODR &= ~(BIT13); // 'set' pin to 0 is 'AT' mode.
	waitms(10);
	eputs2(s);
	egets2(buff, sizeof(buff)-1);
	GPIOA->ODR |= BIT13; // 'set' pin to 1 is normal operation mode.
	waitms(10);
	printf("Response: %s", buff);
}

void forward_ini(void)
{
	GPIOB->OTYPER &= ~BIT7; // set to push and pull for square wave
	GPIOB->OTYPER &= ~BIT5;
	
	GPIOB->ODR &= ~BIT6; //set to 0
	GPIOB->ODR &= ~BIT4;
}

void backward_ini(void)
{
	GPIOB->OTYPER &= ~BIT6;
	GPIOB->OTYPER &= ~BIT4;
	
	GPIOB->ODR &= ~BIT7;
	GPIOB->ODR &= ~BIT5;
}


void TIM2_Handler(void) 
{
	TIM2->SR &= ~BIT0; // clear update interrupt flag
	PWM_Counter++;

    if(pwmy >= 0){
	if(pwm1>PWM_Counter){
		GPIOB->ODR |= BIT7;
	    }
	else{
		GPIOB->ODR &= ~BIT7;
	    }
	
	if(pwm2>PWM_Counter){
		GPIOB->ODR |= BIT5;
	    }
	else{
		GPIOB->ODR &= ~BIT5;
	    }
	
	if (PWM_Counter > 255)
        {
		PWM_Counter=0;
		GPIOB->ODR |= (BIT7|BIT5);
	    }
	}

    if(pwm1 ==0  && pwm2 ==0 )
    {
		GPIOB->ODR &= ~BIT6;
		GPIOB->ODR &= ~BIT4;
		GPIOB->ODR &= ~BIT7;
		GPIOB->ODR &= ~BIT5;
	}

    
    if(pwmy <0){
	if(pwm1>PWM_Counter){
		GPIOB->ODR |= BIT6;
	    }
	else{
		GPIOB->ODR &= ~BIT6;
	    }
	
	if(pwm2>PWM_Counter){
		GPIOB->ODR |= BIT4;
	    }
	else{
		GPIOB->ODR &= ~BIT4;
	    }
	
	if (PWM_Counter > 255){
		PWM_Counter=0;
		GPIOB->ODR |= (BIT6|BIT4);
        }
	}
}

void process_buff(const char *buff, char *xbuff, char *ybuff) {
    static char last_x[1] = "1"; // Static storage for last good 'x' value
    static char last_y[1] = "1"; // Static storage for last good 'y' value
    int x, y;
    char postCheck;

    // Validate format: exactly one ',' and x, y are single digits (0-9)
    if (sscanf(buff, "%1d,%1d", &x, &y, &postCheck) == 2) {
        // Successfully parsed x and y as single digits without extra characters following
        sprintf(xbuff, "%d", x); // Convert integer x to string and store in xbuff
        sprintf(ybuff, "%d", y); // Convert integer y to string and store in ybuff

        // Update last good values
        strcpy(last_x, xbuff);
        strcpy(last_y, ybuff);
    } else {
        // Parsing failed or validation not passed, fallback to last good values
        strcpy(xbuff, last_x);
        strcpy(ybuff, last_y);
    }
}

long int GetPeriod (int n)
{
	__disable_irq();
	int i;
	unsigned int saved_TCNT1a, saved_TCNT1b;
	
	SysTick->LOAD = 0xffffff;  // 24-bit counter set to check for signal present
	SysTick->VAL = 0xffffff; // load the SysTick counter
	SysTick->CTRL  = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk; // Enable SysTick IRQ and SysTick Timer */
	while (PIN_PERIOD!=0) // Wait for square wave to be 0
	{
		if(SysTick->CTRL & BIT16) return 0;
	}
	SysTick->CTRL = 0x00; // Disable Systick counter

	SysTick->LOAD = 0xffffff;  // 24-bit counter set to check for signal present
	SysTick->VAL = 0xffffff; // load the SysTick counter
	SysTick->CTRL  = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk; // Enable SysTick IRQ and SysTick Timer */
	while (PIN_PERIOD==0) // Wait for square wave to be 1
	{
		if(SysTick->CTRL & BIT16) return 0;
	}
	SysTick->CTRL = 0x00; // Disable Systick counter
	
	SysTick->LOAD = 0xffffff;  // 24-bit counter reset
	SysTick->VAL = 0xffffff; // load the SysTick counter to initial value
	SysTick->CTRL  = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk; // Enable SysTick IRQ and SysTick Timer */
	for(i=0; i<n; i++) // Measure the time of 'n' periods
	{
		while (PIN_PERIOD!=0) // Wait for square wave to be 0
		{
			if(SysTick->CTRL & BIT16) return 0;
		}
		while (PIN_PERIOD==0) // Wait for square wave to be 1
		{
			if(SysTick->CTRL & BIT16) return 0;
		}
	}
	SysTick->CTRL = 0x00; // Disable Systick counter
	
	__enable_irq();
	return 0xffffff-SysTick->VAL;
}


long int GetPulse (void)
{
	__disable_irq();
	unsigned int saved_TCNT1a, saved_TCNT1b;
	
	SysTick->LOAD = 0xffffff;  // 24-bit counter set to check for signal present
	SysTick->VAL = 0xffffff; // load the SysTick counter
	SysTick->CTRL  = SysTick_CTRL_CLKSOURCE_Msk| SysTick_CTRL_ENABLE_Msk; // Enable SysTick IRQ and SysTick Timer */
	while (PIN_SONAR!=0) // Wait signal to be 0
	{
		if(SysTick->CTRL & BIT16) return 0;
	}
	SysTick->CTRL = 0x00; // Disable Systick counter

	SysTick->LOAD = 0xffffff;  // 24-bit counter set to check for signal present
	SysTick->VAL = 0xffffff; // load the SysTick counter
	SysTick->CTRL  = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk; // Enable SysTick IRQ and SysTick Timer */
	while (PIN_SONAR==0) // Wait for signal to be 1
	{
		if(SysTick->CTRL & BIT16) return 0;
	}
	SysTick->CTRL = 0x00; // Disable Systick counter
	
	SysTick->LOAD = 0xffffff;  // 24-bit counter reset
	SysTick->VAL = 0xffffff; // load the SysTick counter to initial value
	SysTick->CTRL  = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk; // Enable SysTick IRQ and SysTick Timer */

	while (PIN_SONAR!=0) // Wait for square wave to be 0
	{
		if(SysTick->CTRL & BIT16) return 0;
	}

	SysTick->CTRL = 0x00; // Disable Systick counter

	__enable_irq();
	return 0xffffff-SysTick->VAL;
}


#define PA4_0 (GPIOA->ODR &= ~BIT4)
#define PA4_1 (GPIOA->ODR |=  BIT4)
#define PA5_0 (GPIOA->ODR &= ~BIT5)
#define PA5_1 (GPIOA->ODR |=  BIT5)

#define PA3_0 (GPIOA->ODR &= ~BIT3)
#define PA3_1 (GPIOA->ODR |=  BIT3)
#define PA7_0 (GPIOA->ODR &= ~BIT7)
#define PA7_1 (GPIOA->ODR |=  BIT7)




int main(void){
	char buff[10];
	char buffs[30];
    int cnt=0;
    
    //pwm
    int npwm;
    int power;
    char xbuff[1];
    char ybuff[1];
    int tempx;
    int tempy;

    //metal dector
    int level;
    long int ini_f = 1000000;
    long int ini_t = 0;
    int i;
    int flag = 1;
    long int count = 0;
	long int f;
    int pre_y = 4;
    
    //sonar
    long int pulse1;
    int state = 0;

	Hardware_Init();
	pwm_harware();
	initUART2(9600);
	
	waitms(1000); // Give putty some time to start.
	printf("\r\nJDY-40 test\r\n");

	// We should select an unique device ID.  The device ID can be a hex

	SendATCommand("AT+DVID1314\r\n");  

	// To check configuration
	SendATCommand("AT+VER\r\n");
	SendATCommand("AT+BAUD\r\n");
	SendATCommand("AT+RFID1314\r\n");
	SendATCommand("AT+DVID1314\r\n");
	SendATCommand("AT+RFC007\r\n");
	SendATCommand("AT+POWE\r\n");
	SendATCommand("AT+CLSS\r\n");
	
	
	
	printf("\r\nPress and hold a push-button attached to PA8 (pin 18) to transmit.\r\n");
	//__disable_irq();
		PA3_0;
		PA4_0;
		PA5_0;
		PA7_0;
		//PB3_1;
	
	cnt=0;
	//state =0;
	//printf("%d",state);
    
	while(1){
		
		count=GetPeriod(30);
		//pwm_harware();
		//__disable_irq();
		//__enable_irq();
		//count = 0;
		//pwm_harware();
		if(count>0)
		{
			f=(F_CPU*30)/count;
			
            if (flag) {
                   for (int i = 0; i < 30; i++) {
                    if(ini_f > f){
                        ini_f = f;
                    }
            	flag = 0; 
            }

			//printf("inif: %d, f: %d\n\r",ini_f,f);
		}
			//printf("inif: %d, f: %d\n\r",ini_f,f);
			//eputs("f=");
			//PrintNumber(ini_f, 10, 7);
			//eputs("Hz, count=");
			//PrintNumber(f, 10, 7);
			//eputs("          \r\n");
		}
			
				
			
			
			//sprintf(buffs, "  12\r\n");
			//eputs2(buffs);
			//waitms(20);
		if (f<ini_f){
		eputs("0\n\r");
		sprintf(buffs, "000\r\n");
		eputs2(buffs);
	    waitms(20);
		}
		if(f>ini_f&&f<ini_f+90){
		eputs("1\n\r");
		sprintf(buffs, "111\r\n");
		eputs2(buffs);
	    waitms(20);
		}
		if(f>=ini_f+90&&f<ini_f+180){
		sprintf(buffs, "222\r\n");
		eputs2(buffs);
	    waitms(20);
		//eputc2('2\n\r');
		}
		if(f>=ini_f+180){
		eputs("3\n\r");
		sprintf(buffs, "333\r\n");
		eputs2(buffs);
	    waitms(20);
		}
		
		if (ReceivedBytes2()>0){
			egets2(buff, sizeof(buff)-1);
			//printf("%s\n\r", buff);

            process_buff(buff, xbuff, ybuff);
            
            tempx = atoi(xbuff);
    		tempy = atoi(ybuff);
    		
            printf("x:%d y:%d\n\r", tempx, tempy);
            
            if (tempy > 4){
    		    double resulty = (tempy-4.0)/5.0*100.00;
				pwmy = (int)resulty;
    		}
    		if (tempy < 4){
    			double resulty = (tempy-4.0)/4.0*100.00;
				pwmy = (int)resulty;
				}
            if(tempy == 4){
                pwmy = 0;
            }
            
            double resultx = (tempx-4.0)/4.0*100.00;
			pwmx = (int)resultx;
			
			if(pwmx>100){
			pwmx=100;
			}
			
			//printf("x:%d\n\r",pwmx);
			//printf("y:%d\n\r",pwmy);


			/*if(pwmy > 0 && pwmy < pre_y){
			 PA7_1;
			 waitms(300);
			}
			if(pwmy == 0 && pwmx ==0){
				PA7_1;
			}
			else{
			PA7_0;
			}*/
			
			if(pwmy < 0){
				PA3_1;
			}
			if(pwmy>=0){
				PA3_0;
			}
			
			if(pwmx == 0){
				PA4_0;
				PA5_0;
			}
			if(pwmx<0){
				PA5_0;
				PA4_1;
				}
			if(pwmx>0){
				PA4_0;
				PA5_1;
				}


			pre_y = pwmy;

            if(pwmy > 0){
            
 			forward_ini();
 			
 			double resultp = 255.0*abs(pwmy)/100.0;
 			power = (int)resultp;
			//printf("%d\n",power);
 			if (pwmx > 0){
 			    //eputs("right \n");
 			    double result1 = power - (power * abs(pwmx) / 100.0);
 			    pwm1 = (int)result1;
 			    //printf("1:%d\n\r",pwm1);
 			    pwm2 = power;
 			    //printf("2:%d\n\r",pwm2);
 			    }
 			else if(pwmx < 0){
 			    pwm1 = power;
 			    //printf("1:%d\n\r",pwm1);
 			    double result = power - (power * abs(pwmx) / 100.0);
 			    pwm2 = (int)result;
 			    //printf("2:%d\n\r",pwm2);
 			    }
 			if(pwmx == 0){
 			    //eputs("right\n\n");
 			    pwm1 = power;
 			    //eputs("%d",pwm1\n);
 			    pwm2 = power-10;
 			    //eputs("%d",pwm2\n);
 			}
 		}

        if(pwmy <0){
 			backward_ini();
 			double resultp = 255.0*abs(pwmy)/100.0;
 			power = (int)resultp;
			//printf("%d\n",power);
 			if (pwmx > 0){
 			//eputs("right \n");
 			double result1 = power - (power * abs(pwmx) / 100.0);
 			pwm1 = (int)result1-8;
 			//printf("1:%d\n\r",pwm1);
 			pwm2 = power;
 			//printf("2:%d\n\r",pwm2);
 			}
 			else if(pwmx < 0){
 			pwm1 = power-8;
 			//printf("1:%d\n\r",pwm1);
 			double result = power - (power * abs(pwmx) / 100.0);
 			pwm2 = (int)result;
 			//printf("2:%d\n\r",pwm2);
 			}
 			if(pwmx==0){
 			//eputs("right\n\n");
 			pwm1 = power;
 			//eputs("%d",pwm1\n);
 			pwm2 = power-10;
 			//eputs("%d",pwm2\n);
 			}
 		}

        if(pwmy ==0){
			
 			forward_ini();
 			double result = 255.0*abs(pwmx)/100.0;
 			power = (int)result;
 			//printf("power:%d\n\r",power);
			if(pwmx > 0){
				pwm1 = 0;
				pwm2 = power;
				//eputs("right\n");
			}
			else if (pwmx < 0){
				pwm1 = power;
				pwm2 = 0;
			}
			if(pwmx ==0){
				pwm1 = 0;
				//printf("right %d\n",pwm1);
				pwm2 = 0;
				//printf("%d\n",pwm1);
				}
			}
			if(pwm1<0){
			pwm1 = 0;
			}
		}
		printf("%d\r\n",state);
		
		
		if((GPIOA->IDR&BIT1)==0){
			waitms(200);
			if((GPIOA->IDR&BIT1)==0){
				state++;
				}
		}
			
		//else{
		//	printf("Nothing transmitted.\n\r");
		//	waitms(500);
		//}
		
		while(state == 1){
		//printf("%d\r\n",state);
			backward_ini();
			
			if(PIN_SONAR!=0){
			pulse1=GetPulse();
			}
   			
   			printf("T=%d       \r", pulse1);
   			
  			pwm1 = 100;
    		pwm2 = 100;
   			if(pulse1 <= 9600){
   			  	backward_ini();
   	 			printf("in if");
    			pwm1 = 0;
    			pwm2 = 0;

    		//waitms(200);
    		}
		if((GPIOA->IDR&BIT1)==0){
			waitms(200);
			if((GPIOA->IDR&BIT1)==0){
				state++;
				break;
				}
			}
		 
		}
		//pin7
		while(state == 2){
		printf("s3");
			backward_ini();
			
			if(PIN_SONAR!=0){
			pulse1=GetPulse();
			}
   			
   			printf("3T=%d       \r", pulse1);
   			pwm1 = 0;
   			pwm2 = 0;
   			
   			if(pulse1 >= 10000){
   			  	backward_ini();
   	 			printf("in if");
    			pwm1 = 100;
    			pwm2 = 100;

    		//waitms(200);
    		}
    		
    		
   				
		}
	}

}
