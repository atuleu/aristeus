#ifndef PIN_CONFIG_H
#define PIN_CONFIG_H

// $[CMU]
// [CMU]$

// $[PRS.ASYNCH0]
// [PRS.ASYNCH0]$

// $[PRS.ASYNCH1]
// [PRS.ASYNCH1]$

// $[PRS.ASYNCH2]
// [PRS.ASYNCH2]$

// $[PRS.ASYNCH3]
// [PRS.ASYNCH3]$

// $[PRS.ASYNCH4]
// [PRS.ASYNCH4]$

// $[PRS.ASYNCH5]
// [PRS.ASYNCH5]$

// $[PRS.ASYNCH6]
// [PRS.ASYNCH6]$

// $[PRS.ASYNCH7]
// [PRS.ASYNCH7]$

// $[PRS.ASYNCH8]
// [PRS.ASYNCH8]$

// $[PRS.ASYNCH9]
// [PRS.ASYNCH9]$

// $[PRS.ASYNCH10]
// [PRS.ASYNCH10]$

// $[PRS.ASYNCH11]
// [PRS.ASYNCH11]$

// $[PRS.SYNCH0]
// [PRS.SYNCH0]$

// $[PRS.SYNCH1]
// [PRS.SYNCH1]$

// $[PRS.SYNCH2]
// [PRS.SYNCH2]$

// $[PRS.SYNCH3]
// [PRS.SYNCH3]$

// $[GPIO]
// [GPIO]$

// $[TIMER0]
// [TIMER0]$

// $[TIMER1]
// [TIMER1]$

// $[TIMER2]
// [TIMER2]$

// $[TIMER3]
// [TIMER3]$

// $[TIMER4]
// [TIMER4]$

// $[USART0]
// [USART0]$

// $[USART1]
// [USART1]$

// $[I2C1]
// [I2C1]$

// $[PDM]
// [PDM]$

// $[LETIMER0]
// [LETIMER0]$

// $[IADC0]
// [IADC0]$

// $[I2C0]
// I2C0 SCL on PB03
#ifndef I2C0_SCL_PORT                           
#define I2C0_SCL_PORT                            SL_GPIO_PORT_B
#endif
#ifndef I2C0_SCL_PIN                            
#define I2C0_SCL_PIN                             3
#endif

// I2C0 SDA on PB04
#ifndef I2C0_SDA_PORT                           
#define I2C0_SDA_PORT                            SL_GPIO_PORT_B
#endif
#ifndef I2C0_SDA_PIN                            
#define I2C0_SDA_PIN                             4
#endif

// [I2C0]$

// $[EUART0]
// EUART0 RX on PA06
#ifndef EUART0_RX_PORT                          
#define EUART0_RX_PORT                           SL_GPIO_PORT_A
#endif
#ifndef EUART0_RX_PIN                           
#define EUART0_RX_PIN                            6
#endif

// EUART0 TX on PA05
#ifndef EUART0_TX_PORT                          
#define EUART0_TX_PORT                           SL_GPIO_PORT_A
#endif
#ifndef EUART0_TX_PIN                           
#define EUART0_TX_PIN                            5
#endif

// [EUART0]$

// $[PTI]
// [PTI]$

// $[MODEM]
// [MODEM]$

// $[CUSTOM_PIN_NAME]
#ifndef _PORT                                   
#define _PORT                                    SL_GPIO_PORT_A
#endif
#ifndef _PIN                                    
#define _PIN                                     0
#endif










#ifndef LPS22DF_INT_PORT                        
#define LPS22DF_INT_PORT                         SL_GPIO_PORT_B
#endif
#ifndef LPS22DF_INT_PIN                         
#define LPS22DF_INT_PIN                          1
#endif














// [CUSTOM_PIN_NAME]$


#endif // PIN_CONFIG_H


