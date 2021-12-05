//先是自己的
#include "structHeader.h"
#include "chat_message.hpp"

//然后是第三方的
#include <boost/asio.hpp>

//然后是c++库函数
#include <deque>
#include <iostream>
#include <thread>

//最后是c库函数
#include <cstdlib>

//done!!

using boost::asio::ip::tcp;
//服务器和客户端的协议一般都是共用的
//这个统一的chat_message就相当于是协议
using chat_message_queue = std::deque<chat_message>;

class chat_client{
public:
  chat_client(boost::asio::io_context& io_context,
      const tcp::resolver::results_type& endpoints)
    : io_context_(io_context),
      socket_(io_context)
  { //这里在构造的时候就已经建立了网络连接
    //有优有劣，优就是接口比较简约；劣就是有时候不希望构造的时候就连接
    //灵活性会差一些
    do_connect(endpoints);
  }

  void write(const chat_message& msg)
  { //这里用的是post，为什么呢？
    //就相当于用post生成一个事件，这个事件在io_context的控制下去跑
    //因为io_context是可以在多线程下面跑的，在后面调用write是放到
    //另外一个线程里面跑的
    boost::asio::post(io_context_,
        [this, msg]() //这里msg是值拷贝，而不是值引用
        { //这里和chat message中的deliver处理是一样的
          bool write_in_progress = !write_msgs_.empty();
          write_msgs_.push_back(msg);
          //只有write_msgs_是空的时候才进行do_write，防止调用两次do_write
          if (!write_in_progress){
            do_write();
          }
        });
  }

  void close()
  { //这里调用close的时候也调用post
    //就相当于用post生成一个事件，这个事件在io_context的控制下去跑
    //这里因为在不同线程下，可能会出现资源占用的情况，需要io控制
    boost::asio::post(io_context_, [this]() { socket_.close(); });
  }

private:
  //这里是异步连接
  //有什么好处呢，比如说游戏，在后台连接的时候就会准备相关的
  //图形渲染，还有音效处理相关的东西，连接好了这些准备也准备好了 
  void do_connect(const tcp::resolver::results_type& endpoints){
    boost::asio::async_connect(socket_, endpoints,
        [this](boost::system::error_code ec, tcp::endpoint)
        { //回调函数
          if (!ec){
            do_read_header();
          }
        });
  }

  //这里和服务端一样，也是先读头部的信息
  void do_read_header(){
    boost::asio::async_read(socket_,
        boost::asio::buffer(read_msg_.data(), chat_message::header_length),
        [this](boost::system::error_code ec, std::size_t /*length*/){
          if (!ec && read_msg_.decode_header()){
            //通过头部检查body的合法性
            do_read_body();
          }
          else
          { //这里错误处理是关闭连接，为什么是关闭连接呢？
            //这是在同一个线程下面的处理，可以直接用close
            //而不用io_context去控制
            socket_.close();
          }
        });
  }
  
  //处理服务器发过来的东西，这里就是定义的RoomInformation
  void do_read_body(){
    boost::asio::async_read(socket_,
        boost::asio::buffer(read_msg_.body(), read_msg_.body_length()),
        [this](boost::system::error_code ec, std::size_t /*length*/){
          if (!ec){ 
            //老样子判断合法性
            if(read_msg_.body_length() == sizeof(RoomInformation) && 
                read_msg_.type() == MT_ROOM_INFO) {
                //这里是非常机缘巧合的，按理说要进行很多合法性检查
                //客户端和服务端很多就是对消息的处理
                const RoomInformation *info = 
                    reinterpret_cast<const RoomInformation*>(read_msg_.body());
                
                std::cout << "client: '";
                assert(info->name.nameLen <= sizeof(info->name.name));
                std::cout.write(info->name.name, info->name.nameLen);
                
                std::cout << "  says : '";
                assert(info->chat.infoLen <= sizeof(info->chat.information));
                std::cout.write(info->chat.information, info->chat.infoLen);
                
                std::cout << std::endl;
                }
            do_read_header();
          }
          else{
            socket_.close();
          }
        });
  }
  
  //往服务器里面写
  void do_write(){
    boost::asio::async_write(socket_,
        boost::asio::buffer(write_msgs_.front().data(),
          write_msgs_.front().length()),
        [this](boost::system::error_code ec, std::size_t /*length*/){
          if (!ec){
            write_msgs_.pop_front();
            //没写完就继续写
            if (!write_msgs_.empty()){
              do_write();
            }
          }
          else{
            socket_.close();
          }
        });
  }

private:
  //四个成员，前两个负责通信连接的，后两个负责收发消息
  boost::asio::io_context& io_context_;
  tcp::socket socket_;
  chat_message read_msg_;
  //std::deque<chat_message> == chat_message_queue
  chat_message_queue write_msgs_;
};

int main(int argc, char* argv[])
{
  try{
    if (argc != 3){
      //这里是服务器的ip和端口号
      std::cerr << "Usage: chat_client <host> <port>\n";
      return 1;
    }

    boost::asio::io_context io_context;

    tcp::resolver resolver(io_context);
    //获得服务器的ip和端口号，并封装到endpoints中
    //这里auto的类型是tcp::resolver::results_type
    auto endpoints = resolver.resolve(argv[1], argv[2]);
    //这里chat_client的构造函数里面
    //进行了两个工作，1 是和服务器进行连接
    //2 是如果有服务器的数据，就开始读报头（异步读）
    chat_client c(io_context, endpoints);
    
    //这里和网络相关的内容都在另外一个线程里面
    //像连接、接收服务器数据、向服务器发数据都是这个线程在干
    std::thread t([&io_context](){ io_context.run(); });
    
    //这里主线程工作就是等待用户的输入，然后解析成chat_message
    char line[chat_message::max_body_length + 1];
    //正常退出是标准输入退出，在linux下是ctrl d
    while (std::cin.getline(line, chat_message::max_body_length + 1)){
      chat_message msg;
      auto type = 0;
      //这里有点像迭代器，获得line的输入
      std::string input(line, line + std::strlen(line));
      std::string output;
      //都封装到这个parseMessage里面，整个框架就可以复用了
      if(parseMessage(input,&type,output)){
        //output是string, string.data()和string.c_str()很像
        //c_str()以结束符结尾，data()返回的是一个字符数组
        //output就是body的内容，需要通过msg.setMessage搞一个头
        //然后封装成chat_message
        msg.setMessage(type, output.data(), output.size());
        c.write(msg);
        std::cout << "write message for server " << output.size() << std::endl;
      }
    }

    c.close();
    //这里如果不先close，t就永远不会结束，因为里面还有注册的事件
    t.join();
  }
  catch (std::exception& e){
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}
