#include "structHeader.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

//done!!

//input是传参，后面两个是输出
bool parseMessage(const std::string& input, int *type, std::string& outbuffer){
    //string返回的不是迭代器，和历史有关
    auto pos = input.find_first_of(" ");
    //这一部分负责解析，如果没有空格或者空格位置在第一个（没有头部），认为有错
    if(pos == std::string::npos || pos == 0)
        return false;
    //不同消息的消息实体不一样
    //比如"BindName ok" 
    auto command = input.substr(0,pos);
    if(command == "BindName") {
        std::string name = input.substr(pos+1);
        //看看后面名字的长度是不是大于32，如果大于认为不合法
        if(name.size() > 32)
            return false;
        //如果type不是空指针,给type赋值
        if(*type == 0)
            *type = MT_BIND_NAME;
        //定义bindname，封装到bindname结构体里面
        /*struct BindName{
            char name[32];
            int nameLen;
        };*/
        BindName bindInfo;
        bindInfo.nameLen = name.size();
        std::memcpy(&(bindInfo.name),name.data(),name.size());
        //这里由于struct的大小端和对齐都ok，所以这里用的强转
        auto buffer = reinterpret_cast<const char *>(&bindInfo);
        //相当于把bindInfo的所有内容按字符编码(const char*)转到buffer里面去
        //这里就是把结构体转换成对应字节流，输出到output里面去
        //这里outbuffer是string
        outbuffer.assign(buffer, buffer + sizeof(bindInfo));
        return true;
    } else if(command == "Chat") {
        std::string chat = input.substr(pos+1);
        if(chat.size() > 256)
            return false;
        //这里和上面也很像，三板斧
        //先判断合法，然后封装到struct里面，最后转换成字节流输出到output里面去
        ChatInformation info;
        info.infoLen = chat.size();
        std::memcpy(&(info.information),chat.data(),chat.size());
        auto buffer = reinterpret_cast<const char *>(&info);
        outbuffer.assign(buffer, buffer + sizeof(info));
        if(*type == 0)
            *type = MT_CHAT_INFO;
        return true;
    }
    return false;
} 
