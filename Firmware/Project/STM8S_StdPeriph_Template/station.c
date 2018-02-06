#include "stm8s.h"
#include <stdlib.h>
#include "station.h" 
#include "pid.h"
#include "7-seg.h"
#include "button.h"
#include "thermo.h"
#include "control.h"

#pragma location=FLASH_DATA_START_PHYSICAL_ADDRESS 
__no_init volatile uint16_t eeSetpoint;

uint8_t prediv = 0;
uint32_t tempaccum;
uint16_t timedivider;
static uint16_t display_setpoint=DISPLAY_SETPOINT_TIMEOUT;
uint16_t Temperature = 0;
uint16_t temp_prev, temp_curr = 0;
extern uint16_t GetAdcValue(ADC1_Channel_TypeDef channel);
static uint16_t Power = 0;  
static uint32_t SecondTick = 0;
pid_t pid_s;
uint16_t Setpoint=150;
uint16_t *lcddata;
uint8_t StbyMode=FALSE;

typedef struct {
	int32_t q; //process noise covariance
	int32_t r; //measurement noise covariance
	int32_t x; //value
	int32_t p; //estimation error covariance
	int32_t k; //kalman gain
} kalman_state;

kalman_state result_x;

void kalman_update (int32_t measurement)
{
	result_x.p = result_x.p + result_x.q;	
	//measurement update
	result_x.k = result_x.p / (result_x.p + result_x.r);
	result_x.x = result_x.x + result_x.k * (measurement - result_x.x);
	result_x.p = (1000UL - result_x.k) * result_x.p;  
}

short kalman_get_x (int32_t v)
{
	kalman_update ((int32_t) v);
	
	return (int32_t) result_x.x;
}

int kalman_init ()
{
	result_x.q = 1;
	result_x.r = 1000;
	result_x.p = 0; //0.22
	result_x.x = 0;
	result_x.k = 0;
  
	return 0;
}

void Soldering_TIM2_Config(void)
{
  /* Time base configuration */
  TIM2_TimeBaseInit(TIM2_PRESCALER_32, 512);

  /* PWM1 Mode configuration: Channel3 */         
  TIM2_OC3Init(TIM2_OCMODE_PWM1, TIM2_OUTPUTSTATE_ENABLE, 0, TIM2_OCPOLARITY_HIGH);
  TIM2_OC3PreloadConfig(ENABLE);

//    /* PWM1 Mode configuration: Channel3 */         
//  TIM2_OC2Init(TIM2_OCMODE_PWM1, TIM2_OUTPUTSTATE_ENABLE, 0, TIM2_OCPOLARITY_HIGH);
//  TIM2_OC2PreloadConfig(ENABLE);
  
  TIM2_ARRPreloadConfig(ENABLE);

  /* TIM2 enable counter */
  TIM2_Cmd(ENABLE);
}

void Soldering_ADC_Config (void)
{
  /*  Init GPIO for ADC2 */
  GPIO_Init(ADC_GPIO_PORT, ADC_GPIO_PIN, GPIO_MODE_IN_FL_NO_IT);
  
  // Deinit ADC
  ADC1_DeInit();
  
  ADC1_Init(
    ADC1_CONVERSIONMODE_CONTINUOUS,
    ADC1_CHANNEL_4,
    ADC1_PRESSEL_FCPU_D4,
    ADC1_EXTTRIG_TIM, DISABLE,
    ADC1_ALIGN_RIGHT,
    ADC1_SCHMITTTRIG_CHANNEL4, DISABLE);

  ADC1_Cmd(ENABLE);
}

void Soldering_Main(void)
{
  Control_Init();
  
  if ((eeSetpoint > 450) || (eeSetpoint < 150)) eeSetpoint = 150;
  
  //����������� �������� ������� �� EEPROM
  Setpoint = eeSetpoint;
  
  Control_SetT(Setpoint);
  
//  pid_s.KP = 8; //8
//  pid_s.KI = 22; //22
//  pid_s.KD = 4; //4
//  pid_s.KT = 30; //30
  
  //kalman_init();

  while(1) 
  {
  if ((eButtonGetEvent(BUTTON_KEY) == eButtonEventHold)) {
      switch(StbyMode)
      {
      case MODE_WORKING:
        StbyMode = MODE_POWEROFF;
        break;
      case MODE_STANDBY:        
        StbyMode = MODE_WORKING;
        break;
      case MODE_POWEROFF:
        StbyMode = MODE_WORKING;
        break;
      }
      //if (StbyMode == MODE_WORKING) StbyMode = MODE_POWEROFF; else StbyMode = FALSE;
    }
    
    uint8_t state = ENC_GetStateEncoder();

			if (state != 0) {
                          StbyMode = MODE_WORKING;
                          SecondTick = 0;
                          
                          display_setpoint = DISPLAY_SETPOINT_TIMEOUT;
                          
				if (state == RIGHT_SPIN) {
					Setpoint+=5;
                                        if (Setpoint >= 450) Setpoint = 450;
                                        Control_SetT(Setpoint); 
				}
				if (state == LEFT_SPIN) {
                                        Setpoint-=5;
                                        if (Setpoint <= 150) Setpoint = 150;
                                        Control_SetT(Setpoint); 
				}
			}
    
  }
}

int32_t varVolt = 112;
int32_t varProcess = 100;
int32_t Pc = 0;
int32_t G = 0;
int32_t P=100000;
int32_t Xp = 0.0;
int32_t Zp = 0;
int32_t Xe=0;

int32_t Kalman(int32_t voltage)
{
Pc = P+varProcess;
G = Pc/(Pc+varVolt);
P = (100000-G)*Pc;
Xp = Xe;
Zp = Xp;
Xe = G * (voltage - Zp) + Xp;

return Xe;
}

uint8_t tempcount = 0;
uint16_t microvolts;
void Soldering_ISR (void)
{
  /* In order to detect unexpected events during development,
     it is recommended to set a breakpoint on the following instruction.
  */

   timedivider++;
  
   if ((StbyMode == MODE_WORKING) || (StbyMode == MODE_STANDBY)) SecondTick++;

  if (SecondTick ==  STANDBY_TIME_MIN * 60000UL) // 5 minutes
   {
     StbyMode = MODE_STANDBY;
     //SecondTick = 0;
   }     
    
    if (SecondTick == POWEROFF_TIME_MIN * 60000UL) // 30 minutes
   {
     StbyMode = MODE_POWEROFF;
     SecondTick = 0;
   } 
  
  
   if (timedivider == 1) {
     
     GPIO_WriteLow(ADC_GPIO_PORT, ADC_GPIO_PIN);
     GPIO_Init(ADC_GPIO_PORT, ADC_GPIO_PIN, GPIO_MODE_IN_FL_NO_IT);
     //TIM2_SetCompare3(0);
     GPIO_WriteLow(CONTROL_GPIO_PORT, CONTROL_GPIO_PIN);
     ADC1_Cmd(ENABLE);
   }
   
   
   if (timedivider == 2) {
     tempaccum += GetAdcValue(ADC_SOLDER_TEMP_CHANNEL);
     //TIM2_SetCompare3(Power);
     tempcount++;
   }  
  
  
     if (timedivider == (MEASURING_INTERVAL_TICKS-Power+1)) {
     GPIO_WriteHigh(CONTROL_GPIO_PORT, CONTROL_GPIO_PIN);
   }
  
  if (display_setpoint)
    {
      display_setpoint--;
      //������� - �� �����
      lcddata = &Setpoint;
      //���� ��������� ����� ����������� �������� �������
      if (!display_setpoint) {
        //����������� - �� �����
        lcddata = &Temperature;
        //lcddata = &Power;
        eeSetpoint = Setpoint;
      }
    }
    
    ssegWriteStr("   ", 3, SEG1);

     switch(StbyMode)
      {
      case MODE_WORKING:       
      if ((Temperature > 480) &&  !display_setpoint) 
        ssegWriteStr("---", 3, SEG1); 
        else
      ssegWriteInt(*lcddata);       
      break;
      case MODE_STANDBY:
      ssegWriteStr("Stb", 3, SEG1);
      break;     
      case MODE_POWEROFF:
      ssegWriteStr("OFF", 3, SEG1);
      break; 
      }
    
     if (tempcount == N_MEASUREMENTS_OF_TEMPERATURE) {
        
     Temperature = Kalman(Convert(tempaccum/N_MEASUREMENTS_OF_TEMPERATURE, 1));//Convert(tempaccum/3,1);//kalman_get_x(Convert(tempaccum/3,1));

      switch(StbyMode)
      {
      case MODE_WORKING:    
      Control_SetT(Setpoint);
      Control_SetTc(Temperature);
      break;
      case MODE_STANDBY:
      Control_SetT(Setpoint/2);
      Control_SetTc(Temperature);
      break;
      case MODE_POWEROFF:
      Control_SetT(0);
      Control_SetTc(Temperature); 
      break;      
      }  

     Control_Exe();
     
     Power = Control_GetP();
     
     tempaccum = 0;
     tempcount = 0;

   }
     
    if (timedivider == MEASURING_INTERVAL_TICKS) { //20
     timedivider = 0;
     ADC1_Cmd(DISABLE);
    GPIO_Init(ADC_GPIO_PORT, ADC_GPIO_PIN, GPIO_MODE_OUT_PP_HIGH_FAST);
    GPIO_WriteHigh(ADC_GPIO_PORT, ADC_GPIO_PIN);
     
   }
}