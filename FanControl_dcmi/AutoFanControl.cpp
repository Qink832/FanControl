#include <syslog.h>
#include <unistd.h>
#include "dcmi_interface_api.h"
#include "TemperatureControl.h"


int main()
{
    int ret = -1;
    TemperCtrl tempCtrl;

    ret = dcmi_init();
    if (ret != RET_OK) {
        syslog(LOG_INFO, "[ERROR] Failed to init dcmi.");
        return ret;
    }

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



