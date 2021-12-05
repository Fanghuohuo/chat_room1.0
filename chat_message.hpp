#ifndef CHAT_MESSAGE_HPP
#define CHAT_MESSAGE_HPP
#include "structHeader.h"

#include <iostream>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>

//done!!
//服务器开发关键主要是对客户端数据的处理

//这里相当于把聊天对话的信息封装了一下
class chat_message
{
public:
  //首先定义了头部长度和最大body的长度
  //因为头部是定长的，所以好处理，所以一开始一般先处理头部
  enum { header_length = sizeof(Header) };
  enum { max_body_length = 512 };

  chat_message(){}

  const char* data() const{
    return data_;
  }

  char* data(){
    return data_;
  }

  //这个长度总长度，因此是头部长度加上body的长度
  std::size_t length() const{
    return header_length + m_header.bodySize;
  }

  //data_相当于是数组的开始，然后偏移header_length的那个地址
  //就是body的开始，然后返回
  const char* body() const{
    return data_ + header_length;
  }

  char* body(){
    return data_ + header_length;
  }

  //返回m_header的成员type即可
  int type() const {
    return m_header.type;
  }

  std::size_t body_length() const{
    return m_header.bodySize;
  }
  
  //这里在客户端和服务器中都有用到，就是把外面的RoomInformation、ChatInformation
  //之类的统一封装成chat_message这种结构，chat_message就像一个协议一样
  //大家都搞成这个样子
  void setMessage(int messageType, const void* buffer, std::size_t bufferSize){
    //这个buffer就是前面整个结构体
    //比如是RoomInformation，那现在需要搞成Header和data两部分
    //检查合法性
    assert(bufferSize <= max_body_length);
    //相当于buffer就是body的内容，然后再封一个头部
    m_header.bodySize = bufferSize;
    m_header.type = messageType;
    //内容都拷贝到chat_message中,这个body就是data里面body的地址
    //data就是data里面首地址，这两步就是把这两个赋值的
    std::memcpy(body(), buffer, bufferSize);
    std::memcpy(data(), &m_header, sizeof(m_header)); 
  } 

  //对header进行分析（其实header就存了body的长度）
  bool decode_header(){
    //先提取出header
    std::memcpy(&m_header, data(), header_length);
    //之后判断header的合法性
    if(m_header.bodySize > max_body_length){
        std::cout << "body size " << m_header.bodySize << " is too long!!"
        << "type is " << m_header.type << std::endl;
        return false; 
    }
    return true;
  }

private:
  //原来相当于只有bodysize，现在多加了一个类型，因为可能有bindname的内容
  Header m_header;
  char data_[header_length + max_body_length];
};

#endif // CHAT_MESSAGE_HPP
