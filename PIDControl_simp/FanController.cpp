#include "FanController.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <syslog.h>
#include <string.h>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <cmath> 

#define TARGET_TEMP 70.0
#define SAFE_TEMP 75.0
#define CRITICAL_TEMP 85.0

#define SAFE_SYS_TEMP 65.0
#define CRITICAL_SYS_TEMP 85.0

#define PWM_MIN 40
#define PWM_MAX 70

#define CPU_KP 5.5
#define CPU_KI 0.5
#define CPU_KD 0.1
#define CPU_INTEGRAL 0
#define CPU_MAX_POWER 95000    //单位：mW
#define CPU_TEMP_FILE_PATH "/sys/class/thermal/thermal_zone0/temp"

#define SYS_KP 7.5
#define SYS_KI 0.5
#define SYS_KD 0.1
#define SYS_INTEGRAL 0

// 300V
#define THRV_KP 7.5
#define THRV_KI 0.5
#define THRV_KD 0.1
#define THRV_INTEGRAL 0
#define THRV_MAX_POWER 720    //单位：0.1W

// 300I Pro
#define THRIPRO_KP 7.5
#define THRIPRO_KI 0.5
#define THRIPRO_KD 0.1
#define THRIPRO_INTEGRAL 0
#define THRIPRO_MAX_POWER 720    //单位：0.1W

// 300I Duo
#define THRIDUO_KP 7.5
#define THRIDUO_KI 0.5
#define THRIDUO_KD 0.1
#define THRIDUO_INTEGRAL 0
#define THRIDUO_MAX_POWER 1500    //单位：0.1W

using namespace std;
using namespace chrono;


// 通用小函数
string Int2StrPadZero(int value, int length)
{
    string result = to_string(value);
    if (result.length() < length)
    {
        result = string(length - result.length(), '0').append(result);
    }

    return result;
}

int ExecCommand(int serialFd, const char* cmd, char* recvBuf, int recvBufLen)
{
    IF_COND_FAIL(serialFd != 0, "[ERROR] Serial port /dev/fanctrl is not open.", return -1;);

    int ret = write(serialFd, cmd, strlen(cmd));
    IF_COND_FAIL(ret >= 0, (string("[ERROR] Failed to write the command. cmd: ") + cmd).data(), return ret;);

    usleep(100000); //100ms
    memset(recvBuf, 0, recvBufLen);

    ret = read(serialFd, recvBuf, recvBufLen);
    IF_COND_FAIL(ret >= 0, (string("[ERROR] Failed to read the result. cmd: ") + cmd).data(), return ret;);

    return 0;
}

float ReadCpuTemp()
{
    float cpuTemp = CRITICAL_TEMP;

    ifstream file(CPU_TEMP_FILE_PATH);
    if(file.is_open())
    {
        string content;
        getline(file, content);
        cpuTemp = (float)stoi(content) / 1000;
        file.close();
    }

    return cpuTemp;
}


// FanController 成员函数
FanController::FanController(int fd)
    : m_kp(0), m_ki(0), m_kd(0), m_integral(0), m_curPwm(0), m_fd(fd)
{
    m_prevError = 0;
    m_lastTime = steady_clock::now();
    m_criticalFlag = false;
    memset(m_recvBuf, 0, MAX_RECV_BUF_SIZE);
}

FanController::FanController(double kp, double ki, double kd, double integral, int fd)
    : m_kp(kp), m_ki(ki), m_kd(kd), m_integral(integral), m_curPwm(0), m_fd(fd)
{
    m_prevError = 0;
    m_lastTime = steady_clock::now();
    m_criticalFlag = false;
    memset(m_recvBuf, 0, MAX_RECV_BUF_SIZE);
}

void FanController::SetPidParams(double kp, double ki, double kd, double integral)
{
    m_kp = kp;
    m_ki = ki;
    m_kd = kd;
    m_integral = integral;
}

void FanController::Restart(int fd) 
{
    m_fd = fd;
    m_integral = 0;
    m_prevError = 0;
    m_curPwm = 0;
    m_lastTime = steady_clock::now();
    m_criticalFlag = false;
    memset(m_recvBuf, 0, MAX_RECV_BUF_SIZE);
}

void FanController::Reset()
{
    m_integral = 0;
    m_prevError = 0;
    m_lastTime = steady_clock::now();
    m_criticalFlag = false;
    memset(m_recvBuf, 0, MAX_RECV_BUF_SIZE);
}

int FanController::CalcPwm(float& curTemp)
{
    curTemp = ReadTemp();

    if(curTemp > CRITICAL_TEMP)
    {
        m_criticalFlag = true;
    }

    if(m_criticalFlag)
    {
        if(curTemp < SAFE_TEMP)
        {
            m_criticalFlag = false;
            Reset();
        }
        else
        {
            return 100;
        }
    }

    auto now = steady_clock::now();
    duration<double> dtDuration = now - m_lastTime;
    double dt = dtDuration.count();
    dt = max(dt, 0.001);
    double error = curTemp - TARGET_TEMP;
    m_integral += error * dt;
    double derivative = (error - m_prevError) / dt;
    double output = m_kp * error + m_ki * m_integral + m_kd * derivative;
    int pwm = static_cast<int>(round(output));
    pwm = max(PWM_MIN, min(pwm, PWM_MAX));
    if (pwm != static_cast<int>(round(output))) {
        m_integral = (pwm - m_kp * error - m_kd * derivative) / m_ki;
    }
    
    m_prevError = error;
    m_lastTime = now;
    return pwm;
}


// CPUController 成员函数
CPUController::CPUController(int fd)
    : FanController(CPU_KP, CPU_KI, CPU_KD, CPU_INTEGRAL, fd) 
{
    cardPwmVec.resize(g_params.cardPwmVec.size());
}

float CPUController::ReadTemp()
{
    return ReadCpuTemp();
}

void CPUController::SetCardPwm()
{
    vector<int> pwmVec;
    string      cmd;
    int         ret = -1;



    pwmVec = g_params.getPwmVec();
    if(cardPwmVec.size() != pwmVec.size())
    {
        cardPwmVec.resize(pwmVec.size(), 0);
    }

    for(int i = 0; i < pwmVec.size(); ++i)
    {
        if(pwmVec[i] == cardPwmVec[i])
        {
            continue;
        }

        if(pwmVec[i] < 0 || pwmVec[i] > 100)
        {
            cmd = cmd = "$F" + to_string(i + 2) + "S100";
            ret = ExecCommand(m_fd, cmd.data(), m_recvBuf, MAX_RECV_BUF_SIZE);
            IF_COND_FAIL(ret == 0, ("[ERROR] Fail to set AI_CARD" + to_string(i + 1) + " pwm !!!").data(), continue;);
            cardPwmVec[i] = 100;
            syslog(LOG_INFO, "[WARN] Invalid AI_CARD%d pwm %d, set pwm 100 success, current pwm: 100", i + 1, pwmVec[i]);
        }
        else
        {
            cmd = cmd = "$F" + to_string(i + 2) + "S" + Int2StrPadZero(pwmVec[i], 3);
            ret = ExecCommand(m_fd, cmd.data(), m_recvBuf, MAX_RECV_BUF_SIZE);
            IF_COND_FAIL(ret == 0, ("[ERROR] Fail to set AI_CARD" + to_string(i + 1) + " pwm !!!").data(), continue;);
            cardPwmVec[i] = pwmVec[i];
            syslog(LOG_INFO, "[INFO] Set AI_CARD%d pwm success, current pwm: %d", i + 1, pwmVec[i]);
            // cout << "[INFO] Set AI_CARD" << i + 1 << " pwm success, current pwm: " << pwmVec[i] << endl;
        }
    }
}

void CPUController::SetPwm()
{
    int         cpuPwm = 0;
    float       curTemp = 0;
    int         ret = -1;
    string      cmd;

    SetCardPwm();
    cpuPwm = CalcPwm(curTemp);
    if(m_curPwm == cpuPwm)
    {
        return;
    }

    cmd = "$F0S" + Int2StrPadZero(cpuPwm, 3);
    ret = ExecCommand(m_fd, cmd.data(), m_recvBuf, MAX_RECV_BUF_SIZE);
    IF_COND_FAIL(ret == 0, "[ERROR] Fail to set cpu pwm !!!", return;);

    m_curPwm = cpuPwm;
    syslog(LOG_INFO, "[INFO] cpu temperatrue: %f.", curTemp);
    // cout << "[INFO] cpu temperatrue:" << curTemp << endl;
    syslog(LOG_INFO, "[INFO] Set cpu pwm success, current pwm: %d, cmd is %s, res is %s", cpuPwm, cmd.data(), m_recvBuf);
    // cout << "[INFO] Set cpu pwm success, current pwm: " << cpuPwm << ", cmd is " << cmd.data() << endl;
}


// SysController 成员函数
SysController::SysController(int fd)
    : FanController(SYS_KP, SYS_KI, SYS_KD, SYS_INTEGRAL, fd) 
{
    
}

float SysController::ReadTemp()
{
    // return ReadCpuTemp();
    
    int ret = -1;
    int sysTemp = 0;

    ret = ExecCommand(m_fd, "!GTP", m_recvBuf, MAX_RECV_BUF_SIZE);
    IF_COND_FAIL(ret == 0, "[ERROR] SysController.ReadTemp: Fail to read sys temp", return CRITICAL_TEMP;);

    try
    {
        sysTemp = stoi(std::string(m_recvBuf).substr(5,2));
    }
    catch (const std::invalid_argument& e)
    {
        syslog(LOG_INFO, "[ERROR] Fail to get mainboard temperatrue, stoi error, original temperStr: %s, sub(5,2), set temper is 85", m_recvBuf);
        sysTemp = CRITICAL_TEMP;
    }

    return sysTemp;
}

void SysController::SetPwm()
{
    int     pwm = 0;
    float   curTemp = 0;
    int     ret = -1;
    string  cmd;

    curTemp = ReadTemp();
    if(curTemp < SAFE_SYS_TEMP)
    {
        pwm = pwm_safe;
    }
    else if(curTemp < CRITICAL_SYS_TEMP)
    {
        pwm = pwm_warn;
    }
    else
    {
        pwm = pwm_dang;
    }


    if(m_curPwm == pwm)
    {
        return;
    }

    cmd = "$F1S" + Int2StrPadZero(pwm, 3);
    ret = ExecCommand(m_fd, cmd.data(), m_recvBuf, MAX_RECV_BUF_SIZE);
    IF_COND_FAIL(ret == 0, "[ERROR] Fail to set system pwm !!!", return;);

    m_curPwm = pwm;
    syslog(LOG_INFO, "[INFO] mainboard temperatrue: %f.", curTemp);
    // cout << "[INFO] mainboard temperatrue:" << curTemp << endl;
    syslog(LOG_INFO, "[INFO] Set mainboard pwm success, current pwm: %d, cmd is %s, res is %s", pwm, cmd.data(), m_recvBuf);
    // cout << "[INFO] Set mainboard pwm success, current pwm: " << pwm << ", cmd is " << cmd.data() << endl;
}

