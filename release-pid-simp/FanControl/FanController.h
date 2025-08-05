#ifndef __FAN_CONTROLLER_H__
#define __FAN_CONTROLLER_H__

#include <chrono>
#include <string>
#include <shared_mutex>
#include <mutex>
#include <vector>

#define MAX_RECV_BUF_SIZE   1024
#define MODE_FILE_PATH  "/etc/FanControlParams.json"
// #define _PRINT_SYS_LOG
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
    std::vector<int>    cardPwmVec;
    std::shared_mutex   mutex;

    void update(bool flag, std::vector<int> pwmVec)
    {
        std::unique_lock<std::shared_mutex> lock(mutex);
        autoFlag = flag;
        cardPwmVec.assign(pwmVec.begin(), pwmVec.end());
    }
    
    bool getMode()
    {
        std::unique_lock<std::shared_mutex> lock(mutex);
        return autoFlag;
    }

    std::vector<int> getPwmVec()
    {
        std::unique_lock<std::shared_mutex> lock(mutex);
        return cardPwmVec;
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

    virtual float ReadTemp() = 0;
    virtual int CalcPwm(float& curTemp);
    void Reset();
};

class CPUController : public FanController
{
public:
    CPUController(int fd);
    void SetPwm();

protected:
    float ReadTemp();
    void SetCardPwm();

    std::vector<int>    cardPwmVec;
};

class SysController : public FanController
{
public:
    SysController(int fd);
    void SetPwm();

protected:
    float ReadTemp();

private:
    const int pwm_safe = 10;
    const int pwm_warn = 20;
    const int pwm_dang = 100;
};

#endif // __FAN_CONTROLLER_H__

