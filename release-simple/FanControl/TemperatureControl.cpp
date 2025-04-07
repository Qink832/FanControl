#include <iostream>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <syslog.h>
#include <string.h>
#include "FanControlGlobal.h"
#include "SerialPort.h"
#include "TemperatureControl.h"

#define MODE_FILE_PATH  "/etc/FanControlParam.txt"
#define CPU_TEMP_FILE_PATH "/sys/class/thermal/thermal_zone0/temp"

#define MAX_RECV_BUF_SIZE   1024

// cpu power单位：W
#define MAX_CPU_POWER 95000
// NPU设备 power单位： 0.1W
#define MAX_300IPRO_POWER 720
#define MAX_300V_POWER 720
#define MAX_300IDUO_POWER 1500

#define SAFE_POWER_RANGE 0.2
#define WARN_POWER_RANGE 0.7

int g_fd = 0;

void* ModeListen(void* arg);
void* TemperListen(void* arg);

TemperCtrl::TemperCtrl()
    : m_autoFlag(true), m_reStartFlag(false)
{
    
}

TemperCtrl::~TemperCtrl()
{

}

bool TemperCtrl::Init()
{
    //上锁,打开文件,写入1
    int fd = open(MODE_FILE_PATH, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
    if (fd == -1)
    {
        syslog(LOG_INFO, "[ERROR] TemperCtrl.Init: Failed to open %s.", MODE_FILE_PATH);
        std::cerr << "[ERROR] TemperCtrl.Init: Failed to open " << MODE_FILE_PATH << std::endl;
        return false;
    }

    // 请求排他锁（写锁），并等待直到成功获取锁
    if (flock(fd, LOCK_EX) == -1)
    {
        syslog(LOG_INFO, "[ERROR] TemperCtrl.Init: Failed to lock file.");
        std::cerr << "[ERROR] TemperCtrl.Init: Failed to lock file." << std::endl;
        close(fd);
        return false;
    }

    // 写入 1
    std::ofstream file(MODE_FILE_PATH, std::ios::out);
    if (file.is_open())
    {
        file << "1" << std::endl;
        file.close();
    }
    
    // 解锁
    flock(fd, LOCK_UN);
    close(fd);

    m_autoFlag = true;
    return true;
}

void TemperCtrl::Run()
{
    pthread_create(&m_modeTid, NULL, ModeListen, &m_autoFlag);
    pthread_create(&m_temperTid, NULL, TemperListen, &m_autoFlag);
    
    pthread_detach(m_modeTid);
    pthread_detach(m_temperTid);
}

bool TemperCtrl::IsAuto()
{
    return m_autoFlag;
}

void TemperCtrl::ReStart()
{
    syslog(LOG_INFO, "[INFO] Auto mode close, Manual mode open.");
    pthread_create(&m_temperTid, NULL, TemperListen, &m_autoFlag);
    pthread_detach(m_temperTid);
    m_reStartFlag = false;
}

void TemperCtrl::Close()
{ 
    if(!m_reStartFlag)
    {
        SerialClose(&g_fd);
        m_reStartFlag = true;
        syslog(LOG_INFO, "[INFO] Serial port close, mode is Manual.");
    }
}

bool TemperCtrl::IsRestart()
{
    return m_reStartFlag;
}

void* ModeListen(void* arg)
{
    bool* pAutoFlag = (bool*)arg;

    while(true)
    {
        std::ifstream file(MODE_FILE_PATH);
        if(file.is_open())
        {
            std::string content;
            getline(file, content);
            *pAutoFlag = (content == "0") ? false : true;
            file.close();
        }
        
        sleep(1);
    }
    
    return NULL;
}

int ExecCommand(int serialFd, const char* cmd, char* recvBuf, int recvBufLen)
{
    if(serialFd == 0)
    {
        syslog(LOG_INFO, "[ERROR] Serial port /dev/ttyUSB0 is not open.");
        return -1;
    }

    int ret = write(serialFd, cmd, strlen(cmd));
    if(ret < 0)
    {
        syslog(LOG_INFO, "[ERROR] Failed to write the command. cmd: %s", cmd);
        return ret;
    }

    usleep(100000); //100ms
    memset(recvBuf, 0, recvBufLen);

    ret = read(serialFd, recvBuf, recvBufLen);
    if(ret < 0)
    {
        syslog(LOG_INFO, "[ERROR] Failed to read the result. cmd:  %s", cmd);
        return ret;
    }

    return 0;
}

std::string Int2StrPadZero(int value, int length)
{
    std::string result = std::to_string(value);
    if (result.length() < length)
    {
        result = std::string(length - result.length(), '0').append(result);
    }

    return result;
}

bool PathExists(const char* path) 
{
    return access(path, F_OK) == 0;
}

void* TemperListen(void* arg)
{
    bool*       pAutoFlag = (bool*)arg;
    int         tarPwm = 0;
    int         cpuPwm = 0, sysPwm = 0;
    int         cpuTemp = 0, sysTemp = 0;
    int         cpuPower = 0;
    PowerType   powerType = POWER_SAFE;
    int         npuPwm1 = 0, npuPwm2 = 0;
    char        recvBuf[MAX_RECV_BUF_SIZE] = {0};
    int         ret = -1;

    //打开串口
    ret = SerialOpen(&g_fd);
    if(ret != 0)
    {
        syslog(LOG_INFO, "[ERROR] Fail to open serial port /dev/ttyUSB0, process exit.");
        exit(-1);
    }

    while(*pAutoFlag)
    {
        // mainboard 单独处理
        //获取环境温度
        ret = ExecCommand(g_fd, "!GTP", recvBuf, MAX_RECV_BUF_SIZE);
        if(ret != 0)
        {
            continue;
        }

        try
        {
            sysTemp = stoi(std::string(recvBuf).substr(5,2));
        }
        catch (const std::invalid_argument& e)
        {
            syslog(LOG_INFO, "[ERROR] Fail to get mainboard temperatrue, stoi error, original temperStr: %s, sub(5,2), set temper is 50", recvBuf);
            sysTemp = 50;
        }

        tarPwm = label_main[(sysTemp+4)/5];
        if(tarPwm != sysPwm)
        {
            std::string cmd = "$F1S" + Int2StrPadZero(tarPwm, 3);
            ret = ExecCommand(g_fd, cmd.data(), recvBuf, MAX_RECV_BUF_SIZE);
            if(ret != 0)
            {
                continue;
            }

            sysPwm = tarPwm;
            syslog(LOG_INFO, "[INFO] Set mainboard pwm success, mainboard temperatrue:%d, current pwm: %d", sysTemp, sysPwm);
        }

        // cpu 单独处理
        // 查询cpu温度
        std::ifstream file(CPU_TEMP_FILE_PATH);
        if(file.is_open())
        {
            std::string content;
            getline(file, content);
            cpuTemp = stoi(content) / 1000;
            file.close();
        }

        // 查询cpu功率
        ret = ExecCommand(g_fd, "#GPV", recvBuf, MAX_RECV_BUF_SIZE);
        if(ret != 0)
        {
            continue;
        }
        
        std::string result = recvBuf;
        int pos_start = result.find_last_of("=") + 1;
        int pos_end = result.find_last_of(" ");
        try
        {
            cpuPower = stoi(result.substr(pos_start, pos_end-pos_start));
        }
        catch(const std::invalid_argument& e)
        {
            syslog(LOG_INFO, "[ERROR] Fail to get cpu power, stoi error, original powerStr: %s, sub(%d,%d), set cpu power is %d", result, pos_start, pos_end-pos_start, MAX_CPU_POWER);
            cpuPower = MAX_CPU_POWER;
        }

        powerType = (cpuPower < MAX_CPU_POWER * SAFE_POWER_RANGE) ? POWER_SAFE : (cpuPower < MAX_CPU_POWER * WARN_POWER_RANGE) ? POWER_WARN : POWER_DANG;


        if(sysTemp > 54)
        {
            tarPwm = 100;
        }
        else
        {
            tarPwm = label_cpu[(cpuTemp+4)/5][powerType];
        }

        if(tarPwm != cpuPwm)
        {
            std::string cmd = "$F0S" + Int2StrPadZero(tarPwm, 3);
            ret = ExecCommand(g_fd, cmd.data(), recvBuf, MAX_RECV_BUF_SIZE);
            if(ret != 0)
            {
                continue;
            }

            cpuPwm = tarPwm;
            syslog(LOG_INFO, "[INFO] Set cpu pwm success, cpu temperatrue:%d, cpu power: %d, current pwm: %d", cpuTemp, cpuPower, cpuPwm);
        }

        // 查询bus_id 1 是否插卡
        if(PathExists("/sys/bus/pci/devices/0000:01:00.0"))
        {
            tarPwm = 50;
        }
        else
        {
            tarPwm = 20;
        }

        if(tarPwm != npuPwm1)
        {
            std::string cmd = "$F2S" + Int2StrPadZero(tarPwm, 3);
            ret = ExecCommand(g_fd, cmd.data(), recvBuf, MAX_RECV_BUF_SIZE);
            if(ret != 0)
            {
                continue;
            }

            npuPwm1 = tarPwm;
            syslog(LOG_INFO, "[INFO] AI_CARD 1 set pwm success, current pwm: %d", npuPwm1);
        }

        // 查询bus_id 2 是否插卡
        if(PathExists("/sys/bus/pci/devices/0000:02:00.0"))
        {
            tarPwm = 50;
        }
        else
        {
            tarPwm = 20;
        }

        if(tarPwm != npuPwm2)
        {
            std::string cmd = "$F3S" + Int2StrPadZero(tarPwm, 3);
            ret = ExecCommand(g_fd, cmd.data(), recvBuf, MAX_RECV_BUF_SIZE);
            if(ret != 0)
            {
                continue;
            }

            npuPwm2 = tarPwm;
            syslog(LOG_INFO, "[INFO] AI_CARD 2 set pwm success, current pwm: %d", npuPwm2);
        }

        sleep(5);
    }

    //关闭串口
    SerialClose(&g_fd);
    
    return NULL;
}











