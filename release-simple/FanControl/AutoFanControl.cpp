#include <syslog.h>
#include <unistd.h>
#include "TemperatureControl.h"


int main()
{
    int ret = -1;
    TemperCtrl tempCtrl;

    tempCtrl.Init();
    tempCtrl.Run();

    while(true)
    {
        if(tempCtrl.IsAuto())
        {
            if(tempCtrl.IsRestart())
            {
                tempCtrl.ReStart();
            }
        }
        else
        {
            tempCtrl.Close();
        }

        sleep(1);
    }

    return 0;
}



