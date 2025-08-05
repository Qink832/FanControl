#include "FanController.h"
#include "dcmi_interface_api.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/ioctl.h> 
#include <syslog.h>
#include <string.h>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <cmath> 

#define CARD_MIN_TEMP 50.0
#define TARGET_TEMP 65.0
#define SAFE_TEMP 75.0
#define CRITICAL_TEMP 85.0

#define SAFE_SYS_TEMP 65.0
#define CRITICAL_SYS_TEMP 85.0

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

#define DEFAULT_FAN_MODE 2

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


int Pwm2Duty(int pwm)
{
    IF_COND_FAIL(pwm > 0 && pwm < 101, ("[ERROR] Pwm2Duty: Invalid pwm! Pwm is " + to_string(pwm)).data(), return 255);

    int duty = pwm * 255 / 100;
    return duty;
}


int Duty2Pwm(int duty)
{
    IF_COND_FAIL(duty > 0 && duty < 256, ("[ERROR] Duty2Pwm: Invalid duty! Duty is " + to_string(duty)).data(), return 100);

    int pwm = duty * 100 / 255;
    return pwm;
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



int ExecCommand(int cmd, void* pIn)
{
    int fd = open("/dev/aaeon_sio",O_RDWR);
    IF_COND_FAIL(fd != -1, "[ERROR] Fail to open /dev/aaeon_sio!!!", return -1);

    int ret = ioctl(fd, cmd, pIn);
    IF_COND_FAIL(ret == 0, ("[ERROR] Failed to ioctl the command. cmd: "+ to_string(cmd)).data(), return ret;);

    close(fd);
    return 0;
}


// FanController 成员函数
FanController::FanController()
    : m_kp(0), m_ki(0), m_kd(0), m_integral(0), m_curPwm(0)
{
    m_prevError = 0;
    m_lastTime = steady_clock::now();
    m_criticalFlag = false;
}

FanController::FanController(double kp, double ki, double kd, double integral, int fd)
    : m_kp(kp), m_ki(ki), m_kd(kd), m_integral(integral), m_curPwm(0)
{
    m_prevError = 0;
    m_lastTime = steady_clock::now();
    m_criticalFlag = false;
}

void FanController::SetPidParams(double kp, double ki, double kd, double integral)
{
    m_kp = kp;
    m_ki = ki;
    m_kd = kd;
    m_integral = integral;
}

void FanController::Restart() 
{
    m_integral = 0;
    m_prevError = 0;
    m_curPwm = 0;
    m_lastTime = steady_clock::now();
    m_criticalFlag = false;
}

void FanController::Reset()
{
    m_integral = 0;
    m_prevError = 0;
    m_lastTime = steady_clock::now();
    m_criticalFlag = false;
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

// CardController 成员函数
CardController::CardController(int cardId)
    : FanController(), m_cardId(cardId) 
{
    int                         ret = 0;
    char                        product_type_str[64] = {0};
    struct dcmi_tag_pcie_idinfo pcie_idinfo;

    memset(&pcie_idinfo, 0, sizeof(dcmi_tag_pcie_idinfo));
    ret = dcmi_get_pcie_info(m_cardId, 0, &pcie_idinfo);
    IF_COND_FAIL(ret == 0, (string("[ERROR] CardController: Fail to get bus_id, dcmi_get_pcie_info error code: ")+to_string(ret)).data(), pcie_idinfo.bdf_busid = -1);
    m_busId = pcie_idinfo.bdf_busid;

    ret = dcmi_get_product_type(m_cardId, 0, product_type_str, 64);
    IF_COND_FAIL(ret == 0, (string("[ERROR] CardController: Fail to get product type, dcmi_get_product_type error code: ")+to_string(ret)).data(), pcie_idinfo.bdf_busid = -1);
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

float CardController::ReadTemp()
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
    if (pwm != static_cast<int>(round(output))) 
    {
        m_integral = (pwm - m_kp * error - m_kd * derivative) / m_ki;
    }
    
    m_prevError = error;
    m_lastTime = now;
    return pwm;
}

void CardController::SetPwm()
{
    int             pwm = 0, readDuty = 0, readPwm = 0;
    int             curTemp = 0;
    int             ret = -1;
    vector<int>     busIdVec;

    pwm = CalcPwm(curTemp);
    if(m_curPwm == pwm)
    {
        return;
    }

    busIdVec = g_params.getBusIdVec();
    for(auto it = busIdVec.begin(); it != busIdVec.end(); ++it) 
    {
        if(*it == m_busId || *it == -2)
        {
            sio_ioctl_data   cardData;
            int              index = it - busIdVec.begin();

            if(*it == -2)
            {
                pwm = 30;
                syslog(LOG_INFO, "[INFO] Config set %s bus_id -2, pwm 30.", m_proType.data());
            }

            cardData.fan_num = index + 2;
            cardData.fan_mode = DEFAULT_FAN_MODE;
            cardData.duty = Pwm2Duty(pwm);

            ret = ExecCommand(IOC_COMMAND_SET, &cardData);
            IF_COND_FAIL(ret == 0, ("[ERROR] Fail to set " + m_proType + " pwm !!!").data(), continue);
            // 检查是否设置成功
            readDuty = cardData.fan_num;
            ret = ExecCommand(IOC_COMMAND_GET, &readDuty);
            readPwm = Duty2Pwm(readDuty);
            IF_COND_FAIL(ret == 0, ("[ERROR] Fail to get AI_CARD" + to_string((it - busIdVec.begin()) + 1) + " pwm !!!").data(), continue;);
            IF_COND_FAIL(pwm == readPwm || pwm == readPwm + 1 || pwm == readPwm -1, ("[ERROR] AI_CARD" + to_string((it - busIdVec.begin()) + 1) + " pwm != readPwm !!! setPwm is " + to_string(pwm) + ", readPwm is " + to_string(readPwm) + ", duty is " + to_string(readDuty)).data(), continue;);

            m_curPwm = pwm;
            syslog(LOG_INFO, "[INFO] Set %s pwm success, card_id is %d, bus_is is %d, temperatrue: %d, current pwm is %d.", m_proType.data(), m_cardId, m_busId, curTemp, pwm);
            // cout << "[INFO] " << m_proType << " card_id is " << m_cardId << ", temperatrue:" << curTemp << endl;
            // cout << "[INFO] Set " << m_proType << " pwm success, bus_id is " << m_busId << ", current pwm: " << pwm << endl;
        }
    }
}