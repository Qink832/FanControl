#include "FanController.h"
#include "dcmi_interface_api.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <syslog.h>
#include <string.h>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <cmath> 

#define CARD_MIN_TEMP 50
#define TARGET_TEMP 65
#define SAFE_TEMP 75
#define CRITICAL_TEMP 80

#define PWM_MIN 20
#define PWM_MAX 100

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

int g_cardDangFlag = 0;

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
    g_cardDangFlag = 0;
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

int FanController::CalcPwm(int& curTemp)
{
    if(g_cardDangFlag != 0)
    {
        if(m_curPwm != 100)
        {
            syslog(LOG_INFO, "[INFO] card temp is 85, system fan set pwm 100! Flag is %d.", g_cardDangFlag);
            // cout << "[INFO] card temp is 85, system fan set pwm 100! Flag is " << g_cardDangFlag << endl;
        }
        
        return 100;
    }

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
    
}

int CPUController::ReadTemp()
{
    int cpuTemp = CRITICAL_TEMP;

    ifstream file(CPU_TEMP_FILE_PATH); 
    if(file.is_open())
    {
        string content;
        getline(file, content);
        cpuTemp = stoi(content) / 1000;
        file.close();
    }

    return cpuTemp;
}

void CPUController::SetPwm()
{
    int         pwm = 0;
    int         curTemp = 0;
    int         ret = -1;
    string      cmd;
    vector<int> busIdVec;

    pwm = CalcPwm(curTemp);
    if(m_curPwm == pwm)
    {
        return;
    }

    cmd = "$F0S" + Int2StrPadZero(pwm, 3);
    ret = ExecCommand(m_fd, cmd.data(), m_recvBuf, MAX_RECV_BUF_SIZE);
    IF_COND_FAIL(ret == 0, "[ERROR] Fail to set cpu pwm !!!", return;);

    // 检查 cardlist
    busIdVec = g_params.getBusIdVec();
    for(auto it = busIdVec.begin(); it != busIdVec.end(); ++it) 
    {
        if(*it == -1)
        {
            int index = it - busIdVec.begin();
            cmd = cmd = "$F" + to_string(index + 2) + "S030";
            ret = ExecCommand(m_fd, cmd.data(), m_recvBuf, MAX_RECV_BUF_SIZE);
            IF_COND_FAIL(ret == 0, ("[ERROR] Fail to set AI_CARD" + to_string(index + 1) + " pwm !!!").data(), continue;);
            
            syslog(LOG_INFO, "[INFO] Set AI_CARD%d pwm success, current pwm: 30", index + 1);
            // cout << "[INFO] Set AI_CARD" << index + 1 << " pwm success, current pwm: 30" << endl;
        }
        else if(*it == -2)
        {
            int index = it - busIdVec.begin();
            cmd = "$F" + to_string((it - busIdVec.begin()) + 2) + "S" + Int2StrPadZero(pwm, 3);
            ret = ExecCommand(m_fd, cmd.data(), m_recvBuf, MAX_RECV_BUF_SIZE);
            IF_COND_FAIL(ret == 0, ("[ERROR] Fail to set AI_CARD" + to_string(index + 1) + " pwm !!!").data(), continue;);

            syslog(LOG_INFO, "[INFO] Set AI_CARD%d pwm success, current pwm: %d", index + 1, pwm);
            // cout << "[INFO] Set AI_CARD" << index + 1 << " pwm success, current pwm: " << pwm << endl;
            m_curPwm = pwm;
        }
    }

    m_curPwm = pwm;
    syslog(LOG_INFO, "[INFO] cpu temperatrue: %d.", curTemp);
    // cout << "[INFO] cpu temperatrue:" << curTemp << endl;
    syslog(LOG_INFO, "[INFO] Set cpu pwm success, current pwm: %d", pwm);
    // cout << "[INFO] Set cpu pwm success, current pwm: " << pwm << endl;
}


// SysController 成员函数
SysController::SysController(int fd)
    : FanController(SYS_KP, SYS_KI, SYS_KD, SYS_INTEGRAL, fd) 
{
    
}

int SysController::ReadTemp()
{
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
    int     curTemp = 0;
    int     ret = -1;
    string  cmd;

    pwm = CalcPwm(curTemp);
    if(m_curPwm == pwm)
    {
        return;
    }

    cmd = "$F1S" + Int2StrPadZero(pwm, 3);
    ret = ExecCommand(m_fd, cmd.data(), m_recvBuf, MAX_RECV_BUF_SIZE);
    IF_COND_FAIL(ret == 0, "[ERROR] Fail to set system pwm !!!", return;);

    m_curPwm = pwm;
    syslog(LOG_INFO, "[INFO] mainboard temperatrue: %d.", curTemp);
    // cout << "[INFO] mainboard temperatrue:" << curTemp << endl;
    syslog(LOG_INFO, "[INFO] Set mainboard pwm success, current pwm: %d", pwm);
    // cout << "[INFO] Set mainboard pwm success, current pwm: " << pwm << endl;
}


// CardController 成员函数
CardController::CardController(int fd, int cardId)
    : FanController(fd), m_cardId(cardId) 
{
    int                         ret = 0;
    char                        product_type_str[64] = {0};
    struct dcmi_tag_pcie_idinfo pcie_idinfo;

    memset(&pcie_idinfo, 0, sizeof(dcmi_tag_pcie_idinfo));
    ret = dcmi_get_pcie_info(m_cardId, 0, &pcie_idinfo);
    IF_COND_FAIL(ret == 0, (string("[ERROR] CardController: Fail to get bus_id, dcmi_get_pcie_info error code: ")+to_string(ret)).data(), m_initFlag = false);
    m_busId = pcie_idinfo.bdf_busid;

    ret = dcmi_get_product_type(m_cardId, 0, product_type_str, 64);
    IF_COND_FAIL(ret == 0, (string("[ERROR] CardController: Fail to get product type, dcmi_get_product_type error code: ")+to_string(ret)).data(), m_initFlag = false);
    m_proType = product_type_str;

    // set kp,ki,kd
    if(m_proType == "Atlas 300I Duo")
    {
        m_kp = THRIDUO_KP;
        m_ki = THRIDUO_KI;
        m_kd = THRIDUO_KD;
    }
    else if(m_proType == "Atlas 300I Pro")
    {
        m_kp = THRIPRO_KP;
        m_ki = THRIPRO_KI;
        m_kd = THRIPRO_KD;
    }
    else
    {
        // 300V
        m_kp = THRV_KP;
        m_ki = THRV_KI;
        m_kd = THRV_KD;
    }
}

int CardController::ReadTemp()
{
    int cardTemp = CRITICAL_TEMP;
    int ret = -1;
    
    ret = dcmi_get_device_temperature(m_cardId, 0, &cardTemp);
    IF_COND_FAIL(ret == 0, string("[ERROR] CardController.ReadTemp: Fail to get Temp, error code is " + to_string(ret)).data(), return cardTemp);

    return cardTemp;
}


int CardController::CalcPwm(int& curTemp)
{
    int tarTemp = TARGET_TEMP;

    curTemp = ReadTemp();

    if(m_criticalFlag)
    {
        if(curTemp < SAFE_TEMP)
        {
            m_criticalFlag = false;
            g_cardDangFlag -= (m_cardId + 1);
            Reset();
        }
        else
        {
            return 100;
        }
    }
    else
    {
        if(curTemp > CRITICAL_TEMP)
        {
            m_criticalFlag = true;
            g_cardDangFlag += (m_cardId + 1);
        }
    }

    if(curTemp < 50)
    {
        tarTemp = CARD_MIN_TEMP;
    }
    else if(curTemp < TARGET_TEMP)
    {
        tarTemp = (curTemp / 10) * 10;
    }

    auto now = steady_clock::now();
    duration<double> dtDuration = now - m_lastTime;
    double dt = dtDuration.count();
    dt = max(dt, 0.001);
    double error = curTemp - tarTemp;
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

void CardController::SetPwm()
{
    int         pwm = 0;
    int         curTemp = 0;
    int         ret = -1;
    string      cmd;
    vector<int> busIdVec;

    pwm = CalcPwm(curTemp);
    if(m_curPwm == pwm)
    {
        return;
    }

    busIdVec = g_params.getBusIdVec();
    for(auto it = busIdVec.begin(); it != busIdVec.end(); ++it) 
    {
        if(*it == m_busId)
        {
            cmd = "$F" + to_string((it - busIdVec.begin()) + 2) + "S" + Int2StrPadZero(pwm, 3);
            ret = ExecCommand(m_fd, cmd.data(), m_recvBuf, MAX_RECV_BUF_SIZE);
            IF_COND_FAIL(ret == 0, ("[ERROR] Fail to set " + m_proType + " pwm !!!").data(), continue;);
            m_curPwm = pwm;
            
            syslog(LOG_INFO, "[INFO] %s card_id is %d, temperatrue: %d.", m_proType.data(), m_cardId, curTemp);
            // cout << "[INFO] " << m_proType << " card_id is " << m_cardId << ", temperatrue:" << curTemp << endl;
            syslog(LOG_INFO, "[INFO] Set %s pwm success, bus_is is %d, current pwm: %d", m_proType.data(), m_busId, pwm);
            // cout << "[INFO] Set " << m_proType << " pwm success, bus_id is " << m_busId << ", current pwm: " << pwm << endl;
        }
    }
}