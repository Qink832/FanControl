#ifndef FAN_CONTROL_GLOBAL_H
#define FAN_CONTROL_GLOBAL_H

#include <vector>
#include <string>

enum PowerType{
    POWER_SAFE = 0,
    POWER_WARN = 1,
    POWER_DANG = 2
};

//风扇策略 间隔为5度
const int label_main[12] = {0,5,10,15,20,20,20,20,20,40,70,100};

const int label_cpu[21][3] = {{0,0,0}, {0,10,10}, {10,10,20}, {10,20,30}, {20,30,30}, {20,30,40}, 
                        {30,40,50}, {40,50,60}, {40,50,60}, {40,50,60}, {40,50,60}, {40,50,60}, 
                        {40,50,60}, {50,60,70}, {60,70,80}, {80,80,90}, {90,100,100}, {100,100,100}, 
                        {100,100,100}, {100,100,100}, {100,100,100}};

//error code
#define SERIAL_OPEN_ERROR           0xE0000001
#define SERIAL_INIT_ERROR           0xE0000002

#endif // FAN_CONTROL_GLOBAL_H



