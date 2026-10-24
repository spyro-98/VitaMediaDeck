#ifndef VMD_TEST_POWER_H
#define VMD_TEST_POWER_H
#define SCE_POWER_CB_SYSTEM_SUSPEND 0x10000
#define SCE_POWER_CB_SYSTEM_RESUME 0x40000
#define SCE_POWER_CB_APP_RESUME 0x200000
#define SCE_POWER_CB_APP_SUSPEND 0x400000
int scePowerRegisterCallback(int);
int scePowerUnregisterCallback(int);
#endif
