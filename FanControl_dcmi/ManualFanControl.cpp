#ifndef MANUAL_FAN_CONTROL_H
#define MANUAL_FAN_CONTROL_H

#include <iostream>
#include <fstream>
#include <array>
#include <memory>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <string.h>
#include "dcmi_interface_api.h"
#include "SerialPort.h"
#include "json.hpp"

#define MODE_FILE_PATH  "/etc/FanControlParam.txt"
#define CPU_TEMP_FILE_PATH "/sys/class/thermal/thermal_zone0/temp"
#define MAX_RECV_BUF_SIZE   1024

using json = nlohmann::json;

int ExecCommand(int serialFd, const char* cmd, char* recvBuf, int recvBufLen)
{
    if(serialFd == 0)
    {
        std::cout << "[ERROR] Serial port /dev/ttyUSB0 is not open!" << std::endl;
        return -1;
    }

    int ret = write(serialFd, cmd, strlen(cmd));
    if(ret < 0)
    {
        std::cout << "[ERROR] Failed to write the command. cmd: " << cmd << std::endl;
        return ret;
    }

    usleep(100000); //100ms
    memset(recvBuf, 0, recvBufLen);

    ret = read(serialFd, recvBuf, recvBufLen);
    if(ret < 0)
    {
        std::cout << "[ERROR] Failed to read the result. cmd: " << cmd << std::endl;
        return ret;
    }

    return 0;
}


int GetTemper(const int fd, std::string type)
{
    std::string result;
    char recvBuf[MAX_RECV_BUF_SIZE] = {0};
    int device_count = 0;
    int card_id_list[8] = {0};
    int ret = 0;

    if(type == "cpu")
    {
        std::ifstream file(CPU_TEMP_FILE_PATH);
        if(!file.is_open())
        {
            std::cout << "[ERROR] Failed to open the cpu temperature record file. file path: " << CPU_TEMP_FILE_PATH << std::endl;
            return -1;
        }

        std::string content;
        float         cpuTemp = 0;
        getline(file, content);
        file.close();
        if(content.empty())
        {
            std::cout << "[ERROR] Failed to read the cpu temperature file. file path: " << CPU_TEMP_FILE_PATH << std::endl;
            return -1;
        }
        std::cout << "TEMP:" << stof(content) / 1000 << " C" << std::endl;
        goto END;
    }
    else if(type == "mainboard")
    {
        ret = ExecCommand(fd, "!GTP", recvBuf, MAX_RECV_BUF_SIZE);
        if(ret != 0)
        {
            return -1;
        }

        std::cout << "TEMP:" << recvBuf << std::endl;
        goto END;
    }

    ret = dcmi_get_card_num_list(&device_count, card_id_list, 8);
    if(ret != 0)
    { 
        std::cout << "Failed to obtain the device ID list, dcmi_get_card_num_list error code: " << ret << std::endl;
        return -1;
    }

    if(type == "300IPro")
    {
        type = "Atlas 300I Pro";
    }
    else if(type == "300V")
    {
        type = "Atlas 300V";
    }
    else if(type == "300IDuo")
    {
        type = "Atlas 300I Duo";
    }
    else
    {
        std::cout << "[ERROR] The second parameter is invalid. supported input: cpu, 300IPro, 300V, 300IDuo, mainboard." << std::endl;
        return -1;
    }

    for(int i = 0; i < device_count; ++i)
    {
        char product_type_str[64] = {0};
        ret = dcmi_get_product_type(card_id_list[i], 0, product_type_str, 64);
        if(ret != 0)
        {
            std::cout << "[ERROR] Failed to obtain the AI_CARD type, error code: " << ret << ". Use the npu-smi tool or ascend dmi tool to check the device." << std::endl;
            return ret;
        }

        if(type == std::string(product_type_str))
        {
            int npuTemp = 0;
            ret = dcmi_get_device_temperature(card_id_list[i], 0, &npuTemp);
            if(ret != 0)
            {
                std::cout << "[ERROR] Failed to obtain  " << type << " temperature, error code: " << ret << std::endl;
                return ret;
            }
            
            result = "TEMP:" + std::to_string(npuTemp) + " C";
        }
    }

    if(result.empty())
    {
        std::cout << "[ERROR] The target device cannot be queried. Use the npu-smi tool or ascend dmi tool to check for the presence of the device." << type << std::endl;
    }
    else
    {
        std::cout << result << std::endl;
    }
END:
    return 0;
}

int GetPower(const int fd, std::string type)
{
    std::string result;
    char recvBuf[MAX_RECV_BUF_SIZE] = {0};
    int device_count = 0;
    int card_id_list[8] = {0};
    int ret = 0;

    
    if(type == "cpu")
    {
        ret = ExecCommand(fd, "#GPV", recvBuf, MAX_RECV_BUF_SIZE);
        if(ret != 0)
        {
            return ret;
        }
        std::cout << recvBuf << std::endl;
        goto END;
    }

    ret = dcmi_get_card_num_list(&device_count, card_id_list, 8);
    if(ret != 0)
    { 
        std::cout << "Failed to obtain the device ID list, dcmi_get_card_num_list error code: " << ret << std::endl;
        return -1;
    }

    if(type == "300IPro")
    {
        type = "Atlas 300I Pro";
    }
    else if(type == "300V")
    {
        type = "Atlas 300V";
    }
    else if(type == "300IDuo")
    {
        type = "Atlas 300I Duo";
    }
    else
    {
        std::cout << "[ERROR] The second parameter is invalid. supported input: cpu, 300IPro, 300V, 300IDuo." << std::endl;
        return -1;
    }

    for(int i = 0; i < device_count; ++i)
    {
        char product_type_str[64] = {0};
        ret = dcmi_get_product_type(card_id_list[i], 0, product_type_str, 64);
        if(ret != 0)
        {
            std::cout << "[ERROR] Failed to obtain the AI_CARD type, error code: " << ret << ". Use the npu-smi tool or ascend dmi tool to check the device." << std::endl;
            return ret;
        }

        if(type == std::string(product_type_str))
        {
            int power = 0;
            ret = dcmi_mcu_get_power_info(card_id_list[i], &power);
            if(ret != 0|| power == 0x7FFD || power == 0x7FFF)
            {
                std::cout << "[ERROR] Failed to obtain " << type << " power, error code: " << ret << " | " << power << std::endl;
                return ret;
            }
            
            result = "Power:" + std::to_string((float)power/10) + " W";
        }
    }

    if(result.empty())
    {
        std::cout << "[ERROR] The target device cannot be queried. Use the npu-smi tool or ascend dmi tool to check for the presence of the device." << type << std::endl;
    }
    else
    {
        std::cout << result << std::endl;
    }

END:
    return 0;
}

int GetList(const int fd, const int fanBusId_1, const int fanBusId_2)
{
    int device_count = 0;
    int card_id_list[8] = {0};
    int ret = 0;

    ret = dcmi_get_card_num_list(&device_count, card_id_list, 8);
    if(ret != 0)
    { 
        std::cout << "Failed to obtain the device ID list, dcmi_get_card_num_list error code: " << ret << std::endl;
        return -1;
    }

    for(int i = 0; i < device_count; ++i)
    {
        char product_type_str[64] = {0};
        ret = dcmi_get_product_type(card_id_list[i], 0, product_type_str, 64);
        if(ret != 0)
        {
            std::cout << "[ERROR] Failed to obtain the AI_CARD type, error code: " << ret << ". Use the npu-smi tool or ascend dmi tool to check the device." << std::endl;
            return ret;
        }

        struct dcmi_tag_pcie_idinfo pcie_idinfo;
        memset(&pcie_idinfo, 0, sizeof(dcmi_tag_pcie_idinfo));
        ret = dcmi_get_pcie_info(card_id_list[i], 0, &pcie_idinfo);
        if(ret != 0)
        {
            std::cout << "[ERROR] Failed to obtain device bus_id, dcmi_get_pcie_info error code: " << ret << std::endl;
            return -1;
        }

        if(fanBusId_1 == pcie_idinfo.bdf_busid)
        {
            std::cout << product_type_str << ": AI_CARD1" << pcie_idinfo.bdf_busid << std::endl;
        }
        else if(fanBusId_2 == pcie_idinfo.bdf_busid)
        {
            std::cout << product_type_str << ": AI_CARD2" << pcie_idinfo.bdf_busid << std::endl;
        }
        else
        {
            std::cout << "[ERROR] dcmi " << product_type_str << " bus_id does not match FanControlParam.txt; dcmi bus_id is " << pcie_idinfo.bdf_busid << "; file fan_1_bus_id is " << fanBusId_1 << "; file fan_2_bus_id is " << fanBusId_2 << std::endl;
            std::cout << "[ERROR] Please update " << MODE_FILE_PATH << std::endl;
        }
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

int SetFan(const int fd, std::string type, std::string value)
{
    std::string cmd;
    std::string result;
    int         FanPWM = 0;
    char        recvBuf[MAX_RECV_BUF_SIZE] = {0};
    int         ret = 0;

    // 判断 value 是否合法    
    try
    {
        FanPWM = stoi(value);
        if(FanPWM > 100 || FanPWM < 0)
        {
            std::cout << "[ERROR] The entered fan PWM duty cycle is invalid. Please enter a value ranging from 0 to 100." << std::endl;
            return -1;
        }
    }
    catch (const std::invalid_argument& e)
    {
        std::cout << "[ERROR] The input fan PWM duty cycle contains illegal characters. Please enter numbers 0-100." << std::endl;
        return -1;
    }

    value = Int2StrPadZero(FanPWM, 3);

    if(type == "cpu")
    {
        cmd = "$F0S" + value;
    }
    else if(type == "sysFan")
    {
        cmd = "$F1S" + value;
    }
    else if(type == "AI_CARD1")
    {
        cmd = "$F2S" + value;
    }
    else if(type == "AI_CARD2")
    {
        cmd = "$F3S" + value;
    }
    else
    {
        std::cout << "[ERROR] The second parameter is invalid. supported input: cpu, sysFan, AI_CARD1 AI_CARD2." << std::endl;
        return -1;
    }

    ret = ExecCommand(fd, cmd.data(), recvBuf, MAX_RECV_BUF_SIZE);
    if(ret != 0)
    {
        return -1;
    }

    std::cout << "Set PWM success, serial port response: " << recvBuf << std::endl;
    return 0;
}

int SetAuto()
{
    int fp = open(MODE_FILE_PATH, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
    // 请求排他锁（写锁），并等待直到成功获取锁
    if (flock(fp, LOCK_EX) == -1)
    {
        std::cout << "[ERROR] Failed to lock the file. file path:" << MODE_FILE_PATH << std::endl;
        close(fp);
        return -1;
    }

    std::fstream file(MODE_FILE_PATH, std::ios::out);
    if(!file.is_open())
    {
        std::cout << "[ERROR] Failed to open the file. file path:" << MODE_FILE_PATH << std::endl;
        return -1;
    }

    file << "1";
    file.close();
    flock(fp, LOCK_UN);
    close(fp);
    std::cout << "Set automatic mode success!" << std::endl;
    return 0;
}

int GetHelp()
{
    std::cout << "Get temperature: ManFanCtrl -t <device_name> , supported device_name: cpu, mainboard, 300IPro, 300V, 300IDuo" << std::endl;
    std::cout << "Get power: ManFanCtrl -p <device_name> , supported device_name: cpu, 300IPro, 300V, 300IDuo" << std::endl;
    std::cout << "SetPWM: ManFanCtrl -s <device_name> <PWM> , supported device_name: cpu, sysFan, AI_CARD1, AI_CARD2, cmd example: ManFanCtrl -s cpu 10" << std::endl;
    std::cout << "Get AI_CARD list: ManFanCtrl -l" << std::endl;
    std::cout << "Set auto mode: ManFanCtrl -a" << std::endl;
    return 0;
}

int main(int argc, char *argv[])
{
    int fanBusId_1 = 0;
    int fanBusId_2 = 0;
    json root;

    if(argc < 2)
    {
        std::cout << "[ERROR] Parameters are too few, please enter ManFanCtrl -h to view the format!" << std::endl;
        return -1;
    }

    std::fstream file(MODE_FILE_PATH, std::ios::in);
    int fp = 0;

    if(file.is_open())
    {
        bool flag = true;

        file >> root;
        file.close();

        flag = root["mode"];
        fanBusId_1 = root["fan_1_bus_id"];
        fanBusId_2 = root["fan_2_bus_id"];
        if(!flag)
        {
            goto FILE_OK;
        }
    }

    //修改文件
    fp = open(MODE_FILE_PATH, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
    // 请求排他锁（写锁），并等待直到成功获取锁
    if (flock(fp, LOCK_EX) == -1)
    {
        std::cout << "[ERROR] Failed to lock the file. file path: " << MODE_FILE_PATH << std::endl;
        close(fp);
        return -1;
    }

    file.open(MODE_FILE_PATH, std::ios::out);
    if(!file.is_open())
    {
        std::cout << "[ERROR] Failed to open the file. file path: " << MODE_FILE_PATH << std::endl;
        return -1;
    }

    root["mode"] = false;
    file << root.dump(4);
    file.close();
    flock(fp, LOCK_UN);
    close(fp);
    //等待自动程序关闭串口
    sleep(1);

FILE_OK:

    //初始化串口
    int fd = 0, ret = 0;
    ret = SerialOpen(&fd);
    if(ret != 0)
    {
        std::cout << "[ERROR] Fail to open serial port /dev/ttyUSB0!" << std::endl;
        return ret;
    }

    ret = dcmi_init();
    if(ret != 0)
    {
        std::cout << "[ERROR] Fail to init dcmi, error code: " << ret << std::endl;
        return ret;
    }

    if(std::string(argv[1]) == "-h")
    {
        ret = GetHelp();
    }
    else if(std::string(argv[1]) == "-t")
    {
        if(argc < 3)
        {
            std::cout << "[ERROR] param is too few, cmd format: ManFanCtrl -t <device_name>, device_name: cpu, mainboard, 300IPro, 300V, 300IDuo" << std::endl;
            return -1;
        }

        ret = GetTemper(fd, argv[2]);
    }   
    else if(std::string(argv[1]) == "-p")
    {
        if(argc < 3)
        {
            std::cout << "[ERROR] param is too few, cmd format: ManFanCtrl -p <device_name>, device_name: cpu, 300IPro, 300V, 300IDuo" << std::endl;
            return -1;
        }

        ret = GetPower(fd, argv[2]);
    } 
    else if(std::string(argv[1]) == "-l")
    {
        ret = GetList(fd, fanBusId_1, fanBusId_2);
    }
    else if(std::string(argv[1]) == "-s")
    {
        if(argc < 4)
        {
            std::cout << "[ERROR] param is too few, ManFanCtrl -s <device_name> <PWM>, device_name: cpu, sysFan, AI_CARD1, AI_CARD2，命令示例： ManFanCtrl -s cpu 10" << std::endl;;
            return -1;
        }
        ret = SetFan(fd, argv[2], argv[3]);
    }
    else if(std::string(argv[1]) == "-a")
    {
        ret = SetAuto();
    }
    else
    {
        std::cout << "[ERROR] First param error, supported type: -h(help), -l(Get AI_CARD list), -t(Get temperatrue), -p(Get power), -s(Set PWM), -a(Set auto mode)!" << std::endl;
        return -1;
    }
    
    SerialClose(&fd);
    return ret;
}



#endif // MANUAL_FAN_CONTROL_H