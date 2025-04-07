#ifndef TEMPER_CTRL_H
#define TEMPER_CTRL_H

#include <pthread.h>

#define RET_OK 0

class TemperCtrl{

public:
    TemperCtrl();
    ~TemperCtrl();

    bool Init();
    void Run();
    bool IsAuto();
    void ReStart();
    void Close();
    bool IsRestart();

private:
    bool            m_autoFlag;
    bool            m_reStartFlag;
    pthread_t       m_modeTid;
    pthread_t       m_temperTid;
};

#endif //TEMPER_CTRL_H