#include "FanController.h"
#include "SerialPort.h"
#include "json.hpp"
#include "dcmi_interface_api.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <syslog.h>
#include <iostream>
#include <fstream>
// #include <filesystem>
#include <sys/stat.h>

using json = nlohmann::json;
using namespace std;

GlobalParams            g_params;
std::shared_mutex       params_mutex;

const string DEFAULT_JSON = R"(
    {
        "mode": true,
        "card_fan_bus_id_list": [1,2]
    }
)";

bool file_exists(const std::string& path) {
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

bool CreateDefaultFile(string filePath)
{
    int ret = -1;
    int fd = -1;

    // 写锁
    fd = open(filePath.data(), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
    IF_COND_FAIL(fd != -1, (string("[ERROR] CreateDefaultFile: Failed to get fd")+filePath).data(), return false);

    ret = flock(fd, LOCK_EX);
    IF_COND_FAIL(ret != -1, "[ERROR] CreateDefaultFile: Failed to lock file, LOCK_EX", return false);

    // 以写入模式创建文件
    ofstream outfile(filePath);
    IF_COND_FAIL(outfile.is_open(), "[ERROR] CreateDefaultFile: Fail to create /etc/FanControlParams.json", return false);
    
    outfile << DEFAULT_JSON;
    outfile.close();

    // 解锁
    flock(fd, LOCK_UN);
    close(fd);
    
    return true;
}

void* ParamsListen(void* arg)
{
    int         ret = -1;
    int         fd = -1;
    json        root;

    while(true)
    {
        // 读锁
        fd = open(MODE_FILE_PATH, O_WRONLY, S_IRUSR | S_IWUSR);
        IF_COND_FAIL(fd != -1, (string("[ERROR] ParamsListen : Failed to get fd")+MODE_FILE_PATH).data(), continue;);
        
        ret = flock(fd, LOCK_SH);
        IF_COND_FAIL(ret != -1, "[ERROR] ParamsListen : Failed to lock file, LOCK_SH", continue;);

        // 解析 json
        ifstream  file(MODE_FILE_PATH);
        IF_COND_FAIL(file.is_open(), "[ERROR] ParamsListen : Failed to open file", continue;);

        file >> root;
        if(root["mode"].is_null() || !root["mode"].is_boolean())
        {
            syslog(LOG_INFO, "[ERROR] ParamsListen : Failed to parse json file, mode is null or not bool type. Create default json file.");
            // cout << "[ERROR] ParamsListen : Failed to parse json file, mode is null or not bool type. Create default json file." << endl;
            CreateDefaultFile(MODE_FILE_PATH);
            continue;
        }
        else if(root["card_fan_bus_id_list"].is_null() || !root["card_fan_bus_id_list"].is_array())
        {
            syslog(LOG_INFO, "[ERROR] ParamsListen : Failed to parse json file, card_fan_bus_id_list is null or not array type. Create default json file.");
            // cout << "[ERROR] ParamsListen : Failed to parse json file, card_fan_bus_id_list is null or not array type. Create default json file." << endl;
            CreateDefaultFile(MODE_FILE_PATH);
            continue;
        }

        g_params.update(root["mode"], root["card_fan_bus_id_list"].get<vector<int>>());

        //解锁
        flock(fd, LOCK_UN);
        close(fd);

        //重置变量，间隔1s
        root.clear();
        ret = fd = -1;
        sleep(1);
    }

    return NULL;
}

int main()
{
    int                     fd = 0;
    int                     ret = -1;
    pthread_t               paramsTid;

    // 打开串口
    ret = SerialOpen(&fd);
    IF_COND_FAIL(ret == 0, "[ERROR] Fail to open serial port /dev/fanctrl, process exit", return -1);

    // 检查配置文件是否存在，不存在就创建默认的
    // if(!filesystem::exists(MODE_FILE_PATH))
    if(!file_exists(MODE_FILE_PATH))
    {
        IF_COND_FAIL(CreateDefaultFile(MODE_FILE_PATH), "CreateDefaultFile fail, process exit", return -1);
    }

    // 创建线程，不断更新 g_params
    pthread_create(&paramsTid, NULL, ParamsListen, NULL);
    pthread_detach(paramsTid);

    CPUController           cpuCtrl(fd);
    SysController           sysCtrl(fd);
    bool                    resetFlag = false;
    int                     cardNum = 0;
    int                     cardList[8] = {0};
    vector<CardController>  cardCtrlVec;

    // 先开cpu和sys的风扇
    cpuCtrl.SetPwm();
    sysCtrl.SetPwm();

    ret = dcmi_init();
    IF_COND_FAIL(ret == 0 || ret == -8005, ("[ERROR] dcmi_init fail, ret is" + to_string(ret) + ", process exit").data(), return -1;);

    ret = dcmi_get_card_list(&cardNum, cardList, 8);
    IF_COND_FAIL(ret == 0 || ret == -8005, ("[ERROR] dcmi_get_card_list fail, ret is" + to_string(ret) + ", process exit").data(), return -1;);

    // cout << "[INFO] card_num is " << cardNum << endl;

    for(int i = 0; i < cardNum; ++i)
    {
        // cout << "[INFO] card_list[" << i << "] is " << cardList[i] << endl;
        CardController item(fd, cardList[i]);
        cardCtrlVec.push_back(item);
    }

    while(true)
    {
        if(g_params.getMode())
        {
            if(resetFlag)
            {
                // 打开串口
                ret = SerialOpen(&fd);
                IF_COND_FAIL(ret == 0, "[ERROR] Fail to open serial port /dev/fanctrl, process exit", return -1);

                cpuCtrl.Restart(fd);
                sysCtrl.Restart(fd);
                for(auto it = cardCtrlVec.begin(); it != cardCtrlVec.end(); ++it)
                {
                    it->Restart(fd);
                }

                resetFlag = false;
            }

            cpuCtrl.SetPwm();
            sysCtrl.SetPwm();
            for(auto it = cardCtrlVec.begin(); it != cardCtrlVec.end(); ++it)
            {
                it->SetPwm();
            }
        }
        else
        {
            resetFlag = true;

            // 关闭串口
            SerialClose(&fd);
            syslog(LOG_INFO, "[INFO] Serial port close, mode is Manual.");
            // cout << "[INFO] Serial port close, mode is Manual." << endl;
        }

        sleep(5);
    }

    return 0;
}