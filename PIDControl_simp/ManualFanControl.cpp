#include <iostream>
#include <fstream>
#include <array>
#include <memory>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <string.h>
#include "SerialPort.h"
#include "json.hpp"

using json = nlohmann::json;
using namespace std;


#define MAX_RECV_BUF_SIZE   1024
#define MODE_FILE_PATH  "/etc/FanControlParams.json"
#define CPU_TEMP_FILE_PATH "/sys/class/thermal/thermal_zone0/temp"

#define IF_COND_FAIL(cond, log, todo) \
    do \
    { \
        if (!(cond)) { \
            std::cerr << log << std::endl; \
            todo; \
        } \
    } while (0)


int ExecCommand(int serialFd, const char* cmd, char* recvBuf, int recvBufLen)
{
    IF_COND_FAIL(serialFd != 0, "[ERROR] Serial port /dev/fanctrl is not open.", return -1;);

    int ret = write(serialFd, cmd, strlen(cmd));
    IF_COND_FAIL(ret >= 0, (string("[ERROR] Failed to write the command. cmd: ") + cmd), return ret;);

    usleep(200000); //200ms
    memset(recvBuf, 0, recvBufLen);

    ret = read(serialFd, recvBuf, recvBufLen);
    IF_COND_FAIL(ret >= 0, (string("[ERROR] Failed to read the result. cmd: ") + cmd), return ret;);

    return 0;
}


int GetTemper(const int fd)
{
    string  result;
    string  content;
    char    recvBuf[MAX_RECV_BUF_SIZE] = {0};
    int     ret = 0;

    ifstream file(CPU_TEMP_FILE_PATH);
    IF_COND_FAIL(file.is_open(), (string("[ERROR] Failed to open the cpu temperature record file. file path: ") + CPU_TEMP_FILE_PATH), goto CPU_END);

    getline(file, content);
    file.close();
    IF_COND_FAIL(!content.empty(), (string("[ERROR] Failed to read the cpu temperature file. file path: ") + CPU_TEMP_FILE_PATH), goto CPU_END);

    cout << "CPU Temperature:" << stof(content) / 1000 << " C\n" << endl;

CPU_END:

    ret = ExecCommand(fd, "!GTP", recvBuf, MAX_RECV_BUF_SIZE);
    IF_COND_FAIL(ret == 0, "[ERROR] Failed to get mainboard temperature.", return -1);

    cout << "Mainboard Temperature:" << recvBuf << endl;

    return 0;
}

int GetPower(const int fd)
{
    string  result;
    char    recvBuf[MAX_RECV_BUF_SIZE] = {0};
    int     ret = 0;

    
    ret = ExecCommand(fd, "#GPV", recvBuf, MAX_RECV_BUF_SIZE);
    IF_COND_FAIL(ret == 0, "[ERROR] Failed to get cpu power", return -1);

    cout << "CPU Power: \n" << recvBuf << endl;

    return 0;
}


string Int2StrPadZero(int value, int length)
{
    string result = to_string(value);
    if (result.length() < length)
    {
        result = string(length - result.length(), '0').append(result);
    }

    return result;
}

void UpdateJsonFile(int index, int pwm)
{
    json            root;
    fstream         file(MODE_FILE_PATH, ios::in);
    int             fp = 0;
    int             size = 0;
    vector<int>     pwmVec;

    IF_COND_FAIL(file.is_open(), string("[ERROR] Failed to open config file, file path is :") + MODE_FILE_PATH, return);

    file >> root;
    file.close();

    pwmVec = root["card_fan_pwm_list"].get<vector<int>>();
    size = pwmVec.size();
    if(index < size)
    {
        pwmVec[index] = pwm;
    }
    else
    {
        for(int i = size - 1; i < index; ++i)
        {
            pwmVec.push_back(pwm);
        }
    }

    root["card_fan_pwm_list"] = pwmVec;

    //修改文件
    fp = open(MODE_FILE_PATH, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
    // 请求排他锁（写锁），并等待直到成功获取锁
    if (flock(fp, LOCK_EX) == -1)
    {
        cout << "[ERROR] Failed to lock the file. file path: " << MODE_FILE_PATH << endl;
        close(fp);
        return;
    }

    file.open(MODE_FILE_PATH, ios::out);
    if(!file.is_open())
    {
        cout << "[ERROR] Failed to open the file. file path: " << MODE_FILE_PATH << endl;
        return;
    }

    file << root.dump(4);
    file << "\n";
    file.close();
    flock(fp, LOCK_UN);
    close(fp);
}

int SetFan(const int fd, string type, string value)
{
    string      cmd;
    string      result;
    int         FanPWM = 0;
    char        recvBuf[MAX_RECV_BUF_SIZE] = {0};
    int         ret = 0;

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
        UpdateJsonFile(0, FanPWM);
    }
    else if(type == "AI_CARD2")
    {
        cmd = "$F3S" + value;
        UpdateJsonFile(1, FanPWM);
    }
    else
    {
        cout << "[ERROR] The second parameter is invalid. supported input: cpu, sysFan, AI_CARD1 AI_CARD2." << endl;
        return -1;
    }

    ret = ExecCommand(fd, cmd.data(), recvBuf, MAX_RECV_BUF_SIZE);
    if(ret != 0)
    {
        return -1;
    }

    cout << "Set PWM success, serial port response: " << recvBuf << endl;
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

int GetHelp()
{
    cout << "Get devices temperature: ManFanCtrl -t" << endl;
    cout << "Get devices power: ManFanCtrl -p" << endl;
    cout << "SetPWM: ManFanCtrl -s <device_name> <PWM> , supported device_name: cpu, sysFan, AI_CARD1, AI_CARD2. Cmd example: ManFanCtrl -s cpu 10" << endl;
    cout << "Set auto mode: ManFanCtrl -a" << endl;
    cout << "Get version information: -v" << endl;
    cout << "Get cpu fan speed: -r" << endl;
    return 0;
}

int GetCpuFanSpeed(const int fd)
{
    int     ret = -1;
    char    recvBuf[MAX_RECV_BUF_SIZE] = {0};

    ret = ExecCommand(fd, "@GSV", recvBuf, MAX_RECV_BUF_SIZE);
    IF_COND_FAIL(ret == 0, ("[ERROR] Failed to get cpu fan speed. ret is " + to_string(ret)).data(), return ret);

    cout << recvBuf << endl;
    
    return 0;
}

int main(int argc, char *argv[])
{
    json        root;

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

    //初始化串口
    int fd = 0, ret = 0;
    ret = SerialOpen(&fd);
    if(ret != 0)
    {
        cout << "[ERROR] Fail to open serial port /dev/fanctrl!" << endl;
        return ret;
    }

    if(string(argv[1]) == "-h")
    {
        ret = GetHelp();
    }
    else if(string(argv[1]) == "-t")
    {
        ret = GetTemper(fd);
    }   
    else if(string(argv[1]) == "-p")
    {
        ret = GetPower(fd);
    } 
    else if(string(argv[1]) == "-s")
    {
        if(argc < 4)
        {
            cout << "[ERROR] param is too few, ManFanCtrl -s <device_name> <PWM>, device_name: cpu, sysFan，命令示例： ManFanCtrl -s cpu 10" << endl;;
            return -1;
        }
        ret = SetFan(fd, argv[2], argv[3]);
    }
    else if(string(argv[1]) == "-a")
    {
        ret = SetAuto();
    }
    else if (string(argv[1]) == "-v")
    {
        cout << "Type: PID_SIMP" << endl;
        cout << "Version: 1.0.0" << endl;
        cout << "Time: 2025.7.23" << endl;
    }
    else if(string(argv[1]) == "-r")
    {
        ret = GetCpuFanSpeed(fd);
    }
    else
    {
        cout << "[ERROR] First param error, supported type: -h(help), -t(Get temperatrue), -p(Get power), -s(Set PWM), -a(Set auto mode)!" << endl;
        return -1;
    }
    
    SerialClose(&fd);
    return ret;
}
