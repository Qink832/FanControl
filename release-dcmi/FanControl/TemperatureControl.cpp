#include <iostream>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <syslog.h>
#include <string.h>
#include "dcmi_interface_api.h"
#include "FanControlGlobal.h"
#include "SerialPort.h"
#include "json.hpp"
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
int g_fanBusId_1 = 0;
int g_fanBusId_2 = 0;
const std::string DEFAULT_JSON = R"(
    {
        "mode": true,
        "fan_1_bus_id": 1,
        "fan_2_bus_id": 2
    }
    )";

void* ModeListen(void* arg);
void* TemperListen(void* arg);

using json = nlohmann::json;

TemperCtrl::TemperCtrl()
    : m_autoFlag(true), m_reStartFlag(false)
{
    
}

TemperCtrl::~TemperCtrl()
{

}

bool CreateDefaultFile(std::string filePath)
{
    // 以写入模式创建文件
    std::ofstream outfile(MODE_FILE_PATH);
    if (!outfile.is_open()) 
    {
        syslog(LOG_INFO, "[ERROR] TemperCtrl.Init: Failed create FanControlParam.txt .");
        std::cerr << "[ERROR] TemperCtrl.Init: Failed create FanControlParam.txt ." << std::endl;
        return false;
    }
    
    outfile << DEFAULT_JSON;
    outfile.close();
    g_fanBusId_1 = 1;
    g_fanBusId_2 = 2;
    
    return true;
}

bool TemperCtrl::Init()
{
    bool ret = false;

    //上锁,打开文件,mode 写入 1
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

    // 写入 mode 1
    std::fstream file(MODE_FILE_PATH, std::ios::in | std::ios::out);

    // 如果文件不存在
    if (!file.is_open()) 
    {
        ret = CreateDefaultFile(MODE_FILE_PATH);
    }
    else
    {
        json root;
        try
        {
            file >> root;
            m_autoFlag = root["mode"];
            g_fanBusId_1 = root["fan_1_bus_id"];
            g_fanBusId_2 = root["fan_2_bus_id"];
        }
        catch (const json::parse_error& e) 
        {
            syslog(LOG_INFO, "[INFO] TemperCtrl.Init: Failed parse FanControlParam.txt, content is not json. Start to create a json file.");
            std::cerr << "[INFO] TemperCtrl.Init: Failed parse FanControlParam.txt, content is not json. Start to create a json file." << std::endl;
            
            ret = CreateDefaultFile(MODE_FILE_PATH);
            m_autoFlag = true;
        }
        catch (const json::type_error& e)
        {
            syslog(LOG_INFO, "[INFO] TemperCtrl.Init: Failed parse FanControlParam.txt, fan_1_bus_id or fan_2_bus_id is null. Start to create a json file.");
            std::cerr << "[INFO] TemperCtrl.Init: Failed parse FanControlParam.txt, fan_1_bus_id or fan_2_bus_id is null. Start to create a json file." << std::endl;
            
            ret = CreateDefaultFile(MODE_FILE_PATH);
            m_autoFlag = true;
        }
    }

    // 解锁
    flock(fd, LOCK_UN);
    close(fd);

    return ret;
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
            json root;
            file >> root;
            *pAutoFlag = root["mode"];
            g_fanBusId_1 = root["fan_1_bus_id"];
            g_fanBusId_2 = root["fan_2_bus_id"];
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

void* TemperListen(void* arg)
{
    bool*       pAutoFlag = (bool*)arg;
    int         tarPwm = 0;
    int         cpuPwm = 0, sysPwm = 0;
    int         cpuTemp = 0, sysTemp = 0;
    int         cpuPower = 0;
    int         card_num = 0;
    int         card_list[8] = {0}, card_temp_list[8] = {0}, card_pwm_list[8] = {0};
    PowerType   powerType = POWER_SAFE;
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

        // NPU 循环处理
        // 读取cardList
        memset(card_list, 0, sizeof(card_list));
        ret = dcmi_get_card_list(&card_num, card_list, 8);
        for(int i = 0; i < card_num; ++i)
        {
            // 读取deviceList
            int device_id_max = 0;
            int mcu_id = 0;
            int cpu_id = 0;
            int card_id = card_list[i];
            int power = 0;
            ret = dcmi_get_device_id_in_card(card_id, &device_id_max, &mcu_id, &cpu_id);
            if(ret != 0)
            {
                syslog(LOG_INFO, "[ERROR] Faile to get device id list, dcmi_get_device_id_in_card error code: %d, set AI_CARD1 and AI_CARD2 pwm 100", ret);
                ExecCommand(g_fd, "$F2S100", recvBuf, MAX_RECV_BUF_SIZE);
                ExecCommand(g_fd, "$F3S100", recvBuf, MAX_RECV_BUF_SIZE);
                break;
            }

            // 读取productType
            char product_type_str[64] = {0};
            ret = dcmi_get_product_type(card_id, 0, product_type_str, 64);
            if(ret != 0)
            {
                syslog(LOG_INFO, "[ERROR] Fail to get product type, dcmi_get_product_type error code: %d, set AI_CARD1 and AI_CARD2 pwm 100", ret);
                tarPwm = 100;
                goto LOOP_END;
            }

            // 读取temper
            if(std::string(product_type_str) == "Atlas 300I Duo")
            {
                ret = dcmi_get_device_temperature(card_id, 0, &card_temp_list[i]);
                if(ret != 0)
                {
                    syslog(LOG_INFO, "[ERROR] Product_type: %s, dcmi_get_device_temperature error, error: %d, temperatrue set 80", product_type_str, ret);
                    card_temp_list[i] = 80;
                }

                int temp = 0;
                ret = dcmi_get_device_temperature(card_id, device_id_max-1, &temp);
                if(card_temp_list[i] < temp)
                {
                    card_temp_list[i] = temp;
                }
            }
            else 
            {
                ret = dcmi_get_device_temperature(card_id, 0, &card_temp_list[i]);
                if(ret != 0)
                {
                    syslog(LOG_INFO, "[ERROR] Product_type: %s, dcmi_get_device_temperature error, error code: %d, temperatrue set 80", product_type_str, ret);
                    card_temp_list[i] = 80;
                }
            }

            // 读取power
            ret = dcmi_mcu_get_power_info(card_id, &power);
            if(ret != 0 || power == 0x7FFD || power == 0x7FFF)
            {
                syslog(LOG_INFO, "[ERROR] Product_type: %s, dcmi_mcu_get_power_info error, error code: %#X", product_type_str, power);
                power = MAX_CPU_POWER; // cpu power是最大的数据，后续计算与满负载效果相同
            }

            if(std::string(product_type_str) == "Atlas 300I Duo")
            {
                powerType = (power < MAX_300IDUO_POWER * SAFE_POWER_RANGE) ? POWER_SAFE : (power < MAX_300IDUO_POWER * WARN_POWER_RANGE) ? POWER_WARN : POWER_DANG;
                tarPwm = label_300IDuo[(card_temp_list[i]+4)/5][powerType];
            }
            else if(std::string(product_type_str) == "Atlas 300I Pro")
            {
                powerType = (power < MAX_300IPRO_POWER * SAFE_POWER_RANGE) ? POWER_SAFE : (power < MAX_300IPRO_POWER * WARN_POWER_RANGE) ? POWER_WARN : POWER_DANG;
                tarPwm = label_300IPro[(card_temp_list[i]+4)/5][powerType];
            }
            else // 300V
            {
                powerType = (power < MAX_300V_POWER * SAFE_POWER_RANGE) ? POWER_SAFE : (power < MAX_300V_POWER * WARN_POWER_RANGE) ? POWER_WARN : POWER_DANG;
                tarPwm = label_300V[(card_temp_list[i]+4)/5][powerType];
            }
LOOP_END:
            // 处理风扇
            struct dcmi_tag_pcie_idinfo pcie_idinfo;
            memset(&pcie_idinfo, 0, sizeof(dcmi_tag_pcie_idinfo));
            ret = dcmi_get_pcie_info(card_id, 0, &pcie_idinfo);
            if(ret != 0 || pcie_idinfo.bdf_busid == 0)
            {
                syslog(LOG_INFO, "[ERROR] Fail to get bus_id, dcmi_get_pcie_info error code:%d, bus_id: %d, set AI_CARD1 and AI_CARD2 pwm 100", ret, pcie_idinfo.bdf_busid);
                ExecCommand(g_fd, "$F2S100", recvBuf, MAX_RECV_BUF_SIZE);
                ExecCommand(g_fd, "$F3S100", recvBuf, MAX_RECV_BUF_SIZE);
                break;
            }
            
            if(tarPwm != card_pwm_list[i])
            {
                if(pcie_idinfo.bdf_busid == g_fanBusId_1)
                {
                    std::string cmd = "$F2S"+ Int2StrPadZero(tarPwm, 3);
                    ret = ExecCommand(g_fd, cmd.data(), recvBuf, MAX_RECV_BUF_SIZE);
                }
                else if(pcie_idinfo.bdf_busid == g_fanBusId_2)
                {
                    std::string cmd = "$F3S"+ Int2StrPadZero(tarPwm, 3);
                    ret = ExecCommand(g_fd, cmd.data(), recvBuf, MAX_RECV_BUF_SIZE);
                }
                else
                {
                    syslog(LOG_INFO, "[ERROR] FanControlParam.txt: fan_1_bus_id is %d, fan_2_bus_id is %d; dcmi %s bus_id is %d", g_fanBusId_1, g_fanBusId_2, product_type_str, pcie_idinfo.bdf_busid);
                    syslog(LOG_INFO, "[ERROR] set AI_CARD1 and AI_CARD2 pwm 100");
                    ExecCommand(g_fd, "$F2S100", recvBuf, MAX_RECV_BUF_SIZE);
                    ExecCommand(g_fd, "$F3S100", recvBuf, MAX_RECV_BUF_SIZE);
                    continue;
                }

                if(ret != 0)
                {
                    continue;
                }

                card_pwm_list[i] = tarPwm;
                syslog(LOG_INFO, "[INFO] bus_id %d, %s set pwm success, temperatrue: %d, power: %d(unit 0.1W), current pwm:%d", pcie_idinfo.bdf_busid, product_type_str, card_temp_list[i], power, card_pwm_list[i]);
            }
        }

        sleep(5);
    }

    //关闭串口
    SerialClose(&g_fd);
    
    return NULL;
}











