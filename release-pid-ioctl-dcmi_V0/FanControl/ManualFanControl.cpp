#include <iostream>
#include <fstream>
#include <array>
#include <memory>
#include <regex>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h> 
#include <sys/file.h>
#include <string.h>
#include "dcmi_interface_api.h"
#include "json.hpp"

using json = nlohmann::json;
using namespace std;


#define MODE_FILE_PATH  "/etc/FanControlParams.json"
#define DEFAULT_FAN_MODE 2

#define IF_COND_FAIL(cond, log, todo) \
    do \
    { \
        if (!(cond)) { \
            std::cerr << log << std::endl; \
            todo; \
        } \
    } while (0)

struct sio_ioctl_data {
    unsigned char fan_num;
    unsigned char fan_mode;
    unsigned char duty;
};

#define IOC_MAGIC 'c'
#define IOC_COMMAND_SET _IOW(IOC_MAGIC,0,struct sio_ioctl_data)
#define IOC_COMMAND_GET _IOWR(IOC_MAGIC,1,int)
#define IOC_COMMAND_RPM _IOWR(IOC_MAGIC,2,int)

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

int ExecCommand(int fd, int cmd, void* pIn)
{
    IF_COND_FAIL(fd != 0 && fd != -1, "[ERROR] Fd is " + to_string(fd) + ", /dev/aaeon_sio is not open.", return -1;);

    int ret = ioctl(fd, cmd, pIn);
    IF_COND_FAIL(ret == 0, ("[ERROR] Failed to ioctl the command. cmd: "+ to_string(cmd)).data(), return ret;);

    return 0;
}


int GetTemper()
{
    int     device_count = 0;
    int     card_id_list[8] = {0};
    int     ret = 0;

    ret = dcmi_get_card_num_list(&device_count, card_id_list, 8);
    IF_COND_FAIL(ret == 0, (string("[ERROR] Failed to obtain the device ID list, dcmi_get_card_num_list error code: ") + to_string(ret)), return -1);

    for(int i = 0; i < device_count; ++i)
    {
        char product_type_str[64] = {0};
        ret = dcmi_get_product_type(card_id_list[i], 0, product_type_str, 64);
        IF_COND_FAIL(ret == 0, (string("[ERROR] Failed to obtain the AI_CARD type, error code: ") + to_string(ret) + ". Use the npu-smi tool or ascend dmi tool to check the device."), continue;);

        
        int npuTemp = 0;
        ret = dcmi_get_device_temperature(card_id_list[i], 0, &npuTemp);
        IF_COND_FAIL(ret == 0, (string("[ERROR] Failed to obtain  ") + product_type_str + " temperature, error code: " + to_string(ret)), continue);
        
        cout << product_type_str << "(card_id: " << card_id_list[i] << ") Temperature:" + to_string(npuTemp) + " C\n" << endl;
    }

    return 0;
}

int GetPower()
{
    int     device_count = 0;
    int     card_id_list[8] = {0};
    int     ret = 0;

    ret = dcmi_get_card_num_list(&device_count, card_id_list, 8);
    IF_COND_FAIL(ret == 0, (string("[ERROR] Failed to obtain the device ID list, dcmi_get_card_num_list error code: ") + to_string(ret)), return -1);

    for(int i = 0; i < device_count; ++i)
    {
        char product_type_str[64] = {0};
        ret = dcmi_get_product_type(card_id_list[i], 0, product_type_str, 64);
        IF_COND_FAIL(ret == 0, (string("[ERROR] Failed to obtain the AI_CARD type, error code: ") + to_string(ret) + ". Use the npu-smi tool or ascend dmi tool to check the device."), continue);

        int power = 0;
        ret = dcmi_mcu_get_power_info(card_id_list[i], &power);
        IF_COND_FAIL(ret == 0 && power != 0x7FFD && power != 0x7FFF, (string("[ERROR] Failed to obtain ") + product_type_str + " power, error code: " + to_string(ret)), continue);
        
        cout << product_type_str << "(card_id: " << card_id_list[i] << ") Power:" + to_string((float)power/10) << " W\n" << endl;
    }

    return 0;
}

int GetCardFanList()
{
    int ret = -1;
    vector<int> cardVec;

    // 读锁
    int fd = open(MODE_FILE_PATH, O_WRONLY, S_IRUSR | S_IWUSR);
    IF_COND_FAIL(fd != -1, (string("[ERROR] Failed to get lock fd")+MODE_FILE_PATH).data(), return -1;);
    
    ret = flock(fd, LOCK_SH);
    IF_COND_FAIL(ret != -1, "[ERROR] Failed to lock file, LOCK_SH", return -1;);

    json        root;
    ifstream    file(MODE_FILE_PATH);

    IF_COND_FAIL(file.is_open(), string("[ERROR] Failed to open config file, file path: ") + MODE_FILE_PATH, return -1);
    file >> root;
    file.close();
    cardVec = root["card_fan_bus_id_list"].get<vector<int>>();
    for(auto it = cardVec.begin(); it != cardVec.end(); ++it)
    {
        cout << "AI_CARD" << (it - cardVec.begin()) + 1 << " ----- bus_id: " << *it << endl;  
    }
    
    return 0;
}

int SetFan(const int fd, string type, string value)
{
    regex           pattern("^AI_CARD([1-9])$");
    std::smatch     matches;
    int             FanPWM = 0;
    int             ret = 0;
    sio_ioctl_data  cardData;

    // 判断 value 是否合法    
    try
    {
        FanPWM = stoi(value);
        if(FanPWM > 100 || FanPWM < 0)
        {
            cout << "[ERROR] The entered fan PWM duty cycle is invalid. Please enter a value ranging from 0 to 100." << endl;
            return -1;
        }
    }
    catch (const invalid_argument& e)
    {
        cout << "[ERROR] The input fan PWM duty cycle contains illegal characters. Please enter numbers 0-100." << endl;
        return -1;
    }

    if(regex_match(type, matches, pattern))
    {
        string numStr = matches[1].str();
        int number = std::stoi(numStr);
        cardData.fan_num = number + 1;
        cardData.fan_mode = DEFAULT_FAN_MODE;
        cardData.duty = Pwm2Duty(FanPWM);
    }
    else
    {
        cout << "[ERROR] The second parameter is invalid. supported input: cpu, sysFan, AI_CARD1 AI_CARD2." << endl;
        return -1;
    }

    ret = ExecCommand(fd, IOC_COMMAND_SET, &cardData);
    if(ret != 0)
    {
        return -1;
    }

    cout << "Set PWM success!" << endl;
    return 0;
}

int SetAuto()
{
    json root;

    int fp = open(MODE_FILE_PATH, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
    // 请求排他锁（写锁），并等待直到成功获取锁
    if (flock(fp, LOCK_EX) == -1)
    {
        cout << "[ERROR] Failed to lock the file. file path:" << MODE_FILE_PATH << endl;
        close(fp);
        return -1;
    }

    fstream file(MODE_FILE_PATH, ios::in);
    IF_COND_FAIL(file.is_open(), string("[ERROR] Failed to open the file. file path:") + MODE_FILE_PATH, return -1);

    file >> root;
    file.close();

    file.open(MODE_FILE_PATH, ios::out);
    IF_COND_FAIL(file.is_open(), string("[ERROR] Failed to open the file. file path:") + MODE_FILE_PATH, return -1);

    root["mode"] = true;
    file << root.dump(4);
    file << "\n";
    file.close();
    flock(fp, LOCK_UN);
    close(fp);
    cout << "Set automatic mode success!" << endl;
    return 0;
}

int GetRpm(int fd)
{
    int     cpuRpm = 1, cardRpm_1 = 2, cardRpm_2 = 3;
    int     ret = 0;

    ret = ExecCommand(fd, IOC_COMMAND_RPM, &cpuRpm);
    IF_COND_FAIL(ret == 0, "[ERROR] Failed to get the cpu fan speed.", goto CPU_END);
    cout << "CPU Fan Speed:" << cpuRpm << "\n" << endl;

CPU_END:

    ret = ExecCommand(fd, IOC_COMMAND_RPM, &cardRpm_1);
    IF_COND_FAIL(ret == 0, "[ERROR] Failed to get the AI_CARD1 fan speed.", goto CARD_1_END);
    cout << "AI_CARD1 Fan Speed:" << cardRpm_1 << "\n" << endl;

CARD_1_END:

    ret = ExecCommand(fd, IOC_COMMAND_RPM, &cardRpm_2);
    IF_COND_FAIL(ret == 0, "[ERROR] Failed to get the AI_CARD2 fan speed.", return -1);
    cout << "AI_CARD2 Fan Speed:" << cardRpm_2 << "\n" << endl;

    return 0;
}

int GetDuty(int fd)
{
    int     cpuDuty = 1, cardDuty_1 = 2, cardDuty_2 = 3;
    int     cpuPwm = 0, cardPwm_1 = 0, cardPwm_2 = 0;
    int     ret = 0;

    ret = ExecCommand(fd, IOC_COMMAND_GET, &cpuDuty);
    IF_COND_FAIL(ret == 0, "[ERROR] Failed to get the cpu fan speed.", goto CPU_END);
    cpuPwm = Duty2Pwm(cpuDuty);
    cout << "CPU Duty: " << cpuDuty << endl;
    cout << "CPU Pwm: " << cpuPwm << "\n" << endl;

CPU_END:

    ret = ExecCommand(fd, IOC_COMMAND_GET, &cardDuty_1);
    IF_COND_FAIL(ret == 0, "[ERROR] Failed to get the AI_CARD1 fan speed.", goto CARD_1_END);
    cardPwm_1 = Duty2Pwm(cardDuty_1);
    cout << "AI_CARD1 Duty: " << cardDuty_1 << endl;
    cout << "AI_CARD1 Pwm: " << cardPwm_1 << "\n" << endl;

CARD_1_END:

    ret = ExecCommand(fd, IOC_COMMAND_GET, &cardDuty_2);
    IF_COND_FAIL(ret == 0, "[ERROR] Failed to get the AI_CARD2 fan speed.", return -1);
    cardPwm_2 = Duty2Pwm(cardDuty_2);
    cout << "AI_CARD2 Duty: " << cardDuty_2 << endl;
    cout << "AI_CARD2 Pwm: " << cardPwm_2 << "\n" << endl;

    return 0;
}

int GetHelp()
{
    cout << "Get devices temperature: ManFanCtrl -t" << endl;
    cout << "Get devices power: ManFanCtrl -p" << endl;
    cout << "SetPWM: ManFanCtrl -s <device_name> <PWM> , supported device_name: AI_CARD1, AI_CARD2, cmd example: ManFanCtrl -s cpu 10" << endl;
    cout << "Get AI_CARDX --- bus_id list: ManFanCtrl -l" << endl;
    cout << "Set auto mode: ManFanCtrl -a" << endl;
    cout << "Get fan speed: ManFanCtrl -r" << endl;
    cout << "Get fan duty and pwm: ManFanCtrl -d" << endl;
    return 0;
}

int main(int argc, char *argv[])
{
    json        root;
    vector<int> cardVec;

    if(argc < 2)
    {
        cout << "[ERROR] Parameters are too few, please enter ManFanCtrl -h to view the format!" << endl;
        return -1;
    }

    fstream file(MODE_FILE_PATH, ios::in);
    int fp = 0;

    IF_COND_FAIL(file.is_open(), string("[ERROR] Failed to open config file, file path is :") + MODE_FILE_PATH, return -1);

    file >> root;
    file.close();
    cardVec = root["card_fan_bus_id_list"].get<vector<int>>();
    if(!(root["mode"]))
    {
        goto FILE_OK;
    }

    //修改文件
    fp = open(MODE_FILE_PATH, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
    // 请求排他锁（写锁），并等待直到成功获取锁
    if (flock(fp, LOCK_EX) == -1)
    {
        cout << "[ERROR] Failed to lock the file. file path: " << MODE_FILE_PATH << endl;
        close(fp);
        return -1;
    }

    file.open(MODE_FILE_PATH, ios::out);
    if(!file.is_open())
    {
        cout << "[ERROR] Failed to open the file. file path: " << MODE_FILE_PATH << endl;
        return -1;
    }

    root["mode"] = false;
    file << root.dump(4);
    file << "\n";
    file.close();
    flock(fp, LOCK_UN);
    close(fp);
    //等待自动程序关闭串口
    sleep(1);

FILE_OK:

    //open
    int fd = 0, ret = 0;
    fd = open("/dev/aaeon_sio",O_RDWR);
    if(fd == -1)
    {
        cout << "[ERROR] Fail to open /dev/aaeon_sio!" << endl;
        return ret;
    }

    ret = dcmi_init();
    if(ret != 0)
    {
        cout << "[ERROR] Fail to init dcmi, error code: " << ret << endl;
        return ret;
    }

    if(string(argv[1]) == "-h")
    {
        ret = GetHelp();
    }
    else if(string(argv[1]) == "-t")
    {
        ret = GetTemper();
    }   
    else if(string(argv[1]) == "-p")
    {
        ret = GetPower();
    } 
    else if(string(argv[1]) == "-l")
    {
        ret = GetCardFanList();
    }
    else if(string(argv[1]) == "-s")
    {
        if(argc < 4)
        {
            cout << "[ERROR] param is too few, ManFanCtrl -s <device_name> <PWM>, device_name: AI_CARD1, AI_CARD2，命令示例： ManFanCtrl -s cpu 10" << endl;;
            return -1;
        }
        ret = SetFan(fd, argv[2], argv[3]);
    }
    else if(string(argv[1]) == "-a")
    {
        ret = SetAuto();
    }
    else if(string(argv[1]) == "-r")
    {
        ret = GetRpm(fd);
    }
    else if(string(argv[1]) == "-d")
    {
        ret = GetDuty(fd);
    }
    else
    {
        cout << "[ERROR] First param error, supported type: -h(help), -l(Get AI_CARD --- bus_id list), -t(Get temperatrue), -p(Get power), -s(Set PWM), -a(Set auto mode)!" << endl;
        return -1;
    }
    
    close(fd);
    return ret;
}
