#ifndef __FAN_CONTROLLER_H__
#define __FAN_CONTROLLER_H__

#include <chrono>
#include <vector>
#include <string>
#include <shared_mutex>
#include <mutex>

#define MAX_RECV_BUF_SIZE   1024
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

struct GlobalParams 
{
    bool                autoFlag = false;
    std::vector<int>    cardBusIdVec;
    std::shared_mutex   mutex;

    void update(bool flag, std::vector<int> busIdVec)
    {
        std::unique_lock<std::shared_mutex> lock(mutex);
        autoFlag = flag;
        cardBusIdVec.assign(busIdVec.begin(), busIdVec.end());
    }
    
    bool getMode()
    {
        std::unique_lock<std::shared_mutex> lock(mutex);
        return autoFlag;
    }

    std::vector<int> getBusIdVec()
    {
        std::unique_lock<std::shared_mutex> lock(mutex);
        return cardBusIdVec;
    }
};

extern GlobalParams             g_params;

class FanController
{
public:
    FanController(int fd);
    FanController(double kp, double ki, double kd, double integral, int fd);
    void Restart(int fd);
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
    int                                                 m_fd;
    char                                                m_recvBuf[MAX_RECV_BUF_SIZE];
    bool                                                m_criticalFlag;
    int                                                 m_curPwm;

    virtual int ReadTemp() = 0;
    virtual int CalcPwm(int& curTemp);
    void Reset();
};

class CPUController : public FanController
{
public:
    CPUController(int fd);
    void SetPwm();

protected:
    int ReadTemp();
};

class SysController : public FanController
{
public:
    SysController(int fd);
    void SetPwm();

protected:
    int ReadTemp();
};

class CardController : public FanController
{
public:
    CardController(int fd, int cardId);
    void SetPwm();

protected:
    int CalcPwm(int& curTemp);
    int ReadTemp();

private:
    bool                                                m_initFlag;
    int                                                 m_cardId;
    bool                                                m_fullFlag;
    int                                                 m_busId;
    std::string                                         m_proType;
};

#endif // __FAN_CONTROLLER_H__

