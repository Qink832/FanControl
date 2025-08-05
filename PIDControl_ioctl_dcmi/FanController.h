#ifndef __FAN_CONTROLLER_H__
#define __FAN_CONTROLLER_H__

#include <chrono>
#include <vector>
#include <string>
#include <shared_mutex>
#include <mutex>

#define MAX_CARD_NUM    8
#define MODE_FILE_PATH  "/etc/FanControlParams.json"
#define _PRINT_SYS_LOG
// log 默认是 char*
#ifdef _PRINT_SYS_LOG
    #define IF_COND_FAIL(cond, log, todo) \
        do { \
            if (!(cond)) { \
                syslog(LOG_INFO, "%s", log); \
                todo; \
            } \
        } while (0)
#else
    #define IF_COND_FAIL(cond, log, todo) \
        do { \
            if (!(cond)) { \
                std::cerr << log << std::endl; \
                todo; \
            } \
        } while (0)
#endif //_PRINT_SYS_LOG

struct sio_ioctl_data {
    unsigned char fan_num;
    unsigned char fan_mode;
    unsigned char duty;
};

#define IOC_MAGIC 'c'
#define IOC_COMMAND_SET _IOW(IOC_MAGIC,0,struct sio_ioctl_data)
#define IOC_COMMAND_GET _IOWR(IOC_MAGIC,1,int)
#define IOC_COMMAND_RPM _IOWR(IOC_MAGIC,2,int)


struct GlobalParams 
{
    bool                autoFlag = false;
    // std::vector<int>    cardBusIdVec;
    std::shared_mutex   mutex;

    // void update(bool flag, std::vector<int> busIdVec)
    void update(bool flag)
    {
        std::unique_lock<std::shared_mutex> lock(mutex);
        autoFlag = flag;
        // cardBusIdVec.assign(busIdVec.begin(), busIdVec.end());
    }
    
    bool getMode()
    {
        std::unique_lock<std::shared_mutex> lock(mutex);
        return autoFlag;
    }

    // std::vector<int> getBusIdVec()
    // {
    //     std::unique_lock<std::shared_mutex> lock(mutex);
    //     return cardBusIdVec;
    // }
};

extern GlobalParams             g_params;

class FanController
{
public:
    FanController();
    FanController(double kp, double ki, double kd, double integral, int fd);
    void Restart();
    virtual void SetPwm() = 0;
    void SetPidParams(double kp, double ki, double kd, double integral);

protected:
    double                                              m_kp;
    double                                              m_ki;
    double                                              m_kd;
    double                                              m_integral;
    double                                              m_prevError;
    std::chrono::time_point<std::chrono::steady_clock>  m_lastTime;
    int                                                 m_curTemp;
    bool                                                m_criticalFlag;
    int                                                 m_curPwm;

    // virtual int CalcPwm(float& curTemp);    
    virtual float ReadTemp() = 0;
    void Reset();
};


class SysController : public FanController
{
public:
    SysController();
    void SetPwm();

protected:
    float ReadTemp();

private:
    int CalcPwm(float& curTemp);
};



class CardController : public FanController
{
public:
    CardController();
    void SetPwm();
    bool Init() { return m_initFlag; }

protected:
    int CalcPwm(int index, int& curTemp);
    float ReadTemp();

private:
    bool                                                m_initFlag;
    // int                                                 m_cardId;
    bool                                                m_fullFlag;
    int                                                 m_cardList[MAX_CARD_NUM];
    int                                                 m_cardNum;
    double                                              m_kpList[MAX_CARD_NUM];
    double                                              m_kiList[MAX_CARD_NUM];
    double                                              m_kdList[MAX_CARD_NUM];
    int                                                 m_busIdList[MAX_CARD_NUM];
    std::string                                         m_proTypeList[MAX_CARD_NUM];
};

#endif // __FAN_CONTROLLER_H__

