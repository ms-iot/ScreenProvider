#ifndef __LINUX_ILI9341_H 
#define __LINUX_ILI9341_H

#define ILI9341_NOP                               0x00
#define ILI9341_SOFTWARE_RESET                    0x01
#define ILI9341_READ_DISPLAY_ID_INFO              0x04
#define ILI9341_READ_DISPLAY_STATUS               0x09
#define ILI9341_READ_DISPLAY_POWER_MODE           0x0A
#define ILI9341_READ_DISPLAY_MADCTL               0x0B 
#define ILI9341_READ_DISPLAY_PIX_FORMAT           0x0C
#define ILI9341_READ_DISPLAY_IMA_FORMAT           0x0D
#define ILI9341_READ_DISPLAY_SIG_MODE             0x0E
#define ILI9341_READ_DISPLAY_SELF_DIAG_RESULT     0x0F
#define ILI9341_SLEEP_MODE_ON                     0x10
#define ILI9341_SLEEP_MODE_OUT                    0x11
#define ILI9341_PARTIAL_MODE_ON                   0x12
#define ILI9341_NORM_DISP_ON                      0x13

#define ILI9341_DISP_INVERS_OFF                   0x20
#define ILI9341_DISP_INVERS_ON                    0x21
#define ILI9341_GAMMA_SET                         0x26
#define ILI9341_DISP_OFF                          0x28
#define ILI9341_DISP_ON                           0x29
#define ILI9341_COLUMN_ADDRESS_SET                0x2A
#define ILI9341_PAGE_ADDRESS_SET                  0x2B
#define ILI9341_RAM_WR                            0x2C
#define ILI9341_RGB_SET                           0x2D
#define ILI9341_RAM_READ                          0x2E

#define ILI9341_PARTIAL_AREA                      0x30
#define ILI9341_VERTICAL_SCRL_DEF                 0x33
#define ILI9341_TEARING_EFF_ON                    0x34
#define ILI9341_TEARING_EFF_OFF                   0x35
#define ILI9341_MEM_ACCESS_CTRL                   0x36
#define ILI9341_VERT_SCRL_START_ADDRESS           0x37
#define ILI9341_IDLE_MODE_OFF                     0x38
#define ILI9341_IDLE_MODE_ON                      0x39
#define ILI9341_PIX_SET                           0x3A

#define ILI9341_FRM_RATE_CTRL1                    0xB1
#define ILI9341_FRM_RATE_CTRL2                    0xB2
#define ILI9341_FRM_RATE_CTRL3                    0xB3
#define ILI9341_DISP_INVERS_CTRL                  0xB4
#define ILI9341_BLANKING_PORCH_CTRL               0xB5
#define ILI9341_DISP_FUNC_CTRL                    0xB6
#define ILI9341_ENTRY_MODE_SET                    0xB7

#define ILI9341_PW_CTRL1                          0xC0
#define ILI9341_PW_CTRL2                          0xC1
#define ILI9341_VCOM_CTRL1                        0xC5
#define ILI9341_VCOM_CTRL2                        0xC7
#define ILI9341_PW_CTRLA                          0xCB
#define ILI9341_PW_CTRLB                          0xCF

#define ILI9341_READ_ID1                          0xDA
#define ILI9341_READ_ID2                          0xDB
#define ILI9341_READ_ID3                          0xDC
#define ILI9341_READ_ID4                          0xD3

#define ILI9341_POS_GAMMA_CORR                    0xE0
#define ILI9341_NEG_GAMMA_CORR                    0xE1
#define ILI9341_DIGI_GAMMA_CTRL1                  0xE2
#define ILI9341_DIGI_GAMMA_CTRL2                  0xE3
#define ILI9341_TIMING_CTRLA                      0xE8
#define ILI9341_TIMING_CTRLB                      0xEA
#define ILI9341_PWR_SEQ                           0xED

#define ILI9341_ENABLE_3GAM                       0xF2
#define ILI9341_INTERFACE_CTRL                    0xF6
#define ILI9341_PUMP_RATIO_CTRL                   0xF7

#endif /* __LINUX_ILI9341_H */
